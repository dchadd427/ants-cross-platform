# Ants Remake Orchestration Plan

## Objective
Build and verify a modern, high-performance, deterministic cross-platform engine remake of Ants (1995/1998) that directly loads original raw binary assets (`ants.chd` and `Maps/*.LVL`) without pre-conversion, faithfully executing all authentic mechanics, physics, and audiovisual presentation.

## Strategy: Dual Track Project Orchestration
1. **Phase 0: Scope Survey & Feature Inventory**
   - Survey authoritative specification (`GAME_REVERSE_ENGINEERING.md`) and raw binary assets (`Original-Ants/`).
   - Dispatch 3 parallel explorers/spec miners:
     * Miner 1: Asset Decoding & Audio Specifications (`ants.chd`, `Maps/*.LVL`, sprite palettes, mirroring, sound formats)
     * Miner 2: Simulation Grid, Ticks, Rules & Mechanics (damage matrix, knockback, combat AI, bomb/fire physics, timers, base mechanics, alliances, scorecard)
     * Explorer 3: Application Architecture, HUD, Renderer, Platform Toolchains & Testability
   - Synthesize results into `PROJECT.md` at project root with complete Architecture, Feature Inventory, Milestones, Interface Contracts, and Code Layout.

2. **Phase 1: Dual Track Dispatch**
   - **E2E Testing Track**: Dispatches an E2E Testing Orchestrator / Test Writers to design opaque-box test suites (Tiers 1-4: Feature coverage >=5/feature, Boundary/Corner >=5/feature, Pairwise combinations, Real-world scenarios). Publishes `TEST_READY.md`.
   - **Implementation Track**:
     * Milestone 1: Native Binary Asset Decoder (`ants-assets`): parser for `ants.chd` (header, 256-color palette, 2,794 sprites, 91 PCM audio clips, 1,344 animation sequences), `Maps/*.LVL`, 5-to-8 directional mirroring.
     * Milestone 2: Deterministic Simulation Engine & Game Rules (`ants-sim`): tick-based deterministic simulation grid, unit damage matrix, combat ant guard AI, placement rules, bomb/fire mechanics, 180s timers, base queuing/entry, thief infiltration, alliances, match timer & stats tracking.
     * Milestone 3: Interactive Application & Audio (`ants-app`): 2D integer pixel scaling viewport, in-game HUD (selection card, minimap, hatch controls, egg counter, news banner, match clock), multi-channel audio mixer, end-of-game scorecard modal.
     * Milestone 4 (Final Milestone): 100% E2E test suite pass across all tiers + white-box adversarial coverage hardening (Tier 5).

3. **Phase 2: Gate Enforcement & Integrity Audits**
   - Every milestone executes strict iteration loops: Worker -> 2 Reviewers -> 2 Challengers -> Forensic Auditor.
   - Forensic Auditor has hard binary veto: INTEGRITY VIOLATION fails milestone unconditionally.
   - Comprehensive verification against all acceptance criteria.

4. **Phase 3: Final Reporting & Handoff**
   - Synthesize all verification results.
   - Provide comprehensive handoff report to Sentinel with full evidence.
