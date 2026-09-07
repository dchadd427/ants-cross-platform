# Task Assignment: M1 Reviewer 2

## Objective
Review Milestone 1 (`libants-assets`) for binary decoding fidelity, memory safety, and AddressSanitizer cleanliness.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Test Ready Notification: `/Users/dchadd/Desktop/Ants-Mac/TEST_READY.md`
- Worker Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md`

## Verification Requirements
1. Verify binary decoding correctness:
   - Header (version 9, palette size 1024, table offsets 1052, 6835937, 7903773, 7903835).
   - Palette (256 entries, Little-Endian RGBA, color 254 transparent Alpha=0, 4 team color ranges).
   - Table 1 (2,794 sprites, pitch stride padding handled, no gray padding artifacts in RGBA output).
   - Table 2 (91 audio clips, 88 mono, 3 stereo, 15 unnamed clips handled, standard 44-byte RIFF WAV reconstruction).
   - Table 3 (4 tags) and Table 4 (1,344 animations, signed bounding boxes and frame offsets, 365 sound triggers).
   - Maps (all 6 maps in `Maps/*.LVL` parsed with Layer 1, Layer 2 sentinel 0x7FFE, and 4 trailing blocks with rem=0).
   - 5-to-8 Directional Mirroring ($dx' = -(dx+W)$, $p'(x, y) = p(W-1-x, y)$, lunchbox prefix mappings).
2. Build and run under AddressSanitizer:
   ```bash
   cmake -B build_asan -DENABLE_ASAN=ON
   cmake --build build_asan
   ./build_asan/tests/test_assets/test_assets
   ./build_e2e/e2e_runner --all
   ```
3. Render verdict: `APPROVE` or `REQUEST_CHANGES` in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_2/handoff.md`.
