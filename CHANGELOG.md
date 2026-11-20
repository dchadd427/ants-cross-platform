# Changelog

What changed in each release of the Ants remake, newest first, a few lines each: what a player sees and, only when it changed, the rules or the network protocol. The version is the one line of the file `VERSION` (how it moves: [`docs/WORKFLOW.md`](docs/WORKFLOW.md)); every build also has a build id, the short git commit, shown by `ants --version`, `ants_server --version` and the footer of the web page.

An entry is 5 - 15 lines in a fixed template: the heading `## vX.Y.Z - YYYY-MM-DD - title`, then **For players:** (1 - 6 bullets), **Rules / network:** (only if the rules or the network protocol changed: what, and the protocol number), **Fixes:** (optional, one line each), **For operators:** (optional: what only the person who runs a server or the site sees) and **Details:** (a link to the commit range). The "What's new" window of the site shows the bullets under **For players:** and **Fixes:** and nothing else, so a line about the administration page, a server setting, an environment variable or the database goes under **For operators:**; a release that has only such lines has no **For players:** paragraph. No test counts and no mutation or review lists: those belong in commit messages and documents. A change that goes live writes its entry under **Next** (a section headed `## Next`, above the newest release) in its own pull request, and `tools/release.py` turns it into the entry when `VERSION` moves (each such change bumps the version, by default the patch number).

<!--
Template of an entry (copy it, keep the labels and the order, leave out a paragraph that has nothing to say):

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet: what a player sees or can do now

**Rules / network:** only when the rules or the protocol changed: what, and "network protocol N: a vA.B.C game cannot join"

**Fixes:**
- one line each (optional)

**For operators:** only for what a person who runs a server or the site sees (the administration page, a setting, an environment variable, the database); a release with only such lines has no "For players" paragraph
- one bullet each

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ed3f1214...b2150865)

Work that is not released yet is written in the same template under a heading that says only "## Next" (no version, no date), above the newest release;
`tools/release.py X.Y.Z "title"` turns that heading into "## vX.Y.Z - date - title" and refuses when there is no "## Next" or it is empty.
-->

## v0.18.0 - 2026-11-19 - Dropped food can be taken only once

**For players:**
- **Food dropped by a dying ant can be taken only once.** When a thief carrying food died on a spot where a food pile had already been emptied, its lunch box could be eaten again and again for full points. Now the first ant to take it empties it, and it is gone. The 1998 game has the same flaw; this fix is a deliberate change from it.

**Rules / network:** the original game's rules number moves from 9 to 13 (game mode 187 keeps 12); recordings of earlier numbers still play as they were recorded. Network protocol 25: a v0.17 page or game cannot join a v0.18 match, so reload the page.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/837d81c1...9e729c46)

## v0.17.13 - 2026-11-19 - Phone fixes: keyboard in Options, watch bar, countdown, full-screen exit

**For players:**
- **Phones: the quick chat keys (F9 - F12) in Options open the keyboard** when tapped, and keep what you type.
- **Phones: the watch page's controls fit on one row** at every phone width; the settings gear sits above the end of the timeline.
- **Phones: no Fullscreen button covers the players on a small countdown card;** the Full button at the top is the way.
- **iPhone held sideways in full screen:** the round exit button sits in the black margin beside the picture, no longer over the minimap.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.12 page or game still joins.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b842a06e...11d4a225)

## v0.17.12 - 2026-11-19 - New windows, a Fullscreen button, live matches at once, alerts

**For players:**
- **A new look for the game's windows.** Options, Quick Help, the quit question and the other windows over the match get brushed-metal buttons, sliders and switches with the New art; Classic keeps the original's. The game behind an open window is darkened evenly.
- **A Fullscreen button in the game's top bar,** right of Quit. On an iPhone, where Safari has no full screen for pages, it uses the page's own full screen.
- **Matches show as in progress the moment they start,** with Watch. A match between people is shown to watchers 30 seconds late; matches of computer players are shown almost live.
- **Alerts on Ants & Uncles:** a note when someone joins your room, and the tab blinks when your match starts while you are in another tab. Both can be turned off under the bell, next to the menu button.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.11 page or game still joins. Under the hood: the game, the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/30ddce44...9548c0a5)

## v0.17.11 - 2026-11-18 - Your orders act at once online, one window for watching

**For players:**
- **Your own orders act at once in online matches.** Your ants now answer a click right away on your screen, as in a game on your own computer, whatever your connection. Attack clicks on a moving ant land far more often, and a well-timed re-order on mud works again with a slow connection. The server still decides every result, so nothing changes for the other players.
- **Watching no longer leaves you with two front pages.** Watch and Matches open in the same tab; while you sit in a room they open a new tab, and Home in that tab closes it and brings you back to your room.
- **The Games count includes matches in progress.**

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.10 page or game still joins. Under the hood: the game and the web pages are rebuilt. To switch the new behavior off for one game, add `?prediction=off` to the game page's address.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/cb9b01b0...c59ad097)

## v0.17.10 - 2026-11-18 - Bot matches on the administration page, longer replay storage

**For players:**
- **Replays are kept longer.** The server now keeps up to 1 GB of matches for up to 90 days, and matches of computer players have their own smaller share, so they do not push out matches played by people.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.9 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**For operators:**
- **Computer-player matches can be started and scheduled from the site's administration page.** Start one or several at once, stop them, and set repeating schedules in Pacific time. Setting `ANTS_BOT_MATCH_EVERY_MIN=0` switches schedules off on the server.
- **The database moves to schema 9 on the first start.** An older server cannot open it afterward.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/109000f6...4a6b395e)

## v0.17.9 - 2026-11-18 - New pointers, Map Maker clumps and real map pictures

