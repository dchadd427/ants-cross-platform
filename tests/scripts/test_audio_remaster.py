#!/usr/bin/env python3
"""Unit tests for the Ants (1998) audio remasterer and Web Audio playback engine.

Pure-stdlib classification, metadata, and Node syntax/API contract tests run
unconditionally (including on CI runners without NumPy). DSP waveform and full
91-WAV pipeline checks are gated with @unittest.skipUnless(HAS_NUMPY, ...).
"""
from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from tools.ant_models import remaster_audio  # noqa: E402

try:
    import numpy as np  # noqa: E402

    HAS_NUMPY = True
except ImportError:
    np = None  # type: ignore
    HAS_NUMPY = False

SRC_SOUNDS_DIR = os.path.join(REPO, "asset_catalog", "sounds")
PLAY_AUDIO_JS = os.path.join(REPO, "tools", "ant_models", "play_audio.js")
AUDIO_COMPARE_HTML = os.path.join(REPO, "tools", "ant_models", "audio_compare.html")


class AudioMetadataContractTests(unittest.TestCase):
    """Pure-stdlib tests verifying 91-sound coverage and playback/voice tables."""

    def test_all_91_source_wavs_present_and_classified(self) -> None:
        sources = remaster_audio.discover_source_wavs(SRC_SOUNDS_DIR)
        self.assertEqual(len(sources), remaster_audio.EXPECTED_SOUND_COUNT)
        ids = [sid for sid, _, _ in sources]
        self.assertEqual(ids, list(range(remaster_audio.EXPECTED_SOUND_COUNT)))

        for sid, name, _ in sources:
            cat = remaster_audio.classify_sound(sid, name)
            self.assertIn(cat, remaster_audio.CATEGORY_LABELS)
            meta = remaster_audio.playback_metadata(sid, cat)
            self.assertIn(meta["pitch_jitter"], (0.0, 0.03))
            self.assertIn(meta["priority"], (1, 2, 3, 4))

    def test_pitch_variation_rules_zero_on_voices_ui_and_chimes(self) -> None:
        for sid in range(remaster_audio.EXPECTED_SOUND_COUNT):
            cat = remaster_audio.classify_sound(sid)
            meta = remaster_audio.playback_metadata(sid, cat)
            if cat in ("voice_ant", "voice_announcer", "ui", "chime_pickup") or sid == 4:
                self.assertEqual(meta["pitch_jitter"], 0.0, f"Sound {sid} ({cat}) must not jitter pitch")
        # Combat attacks and water/digging get subtle +/-3% micro-variation
        for sid in (57, 66, 71, 78, 81, 83):
            meta = remaster_audio.playback_metadata(sid, remaster_audio.classify_sound(sid))
            self.assertAlmostEqual(meta["pitch_jitter"], 0.03, places=6)

    def test_all_ant_voices_have_distinct_class_profiles(self) -> None:
        # Verify every class has its requested vocal direction
        worker = remaster_audio.voice_profile_for(14)  # gantrdy
        combat = remaster_audio.voice_profile_for(27)  # combrdy1
        bomber = remaster_audio.voice_profile_for(36)  # bombrdy
        swimmer = remaster_audio.voice_profile_for(32)  # brdgrdy
        fire = remaster_audio.voice_profile_for(22)  # firerdy
        thief = remaster_audio.voice_profile_for(18)  # theifrdy
        for prof in (worker, combat, bomber, swimmer, fire, thief):
            self.assertIsNotNone(prof)
        self.assertGreater(worker["semitones"], 0.8)
        self.assertLess(combat["semitones"], -1.4)
        self.assertGreater(bomber["semitones"], 1.2)
        self.assertLess(swimmer["semitones"], 0.0)
        self.assertGreater(fire["semitones"], 0.4)
        self.assertLess(thief["semitones"], -0.7)
        # 1998 duplicate pairs receive distinct remastered profiles
        self.assertNotEqual(
            remaster_audio.voice_profile_for(7)["semitones"],
            remaster_audio.voice_profile_for(12)["semitones"],
        )
        self.assertNotEqual(
            remaster_audio.voice_profile_for(32)["semitones"],
            remaster_audio.voice_profile_for(33)["semitones"],
        )

    def test_js_size_syntax_and_node_runtime_contract(self) -> None:
        with open(PLAY_AUDIO_JS, "r", encoding="utf-8") as f:
            js_lines = f.read().splitlines()
        self.assertLess(len(js_lines), 600, f"play_audio.js must stay < 600 lines (got {len(js_lines)})")
        self.assertTrue(os.path.isfile(AUDIO_COMPARE_HTML))

        node = shutil.which("node")
        if not node:
            self.skipTest("node not installed")

        chk = subprocess.run([node, "--check", PLAY_AUDIO_JS], capture_output=True, text=True)
        self.assertEqual(chk.returncode, 0, chk.stderr)

        # Run play_audio.js in Node with a lightweight mock Web AudioContext to verify
        # mode switching, panning, pitch-jitter rules, polyphony cap (12), and voice stealing.
        script = r"""
        const assert = require("assert");
        const started = [];
        const stopped = [];
        class MockParam {
          constructor(v) { this.value = v; }
          cancelScheduledValues() {}
          setTargetAtTime(v) { this.value = v; }
        }
        class MockNode { connect() {} }
        class MockGain extends MockNode { constructor() { super(); this.gain = new MockParam(1); } }
        class MockPanner extends MockNode { constructor() { super(); this.pan = new MockParam(0); } }
        class MockComp extends MockNode {
          constructor() {
            super();
            this.threshold = new MockParam(-14); this.knee = new MockParam(18);
            this.ratio = new MockParam(4); this.attack = new MockParam(0.004); this.release = new MockParam(0.14);
          }
        }
        class MockSource extends MockNode {
          constructor() { super(); this.buffer = null; this.playbackRate = new MockParam(1); this.onended = null; }
          start() { started.push(this); }
          stop() { stopped.push(this); }
        }
        global.AudioContext = class {
          constructor() { this.state = "suspended"; this.currentTime = 0; this.destination = new MockNode(); }
          async resume() { this.state = "running"; }
          createGain() { return new MockGain(); }
          createStereoPanner() { return new MockPanner(); }
          createDynamicsCompressor() { return new MockComp(); }
          createBufferSource() { return new MockSource(); }
          async decodeAudioData() { return { duration: 0.4 }; }
        };
        global.fetch = async () => ({ ok: true, arrayBuffer: async () => new ArrayBuffer(16) });
        const audio = require(process.argv[1]);
        assert.strictEqual(typeof audio.init, "function");
        assert.strictEqual(typeof audio.unlock, "function");
        assert.strictEqual(typeof audio.setMode, "function");
        assert.strictEqual(typeof audio.getMode, "function");
        assert.strictEqual(typeof audio.play, "function");
        assert.strictEqual(typeof audio.setMusic, "function");
        assert.strictEqual(audio.getMode(), "remastered");
        assert.strictEqual(audio.cycleMode(), "classic");
        assert.strictEqual(audio.cycleMode(), "muted");
        assert.strictEqual(audio.cycleMode(), "remastered");

        const manifest = {
          sounds: [
            { id: 4, name: "bombexp", remastered_url: "remastered/b.wav", classic_url: "classic/b.wav", playback: { pitch_jitter: 0.0, non_spatial: false, priority: 4 } },
            { id: 14, name: "gantrdy", remastered_url: "remastered/v.wav", classic_url: "classic/v.wav", playback: { pitch_jitter: 0.0, non_spatial: false, priority: 3 } },
            { id: 57, name: "attack", remastered_url: "remastered/a.wav", classic_url: "classic/a.wav", playback: { pitch_jitter: 0.03, non_spatial: false, priority: 1 } },
          ],
          music: [{ id: "ants2a", title: "ANTS2A", url: "music/ANTS2A.mp3" }],
        };
        audio._loadManifestObject(manifest, "models/audio/");
        (async () => {
          assert.strictEqual(await audio.unlock(), true);
          // Fill 15 voices with force=true -> cap strictly <= 12 with voice stealing!
          for (let i = 0; i < 15; i++) {
            audio.play(57, 10 + i, 10, 10, 10, { force: true });
            await new Promise((r) => setTimeout(r, 2));
          }
          const st = audio.getStats();
          assert.ok(st.activeVoices <= 12, "Polyphony cap violated: " + st.activeVoices);
          assert.ok(st.stolen >= 3, "Expected voice stealing, got " + st.stolen);
          // Voice line must have exact playbackRate 1.0
          const before = started.length;
          audio.play(14, 10, 10, 10, 10, { force: true });
          await new Promise((r) => setTimeout(r, 5));
          assert.strictEqual(started.length, before + 1);
          assert.strictEqual(started[started.length - 1].playbackRate.value, 1.0);
          // Muted mode refuses playback
          audio.setMode("muted");
          assert.strictEqual(audio.play(4, 10, 10, 10, 10, { force: true }), false);
          assert.strictEqual(audio.setMusic(false), false);
          console.log("NODE_AUDIO_CONTRACT_OK");
        })().catch((err) => { console.error(err); process.exit(1); });
        """
        run = subprocess.run([node, "-e", script, PLAY_AUDIO_JS], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("NODE_AUDIO_CONTRACT_OK", run.stdout)


@unittest.skipUnless(HAS_NUMPY, "NumPy required for DSP remastering tests")
class AudioRemasterDspTests(unittest.TestCase):
    """End-to-end DSP quality, header, zero-clipping, and 91-WAV coverage tests."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.tmpdir = tempfile.mkdtemp(prefix="ants_audio_test_")
        cls.manifest = remaster_audio.build_audio_bank(
            src_dir=SRC_SOUNDS_DIR,
            out_dir=cls.tmpdir,
            copy_classic=True,
            copy_music=False,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        shutil.rmtree(cls.tmpdir, ignore_errors=True)

    def test_manifest_covers_all_91_files_with_valid_headers_and_zero_clipping(self) -> None:
        m = self.manifest
        self.assertEqual(m["count"], remaster_audio.EXPECTED_SOUND_COUNT)
        self.assertEqual(m["sample_rate"], 44100)
        self.assertEqual(m["bits"], 16)
        self.assertEqual(len(m["sounds"]), 91)

        manifest_file = os.path.join(self.tmpdir, "manifest.json")
        self.assertTrue(os.path.isfile(manifest_file))
        with open(manifest_file, "r", encoding="utf-8") as f:
            parsed = json.load(f)
        self.assertEqual(parsed["audio_hash"], m["audio_hash"])

        seen_ids = set()
        for entry in m["sounds"]:
            sid = entry["id"]
            seen_ids.add(sid)
            rem_path = os.path.join(self.tmpdir, entry["remastered_url"])
            cls_path = os.path.join(self.tmpdir, entry["classic_url"])
            self.assertTrue(os.path.isfile(rem_path), f"Missing remastered WAV: {rem_path}")
            self.assertTrue(os.path.isfile(cls_path), f"Missing classic WAV: {cls_path}")

            with wave.open(rem_path, "rb") as w:
                sr = w.getframerate()
                ch = w.getnchannels()
                sw = w.getsampwidth()
                nf = w.getnframes()
                raw = w.readframes(nf)

            self.assertEqual(sr, 44100)
            self.assertEqual(sw, 2)
            self.assertEqual(ch, entry["original"]["channels"])
            self.assertGreater(nf, 0)

            samples = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
            self.assertTrue(np.all(np.isfinite(samples)), f"Non-finite samples in sound {sid}")
            peak = float(np.max(np.abs(samples)))
            rms = float(np.sqrt(np.mean(samples * samples)))
            dc = float(np.mean(samples))

            self.assertLessEqual(peak, 0.96, f"Clipping in sound {sid} ({entry['name']}): peak={peak}")
            self.assertGreaterEqual(rms, 0.03, f"Collapsed RMS in sound {sid} ({entry['name']}): rms={rms}")
            self.assertLessEqual(rms, 0.45, f"Over-crushed RMS in sound {sid} ({entry['name']}): rms={rms}")
            self.assertLess(abs(dc), 0.005, f"Residual DC offset in sound {sid} ({entry['name']}): dc={dc}")

            # Duration parity strictly within +/-15 ms
            dur_rem = 1000.0 * nf / float(sr)
            dur_orig = entry["original"]["duration_ms"]
            self.assertLessEqual(
                abs(dur_rem - dur_orig),
                15.0,
                f"Duration drift on sound {sid}: orig={dur_orig}ms rem={dur_rem}ms",
            )

            # Click-free endpoints (first and last sample near zero)
            self.assertLess(abs(float(samples[0])), 0.02, f"Start click on sound {sid}")
            self.assertLess(abs(float(samples[-1])), 0.02, f"End click on sound {sid}")

            # High envelope similarity to 1998 original
            self.assertGreaterEqual(
                entry["remastered"]["env_corr"],
                0.90,
                f"Envelope drifted too far on sound {sid} ({entry['name']}): {entry['remastered']['env_corr']}",
            )

        self.assertEqual(seen_ids, set(range(91)))

    def test_dc_offset_and_start_click_repairs(self) -> None:
        # 079_waterattack had +0.1614 DC surge in 1998; 068_firestartb started at -0.233.
        by_id = {s["id"]: s for s in self.manifest["sounds"]}
        self.assertGreater(abs(by_id[79]["original"]["dc"]), 0.15)
        self.assertLess(abs(by_id[79]["remastered"]["dc"]), 0.003)
        self.assertLess(abs(by_id[68]["remastered"]["dc"]), 0.003)

    def test_determinism_identical_sha256_on_repeat_run(self) -> None:
        tmp2 = tempfile.mkdtemp(prefix="ants_audio_det_")
        try:
            m2 = remaster_audio.build_audio_bank(
                src_dir=SRC_SOUNDS_DIR,
                out_dir=tmp2,
                only_ids=[0, 4, 14, 27, 32, 79],
                copy_classic=False,
                copy_music=False,
            )
            by_id_1 = {s["id"]: s["remastered"]["sha256"] for s in self.manifest["sounds"]}
            for s in m2["sounds"]:
                self.assertEqual(s["remastered"]["sha256"], by_id_1[s["id"]], f"Non-deterministic sound {s['id']}")
        finally:
            shutil.rmtree(tmp2, ignore_errors=True)

    def test_synthetic_sine_upsample_preserves_frequency_and_duration(self) -> None:
        sr_in = 11025
        dur = 0.25
        n = int(round(sr_in * dur))
        t = np.arange(n, dtype=np.float64) / sr_in
        sig = (0.5 * np.sin(2.0 * math.pi * 880.0 * t)).reshape(1, -1)
        out, cat, corr = remaster_audio.remaster_signal(sig, sr_in, sound_id=1, name="powerupc")
        self.assertEqual(cat, "chime_pickup")
        self.assertEqual(out.shape, (1, n * 4))
        self.assertTrue(np.all(np.isfinite(out)))
        self.assertLessEqual(float(np.max(np.abs(out))), 0.96)
        self.assertGreater(corr, 0.95)


if __name__ == "__main__":
    unittest.main()
