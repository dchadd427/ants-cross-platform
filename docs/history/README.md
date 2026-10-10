# History

How things were built, and what was measured and reviewed on the way. Nothing here is needed to understand or change the code: the current behaviour is in the pages that [`README.md`](../../README.md) lists. These pages are kept for the evidence behind decisions; where one of them disagrees with a current page, the current page is right.

**Computer players** (the current rules are in [`BOTS.md`](../BOTS.md))
- [`B1_notes.md`](B1_notes.md): the plumbing (the `ants_ai` library, the controller, the idle bot).
- [`bots_contest_notes.md`](bots_contest_notes.md), [`bots_cantgo_notes.md`](bots_cantgo_notes.md), [`bots_flowers_notes.md`](bots_flowers_notes.md), [`bots_islands_notes.md`](bots_islands_notes.md), [`bots_integration_notes.md`](bots_integration_notes.md): the later batches (the contest for food and fights, the can't-go loop, the flowers, the island expedition, the batches together): evidence, mutants, what was left out.

**Fidelity audit** (the remake against the original program, frozen at v0.0.50)
- [`fidelity-audit/`](fidelity-audit/): one ledger for each area (abilities, ants, combat, effects and objects, food and economy, the hill, input, movement, the scheduler, screens, sound and texts, terrain and fog, the interface).