**For players:**
- **New pointers with the New HUD art.** Every cursor is redrawn to match the new look, sharp at larger window sizes, and the two places where the frame's pieces met badly are mended. Classic keeps the original's.
- **Map Maker clumps.** Each tap in Clump puts a new clump of up to 250 ants and leaves the earlier ones where they are. Pick up moves one clump, and Erase takes away only the clump you touch.
- **Better pictures for players' maps.** Submitted, approved and test maps now get a picture drawn by the game itself, like the library's maps.
- **More maps in the computer players' own matches.** POPcOrN, HELLRAISER and Bombz Away join Highest score, and Fight Island joins 187.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.8 page or game still joins. Under the hood: the game server and the web pages are rebuilt, and the new front page at playants.org may read the public match numbers.

**Fixes:**
- On a phone, the Map Maker's map could stay stretched after choosing a tool, so a tap landed on the wrong cell.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/7c0381af...0469f31d)

## v0.17.8 - 2026-11-17 - Hard computer players keep hitting

**For players:**
- **Hard computer players keep hitting.** In a fight a Hard bot now gives its next blow far sooner, so an ant it attacks gets away less often. Medium and Easy play as before.
- **Computer players on ISLANDS** no longer send an ant over a bridge that is about to go.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.7 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/48c230ad...6c9f09fc)

## v0.17.7 - 2026-11-17 - Back to the room after a match, ISLANDS bridges, Map Maker edges

**For players:**
- **Back to the room after a match.** The game's own Leave Game on the results screen now brings you back to your room, like the button under the game, and a public room stays public for the rematch.
- **Hosting and watching a replay.** While you are in a room, Matches opens in a new tab, so your room stays yours; a page that comes back within a minute takes its seat again, and a seat that ended says so.
- **Computer players on ISLANDS dig bridges** (Hard): a Swimmer digs the cheapest flower link first, a bridge home for hurt or stranded ants, to food on another island and to a Fire or Bomber power-up.
- **Map Maker:** edge pieces show while you paint (no black squares), new water gets a ring of mud beside it with Auto edges on, and places where the game has no edge piece are marked.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.6 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b7ff8ec0...845c2214)

## v0.17.6 - 2026-11-17 - One admin secret, and a closed admin page says why

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.5 page or game still joins. Under the hood: the chat hub, the game server and the web pages are rebuilt.

**For operators:**
- **For the site's admins:** the admin page needs one secret setting instead of two, and when it is closed it now says which part is off. Players see no change.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d148d9ea...711aff60)

## v0.17.5 - 2026-11-17 - A new look for the match screen, faster match history, 187 computer-player matches

**For players:**
- **A new look for the match screen.** The frame, the panel, the pedestals and the buttons are redrawn, sharp on phones and big screens, in the original's colors and shapes. The original's art stays one setting away (the settings menu: HUD art, New or Classic).
- **Matches show up in the history within seconds** of their end, also while the server's own computer-player matches run, and a player's own approved map shows its picture in the history and in Open games.
- **The server's computer-player matches play 187 too:** a coin decides each one, and the Live list marks a 187 match.
- **Map Maker:** typing in a box (the maker's name, the numbers of the Ants panel, the description) keeps the cursor in it.
- **Chat:** an administrator's long message no longer draws over the next one on a phone.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.4 page or game still joins. Under the hood: the game server and the web pages are rebuilt; the web download is larger (the new art: about 12 MB on the wire).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1ec8e125...dd11f9f7)

## v0.17.4 - 2026-11-17 - The screen stays on, steadier Menu, bots that cross bomb fields, a Discord admin page

**For players:**
- **The screen stays on.** A phone or tablet no longer goes dark while you play a match or watch a replay (where the browser supports it); a paused replay lets it sleep again.
- **The front page's Menu button stays put** while the page loads on a phone, instead of jumping from the left to the right.
- **Computer players handle bomb fields better.** They walk several tiles per order through a bomb field instead of one, take a power-up they are standing on by clicking their own tile, hop over a fire wall and a lake to reach a power-up, never plant a bomb under an ant, and try a fresh plan when they get stuck (POPcOrN, the CrUnChY BiTe maps and the other bomb-field maps).

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.3 page or game still joins. Under the hood: the game server's database moves to version 8 (the old admin accounts are removed; an older server cannot open the new file), and the chat hub and the web pages are rebuilt.

**For operators:**
- **A new admin page.** Site admins sign in with Discord on the admin page to review the maps players send, handle chat bans and mutes, read the moderators' log and see how the site is doing; the old admin passwords are gone. Players see no change.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/433de635...4fb44bf6)

## v0.17.3 - 2026-11-16 - A Shift button for touch screens, moderation from the Players list, cleaner scores

**For players:**
- **A Shift button for touch screens.** The hotkey row on a phone or tablet has a Shift button: tap it and your next tap or drag on the map is a Shift-tap, as with a keyboard's Shift (add an ant to the selection or take one out, so a Bomber can be picked on its own); press and hold it to keep Shift on until you tap it again.
- **Moderators act from the Players list.** A moderator or admin opens the same moderation sheet from a person in the front page's Players list, not only from a chat line. Players see no change.
- **Cleaner scores and clock.** The scores and the match clock in the game's top bar use the game's numbers without the black outline.
- **ISLANDS computer players (Hard) waste fewer ants.** Spare ants go and take the Swimmer the flower drops, and corner rows that nobody works are reached on foot.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.2 page or game still joins. Under the hood: the game server, the chat hub and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/6fb219d9...81ef4f92)

## v0.17.2 - 2026-11-16 - Fairer computer players in 187, chat moderation from a phone

