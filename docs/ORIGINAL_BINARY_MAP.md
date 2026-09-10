# Original Ants.exe Binary Architecture & Reverse Engineering Map

Automated static disassembly, control flow graph, and data structure recovery from `Original-Ants/Ants.exe`.

- Total mapped function entry points: **1197**
- Total extracted static strings: **1855**

## 1. Key Subsystem Entry Points with Verified Audio & String Signatures

| Function Address | Calls Out | Inbound Callers | Sounds Dispatched | String Cross-References | Subsystem Classification |
|:-----------------|:---------:|:---------------:|:-------------------|:------------------------|:-------------------------|
| `0x100607b` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10060bc` | 5 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10061e6` | 8 | 1 | buttonclick.wav (0), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1006349` | 15 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100665a` | 3 | 0 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100674e` | 6 | 1 | buttonclick.wav (0) | `!hlp`, `Y_^[` | **Audio / SFX Dispatch** |
| `0x10068be` | 0 | 1 | powerdrip.wav (62) | None | **Audio / SFX Dispatch** |
| `0x10068de` | 6 | 1 | buttonclick.wav (0), powerupc2.wav (2), bombexp.wav (4), fireattack.wav (24) | None | **Audio / SFX Dispatch** |
| `0x10069bb` | 1 | 3 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x10069d8` | 1 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1006af4` | 5 | 1 | powerupc.wav (1) | `@f;E` | **Audio / SFX Dispatch** |
| `0x1006c48` | 1 | 7 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1006c7d` | 2 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1006d19` | 8 | 1 | buttonclick.wav (0), powerupc2.wav (2), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x1006f0e` | 6 | 1 | powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4), fireburnout.wav (5) | None | **Audio / SFX Dispatch** |
| `0x1007025` | 1 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x100707a` | 3 | 0 | buttonclick.wav (0) | `GGKu` | **Audio / SFX Dispatch** |
| `0x10071dd` | 0 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1007202` | 0 | 7 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1007227` | 0 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100724c` | 2 | 1 | buttonclick.wav (0), powerupc2.wav (2), fireburnout.wav (5), gantrdy.wav (14) | None | **Audio / SFX Dispatch** |
| `0x1007352` | 5 | 22 | powerupc.wav (1) | `X_^[` | **Audio / SFX Dispatch** |
| `0x100744f` | 12 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1007710` | 5 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10079e4` | 0 | 4 | brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1007d03` | 4 | 6 | None | `Gf;}`, `^_[]` | **General** |
| `0x1007d59` | 2 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1007e05` | 3 | 1 | combatnetfairy.wav (3), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1008089` | 11 | 1 | buttonclick.wav (0), powerupc2.wav (2), combatnetfairy.wav (3), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1008607` | 3 | 1 | buttonclick.wav (0), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1008871` | 3 | 6 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1008af7` | 2 | 9 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1008b90` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1008bc6` | 0 | 9 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1008ca0` | 6 | 1 | buttonclick.wav (0), powerupc2.wav (2), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x1008e2d` | 0 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1008f34` | 5 | 1 | antdrown.wav (72) | `FDv.jH` | **Audio / SFX Dispatch** |
| `0x1008faa` | 5 | 31 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1009056` | 6 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100912e` | 1 | 0 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100925b` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1009596` | 11 | 3 | powerupc.wav (1), fireburnout.wav (5), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1009825` | 1 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1009899` | 12 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1009bb7` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1009c2e` | 16 | 1 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1009ed3` | 0 | 4 | None | `Sf;4zw` | **General** |
| `0x1009fd8` | 2 | 1 | fireburnout.wav (5) | None | **Audio / SFX Dispatch** |
| `0x100a056` | 6 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100a11c` | 0 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100a179` | 1 | 4 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100a1aa` | 0 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100a1e9` | 0 | 1 | powerupc.wav (1), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x100a23c` | 6 | 4 | None | `jOhh` | **General** |
| `0x100a2a6` | 2 | 1 | waterattack.wav (79) | None | **Audio / SFX Dispatch** |
| `0x100a2c9` | 47 | 1 | powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4), fireburnout.wav (5) | `%s@%s`, `CLEARSTAT`, `CUSS` | **Audio / SFX Dispatch** |
| `0x100b091` | 12 | 2 | bombexp.wav (4) | `close AntsMidi` | **Audio / SFX Dispatch** |
| `0x100bbf2` | 2 | 1 | firerdy.wav (22) | None | **Audio / SFX Dispatch** |
| `0x100bcd6` | 2 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100bd2b` | 9 | 5 | powerupc2.wav (2), bombexp.wav (4), combdo2.wav (30), exithill.wav (43) | None | **Audio / SFX Dispatch** |
| `0x100be6c` | 1 | 2 | buttonclick.wav (0), firerdy.wav (22), brdgrdy.wav (32) | `\Maps` | **Audio / SFX Dispatch** |
| `0x100bebf` | 14 | 1 | powerupc2.wav (2), exithill.wav (43), countdwn.wav (44), attack2.wav (78) | `ARQTASK` | **Audio / SFX Dispatch** |
| `0x100c36b` | 10 | 1 | wateraction.wav (80), shovelgravel.wav (81) | None | **Audio / SFX Dispatch** |
| `0x100c4ed` | 4 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100c58a` | 0 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100c5fa` | 15 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100c76e` | 3 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100c7ac` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100c838` | 5 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100c8dd` | 9 | 1 | powerupc.wav (1), bombexp.wav (4), fireburnout.wav (5), combdo2.wav (30) | `Player group count must b`, `SUVW`, `_^][Y` | **Audio / SFX Dispatch** |
| `0x100cba4` | 10 | 2 | buttonclick.wav (0), underattack.wav (58) | None | **Audio / SFX Dispatch** |
| `0x100ccc0` | 2 | 30 | brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x100cd40` | 2 | 1 | combatnetfairy.wav (3), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100cd7d` | 0 | 21 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100cd9f` | 17 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), allypro.wav (51) | None | **Audio / SFX Dispatch** |
| `0x100cf0f` | 0 | 7 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100cf48` | 2 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100cfce` | 5 | 2 | gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x100d03b` | 25 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4) | `ADDPLYRT`, `Gathering players`, `picked up a player that w` | **Audio / SFX Dispatch** |
| `0x100d791` | 8 | 44 | powerupc.wav (1), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x100d9b1` | 12 | 1 | buttonclick.wav (0), powerupc.wav (1), gantcommand.wav (15), countdwn.wav (44) | None | **Audio / SFX Dispatch** |
| `0x100dbb0` | 54 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), fireburnout.wav (5) | `ANTHILLQ`, `WVWP`, `Wj2WP` | **Audio / SFX Dispatch** |
| `0x100e627` | 4 | 2 | None | `close AntsMidi`, `open "%s" type sequencer `, `play AntsMidi from 0 noti` | **Audio / SFX Dispatch** |
| `0x100e6da` | 2 | 4 | combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x100e714` | 2 | 2 | None | `close AntsMidi` | **Audio / SFX Dispatch** |
| `0x100e7a1` | 0 | 6 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100e839` | 4 | 0 | None | `_^[]`, `close AntsMidi` | **Audio / SFX Dispatch** |
| `0x100e8cc` | 2 | 1 | powerupc.wav (1) | `close AntsMidi` | **Audio / SFX Dispatch** |
| `0x100e944` | 3 | 9 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100e9bb` | 5 | 3 | buttonclick.wav (0) | `[%ld:%02ld] News Flash` | **Audio / SFX Dispatch** |
| `0x100ea6a` | 3 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100eafd` | 4 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100ebb5` | 3 | 2 | bombexp.wav (4) | `Y_^[`, `t(Af` | **Audio / SFX Dispatch** |
| `0x100ec18` | 3 | 1 | buttonclick.wav (0) | `]_^[` | **Audio / SFX Dispatch** |
| `0x100ecdf` | 11 | 1 | buttonclick.wav (0), powerupc2.wav (2), attack2.wav (78) | `Cf;]`, `Gf;}` | **Audio / SFX Dispatch** |
| `0x100eedf` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100ef18` | 11 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100f022` | 7 | 2 | powerupc.wav (1) | `Cf;_$r`, `f9_$v%S` | **Audio / SFX Dispatch** |
| `0x100f17f` | 2 | 5 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x100f2cd` | 3 | 2 | None | `Cf;_$` | **General** |
| `0x100f3ca` | 1 | 3 | brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x100f3e8` | 1 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100f421` | 1 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100f4ab` | 13 | 22 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x100f895` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x100f8bf` | 15 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), powerupd.wav (40) | None | **Audio / SFX Dispatch** |
| `0x100fa50` | 2 | 6 | None | `[_^]` | **General** |
| `0x100fc0d` | 9 | 1 | buttonclick.wav (0), brdgrdy.wav (32), powerupd.wav (40) | `QQSUVW` | **Audio / SFX Dispatch** |
| `0x100fdf8` | 3 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x100fe93` | 8 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1010008` | 7 | 7 | buttonclick.wav (0), wateraction.wav (80) | None | **Audio / SFX Dispatch** |
| `0x10100e5` | 16 | 3 | buttonclick.wav (0), antdrown.wav (72) | `Exiting...`, `chat.txt` | **Audio / SFX Dispatch** |
| `0x1010245` | 9 | 2 | buttonclick.wav (0), powerupc.wav (1), theifrdy.wav (18) | None | **Audio / SFX Dispatch** |
| `0x1010335` | 2 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10103ad` | 3 | 1 | buttonclick.wav (0) | `Vjtj#` | **Audio / SFX Dispatch** |
| `0x10103eb` | 6 | 2 | buttonclick.wav (0) | `Vjtj#` | **Audio / SFX Dispatch** |
| `0x1010560` | 7 | 1 | buttonclick.wav (0), theifattack.wav (20), allynot.wav (52), flythumpb_alt.wav (76) | None | **Audio / SFX Dispatch** |
| `0x1010627` | 5 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10106a9` | 6 | 2 | buttonclick.wav (0), powerupc.wav (1), firerdy.wav (22), combrdy1.wav (27) | None | **Audio / SFX Dispatch** |
| `0x1010743` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10107ad` | 6 | 4 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1010844` | 5 | 2 | buttonclick.wav (0), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10108de` | 5 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1010a03` | 3 | 1 | buttonclick.wav (0), 1min.wav (55) | None | **Audio / SFX Dispatch** |
| `0x1010aca` | 8 | 2 | gantorders.wav (13), gantrdy.wav (14), gantcommand.wav (15), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x1010c14` | 8 | 1 | buttonclick.wav (0), countdwn.wav (44) | `HATCHTSK` | **Audio / SFX Dispatch** |
| `0x1010d26` | 3 | 4 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1010e34` | 3 | 0 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1010ea7` | 1 | 0 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1010f12` | 5 | 2 | antdrown.wav (72) | None | **Audio / SFX Dispatch** |
| `0x10110d6` | 5 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101118a` | 2 | 2 | buttonclick.wav (0), bombexp.wav (4) | `W(_^[` | **Audio / SFX Dispatch** |
| `0x10111d4` | 5 | 4 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | `9~0t`, `V,9~4t`, `~0_^` | **Audio / SFX Dispatch** |
| `0x101148a` | 4 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1011506` | 1 | 1 | buttonclick.wav (0) | `W([_^` | **Audio / SFX Dispatch** |
| `0x1011543` | 2 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1011696` | 0 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10116cb` | 12 | 6 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1011856` | 13 | 7 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10119a8` | 18 | 2 | powerupc.wav (1), anthill.wav (48) | None | **Audio / SFX Dispatch** |
| `0x1011b69` | 4 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1011c90` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1011ccf` | 0 | 3 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1011d0e` | 4 | 4 | buttonclick.wav (0), powerupc.wav (1) | `^,^[` | **Audio / SFX Dispatch** |
| `0x1011d86` | 2 | 0 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1011e4c` | 2 | 1 | powerupc.wav (1) | `CURSORTASK` | **Audio / SFX Dispatch** |
| `0x1011e78` | 11 | 1 | countdwn.wav (44), flythumpb_alt.wav (76) | `CHATAPPD`, `CHATSCRL` | **Audio / SFX Dispatch** |
| `0x10120e9` | 6 | 2 | buttonclick.wav (0), allyon.wav (50) | None | **Audio / SFX Dispatch** |
| `0x1012190` | 8 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101228a` | 0 | 2 | None | `qH_^` | **General** |
| `0x10122d4` | 13 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | `%s @ %s` | **Audio / SFX Dispatch** |
| `0x10123e2` | 13 | 1 | powerupc.wav (1), allyon.wav (50) | `%s (To Teammate):` | **Audio / SFX Dispatch** |
| `0x10125db` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101262a` | 5 | 1 | None | `%s %s` | **General** |
| `0x1012747` | 5 | 6 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10127ac` | 12 | 5 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1012aa0` | 5 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1012c06` | 2 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1012c3b` | 2 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1012c74` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1012ce0` | 15 | 2 | powerupc.wav (1), gantrdy.wav (14), theifrdy.wav (18), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x1013126` | 2 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1013289` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x10133ef` | 24 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), winner.wav (56) | `%s (%d min)` | **Audio / SFX Dispatch** |
| `0x1013b36` | 12 | 1 | powerupc.wav (1), winner.wav (56) | None | **Audio / SFX Dispatch** |
| `0x1013de7` | 17 | 1 | buttonclick.wav (0), powerupc.wav (1), chatsnda.wav (45) | `\*.lvl` | **Audio / SFX Dispatch** |
| `0x1013fc9` | 12 | 4 | powerupc.wav (1), losers.wav (42) | None | **Audio / SFX Dispatch** |
| `0x10140c5` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1014125` | 5 | 2 | buttonclick.wav (0), powerupc.wav (1), losers.wav (42) | None | **Audio / SFX Dispatch** |
| `0x1014228` | 5 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10142cb` | 18 | 1 | powerupc.wav (1), fireattack.wav (24), combgo1.wav (28), combdo2.wav (30) | None | **Audio / SFX Dispatch** |
| `0x101453f` | 7 | 1 | buttonclick.wav (0), theifrdy.wav (18) | None | **Audio / SFX Dispatch** |
| `0x10145d2` | 14 | 2 | winner.wav (56), antdrown.wav (72) | `uOj8` | **Audio / SFX Dispatch** |
| `0x1014820` | 5 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101487c` | 18 | 1 | powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1014e20` | 15 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1015136` | 7 | 1 | losers.wav (42) | `u#j*` | **Audio / SFX Dispatch** |
| `0x10153a1` | 12 | 1 | powerupc.wav (1), theifattack.wav (20), combgo1.wav (28), anthill.wav (48) | None | **Audio / SFX Dispatch** |
| `0x10155ac` | 14 | 1 | buttonclick.wav (0), powerupc.wav (1), theifrdy.wav (18), theifgo.wav (19) | None | **Audio / SFX Dispatch** |
| `0x1015b65` | 23 | 1 | powerupc.wav (1), powerupc2.wav (2), fireattack.wav (24), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x1016045` | 2 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1016063` | 2 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1016081` | 4 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10160e2` | 24 | 1 | powerupc.wav (1), combatnetfairy.wav (3), fireattack.wav (24), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x10163f1` | 4 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1016438` | 25 | 2 | powerupc.wav (1), bombexp.wav (4), fireattack.wav (24), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x101678f` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10167d5` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101681b` | 17 | 2 | powerupc.wav (1), theifrdy.wav (18), theifattack.wav (20), combrdy2.wav (26) | None | **Audio / SFX Dispatch** |
| `0x1016aa2` | 19 | 1 | powerupc.wav (1), theifrdy.wav (18), theifattack.wav (20), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x1016d24` | 8 | 1 | buttonclick.wav (0), powerupc.wav (1), firerdy.wav (22), bombmuffle.wav (74) | `open` | **Audio / SFX Dispatch** |
| `0x1016dfc` | 17 | 1 | powerupc.wav (1), fireattack.wav (24), combgo1.wav (28), combdo2.wav (30) | None | **Audio / SFX Dispatch** |
| `0x1017055` | 10 | 1 | buttonclick.wav (0), powerupc.wav (1), firerdy.wav (22), harvest_alt.wav (77) | `open` | **Audio / SFX Dispatch** |
| `0x1017127` | 27 | 1 | powerupc.wav (1), theifattack.wav (20), fireattack.wav (24), combgo1.wav (28) | `KWFO` | **Audio / SFX Dispatch** |
| `0x1017560` | 0 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10175ad` | 10 | 1 | buttonclick.wav (0), powerupc.wav (1), wateraction.wav (80) | None | **Audio / SFX Dispatch** |
| `0x1018ad3` | 3 | 1 | gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x1018b9f` | 9 | 1 | buttonclick.wav (0), powerupc.wav (1), antdrown.wav (72) | `WSjH` | **Audio / SFX Dispatch** |
| `0x10192fd` | 4 | 1 | buttonclick.wav (0), antdrown.wav (72) | None | **Audio / SFX Dispatch** |
| `0x1019369` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101936d` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1019388` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10193f7` | 15 | 1 | powerupc.wav (1), fireburnout.wav (5), gantattack.wav (16), firerdy.wav (22) | `Pj&W` | **Audio / SFX Dispatch** |
| `0x10197b5` | 0 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10197ce` | 1 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10198d1` | 7 | 2 | bombexp.wav (4), combgo1.wav (28) | `^$_f` | **Audio / SFX Dispatch** |
| `0x10199e1` | 4 | 1 | None | `9~ t` | **General** |
| `0x1019a66` | 9 | 1 | powerupc.wav (1) | `X_^[` | **Audio / SFX Dispatch** |
| `0x1019c31` | 0 | 1 | bombpick.wav (90) | None | **Audio / SFX Dispatch** |
| `0x1019de9` | 3 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1019e20` | 1 | 2 | None | `_^[]` | **General** |
| `0x101a060` | 4 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101a0d3` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101a2aa` | 5 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101a329` | 2 | 0 | brdgrdy.wav (32) | `FL_^` | **Audio / SFX Dispatch** |
| `0x101a40f` | 5 | 0 | brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x101a478` | 9 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101a6a2` | 2 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101a6f9` | 3 | 4 | None | `9VXt` | **General** |
| `0x101a77a` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101a93a` | 8 | 1 | powerupc.wav (1), mslogo.wav (6), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x101ace3` | 1 | 20 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x101ad02` | 26 | 17 | powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3), bombexp.wav (4) | `SUVW` | **Audio / SFX Dispatch** |
| `0x101b52f` | 6 | 1 | antdrown.wav (72) | None | **Audio / SFX Dispatch** |
| `0x101b5f9` | 3 | 3 | buttonclick.wav (0), powerupc2.wav (2), combatnetfairy.wav (3), mslogo.wav (6) | None | **Audio / SFX Dispatch** |
| `0x101b67b` | 4 | 1 | buttonclick.wav (0), powerupc2.wav (2), gantcommand.wav (15), fireattack.wav (24) | `^jF_` | **Audio / SFX Dispatch** |
| `0x101b711` | 4 | 1 | buttonclick.wav (0), powerupc2.wav (2), bump.wav (47), anthill.wav (48) | None | **Audio / SFX Dispatch** |
| `0x101b78a` | 9 | 1 | buttonclick.wav (0), gantorders.wav (13), gantattack.wav (16), firedo.wav (25) | None | **Audio / SFX Dispatch** |
| `0x101b8cb` | 22 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x101c0d5` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101c184` | 5 | 1 | buttonclick.wav (0), winner.wav (56) | None | **Audio / SFX Dispatch** |
| `0x101c221` | 6 | 1 | powerupc.wav (1), gantrdy.wav (14) | None | **Audio / SFX Dispatch** |
| `0x101c2e2` | 5 | 2 | gantcommand.wav (15) | None | **Audio / SFX Dispatch** |
| `0x101c34c` | 17 | 1 | powerupc.wav (1), brdgrdy.wav (32), steala.wav (84) | None | **Audio / SFX Dispatch** |
| `0x101c4f2` | 21 | 1 | buttonclick.wav (0), powerupc.wav (1), anthill.wav (48), underattack.wav (58) | `lSSS` | **Audio / SFX Dispatch** |
| `0x101cb0c` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101cbcc` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101cc1e` | 5 | 8 | flythumpa.wav (64) | `u'9~`t"` | **Audio / SFX Dispatch** |
| `0x101ccaf` | 37 | 1 | powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4), cantgo.wav (17) | `SVWj`, `j2XPf` | **Audio / SFX Dispatch** |
| `0x101d6d6` | 6 | 4 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x101d762` | 6 | 4 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x101d7f9` | 2 | 4 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x101d822` | 1 | 6 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101d858` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101d8a4` | 0 | 2 | powerupc.wav (1) | `f;A>u`, `f;q8t f;A:u` | **Audio / SFX Dispatch** |
| `0x101d8ed` | 5 | 1 | fireattack.wav (24) | None | **Audio / SFX Dispatch** |
| `0x101da6f` | 9 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), flythumpa.wav (64) | None | **Audio / SFX Dispatch** |
| `0x101dbec` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101dd6f` | 4 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101dded` | 4 | 1 | powerupc.wav (1) | `_^[]`, `f9~tu` | **Audio / SFX Dispatch** |
| `0x101de7e` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1), gantrdy.wav (14), theifgo.wav (19) | None | **Audio / SFX Dispatch** |
| `0x101df5d` | 5 | 2 | powerupc2.wav (2), combrdy2.wav (26) | None | **Audio / SFX Dispatch** |
| `0x101e0c2` | 5 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101e165` | 7 | 1 | antstop.wav (61) | `_^][` | **Audio / SFX Dispatch** |
| `0x101e20d` | 4 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101e27f` | 6 | 1 | powerdrip.wav (62) | None | **Audio / SFX Dispatch** |
| `0x101e342` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combat2.wav (60) | None | **Audio / SFX Dispatch** |
| `0x101e433` | 7 | 1 | powerupc2.wav (2), winner.wav (56) | None | **Audio / SFX Dispatch** |
| `0x101e599` | 8 | 1 | buttonclick.wav (0), powerupc2.wav (2), attack.wav (57) | None | **Audio / SFX Dispatch** |
| `0x101e68c` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101e6b3` | 11 | 1 | powerupc.wav (1), powerupd.wav (40) | None | **Audio / SFX Dispatch** |
| `0x101e798` | 13 | 1 | powerupc2.wav (2), allynot.wav (52) | None | **Audio / SFX Dispatch** |
| `0x101e97b` | 11 | 1 | buttonclick.wav (0), powerupc2.wav (2), flythumpa.wav (64) | `_^[]` | **Audio / SFX Dispatch** |
| `0x101eaec` | 13 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), bombgo.wav (37) | `%t6j` | **Audio / SFX Dispatch** |
| `0x101ecdf` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), bombgo.wav (37) | `"t+Wj%j` | **Audio / SFX Dispatch** |
| `0x101edfe` | 25 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), brdgrdy.wav (32) | `9ydt` | **Audio / SFX Dispatch** |
| `0x101f780` | 7 | 5 | powerupc.wav (1) | `X_^[`, `t'f;U` | **Audio / SFX Dispatch** |
| `0x101fc24` | 2 | 1 | theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x101fc50` | 21 | 10 | powerupc.wav (1), powerupc2.wav (2), fireburnout.wav (5), anthill.wav (48) | `~h9}` | **Audio / SFX Dispatch** |
| `0x101ff5a` | 1 | 3 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x101ffab` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1020076` | 6 | 0 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1020128` | 3 | 1 | buttonclick.wav (0), powerupc.wav (1) | `X_^[` | **Audio / SFX Dispatch** |
| `0x10202e7` | 2 | 2 | buttonclick.wav (0), powerupc.wav (1) | `f93u` | **Audio / SFX Dispatch** |
| `0x1020655` | 8 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), mslogo.wav (6) | None | **Audio / SFX Dispatch** |
| `0x10208e8` | 1 | 1 | buttonclick.wav (0), theifdo.wav (21) | None | **Audio / SFX Dispatch** |
| `0x1020951` | 8 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1020c70` | 5 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1020cdb` | 10 | 1 | buttonclick.wav (0), powerupc2.wav (2), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1020e6e` | 7 | 2 | buttonclick.wav (0), powerupc.wav (1), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1020f89` | 5 | 3 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1021044` | 3 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10210fa` | 8 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), mslogo.wav (6) | None | **Audio / SFX Dispatch** |
| `0x10211f2` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x10212a3` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x102137b` | 9 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), cantgo.wav (17) | None | **Audio / SFX Dispatch** |
| `0x1021494` | 3 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x10214d9` | 5 | 5 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102151a` | 10 | 7 | powerupc.wav (1), combatnetfairy.wav (3), winner.wav (56) | None | **Audio / SFX Dispatch** |
| `0x1021664` | 7 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102178a` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), fireburnout.wav (5) | None | **Audio / SFX Dispatch** |
| `0x102184e` | 8 | 1 | powerupc.wav (1), gantorders.wav (13), gantattack.wav (16), allyyes.wav (53) | None | **Audio / SFX Dispatch** |
| `0x1021915` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x10219e8` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1021a6f` | 10 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1021ba4` | 3 | 3 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1021d3d` | 6 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1021e14` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1021e36` | 7 | 0 | powerupc.wav (1), mslogo.wav (6), powerupd.wav (40), stun.wav (70) | None | **Audio / SFX Dispatch** |
| `0x10220f7` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1022158` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102219c` | 3 | 0 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102221b` | 9 | 1 | buttonclick.wav (0), countdwn.wav (44) | `NETINIT` | **Audio / SFX Dispatch** |
| `0x10222e3` | 6 | 1 | powerupc.wav (1), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10223d7` | 3 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1022428` | 13 | 1 | powerupc.wav (1), powerupc2.wav (2), countdwn.wav (44) | `FDTASK`, `Game started! Go get that`, `STOPTASK` | **Audio / SFX Dispatch** |
| `0x1022689` | 2 | 1 | bombattack.wav (38) | None | **Audio / SFX Dispatch** |
| `0x10226c5` | 11 | 5 | powerupc.wav (1), combatnetfairy.wav (3) | `close AntsMidi` | **Audio / SFX Dispatch** |
| `0x1022873` | 1 | 2 | fireburnout.wav (5), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x10228ff` | 9 | 1 | buttonclick.wav (0), powerupc.wav (1), mslogo.wav (6) | `_^[]` | **Audio / SFX Dispatch** |
| `0x1022b03` | 6 | 1 | theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x1022bd8` | 4 | 1 | fireattack.wav (24) | None | **Audio / SFX Dispatch** |
| `0x1022c57` | 20 | 1 | buttonclick.wav (0), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1023166` | 1 | 1 | theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x10231e8` | 1 | 1 | cantgo.wav (17), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x1023220` | 4 | 0 | buttonclick.wav (0), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x102331a` | 4 | 1 | powerupc2.wav (2), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x10233c9` | 3 | 1 | theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x102342b` | 4 | 1 | powerupc2.wav (2), gantorders.wav (13), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x10234da` | 3 | 1 | gantrdy.wav (14), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x102353c` | 5 | 1 | buttonclick.wav (0), powerupc2.wav (2), gantcommand.wav (15), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x10235db` | 2 | 2 | gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x102361e` | 4 | 1 | theifrdy.wav (18), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x102368f` | 4 | 2 | gantattack.wav (16), theifgo.wav (19) | None | **Audio / SFX Dispatch** |
| `0x102370f` | 4 | 1 | theifattack.wav (20), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x10237ab` | 3 | 2 | gantattack.wav (16), theifdo.wav (21) | None | **Audio / SFX Dispatch** |
| `0x1023810` | 5 | 1 | gantattack.wav (16), firerdy.wav (22) | None | **Audio / SFX Dispatch** |
| `0x1023885` | 1 | 6 | theifattack.wav (20), fireattack.wav (24) | None | **Audio / SFX Dispatch** |
| `0x10238b1` | 2 | 0 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1023951` | 4 | 1 | powerupc2.wav (2), theifattack.wav (20), firedo.wav (25) | None | **Audio / SFX Dispatch** |
| `0x1023a00` | 3 | 1 | theifattack.wav (20), combrdy2.wav (26) | None | **Audio / SFX Dispatch** |
| `0x1023a62` | 11 | 1 | buttonclick.wav (0), theifattack.wav (20), combrdy1.wav (27) | None | **Audio / SFX Dispatch** |
| `0x1023bb9` | 5 | 1 | powerupc.wav (1), gantattack.wav (16), combgo1.wav (28) | `Vf9P` | **Audio / SFX Dispatch** |
| `0x1023c62` | 11 | 3 | powerupc.wav (1), gantattack.wav (16), combgo2.wav (29), bombdo.wav (39) | None | **Audio / SFX Dispatch** |
| `0x1023f3f` | 2 | 1 | combdo2.wav (30) | None | **Audio / SFX Dispatch** |
| `0x1023f81` | 4 | 1 | buttonclick.wav (0), powerupc2.wav (2), bombexp.wav (4), combdo1.wav (31) | `u>f;` | **Audio / SFX Dispatch** |
| `0x1024020` | 2 | 1 | gantattack.wav (16), brdggo.wav (33) | None | **Audio / SFX Dispatch** |
| `0x102405c` | 5 | 3 | brdgat.wav (34) | None | **Audio / SFX Dispatch** |
| `0x10240e2` | 3 | 2 | brdgdo.wav (35) | None | **Audio / SFX Dispatch** |
| `0x1024183` | 2 | 1 | gantattack.wav (16), bombrdy.wav (36) | None | **Audio / SFX Dispatch** |
| `0x10241bc` | 2 | 1 | gantattack.wav (16), bombgo.wav (37) | None | **Audio / SFX Dispatch** |
| `0x10241f5` | 1 | 1 | brdgrdy.wav (32), bombdo.wav (39) | None | **Audio / SFX Dispatch** |
| `0x10242d0` | 10 | 0 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4), theifrdy.wav (18) | `YGf;~$r`, `v`SW` | **Audio / SFX Dispatch** |
| `0x10244a7` | 5 | 1 | None | `UPDSDLIST` | **General** |
| `0x1024504` | 4 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1024559` | 5 | 1 | None | `UPDSDLIST` | **General** |
| `0x10245b6` | 4 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102463a` | 6 | 1 | None | `PATHMGR` | **Level & Map Grid** |
| `0x10246e8` | 12 | 1 | buttonclick.wav (0), powerupc.wav (1), allynot.wav (52) | `WVS3` | **Audio / SFX Dispatch** |
| `0x1024817` | 1 | 1 | None | `CHECKGO` | **General** |
| `0x1024839` | 7 | 0 | powerupc.wav (1), theifrdy.wav (18), allyoff.wav (49), allyon.wav (50) | `Gf;~$r`, `JT;M`, `f9^$vAW` | **Audio / SFX Dispatch** |
| `0x1024aa2` | 4 | 1 | powerupc.wav (1) | `CHECKDROP` | **Audio / SFX Dispatch** |
| `0x1024ae4` | 5 | 1 | None | `Invuln` | **General** |
| `0x1024bc3` | 5 | 1 | None | `COMBEVT` | **General** |
| `0x1024c24` | 7 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1024caf` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | `BTNPUSH` | **Audio / SFX Dispatch** |
| `0x1024cf7` | 4 | 1 | buttonclick.wav (0), powerupc.wav (1) | `ANTPAUSE` | **Audio / SFX Dispatch** |
| `0x1024d85` | 2 | 2 | powerupc.wav (1) | `TIMEOUT` | **Audio / SFX Dispatch** |
| `0x1024dd1` | 6 | 2 | buttonclick.wav (0), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1024eba` | 10 | 1 | fireattack.wav (24) | `PLAYBACK` | **Audio / SFX Dispatch** |
| `0x1024f94` | 8 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1025072` | 16 | 0 | buttonclick.wav (0), powerupc.wav (1), cantgo.wav (63) | `f9y$vSV` | **Audio / SFX Dispatch** |
| `0x1025348` | 5 | 0 | buttonclick.wav (0), losers.wav (42) | None | **Audio / SFX Dispatch** |
| `0x10253b5` | 5 | 1 | None | `SCRBUBBLE` | **General** |
| `0x102541d` | 5 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1025515` | 24 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1025ba9` | 0 | 1 | bombmuffle.wav (74) | None | **Audio / SFX Dispatch** |
| `0x1025be2` | 2 | 1 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1025d0c` | 2 | 1 | buttonclick.wav (0), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x1025dff` | 2 | 1 | buttonclick.wav (0), antdrown.wav (72) | None | **Audio / SFX Dispatch** |
| `0x1025e63` | 4 | 1 | brdgrdy.wav (32) | `_^[]` | **Audio / SFX Dispatch** |
| `0x102603f` | 4 | 0 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102609a` | 15 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | `9f9_`, `Gf;{$r`, `HHtQHt>HtKHt(HH` | **Audio / SFX Dispatch** |
| `0x102648f` | 9 | 1 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x102653f` | 13 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1026904` | 6 | 3 | powerupc.wav (1), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x1026aa3` | 12 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1026f91` | 6 | 2 | powerupc.wav (1), powerupc2.wav (2) | `_^[]` | **Audio / SFX Dispatch** |
| `0x10270d2` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1027197` | 6 | 6 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102737e` | 10 | 2 | buttonclick.wav (0), powerupc.wav (1) | `_^[]` | **Audio / SFX Dispatch** |
| `0x1027530` | 12 | 1 | powerupc.wav (1), combatnetfairy.wav (3), bombexp.wav (4) | `f9^F` | **Audio / SFX Dispatch** |
| `0x10277f4` | 15 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | `f9~FvI` | **Audio / SFX Dispatch** |
| `0x1027aae` | 3 | 2 | buttonclick.wav (0), powerupc.wav (1), combatnetfairy.wav (3), bombexp.wav (4) | `Cf;^$r` | **Audio / SFX Dispatch** |
| `0x1027b51` | 14 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1027e65` | 2 | 6 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1027f07` | 9 | 7 | buttonclick.wav (0), powerupc.wav (1), mslogo.wav (6) | `Ht\HuAj` | **Audio / SFX Dispatch** |
| `0x10281c2` | 5 | 3 | None | `Cf;_$r`, `f9_$v'S` | **General** |
| `0x1028360` | 3 | 5 | powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1028491` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | `_^[]` | **Audio / SFX Dispatch** |
| `0x10285a4` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10285f0` | 0 | 2 | None | `P,9|$`, `R(_^` | **General** |
| `0x1028751` | 3 | 2 | None | `[_^]` | **General** |
| `0x10287b5` | 10 | 2 | buttonclick.wav (0), powerupc.wav (1), gantattack.wav (16) | `QVWj`, `X_^[` | **Audio / SFX Dispatch** |
| `0x1028a60` | 5 | 2 | buttonclick.wav (0), 30sec.wav (54) | `tLf;E` | **Audio / SFX Dispatch** |
| `0x1028b4c` | 4 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1028bdd` | 5 | 1 | buttonclick.wav (0), allynot.wav (52) | `CLEARSEL` | **Audio / SFX Dispatch** |
| `0x1028c44` | 5 | 5 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4) | `Cf;^$r`, `f9^$v#S` | **Audio / SFX Dispatch** |
| `0x1028d30` | 4 | 1 | powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1028ee0` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1028ffe` | 5 | 3 | buttonclick.wav (0), winner.wav (56) | None | **Audio / SFX Dispatch** |
| `0x102908b` | 5 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10290e9` | 5 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1029145` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10291e8` | 9 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029296` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10292dc` | 4 | 59 | None | `Could not find the string` | **General** |
| `0x10293bf` | 3 | 24 | None | `YY_^[` | **General** |
| `0x1029476` | 0 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x10294ee` | 0 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102950f` | 0 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1029582` | 0 | 2 | powerupc2.wav (2), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10295c6` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1029634` | 0 | 1 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x10296a6` | 0 | 1 | gantattack.wav (16) | `Problem!` | **Audio / SFX Dispatch** |
| `0x1029709` | 6 | 3 | powerupc.wav (1), harvest_alt.wav (77) | None | **Audio / SFX Dispatch** |
| `0x102979b` | 1 | 14 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10297b9` | 1 | 9 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10297f2` | 0 | 191 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10298b6` | 0 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10299c8` | 0 | 7 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029aa0` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029b51` | 1 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029b72` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029b86` | 6 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029c1a` | 5 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029c69` | 7 | 5 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029e1f` | 0 | 4 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1029e93` | 4 | 24 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1029fe6` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x102a2a4` | 10 | 1 | gantrdy.wav (14), firestartb.wav (68) | None | **Audio / SFX Dispatch** |
| `0x102a4a2` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1) | `Lock` | **Audio / SFX Dispatch** |
| `0x102a5c4` | 5 | 1 | brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x102a710` | 2 | 0 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x102a76c` | 1 | 0 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x102a7b5` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102a881` | 7 | 2 | theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x102a916` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102a966` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102a977` | 9 | 1 | bombexp.wav (4), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x102ab21` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x102ac4a` | 9 | 1 | buttonclick.wav (0), countdwn.wav (44) | None | **Audio / SFX Dispatch** |
| `0x102ad16` | 5 | 4 | powerupc.wav (1), anthill.wav (48) | None | **Audio / SFX Dispatch** |
| `0x102adff` | 3 | 0 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102b032` | 2 | 1 | allyon.wav (50) | None | **Audio / SFX Dispatch** |
| `0x102b05f` | 1 | 3 | None | `Franklin Gothic Medium` | **General** |
| `0x102b0b5` | 5 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x102b21e` | 4 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102b2fa` | 12 | 0 | powerupc.wav (1) | `9^8~<` | **Audio / SFX Dispatch** |
| `0x102b65a` | 3 | 1 | None | `TXTFLASH` | **General** |
| `0x102b696` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102b7cd` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102b7e5` | 3 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102b820` | 6 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102b8d7` | 7 | 4 | buttonclick.wav (0), powerupc.wav (1) | `[_^]` | **Audio / SFX Dispatch** |
| `0x102b997` | 10 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102bac8` | 17 | 2 | buttonclick.wav (0), powerupc.wav (1), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x102bd22` | 4 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102bd7e` | 2 | 17 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102bdab` | 5 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102bdfe` | 18 | 2 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4), theifattack.wav (20) | `vaj,` | **Audio / SFX Dispatch** |
| `0x102c0db` | 10 | 25 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102c261` | 4 | 0 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102c317` | 16 | 1 | countdwn.wav (44), harvest_alt.wav (77) | `REFRESH`, `SOUNDS`, `SSSP` | **Audio / SFX Dispatch** |
| `0x102c631` | 3 | 1 | mslogo.wav (6) | `Count`, `SUVW`, `_^][` | **Audio / SFX Dispatch** |
| `0x102c765` | 5 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x102c819` | 11 | 1 | bombexp.wav (4), gantattack.wav (16), theifwhip.wav (83) | `CreateSurface`, `GetAttachedSurface`, `SetCooperativeLevel` | **Audio / SFX Dispatch** |
| `0x102cb82` | 3 | 1 | gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x102cc2a` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102ccb4` | 3 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102ccdc` | 0 | 2 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x102cd36` | 9 | 2 | powerupc.wav (1), fireburnout.wav (5) | `Flip` | **Audio / SFX Dispatch** |
| `0x102cefe` | 5 | 2 | None | `BltFast` | **General** |
| `0x102cfef` | 4 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102d193` | 1 | 1 | powerupc.wav (1), gantattack.wav (16) | `Qh^_[` | **Audio / SFX Dispatch** |
| `0x102d24f` | 0 | 1 | None | `Qh^_[` | **General** |
| `0x102d3b1` | 7 | 4 | gantattack.wav (16) | `CreateSurface`, `SetColorKey` | **Audio / SFX Dispatch** |
| `0x102d582` | 5 | 1 | None | `GetEntries`, `GetPalette`, `Restore` | **General** |
| `0x102d71e` | 5 | 1 | firestartb.wav (68) | `SetPaletteEntries` | **Audio / SFX Dispatch** |
| `0x102d7cf` | 1 | 2 | buttonclick.wav (0) | `jdX;` | **Audio / SFX Dispatch** |
| `0x102d82d` | 4 | 1 | buttonclick.wav (0), powerupc.wav (1) | `P(_^[`, `X_^[` | **Audio / SFX Dispatch** |
| `0x102d99e` | 3 | 2 | combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x102da16` | 12 | 1 | buttonclick.wav (0), combatnetfairy.wav (3), bombexp.wav (4), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x102dd07` | 7 | 1 | gantattack.wav (16), combgo1.wav (28) | None | **Audio / SFX Dispatch** |
| `0x102ddde` | 8 | 3 | bombexp.wav (4), firestartb.wav (68) | `CreatePalette` | **Audio / SFX Dispatch** |
| `0x102deda` | 14 | 1 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4), scoredn.wav (88) | None | **Audio / SFX Dispatch** |
| `0x102e1cc` | 12 | 1 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4) | `Lock` | **Audio / SFX Dispatch** |
| `0x102e3ae` | 12 | 2 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4), brdgrdy.wav (32) | None | **Audio / SFX Dispatch** |
| `0x102e557` | 5 | 21 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102e664` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102e7be` | 2 | 2 | None | `~0_^` | **General** |
| `0x102e883` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102e955` | 10 | 2 | None | `!;^8|` | **General** |
| `0x102ea91` | 8 | 1 | scoredn.wav (88) | `DuplicateSoundBuffer` | **Audio / SFX Dispatch** |
| `0x102ebc3` | 7 | 1 | powerupc2.wav (2), fireburnout.wav (5), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x102efdc` | 9 | 1 | gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x102f307` | 9 | 2 | buttonclick.wav (0), powerupc.wav (1), theifattack.wav (20) | `CreateSoundBuffer`, `Lock`, `Unlock` | **Audio / SFX Dispatch** |
| `0x102f507` | 8 | 4 | powerupc.wav (1), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x102f695` | 2 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102f6bd` | 6 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x102f725` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102f777` | 16 | 3 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102f8b0` | 9 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102f977` | 6 | 12 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102fa15` | 7 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102fab7` | 7 | 1 | buttonclick.wav (0), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x102fb6d` | 6 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102fd4d` | 3 | 5 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x102fff8` | 10 | 1 | anthill.wav (48) | `VIEWPORT` | **Audio / SFX Dispatch** |
| `0x1030261` | 0 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1030283` | 4 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1030313` | 6 | 3 | theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x10304ae` | 0 | 4 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103057b` | 1 | 5 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x103078d` | 2 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10308d2` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1030913` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1030aeb` | 7 | 1 | powerupc.wav (1) | `THROWEXC` | **Audio / SFX Dispatch** |
| `0x1030bac` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1030c06` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1030d6a` | 6 | 1 | powerupc.wav (1), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x1030e1f` | 20 | 1 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x103106f` | 11 | 1 | buttonclick.wav (0), powerupc.wav (1) | `SVW3` | **Audio / SFX Dispatch** |
| `0x10311a8` | 5 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103127d` | 7 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1031465` | 0 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10314c6` | 14 | 1 | powerupc.wav (1), theifattack.wav (20), combgo1.wav (28), countdwn.wav (44) | `JOYSTICK`, `MOUSE`, `Wj2WS` | **Audio / SFX Dispatch** |
| `0x103166a` | 8 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1031733` | 4 | 1 | None | `_^][` | **General** |
| `0x10317eb` | 2 | 1 | None | `4h0w`, `BPJoystickInput`, `[^_]` | **General** |
| `0x1031891` | 0 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x103189a` | 0 | 3 | mslogo.wav (6) | None | **Audio / SFX Dispatch** |
| `0x10318a9` | 3 | 1 | None | `%s - %s` | **General** |
| `0x1031916` | 0 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1031b63` | 1 | 1 | gantorders.wav (13) | None | **Audio / SFX Dispatch** |
| `0x1031bbd` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1), gantrdy.wav (14), gantcommand.wav (15) | None | **Audio / SFX Dispatch** |
| `0x1031cdc` | 7 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1031ed0` | 1 | 1 | theifattack.wav (20), allyon.wav (50) | None | **Audio / SFX Dispatch** |
| `0x1031f29` | 9 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10320d0` | 17 | 1 | powerupc.wav (1), powerupc2.wav (2), gantattack.wav (16), anthill.wav (48) | None | **Audio / SFX Dispatch** |
| `0x10323fa` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103248a` | 2 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10324a6` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10324b7` | 2 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10324f5` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103250b` | 3 | 16 | powerupc.wav (1) | `_^[]` | **Audio / SFX Dispatch** |
| `0x10325dd` | 6 | 11 | fireattack.wav (24) | None | **Audio / SFX Dispatch** |
| `0x1032665` | 0 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103269b` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1), gantattack.wav (16), bombrdy.wav (36) | None | **Audio / SFX Dispatch** |
| `0x1032795` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10327fd` | 2 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103284c` | 6 | 3 | buttonclick.wav (0), powerupc.wav (1), allynot.wav (52) | None | **Audio / SFX Dispatch** |
| `0x10328dd` | 10 | 1 | buttonclick.wav (0), powerupc.wav (1), bombexp.wav (4), allynot.wav (52) | None | **Audio / SFX Dispatch** |
| `0x10329d6` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10329e1` | 1 | 2 | bombexp.wav (4), fireburnout.wav (5) | None | **Audio / SFX Dispatch** |
| `0x1032a0e` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1032a2d` | 3 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), allyon.wav (50) | None | **Audio / SFX Dispatch** |
| `0x1032a78` | 1 | 1 | buttonclick.wav (0), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1032a96` | 1 | 1 | mslogo.wav (6) | None | **Audio / SFX Dispatch** |
| `0x1032aa4` | 6 | 1 | buttonclick.wav (0), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1032b7a` | 1 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1032bcc` | 1 | 1 | buttonclick.wav (0), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1032bea` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1032ce3` | 2 | 4 | powerupc.wav (1) | `UNKNOWN` | **Audio / SFX Dispatch** |
| `0x1032d34` | 1 | 2 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1032d77` | 4 | 3 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1032de2` | 5 | 3 | powerupc.wav (1), anthill.wav (48) | `9^\tDj0` | **Audio / SFX Dispatch** |
| `0x1032f4a` | 3 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1032f76` | 1 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1032fc7` | 4 | 3 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1033101` | 6 | 1 | buttonclick.wav (0), powerupc.wav (1), allyon.wav (50) | `Unknown` | **Audio / SFX Dispatch** |
| `0x10331ac` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10332b8` | 5 | 1 | None | `BPDPSC` | **Multiplayer Networking** |
| `0x103331f` | 14 | 1 | powerupc.wav (1), powerupc2.wav (2), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x103351e` | 5 | 1 | None | `BPDPSCKA` | **Multiplayer Networking** |
| `0x103357f` | 4 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10335d0` | 5 | 1 | None | `BPDPSINC` | **Multiplayer Networking** |
| `0x1033637` | 7 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10336e8` | 5 | 1 | None | `BPDPSOUT` | **Multiplayer Networking** |
| `0x1033749` | 7 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10337ce` | 5 | 1 | None | `BPDPPULSE` | **Multiplayer Networking** |
| `0x1033874` | 5 | 0 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1033924` | 5 | 1 | None | `BPDPPERF` | **Multiplayer Networking** |
| `0x1033985` | 4 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1033b14` | 1 | 1 | None | `CLRSOCK` | **General** |
| `0x1033b50` | 14 | 0 | buttonclick.wav (0), gantattack.wav (16), combgo1.wav (28) | `QSUVW`, `VC20XC00U` | **Audio / SFX Dispatch** |
| `0x1033e10` | 1 | 2 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1033e70` | 1 | 0 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1033f10` | 1 | 0 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1034010` | 2 | 0 | buttonclick.wav (0), powerupc.wav (1) | `_^[]` | **Audio / SFX Dispatch** |
| `0x1034120` | 1 | 1 | buttonclick.wav (0) | `]_^[` | **Audio / SFX Dispatch** |
| `0x1034230` | 5 | 1 | combrdy1.wav (27) | None | **Audio / SFX Dispatch** |
| `0x10343b0` | 3 | 4 | buttonclick.wav (0), theifgo.wav (19) | None | **Audio / SFX Dispatch** |
| `0x10349c0` | 3 | 5 | fireattack.wav (24), firedo.wav (25) | None | **Audio / SFX Dispatch** |
| `0x1034ab0` | 1 | 2 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x1034ae0` | 1 | 3 | bombexp.wav (4) | `_^][` | **Audio / SFX Dispatch** |
| `0x1034b80` | 1 | 1 | None | `SUVW` | **General** |
| `0x1034b90` | 3 | 2 | theifgo.wav (19) | `WVSh` | **Audio / SFX Dispatch** |
| `0x1034c20` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1034d20` | 3 | 2 | buttonclick.wav (0), theifgo.wav (19) | None | **Audio / SFX Dispatch** |
| `0x1035120` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1035180` | 4 | 19 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1035240` | 3 | 1 | None | `SUVWj` | **General** |
| `0x1035280` | 1 | 1 | flythumpa.wav (64) | None | **Audio / SFX Dispatch** |
| `0x1035770` | 2 | 2 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1035c70` | 0 | 1 | None | `^_[3` | **General** |
| `0x1035d30` | 1 | 1 | None | `VC20XC00U` | **General** |
| `0x1035dd0` | 1 | 1 | None | `VC20XC00U` | **General** |
| `0x1035e80` | 1 | 2 | None | `VC20XC00U` | **General** |
| `0x1035fe0` | 1 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10360c0` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10360e0` | 1 | 3 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10361c0` | 1 | 2 | gantorders.wav (13) | None | **Audio / SFX Dispatch** |
| `0x10361d0` | 1 | 2 | gantorders.wav (13) | None | **Audio / SFX Dispatch** |
| `0x1036450` | 1 | 1 | buttonclick.wav (0), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10365a0` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1036850` | 2 | 1 | buttonclick.wav (0) | `_^]3` | **Audio / SFX Dispatch** |
| `0x10369b0` | 6 | 1 | firedo.wav (25) | None | **Audio / SFX Dispatch** |
| `0x1036cd0` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1036ec0` | 2 | 1 | combrdy1.wav (27) | None | **Audio / SFX Dispatch** |
| `0x10370d0` | 3 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1037150` | 3 | 13 | powerupc.wav (1), gantattack.wav (16) | None | **Audio / SFX Dispatch** |
| `0x10371d0` | 1 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1037218` | 3 | 0 | powerupc.wav (1) | `]_^[` | **Audio / SFX Dispatch** |
| `0x1037330` | 2 | 4 | buttonclick.wav (0), combatnetfairy.wav (3) | `<program name unknown>`, `Microsoft Visual C++ Runt`, `Runtime Error!` | **Audio / SFX Dispatch** |
| `0x10375e0` | 8 | 1 | powerupc.wav (1) | `L$ I`, `_^][` | **Audio / SFX Dispatch** |
| `0x10378a0` | 4 | 1 | buttonclick.wav (0) | `_^][` | **Audio / SFX Dispatch** |
| `0x1037980` | 3 | 2 | None | `VC20XC00U` | **General** |
| `0x1037a60` | 5 | 2 | None | `_^][` | **General** |
| `0x1037af0` | 5 | 1 | None | `VC20XC00U` | **General** |
| `0x1037c80` | 8 | 1 | powerupc.wav (1) | `VC20XC00U` | **Audio / SFX Dispatch** |
| `0x1037e90` | 2 | 2 | None | `VC20XC00U` | **General** |
| `0x1037f90` | 2 | 6 | None | `VC20XC00U` | **General** |
| `0x1038020` | 1 | 5 | None | `VC20XC00U` | **General** |
| `0x10380e0` | 5 | 21 | cantgo.wav (17), fireattack.wav (24) | None | **Audio / SFX Dispatch** |
| `0x1038270` | 3 | 1 | theifgo.wav (19) | `WVSh` | **Audio / SFX Dispatch** |
| `0x1038300` | 2 | 3 | buttonclick.wav (0), powerupc.wav (1), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x1038470` | 4 | 1 | buttonclick.wav (0), bombexp.wav (4) | `?IsProcessorFeaturePresen`, `KERNEL32` | **Audio / SFX Dispatch** |
| `0x10385f0` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1038770` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x10387e0` | 1 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x10388a0` | 7 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1038a00` | 9 | 1 | buttonclick.wav (0) | `|$ 3` | **Audio / SFX Dispatch** |
| `0x1038bb0` | 3 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1038c20` | 1 | 7 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x1038d00` | 3 | 3 | buttonclick.wav (0), powerupc.wav (1) | `_^]3`, `_^][` | **Audio / SFX Dispatch** |
| `0x1038f80` | 0 | 2 | buttonclick.wav (0), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x10390f0` | 0 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x1039150` | 1 | 1 | None | `_^][` | **General** |
| `0x1039280` | 1 | 2 | gantattack.wav (16) | `SUVW` | **Audio / SFX Dispatch** |
| `0x10392e0` | 2 | 3 | bombexp.wav (4) | `_^]3` | **Audio / SFX Dispatch** |
| `0x1039520` | 0 | 1 | None | `_^]3` | **General** |
| `0x10396a0` | 2 | 1 | None | `_^][` | **General** |
| `0x10399d0` | 5 | 1 | powerupc2.wav (2), winner.wav (56) | None | **Audio / SFX Dispatch** |
| `0x1039b20` | 3 | 1 | powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x1039ca0` | 1 | 1 | powerupc.wav (1) | `QSUVW3` | **Audio / SFX Dispatch** |
| `0x1039cb0` | 5 | 1 | powerupc2.wav (2) | `_^][Y` | **Audio / SFX Dispatch** |
| `0x1039ee0` | 4 | 2 | buttonclick.wav (0), powerupc.wav (1) | `+t$$`, `/;t$$u`, `_^]3` | **Audio / SFX Dispatch** |
| `0x103a110` | 4 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x103a2c0` | 4 | 2 | buttonclick.wav (0), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x103a5f0` | 3 | 5 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x103a760` | 4 | 1 | buttonclick.wav (0), powerupc.wav (1) | `_^][Y` | **Audio / SFX Dispatch** |
| `0x103a8f0` | 2 | 3 | powerupc.wav (1), powerupc2.wav (2) | `_^]3`, `_^][Y` | **Audio / SFX Dispatch** |
| `0x103aae0` | 0 | 1 | None | `GetActiveWindow`, `GetLastActivePopup`, `MessageBoxA` | **General** |
| `0x103abd0` | 9 | 1 | powerupc.wav (1), combatnetfairy.wav (3), bombexp.wav (4), theifattack.wav (20) | None | **Audio / SFX Dispatch** |
| `0x103ad80` | 2 | 1 | powerupc.wav (1), combdo1.wav (31), brdgrdy.wav (32), powerupd.wav (40) | None | **Audio / SFX Dispatch** |
| `0x103b340` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2), gantorders.wav (13) | None | **Audio / SFX Dispatch** |
| `0x103b730` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1), theifattack.wav (20), theifdo.wav (21) | None | **Audio / SFX Dispatch** |
| `0x103b8c0` | 5 | 1 | buttonclick.wav (0), powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103beb0` | 1 | 1 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103c0d0` | 5 | 2 | None | `1_^][` | **General** |
| `0x103c2e0` | 2 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x103c320` | 2 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x103c400` | 2 | 3 | buttonclick.wav (0), cantgo.wav (17) | None | **Audio / SFX Dispatch** |
| `0x103c490` | 1 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x103c560` | 10 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x103c900` | 4 | 1 | cantgo.wav (17), theifrdy.wav (18) | None | **Audio / SFX Dispatch** |
| `0x103cb20` | 2 | 1 | buttonclick.wav (0) | None | **Audio / SFX Dispatch** |
| `0x103cc10` | 2 | 6 | cantgo.wav (17) | None | **Audio / SFX Dispatch** |
| `0x103cde0` | 5 | 1 | powerupc.wav (1), combatnetfairy.wav (3) | None | **Audio / SFX Dispatch** |
| `0x103cff0` | 4 | 1 | powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x103d0d0` | 6 | 4 | buttonclick.wav (0), bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x103d2b0` | 1 | 2 | powerupc.wav (1) | None | **Audio / SFX Dispatch** |
| `0x103d480` | 3 | 2 | bombexp.wav (4) | None | **Audio / SFX Dispatch** |
| `0x103dc10` | 5 | 1 | powerupc.wav (1) | `D$H*` | **Audio / SFX Dispatch** |
| `0x103dfa0` | 7 | 1 | buttonclick.wav (0), powerupc.wav (1), powerupc2.wav (2) | None | **Audio / SFX Dispatch** |
| `0x103e0f0` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | `VWSU`, `_^][` | **Audio / SFX Dispatch** |
| `0x103e220` | 2 | 1 | buttonclick.wav (0), powerupc.wav (1) | `WVSU`, `_^][` | **Audio / SFX Dispatch** |
| `0x103e360` | 3 | 2 | None | `D$*f` | **General** |
| `0x1041ab5` | 0 | 1 | powerupc.wav (1), powerupc2.wav (2), bombexp.wav (4) | `ADVAPI32.dll`, `BeginPaint`, `CreateEventA` | **Audio / SFX Dispatch** |

