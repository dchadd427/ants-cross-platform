# History

How things were built, and what was measured and reviewed on the way. Nothing here is needed to understand or change the code: the current behaviour is in the pages that [`README.md`](../../README.md) lists. These pages are kept for the evidence behind decisions; where one of them disagrees with a current page, the current page is right.

**Computer players** (the current rules are in [`BOTS.md`](../BOTS.md))
- [`B1_notes.md`](B1_notes.md): the plumbing (the `ants_ai` library, the controller, the idle bot).
- [`bots_contest_notes.md`](bots_contest_notes.md), [`bots_cantgo_notes.md`](bots_cantgo_notes.md), [`bots_flowers_notes.md`](bots_flowers_notes.md), [`bots_islands_notes.md`](bots_islands_notes.md), [`bots_integration_notes.md`](bots_integration_notes.md): the later batches (the contest for food and fights, the can't-go loop, the flowers, the island expedition, the batches together): evidence, mutants, what was left out.

**The long pages, split** (the rules stay in the page; the tests, the measurements and the story of how each chapter was built are here, in the order of the chapters)
- [`NETWORK_PORT_history.md`](NETWORK_PORT_history.md): the network design ([`NETWORK_PORT.md`](../NETWORK_PORT.md)), including the milestones and the status of each piece.
- [`BOTS_history.md`](BOTS_history.md): the measurements, the tests and the story of each batch of the computer players ([`BOTS.md`](../BOTS.md)), including the roadmap.
- [`GAME_REVERSE_ENGINEERING_early_notes.md`](GAME_REVERSE_ENGINEERING_early_notes.md): the sketch of the architecture and the first findings of the reverse engineering, as they were written down before [section 5 of the specification](../GAME_REVERSE_ENGINEERING.md) took them over; several are corrected there.

**Fidelity audit** (the remake against the original program, frozen at v0.0.50)
- [`fidelity-audit/`](fidelity-audit/): one ledger for each area (abilities, ants, combat, effects and objects, food and economy, the hill, input, movement, the scheduler, screens, sound and texts, terrain and fog, the interface).