**For players:**
- **Fairer computer players in 187.** Each computer player now opens on a different side instead of all of them rushing the same one (on TINY that was always the first seat), and a side down to four ants or fewer sends one ant at each enemy instead of two that get in each other's way.
- **Chat moderators can act from a phone.** A moderator or admin right-clicks a line in the front page chat, or presses and holds it on a phone (or picks it with the arrow keys and Enter), to delete it, mute, kick or ban its writer, or see that person's recent messages in the chat. Players see no change.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.1 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/da6092fb...443b5fbb)

## v0.17.1 - 2026-11-16 - Longer chat lines, bold numbers on every results column, computer players defend better against Thieves

**For players:**
- **Longer chat lines and a faster pace.** A line in the front page chat can be 1,000 characters (was 200). You can send 10 lines at once and then two a second, up to 600 lines an hour and 3,000 a day. The emoji picker follows the new length. The room chat in a game is unchanged.
- **Every number on the results page is bold.** Ants lost, ants killed and hatched now use the same bold face with an outline as the score, on the normal and the 187 results page. In the narrow columns of the classic page a three-digit number is squeezed to fit, where the old face cut off its last digit.
- **Computer players defend better against Thieves.** A Thief on its way to a raid looks again: when walls, bombs or a closed way meet it, it turns to another hill; a Fire Ant is no longer sent to a bank it cannot walk home from.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.0 page or game still joins. Under the hood: the chat server (it reconnects by itself within seconds), the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d63874e8...4a74eadd)

## v0.17.0 - 2026-11-15 - 187 drops nothing, leave your room from Home, a better emoji picker, bots walk round bombs

**For players:**
- **187 puts nothing on the ground.** In a 187 match an ant of a special type (Combat, Bomber, Fire, Swimmer, Thief and the rest) that dies no longer drops its power-up, so a 187 match has no power-ups, no food and no healing from start to end. The original's game is unchanged: there a special ant still drops its power-up.
- **Leave your room from Home.** A small red X on the corner of the My room button leaves the room (when other people are in it you are asked first), and the button now reads My room with its count at every window size. The Games button is gone from Home's top bar: the open games are always on Home, and a phone's bottom strip has Games.
- **A better emoji picker** in the front page chat: 879 emoji in nine groups, a search field (type cat, heart, ant...), your recently used ones, a name line, full keyboard use, and on a phone held upright a sheet that takes the keyboard's place.
- **Computer players walk round bombs** they can see instead of into them, a Bomber clears a bomb that blocks the way to food, and they skip food piles they cannot reach. A Bomber's flight checks its landing spot first, so it no longer lands in the water when something lies there.

**Rules / network:** network protocol 24: a v0.16.11 page or game cannot join (an old page says that a newer version is out: reload it). The replay rules number of 187 is 12; 187 recordings made before keep their old rule and play as they were recorded. The original's game, its rules number and its recordings are unchanged. Under the hood: the game server and the web pages are rebuilt; matches running at the update end.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/06727fbd...2f426f13)

## v0.16.11 - 2026-11-15 - Away on the Players list, press outside a dialog to cancel, computer players that walk where they go

**For players:**
- **People who leave the page stay on the Players list as Away.** When you switch tabs, minimize the window or lock your phone, your name stays in the list for 10 minutes, dimmed, in an Away group at the bottom (staff who are away stay under Staff). It goes back to normal the moment the page returns, and the chat reconnects at once if the connection dropped while the page slept. Logging out, a kick or a ban removes the row right away. Nothing about Away is saved on disk.
- **Press outside a dialog to cancel it.** On every popup (host a game, How it works, the map list, the in-game menu, the Map Maker's boxes, the admin page's boxes) a press on the dimmed page around the box does what Cancel does. A drag that starts inside the box, a text selection or a scroll does not close it. In the game menu with the Leave game question showing, it means Stay.
- **Computer players no longer give orders an ant cannot walk to.** The "Can't go there" messages from computer players on ISLANDS are almost gone, and their ants no longer waste time on those orders.
- **Hard computer players on ISLANDS fly the Bomber to the ring pile earlier.** A Bomber is no longer held back by a mine it cannot reach, so the flight starts much earlier. Easy, Medium and the other maps play as before.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.10 page or game still joins. Under the hood: the chat server, the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/25a64658...6b375eeb)

## v0.16.10 - 2026-11-15 - Chat box on one row, computer players take back power-ups and contest the ISLANDS ring

**For players:**
- **The chat box starts as one row.** On the front page the field, the emoji and microphone buttons and Send are on one row in every window. When your words need a second line, the field takes the top row and the buttons and Send move to a row under it at the right; it goes back to one row when the words fit again. The box has the same rounded corners as the site's buttons.
- **Computer players take back power-ups.** Medium and Hard computer players now pick up a power-up that was dropped by an ant of a kind their team has lost, such as a Combat or Swimmer power-up. Hard also goes for dropped Fire, Bomber and Thief power-ups of other sides. Easy plays as before.
- **Hard computer players on ISLANDS contest the ring.** One of their Bombers flies to the island of a ring pile and lays mines on the way to it, so a rival's Swimmers are thrown back. A Swimmer that was hit is sent back to work, each ring pile gets three Swimmers, and the crew digs the one tile that links the ring to the flower strip beside it so that Swimmer power-ups can be fetched. Easy, Medium and the other maps are unchanged.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.9 page or game still joins. Under the hood: the game server and the web pages are rebuilt; the chat server is unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/491ca15c...c52809d1)

## v0.16.9 - 2026-11-14 - Numbers in the game's font, chat links and times, Discord server nicknames, a resizable front page