---

## 2. Inferred C++ Struct Member Layouts (`AntUnit` & Object Structs)
Analysis of `[esi + offset]`, `[ebx + offset]`, and `[edi + offset]` memory dereferences reveals the original struct layouts:

### `ESI` Pointer Dereference Offsets
| Byte Offset | Hex | Functions Accessing | Inferred Field Role |
|:------------|:---:|:-------------------:|:--------------------|
| +  0 bytes | `+0x0` | 400 functions | Unit ID / Vtable Pointer |
| +  8 bytes | `+0x8` | 165 functions | Tile Y Coordinate |
| + 12 bytes | `+0xc` | 122 functions | Pixel X / Subpixel Coordinate |
| + 16 bytes | `+0x10` | 95 functions | Pixel Y / Subpixel Coordinate |
| + 20 bytes | `+0x14` | 80 functions | HP (Hit Points) |
| + 44 bytes | `+0x2c` | 65 functions | Animation Subitem Index |
| + 24 bytes | `+0x18` | 64 functions | Player / Team ID |
| + 36 bytes | `+0x24` | 51 functions | Facing Direction |
| + 40 bytes | `+0x28` | 49 functions | Animation Tick Counter |
| + 28 bytes | `+0x1c` | 48 functions | Unit Type (Worker/Combat/etc) |
| + 76 bytes | `+0x4c` | 47 functions | Unknown |
| +  4 bytes | `+0x4` | 47 functions | Tile X Coordinate |
| + 72 bytes | `+0x48` | 47 functions | Unknown |
| + 14 bytes | `+0xe` | 46 functions | Unknown |
| + 52 bytes | `+0x34` | 44 functions | Attack Cooldown Ticks |
| + 32 bytes | `+0x20` | 42 functions | Current State (Idle/Walk/Attack) |

