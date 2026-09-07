# Task Assignment: M1 Iteration 2 Challenger 1

## Objective
Empirically stress-test the input-bounded allocation defenses and corrupted header rejections in `libants-assets`:
1. Test malformed/oversized map dimensions (e.g. `width = 0x7FFFFFFF`, `width = 1000`, truncated buffers). Confirm all return `false` gracefully without throwing `std::bad_alloc` or ASan aborts.
2. Test malformed CHD headers (e.g. `size = 20`, truncated table offsets). Confirm clean `false` return.
3. Verify all 26 test cases in `test_assets` and all 24 cases in `test_challenger_m1_1` pass under AddressSanitizer.
4. Render your verdict: `APPROVE` or `REQUEST_CHANGES` in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_1/handoff.md`.

## 2026-09-06T23:08:36Z
You are M1 It2 Challenger 1 for Milestone 1 (ants-assets).
Your identity: challenger_m1_it2_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_1/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_it2/handoff.md

Your mission:
1. Empirically stress-test the input-bounded allocation defenses:
   - Feed oversized dimensions (e.g. width = 0x7FFFFFFF, width = 1000) and truncated buffers to LVLParser. Confirm clean false return with no crash or ASan abort.
   - Feed corrupted headers to CHDParser. Confirm clean false return.
2. Run test suites via ./run_tests.sh --all and ./run_tests.sh --asan.
3. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_1/handoff.md
   and send a completion message to your parent.