**For players:**
- **Numbers in the game's own font.** Hit points (Ctrl+L), damage and heal numbers and score numbers are drawn in the same letters as the game's labels, white with a thin dark outline (heal numbers pale green) and sharp at every window size. A damage or heal number now starts on the middle of the ant, keeps one size, floats up, fades and shrinks as it goes.
- **Chat links and times.** An address that starts with http://, https:// or www. in a chat line is a link that opens in a new tab (long ones are shortened in the middle; the whole address is in its tip). Every line starts with the time, in your own time zone and clock style, with a small Today / Yesterday / date divider between days. Lines no longer show numbers; staff see the number in the tip of the time.
- **Your Discord server nickname is your chat name.** The name shown is now your nickname in the project's Discord server, else your Discord display name, else your username. The switch in the chat menu still offers the username. A changed nickname shows from the next roles check, within a few minutes; old lines keep the name they had.
- **A front page you can resize.** On a wide window (701 pixels and up), drag the join between Open games and Players, or the top of Global chat, to give one more room. Arrow keys move a handle, Enter or a double click puts it back, and the sizes are kept in this browser. A phone has no handles.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.8 page or game still joins. Under the hood: the chat server changed, so it restarts and chat reconnects by itself within seconds; it also holds code for a future admin page that no page uses yet.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a364d921...f2d101bf)

## v0.16.8 - 2026-11-14 - Game page top bar, Playing in the Players list, one chat box, Discord pictures without a new login

**For players:**
- **The game page has the site's top bar.** A game against the computer, a joined match, a recording and a live view show the same bar as every other page: Home, the links, the Settings gear, Fullscreen and Menu. The footer is gone; the version is the last line of the Menu. During a match, Home asks first and the other links open in a new tab, and while the Menu is open the keys stay with the bar and go back to the game afterwards. Reload page, which asks first during a game, is in the Menu.
- **The Players list shows who is Playing.** A public match shows its map and players, a private room or game shows a lock and no details, and a game against the computer reads "<map> . vs computer". The Games list no longer shows an empty "In progress" box while no match is running.
- **One chat box everywhere.** The box with the field on top and the emoji, microphone and Send buttons under it is now used in every window; the field grows to two lines on a wide window and the chat area is a little taller.
- **Discord pictures arrive without logging in again.** A picture is picked up while you are logged in: within a few minutes for members of the project's Discord server, otherwise about a day. The Legal page says so.
- **Live mark.** The red "N live" mark reads "N live: see them on the front page", shows its dot in forced-colors mode and updates when you come back to the tab. The old Live now addresses lead to the front page in any letter case.
- **Chat staff commands.** Staff can name a person by the number of the chat line or by their row in the Players list, list the mutes with /mutes, and a ban can also delete that person's lines of the last hour.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.7 page or game still joins. Under the hood: the chat server changed, so it restarts and chat reconnects by itself within seconds.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f6d76998...1b43c0b5)

## v0.16.7 - 2026-11-14 - Phone hotkey buttons, damage and heal numbers, Automatic picture shape

**For players:**
- **Hotkey buttons on a phone.** While you play, a phone shows buttons for the game's keys, All ants first, then Stop, Home, Next and None, and a More button for the other keys. Held sideways they form a rail beside the picture, held upright a row under it. The "Touch buttons" row of the game's Settings turns them off.
- **Damage and heal numbers over the ants.** A small -N shows over an ant that loses hit points and a +N over an ant that is healed at its hill. They are on by default, never shown under fog of war, and the "Damage & heal numbers" row of the game's Settings (Video) switches them off.
- **Automatic picture shape.** The Picture shape list starts with "Automatic": the shape nearest your screen when the phone is held sideways, 16:9 when held upright, following the way you hold it. A shape you pick is kept for the way the phone was held; every browser starts on Automatic once.
- **The countdown card's Fullscreen button fits the card on a phone.** It takes the size and place of the card's own button and stays inside the card.
- **Quick host, My room and Join use your Discord name when you are logged in.** A guest answer is no longer kept until the page is reloaded.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.6 page or game still joins. Under the hood: the numbers change nothing in a match.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f9011b41...a795f86a)

## v0.16.6 - 2026-11-13 - Chat roles from Discord and Discord pictures in the Players list

**For players:**
- **Chat moderators and administrators now come from the project's Discord server roles.** Whoever holds the moderator or administrator role there has those powers in the front page chat, within a few minutes of getting or losing the role, and the chat shows them: staff names get a color, the Players list puts them first, and moderators see line numbers. The commands are /mute, /unmute, /del and /kick for both roles, and /ban, /unban and /bans for administrators; a moderator cannot act on another staff member. If the check with Discord cannot be made, no power is given and the chat keeps working. The old hand list setting (ANTS_HUB_MODS) no longer does anything.
- **Discord profile pictures in the Players list.** When you log in with Discord, the site keeps one small copy of your profile picture, only while you have a login, and shows it in your circle in the Players list and in the account button at the top. It is shown to everyone who opens the front page unless you switch it off with "Show my Discord picture" in the account menu, or the site's operator hides it. Chat lines still show names only. Switching it off deletes the copy at once. The Legal page says what is kept and for how long.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.5 page or game still joins. Under the hood: the chat server gained the role and picture code and its tests.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/59cbc736...3b57c6ae)

## v0.16.5 - 2026-11-13 - A countdown card for solo play, computer players stay out of your base, Settings anywhere and no Live now page

