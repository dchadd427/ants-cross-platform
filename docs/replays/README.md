# Replays and watching bots: the approved design

The design of replays (`.antsrep` files, a viewer, the replays menu) and of watching bots play each other (1v1v1v1, 1v1, 2v2), made and measured on 2026-10-03; the owner approved the mock-ups ("I really like that. That looks nice. Good job.").

- **Built so far:** most of it, in the form that [`../REPLAYS.md`](../REPLAYS.md) describes (it says what exists where it differs from DESIGN.md); not built: the desktop game's Replays and Watch bots menus and the `--watch` mode. Two differences from the design so far: the game writes a file whole when a match ends or is left (no `.part` file written as it goes, so a crash leaves nothing), and a recording that would pass 1 MiB is not kept (DESIGN.md says 900 KiB, marked incomplete).
- [DESIGN.md](DESIGN.md): the format, the recorder, the viewer, the menus, the web and the server.
- [PLAN.md](PLAN.md): the build in three phases (A: the engine underneath, no visible change; B: the viewer and the menu panels; C: the server's recording and the web page's bar).
- [FACTS.md](FACTS.md): what was measured (file sizes, seek times) and checked in the engine.
- [mockups/](mockups/): the approved pictures (`overview.png` first).

The probes, measurement files and picture scripts that the documents name (`tools/*_probe.cpp`, `measure/`, `mockups/*.py`) stayed with the session that made them; their results are in FACTS.md. Decisions taken since: a bot's "style" is the standard bot's personality (branch bots-b4-1); the server records demo rooms too, within its limits; compact command coding; a replay of another rules version is refused with a message.