### `EBX` Pointer Dereference Offsets
| Byte Offset | Hex | Functions Accessing | Inferred Field Role |
|:------------|:---:|:-------------------:|:--------------------|
| +  0 bytes | `+0x0` | 89 functions | Unit ID / Vtable Pointer |
| + 12 bytes | `+0xc` | 15 functions | Pixel X / Subpixel Coordinate |
| + 24 bytes | `+0x18` | 15 functions | Player / Team ID |
| +  8 bytes | `+0x8` | 13 functions | Tile Y Coordinate |
| +  4 bytes | `+0x4` | 12 functions | Tile X Coordinate |
| +  2 bytes | `+0x2` | 10 functions | Unknown |
| + 28 bytes | `+0x1c` | 10 functions | Unit Type (Worker/Combat/etc) |
| + 16 bytes | `+0x10` | 10 functions | Pixel Y / Subpixel Coordinate |
| + 20 bytes | `+0x14` | 7 functions | HP (Hit Points) |
| + 36 bytes | `+0x24` | 7 functions | Facing Direction |
| +  1 bytes | `+0x1` | 6 functions | Unknown |
| + 14 bytes | `+0xe` | 5 functions | Unknown |
| + 22 bytes | `+0x16` | 5 functions | Unknown |
| +104 bytes | `+0x68` | 5 functions | Unknown |
| + 40 bytes | `+0x28` | 5 functions | Animation Tick Counter |
| + 32 bytes | `+0x20` | 5 functions | Current State (Idle/Walk/Attack) |