**For players:**
- **A countdown card with a Fullscreen button when you play alone against computer players.** A server room with one person in it (with or without computer players) opens with the "Get ready. Fullscreen is one press away." card for 10 seconds, and a game on this computer in the browser does the same, showing the name you typed (the desktop game keeps its 5-second dialog unless asked). Rooms with two or more people keep the 30-second countdown; recordings, live views and rejoins get no card. A server setting, --solo-countdown-seconds (default 10), sets the one-person time.
- **Computer players leave your base alone.** Medium and Hard computer players no longer walk into another player's base to defuse bombs or put out fire walls: a player's own walls and bombs at home are left to that player, an ally's hazards follow the same rule, and the last ants of a computer player stay home.
- **The game's Settings work before a match too.** The gear opens a small panel with Fullscreen, Picture shape and Fullscreen mouse on the game's own screens. This corrects the v0.16.4 note: outside a match the gear was greyed out (its tip said "Not now"), and now it works anywhere. Held keys no longer reach the game when the panel is open.
- **The Live now page is gone.** Running matches are listed on the front page with a Watch button, and the page's old addresses lead there. While matches run, the top bar shows a small red "N live" mark that leads to the front page. The installed app's Live now shortcut is gone too.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.4 page or game still joins. Under the hood: several tests are steadier.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/fb6cf52c...13968e10)

## v0.16.4 - 2026-11-13 - The phone chat box, Settings rows in the game, a new icon, no logo and faster site updates

**For players:**
- **A roomier chat box on a phone held upright.** The text field is on top and a row of buttons (emoji, voice and the rest) sits under it. The field grows as you type, Enter sends, and a small bubble shows while the voice button is listening.
- **No logo at the top of the pages.** The top bars give the room to their buttons instead. A recording, a live view and the Map maker's list of maps now have a Home button.
- **A new icon: the ant's head.** It is the icon of the installed app and of the browser tab. An installed copy picks it up when the browser next refreshes the app's details; an iPhone home screen icon changes only when the app is removed and added again.
- **Picture shape and Fullscreen mouse are rows of the game's Settings.** There is one Settings gear in the game page's top bar; the strip under the game and the room's Picture row are gone (a recording's bar keeps its Picture button). For now these two rows can be changed during a match only: outside a match the menu shows "Not now". A later release will make them work everywhere.
- **No footer on the pages that have the top bar** (Matches, Live now, the map maker and the others). The version and build line is now the last line of the bar's Menu.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.3 page or game still joins. Under the hood: the site's images are built in layers that are reused when only pages or the version change, so later site updates are faster (the first one is slow, since nothing is reused yet), and several tests are steadier.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4eb281f8...870b4439)

## v0.16.3 - 2026-11-12 - The installed app's front page fits the window

**For players:**
- **The front page fits the window in the installed app on a phone.** The bottom tabs (Games, Players, Chat) are no longer cut off by the phone's bars: the page takes the smallest height that the window reports. The game page and the map list use the same care in the installed app.
- **Screen numbers for a bug report.** Add ?vp to the end of the front page's address, or press and hold the app's icon and choose Screen numbers, to see a small box with the window's sizes for a screenshot. It sends and saves nothing.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.2 page or game still joins.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4e6c7c0c...f436bd12)

## v0.16.2 - 2026-11-12 - One map chooser in the room lobby and the site can be installed as an app

**For players:**
- **One chooser for the map in a room.** The room shows the map as one card with its picture, name and facts. The host's Change map button opens a single list of every map the game server offers: the six original maps first, then players' maps, with a search box. The old drop-down and the Browse all maps button are gone.
- **The site can be installed like an app.** In a computer's browser use Install in the menu; on a phone use Add to Home screen. It is called Ants! and opens in a window of its own without the browser's bars.
- **It still needs a connection.** The installed app is the same site: there is no offline mode, and nothing works without the network.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.1 page or game still joins. Under the hood: the size limits of documents, code files and web files sit in one place, one file reads the saved seat of a running game for the front page and the game page, and the testing guide now has written rules for tests and mutants.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/635fcae2...7298d031)

## v0.16.1 - 2026-11-12 - Clean page addresses, a clearer voice button, Have a code, Host a game, a hideable replay bar and one top bar

**For players:**
- **The voice button asks for the microphone and says why when it cannot work.** The front page now allows the microphone, and when voice to text cannot run the chat says why in plain words.
- **Page addresses no longer end in .html.** The front page, the game, Matches and the other pages open at short addresses, and an old link with .html still opens the same page.
- **"Have a code?" opens the Join by code box,** and you can paste a whole room link there instead of only the code.
- **Host a game is a button in the empty Games list.** The hint in the Players list is gone.
- **In a fullscreen replay the arrow hides the bar, and resting the pointer near the bottom brings it back.** Only one arrow shows at a time, and the B key hides and shows the bar too.
- **One bar at the top of the other pages.** Matches, Live now, the map maker and the other pages share one bar with links to each other.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.0 page or game still joins. The rest of this release is internal: unused code and staging leftovers removed, changes to the build checks, and nine tests made deterministic.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/47ca2367...71c9d830)

## v0.16.0 - 2026-11-10 - A top bar, Quick host asks Public or Private, chat emoji and voice buttons, and a new look for the buttons

**For players:**
- **A top bar on phones.** The front page has a bar at the top with your name; the login chip is the name chip, and tapping it opens the account menu (the Forget me control is in that menu and asks once more).
- **My room is there only when you have a room.** Quick host now asks Public or Private before it makes a room, and Host one! goes straight to a public room.
- **The chat has an emoji picker and a voice-to-text button.** The voice button uses the browser's own speech service: Chrome, Edge and Safari send the audio to their maker; the site records nothing.
- **The green buttons have a stamped look,** with a double frame, a weave and a deeper press. The Leave room button is red like Leave game.
- **The Games list shows running games without a switch,** under the open games, so nothing needs scrolling when no game is open; the "Show games in progress" switch is gone.
- **The Players list has no fold button,** and the chat no longer shows a second username next to a name to moderators.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.15.1 page or game still joins. The stack no longer has the one-shot helper of the results folder; the operator may delete the exited container ants-results-init in Portainer. The rest of this release is internal: tests and documents.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/629baa19...4396bf2d)

## v0.15.1 - 2026-11-10 - The game server can always write each finished match's result file

**For players:**
- **The game server can always save a finished match's result.** A change to the server stack makes sure the folder where each finished match's result file is written belongs to the game server, also on a server whose storage was made by an older image. Nothing changes on the web pages or in the game.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.15.0 page or game still joins. The rest of this release is internal: tests and documents.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5b17384e...0a24e791)

## v0.15.0 - 2026-11-10 - A three-panel Home, notices over the page, and games in progress with Watch

**For players:**
- **A new Home.** Games are on the left, Players on the right and the chat at the bottom; the Yard is no longer on the front page. On a phone you see one view at a time, with a strip of buttons to move between Games, Players and Chat, and a phone held sideways keeps the account line.
- **Notices float over the page.** A notice such as "Your room is public now." is a small box over the page, so the page no longer moves when it comes; it goes after 6 seconds, and one that asks something (Rejoin, Make public) stays until you answer. OK closes it, and so does Escape while it has the focus.
- **Games in progress stay in the Games list.** A match that is running is a row of the list with a Watch button, which opens the live match in a new tab. The switch "Show games in progress" at the top of the list hides these rows; this browser remembers the choice.
- **The chat keeps the newest 200 lines.** Besides the limit of 7 days, the chat's history keeps the newest 200 lines; older lines are deleted for good (the Legal page says so).

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.14.1 page or game still joins. The rest of this release is internal: tests and documents.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f6969023...5aee057e)

## v0.14.1 - 2026-11-09 - Discord names in games, the Players list, replay files, and a Hard bot that answers a blow

**For players:**
- **Your Discord name in rooms and games.** On the website nobody types a name any more: if you are logged in with Discord (Log in, in the chat), rooms and games show that name; everybody else plays as "Guest" and six digits, kept by this browser. A name with letters the game cannot show uses the Discord username instead.
- **Display names in the chat.** The chat shows your Discord display name, and the More menu has a switch to show your username instead. A login from before this version shows the username until you log out and in again. Moderators see the username next to a display name.
- **Who is here.** Home has a Players list: the people on the front page now, grouped as waiting in a room, playing and looking around (guests are counted, not named). The host of a public room that waits has a Join button in the list. On a phone the list opens from the Players button.
- **The Games list** shows the picture of a map that a player made, and on a wide window the docked list sits in the middle. On a narrow phone the label of the Room's Public | Private switch stays inside its button.
- **Watch a replay file you downloaded.** Matches has "Open a replay file" (or drop the file on the page): it plays in this tab and is not uploaded.
- **The Hard computer player answers a blow.** When one of its ants is struck, that ant and the ants near it that can strike answer together, as many as the attacker has there and one more (five at most), and keep fighting while their side is at least three fifths as strong. Medium, Easy and the game type 187 play as before. The desktop game's Hard player does the same.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.14.0 page or game still joins. The rest of this release is internal: large source files split into smaller ones, tools and tests for the computer players, and computer-player work that is built but switched off.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/26714f6f...4fd8a6ad)

## v0.14.0 - 2026-11-07 - New rules: 187's closed hill, the last side standing, fire walls and long orders

**For players:**
- **A new game version; older ones cannot join.** A v0.13 game is refused, and a page that stayed open shows "A newer version of Ants is out. Reload the page to update."
- **The game type 187 has a closed hill.** No ant can enter its hill, so nothing heals: an order to go home is refused ("Can't go there.") and an ant at 1 hit point no longer walks home. 187 also ends fairly in two rare cases: a team with only eggs it cannot hatch (no hill) no longer keeps a match going, and a room with more seats than teams no longer ends it at once.
- **The original's game ends when one side is left and no food can score** (before, it played on to the clock). The highest score wins, even for a side with no ant left; a tie for the top, all 0 for example, goes to the side still standing. Power-ups and bombs are not food. It shows on the 29 library maps with no food, such as ARENA and FIGHT.
- **Five fire-wall maps play a little differently** (ANTS_PARTY-V.3.0.5, A_Coaster_Mix_Turnament_Map, Welcome_from_chaos, thief craker2.0, ~~PlAyEr'S_rEvEnGe2): a fire wall that the map file places is free ground once it is put out, as in the original (a dying ant's power-up and the computer players' bombs can use its cell).
- **Long orders on big maps.** The wide search finds a path for an order of 450 tiles, where the original gives up at about 400. The Map Maker (v1.8.0) gives it to a new Extended map by itself where the map plays like the original with it, and the side panel shows "Rules 2" (long orders). A map of the original's rules that is taller than 90 rows keeps its wall at row 90 when it is resized and stays taller (before, Resize took it down). Earlier game versions cannot open a Rules 2 map.
- **Old recordings still play** under the rules they were made with. When the server is updated, matches still running end.

**Rules / network:** network protocol 23 (was 22): no message changed, but 187, the end of the original's game and the fire walls of map files play differently, so a v0.13 game cannot play with this one. The replay rules number is 9 for the original's game (was 5) and 11 for 187 (was 6); recordings of 5 and 6 play as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/76545ba9...b86dab0c)

## v0.13.2 - 2026-11-06 - Per-visitor limits, page headers and a chat store that keeps its size

**For players:**
- **Page headers and checks.** Every page sends a content policy, a framing rule, a referrer policy and a permissions policy; browsers are asked to use https for a week; a game connection from another site's page is refused; the access log keeps paths without queries; the three big game files are compressed once, at build time.
- **Limits per visitor behind a reverse proxy.** The web image's default list of proxy addresses (the private and internal ranges) lets the limits count each visitor's own address behind a proxy at one of them. `ANTS_TRUSTED_PROXIES_FILE` names your own list for a proxy at another address; `ANTS_WEB_BIND_PREFIX` (for example `127.0.0.1:`) publishes the web port on one address. With neither set, the web port is published as before and the image's list applies.
- **Rooms on the server.** A seat whose game state differs from the referee's is dropped alone; the others play on. Room codes use the operating system's random generator. A match whose recording was not kept says why; result files are written whole, and a room named like the statistics file writes `<code>.room.json`.
- **Online rooms play maps of at most 1,000 starting ants (was 8,000).** A room also limits the work that a map sets up (plants and waypoints, food objects, power-ups); every library map is within the limits. A map above a limit stays listed and plays on your own device. A restart record of a match on such a map is refused at server start. A test-map door switched off with `ANTS_TEST_MAP_PORT=0` comes back when that setting is removed.
- **The chat store keeps its size.** When the database nears its limit the oldest lines go (moderators' lines last, the newest 1,000 never); a first start on an almost full store does the same once. Each account has an hourly and a daily line budget. Staff run `python -m hub.admin status` and `purge`. The Legal page says lines are kept up to 7 days.
- **Project files.** `SECURITY.md` (how to report a problem privately), `CODEOWNERS`, tidier deployment notes, and a hygiene check that also looks at file names.

**Rules / network:** none: network protocol 22 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ad0a35d9...316b62a4)

## v0.13.1 - 2026-11-03 - Home: the phone's chat strip is not cut off, and the first page shows the open games

**For players:**
- **The phone's bottom chat strip is no longer cut off.** On some phones the strip along the bottom of Home sat under the phone's own bottom bar and part of it was hidden; it now stays above it.
- **The first page shows the open games right away.** Under the top bar, Home now lists the open games (host, map, seats, bots and **Join**, with **Join by code** at the bottom) without a press. The **Games** button stays and takes you to the list.

**Rules / network:** none: network protocol 22 and the simulation rules are unchanged (the web page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/7fa87f91...6b936a10)

## v0.13.0 - 2026-11-02 - Home, public rooms, the games list and the chat

**For players:**
- **A new front page: Home and the Room.** Home is a top bar over a stand-in picture with **Quick host**, **Quick join**, **My room**, **Games** and the Menu; the Room is the room page under a slim bar with **Home** and **Leave room** (it asks first when other people are in the room). Going between the two is instant and keeps your seat, and a plain visit makes no room until you press My room. [Home and the Room](docs/HOME_SCREEN.md)
- **Public rooms.** The host switches the Room **Private** (link only) or **Public** (listed in Open games). Every new room is private; it is private again when another person leads, and a public room that nobody used for 30 minutes goes private by itself (the host gets a one-tap **Make it public**). **Quick host** makes your room public in one press, and makes the room first if you have none.
- **The Games list** shows the open games, best first: host, map, seats, bots and **Join**, with **Join by code** at the bottom. A room is on it only while a newcomer could join; your own public room is its first row, tagged **Yours** (nothing to press), and counts on the button. **Quick join** takes the best open game for you, trying up to three, with a **Cancel**; a refused Join costs you nothing, your own room is kept until the new one welcomes you.
- **A global chat** in the lower-left corner of Home (a strip along the bottom on a phone). Everybody can read it; to write you log in. It runs in its own container (`ants-hub`), so the site and the games work while it is off, and it keeps lines for 7 days. [The chat hub](docs/HUB.md)
- **Log in with Discord** to write in the chat. It asks for your Discord id and username only and keeps nothing else; **Log out** and **Forget me** (deletes your lines) are in the chat. A Discord account under 7 days old cannot write. Login works once the site's operator has set up the Discord application; until then the chat is read-only for everybody.
- **Everybody needs the new version.** A game or page of v0.12 cannot join a v0.13 server: reload the page.

**Rules / network:** network protocol 22 (was 21): one new message, Visibility (type 35: the host makes a lobby room public or private), and one new Room flag, public; a v0.12 game cannot play with a v0.13 one ([public rooms](docs/PUBLIC_ROOMS.md)). One rule fix: the layer-2 id 38 is no bridge any more, as in the original (a water tile that holds it stays water: only a swimmer can enter it, and anyone else put there drowns). No shipped or community map holds it and every state hash of the library is unchanged, so recordings play as before (rules number 5); only a player's own map that holds id 38 plays differently from v0.12. **If you run a server:** the public door (`--replay-port`) answers `GET /lobbies` and the status of a lobby room has `"public"`; the site's nginx passes `/lobbies`, `/hub/ws` and the login's pages; the stack has a new container `ants-hub` and its volume (the staging stack gets its own, `ants-hub-staging` with the volume `ants-staging-hub-data`), with the `ANTS_HUB_*` settings, and the chat and the login must not go live behind a reverse proxy before `docker/trusted-proxies.conf` names it ([the chat hub](docs/HUB.md#behind-a-reverse-proxy)).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/354be26a...57c6f8ae)

## v0.12.152 - 2026-10-29 - Map Maker: a Bridge chip, and a hint for a bridge that is not on water

**For players:**
- **The Map Maker has a Bridge to put on a map.** In Objects, after the Fire wall and before the four bombs, there is a new chip with the game's own picture of a finished bridge. Tap to put one piece, drag for a line of pieces; the name shows under the chip when it is chosen, and Pick up calls it "Bridge".
- **A plain hint when a bridge is not on water.** A bridge needs water under it. While the Bridge chip is chosen, the line under the palette says how many pieces of the map stand on ground that is not water: "Bridge · 2 pieces are on dry ground. A bridge needs water under it." It counts again after every step, Undo and Redo, and when you open a map. It is only a hint: the map still saves, downloads and passes the check. A phone has no such line.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only). Map Maker v1.7.0.

**Fixes:**
- On a desktop window under 741 pixels high (1280 x 720 with scrollbars that take room) the Objects column was too tall for its 31 chips and cut off the name line: there the chips are 2 pixels shorter and the rows and tool buttons 1 pixel closer. Taller windows and phones look as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/354be26a...bfbcbda8)

## v0.12.151 - 2026-10-28 - Tests: the map submission checks no longer fail on a slow machine

**For players:**
- **No change for players or server owners.** The game, the server and the map files are as before.
- **A test of the map submissions no longer fails on a slow, busy test machine.** It checks that a player can send a map, find it again and withdraw it. The server limits the time it spends checking maps, and on a loaded machine that time ran out in the middle of the test, so the server answered "busy" where the test expected "accepted". The tests now give that limit all the time they need, and a new test keeps the real "busy" answer covered: a send that takes longer than the limit closes the window, and the next window starts clean.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/308ce544...c7027eb3)

## v0.12.150 - 2026-10-28 - Online rooms: at most 8,000 starting ants

**For players:**
- **Online rooms play at most 8,000 starting ants (all four colors added up).** Before, nothing limited the ants: a map with 40,000 ants passed the checker, and starting its match froze the whole server for about 11 seconds, with every room, the lobby and the health check, because setting the ants up takes time that grows with the square of their number. Now a map over 8,000 is refused before any ant is set up. A map that starts 8,000 ants or fewer, including the largest ones of the players (6,000 and 2,772 ants), plays online as before; none of the library's 540 maps starts more than 248.
- **How you see the refusal.** The lobby tells the leader "This map starts too many ants to play online (8,000 at most): choose another map." and the room keeps its map. The map list marks the map "Too many ants to play online". In the Map Maker, **Play this map > With other players** says so and its button is off, and **Submit for approval** and test rooms get a sentence from the server. The map can still be opened, edited and saved, and played on your own computer or against bots.
- **A match with many ants takes more of the server's places.** A match takes one place for every 10,000 cells of its map or every 400 starting ants, whichever is more: 2,400 ants take 6 places, 6,000 take 15 and 8,000 take 20 of the server's 48. A shipped map still takes one. So a 6,000-ant map now takes 15 of the 48 places (only 3 such matches at once; it took 3 before). Matches on a map above the cap are left out of the match history counts (only a recording made before the cap can be one). When a map needs more places than are free, the START waits as it does on any full server ("The server has no place for another match right now: try again in a minute.").

**Rules / network:** none for the simulation and the network: network protocol 21, the simulation rules, the recordings and the map files are unchanged. One rule of online play is new, and a map that was accepted before can now be refused online: a room no longer plays a map that starts more than 8,000 ants.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/dd307dfd...bb58eaa2)
## v0.12.149 - 2026-10-28 - On a phone the health numbers are bigger and outlined

**For players:**
- **On a phone the health numbers are bigger and outlined.** In the browser, "Show health as: Number" had digits 2 to 3 CSS pixels tall on a phone, too small to read. On a small screen they now grow (about 8 CSS pixels tall or more: three times as big on a phone held upright, twice on its side) and get a thin black outline about 1 CSS pixel wide. The desktop game, and the browser in a normal desktop window, are unchanged: the original's white digits, pixel for pixel.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ec8fa2fa...9ee59ee3)

## v0.12.148 - 2026-10-28 - Map Maker: the editor starts with less code

**For players:**
- **No change for players.** The Map Maker looks and works as before, and a map you save or download is byte for byte what it was.
- **The editor starts with less code.** Copy, Save as a new map, Start an Extended map / Make a Classic map and Crop to fit now load the first time you use one of them (two small scripts fetched at once, about 29 KB, 9 KB gzipped), so the Map Maker opens with about 7% less script. On a slow network that first dialog may take a moment (an estimate, not measured). If the scripts cannot load (no network), the page says so in plain words and asks again the next time.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2e45f112...67c5b473)

## v0.12.147 - 2026-10-27 - Map Maker: each start ant faces its own way

**For players:**
- **Map Maker: each start ant now faces its own way.** Before, every starting ant on the map stood facing south. In the game each one starts facing one of seven directions picked at random when a match begins (never north), so the Map Maker now shows each ant facing one of those seven, for every ant type (worker, combat, thief, bomber, swimmer, fire) and team color.
- **It is an example, not what the match will do.** The game picks the facings again for every match, so the map has none; the editor makes one up from each ant's cell (the same every time, so nothing jumps when you put other ants or pan), and the Ants box says so in one line. Nothing is saved in the map: the file is byte for byte what it was.
- The page fetches 36 KB more of pictures when it opens (the extra standing ants), cached after the first visit. Map Maker v1.6.2.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/10bc34ae...6bd2e893)

## Older versions (v0.12.146 and before)

Every release before v0.12.147, and the long notes of v0.12.146 - v0.12.69, v0.12.59 - v0.12.55, v0.12.53 - v0.12.48, v0.12.46 - v0.12.35, v0.12.33 - v0.12.26, v0.12.24 - v0.12.18, v0.12.16 - v0.12.0, v0.11.0, v0.10.0, v0.9.1, v0.9.0, v0.8.3, v0.8.2, v0.8.1, v0.8.0, v0.7.0, v0.6.0, v0.5.1, v0.5.0, v0.4.0, v0.3.0, v0.2.0, v0.1.3, v0.1.2, v0.1.1, v0.1.0 and v0.0.99 - v0.0.90, are only in the git history.
