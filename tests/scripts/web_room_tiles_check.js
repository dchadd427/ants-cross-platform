// Runs the Room on a phone held upright (the four colors as tiles, and the leader's sheet for a color: web/lobby.html with web/front/seat_menu.js, docs/HOME_SCREEN.md "The Room") in the fake browser of
// tests/scripts/web_page_harness.js against a scripted game server. The fake window answers a media query for the size that a check gives it (tests/scripts/fake_media.js), and can be turned.
//   1. which window has tiles: a phone upright does; a desktop window, a phone held sideways and a page whose browser has no media queries keep the rows; turning the window draws the colors again;
//   2. the tiles: one for each color, with a button over it for the leader and none for a guest; no dots, no Remove, no Team buttons, no drop-down on a tile; the marks, the team's tag and the names;
//   3. the same commands: the sheet's team, color, choice and Remove send exactly the bytes that the desktop's controls send (a team pair, a color moved, an open color's choice, Remove and its question, Fog of War);
//   4. the sheet and the room: it follows its player to another color, closes when its player leaves or START is pressed or the window turns, shows the choices as of the room when the room changes, says why a press was refused,
//      gives the focus back to its tile, and a guest's tile opens nothing.
// tests/scripts/test_web_room_tiles.py runs this with node (the quick tier). usage: node web_room_tiles_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, page, find, myRoom, guestRoom, seatsOf, PRIVATE, STARTING, NET0, hex, roomMessage, RULES0, lobbyDir, done, broke } = R;
const Media = require('./fake_media.js');
const FOG = require(path.resolve(lobbyDir, 'front', 'room_fog.js'));
const MSG = NET0.MSG;

// A window of a given size: it answers the page's media queries for it, and turn() makes it another size and tells the page that the answers changed
function phone(w, h) {
    const size = { w, h }, changes = [], win = { innerWidth: w, innerHeight: h };
    win.matchMedia = (q) => ({ get matches() { return Media.matches(q, size.w, size.h); }, addEventListener(t, f) { if (t === 'change') changes.push(f); } });
    return { win, turn(nw, nh) { size.w = nw; size.h = nh; win.innerWidth = nw; win.innerHeight = nh; changes.slice().forEach((f) => f({})); } };
}
const open = (w, h, o) => { const p = phone(w, h); return page(Object.assign({}, o, { options: Object.assign({ win: p.win }, o && o.options) })).then((env) => Object.assign(env, { turn: p.turn })); };
const NAMES = ['Maya', 'Sam', 'Tess', 'Uma'];           // Green, Red, Blue, Black
const slotOf = (env, seat) => env.$('slots').children.filter((li) => li.getAttribute('data-i') === String(seat))[0];
const tapOf = (env, seat) => find(env.$('slots'), 'tap').filter((e) => e.getAttribute('data-tap') === String(seat))[0] || null;
const back = (env) => find(env.body, 'ss-back')[0] || null;
const shown = (env) => !!back(env) && !back(env).hidden;
const keyed = (env, k) => (back(env) ? back(env).findAll((e) => e.getAttribute('data-key') === k)[0] : null) || null;
const inSlots = (env, cls) => find(env.$('slots'), cls);
const sent = (sock, type) => sock.sent.filter((b) => b[0] === type);
const hexes = (list) => list.map(hex);
const room = (sock, names, o) => sock.receive(roomMessage(seatsOf(names), Object.assign({ you: 0, leader: 0, flags: PRIVATE }, o || {})));
const gripOf = (env, seat) => find(env.$('slots'), 'grip').filter((e) => e.getAttribute('data-grip') === String(seat))[0];
const sideButton = (env, seat, side) => env.$('slots').querySelector('[data-seat="' + seat + '"][data-side="' + side + '"]');
const pressMove = (env, from, to) => { for (const seat of [from, to]) env.$('slots').fire('click', { target: gripOf(env, seat), detail: 0, preventDefault() {} }); };       // (the dots' keyboard path: a person, then the color)
const choose = (env, seat, word) => { env.$('slots').querySelector('[data-mode="' + seat + '"]').click(); env.body.findAll((e) => e.getAttribute('data-word') === word && e.tagName === 'li')[0].click(); };
const text = (e) => (e ? e.textContent : null);
const marks = (env) => [0, 1, 2, 3].map((s) => find(slotOf(env, s), 'tmark').map((m) => m.className.replace('tmark ', '')).join(''));

(async () => {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. Which window has tiles
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await open(390, 844);
    myRoom(env, NAMES);
    check('a phone held upright draws the colors as tiles: the body says so, and every color has its button over it', env.body.classList.contains('tiles') && [0, 1, 2, 3].every((s) => !!tapOf(env, s)));
    check('... and nothing of the rows is there: no dots, no Remove, no Team buttons, no drop-down of an open color', ['grip', 'rm', 'teamset', 'mode', 'rdy-chip'].every((c) => inSlots(env, c).length === 0));
    for (const [label, w, h] of [['a desktop window', 1280, 800], ['a phone held sideways', 844, 390], ['a narrow phone held sideways', 667, 375], ['a window as wide as a phone and as low as one held sideways', 600, 500]]) {
        const other = await open(w, h);
        myRoom(other, NAMES);
        check(label + ' (' + w + ' x ' + h + ') keeps the rows: no tiles, the dots and Remove are there, no button covers a color', !other.body.classList.contains('tiles') && inSlots(other, 'grip').length === 4 && inSlots(other, 'rm').length === 3 && inSlots(other, 'tap').length === 0);
    }
    const edge = await open(700, 501);
    myRoom(edge, NAMES);
    check('700 x 501 is the smallest window with tiles that is still a phone\'s width and taller than a phone held sideways', edge.body.classList.contains('tiles'));
    const plain = await page();
    myRoom(plain, NAMES);
    check('a page whose browser has no media queries keeps the rows', !plain.body.classList.contains('tiles') && inSlots(plain, 'grip').length === 4);
    env.turn(1280, 800);
    check('turning the window to a desktop\'s draws the rows (and the sheet, if there was one, goes with the tiles)', !env.body.classList.contains('tiles') && inSlots(env, 'grip').length === 4 && inSlots(env, 'tap').length === 0);
    env.turn(390, 844);
    check('... and turning it back draws the tiles again', env.body.classList.contains('tiles') && inSlots(env, 'tap').length === 4 && inSlots(env, 'grip').length === 0);
    tapOf(env, 1).click();
    check('a press on a tile opens the sheet of its color', shown(env) && text(back(env).findAll((e) => e.tagName === 'h3')[0]) === 'Sam');
    env.turn(1280, 800);
    check('the window turned to a desktop\'s while the sheet was open: the sheet is away, and the page behind it is let go', !shown(env) && !env.$('lobby').hasAttribute('inert'));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The tiles
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await open(390, 844);
    const sock = myRoom(env, NAMES, PRIVATE, { ready: 2 + 8, teamA: 0, teamB: 2 });          // Sam and Uma have said Ready; Green + Blue are one team, Red + Black the other
    await flush();
    same('the tiles lie as the colors lie on the map: Black, Green, Red, Blue', env.$('slots').children.map((li) => li.getAttribute('data-c')), ['black', 'green', 'red', 'blue']);
    same('each tile says its color, the name on a line of its own, and You and Host where they are', [0, 1, 2, 3].map((s) => [find(slotOf(env, s), 'cname').map(text).join(), find(slotOf(env, s), 'you').map(text).join(), find(slotOf(env, s), 'htag').map(text).join(), find(slotOf(env, s), 'nm').map(text).join()]),
        [['Green', 'You', 'Host', 'Maya'], ['Red', '', '', 'Sam'], ['Blue', '', '', 'Tess'], ['Black', '', '', 'Uma']]);
    same('the team of each tile is the room\'s (Green + Blue against Red + Black), and Ready or not is the mark', [0, 1, 2, 3].map((s) => find(slotOf(env, s), 'tteam').map((t) => t.className.replace('tteam ', '')).join()), ['t1', 't2', 't1', 't2']);
    same('the marks: the host\'s is Ready, Sam and Uma said it, Tess did not', marks(env), ['on', 'on', 'no', 'on']);
    check('the leader\'s tile has an arrow; every tile of the leader is a button that opens a dialog and says which color it changes', [0, 1, 2, 3].every((s) => find(slotOf(env, s), 'tchev').length === 1 && tapOf(env, s).getAttribute('aria-haspopup') === 'dialog' && /^Change /.test(tapOf(env, s).getAttribute('aria-label'))));
    check('the tile\'s accessible name still names the color and the person', slotOf(env, 1).getAttribute('aria-label') === 'Red: Sam' && slotOf(env, 0).getAttribute('aria-label') === 'Green: Maya, you, host');
    room(sock, ['Maya', 'Sam', '', ''], { plan: [0, 0, 2, 4] });
    same('a color that nobody holds says what the plan makes it: Open, a computer player\'s level, Closed (Blue here is a Medium bot, Black is closed)', [2, 3].map((s) => find(slotOf(env, s), 'nm').map(text).join()), ['Medium bot', 'Closed']);
    same('... and the colors of one or more people are a dim ant: a bot is not (its mark is the dim Ready)', [marks(env)[2], slotOf(env, 3).className.split(' ').indexOf('k-nobody') !== -1], ['on bot', true]);
    room(sock, ['Maya', 'Sam', '', ''], { plan: [0, 0, 0, 0] });
    same('an open color says Open', find(slotOf(env, 3), 'nm').map(text), ['Open']);
    check('no markup was written for a name or a word', env.innerHTMLWrites.length === 0);
}
{
    const evil = '<img src=x onerror=alert(1)>';
    const env = await open(390, 844);
    const sock = myRoom(env, ['Maya', evil, '', '']);
    check('a name that is markup is text on the tile and in the sheet, and the page wrote no markup', text(find(slotOf(env, 1), 'nm')[0]) === evil && (tapOf(env, 1).click(), text(back(env).findAll((e) => e.tagName === 'h3')[0]) === evil) && env.innerHTMLWrites.length === 0);
    check('... and the sheet is labelled by its title', back(env).children[0].getAttribute('aria-labelledby') === 'ss-title');
    sock.receive(roomMessage(seatsOf(['Maya', evil, '', '']), { you: 0, leader: 0, flags: PRIVATE, ready: 2 }));
}
{   // a guest: the tiles are pictures
    const env = await open(390, 844, { search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = guestRoom(env, 'Maya');
    room(sock, ['Maya', 'Sam', 'Tess', ''], { you: 1, leader: 0, teamA: 0, teamB: 1, ready: 4 });
    check('a guest\'s tiles have no button and no arrow', env.body.classList.contains('tiles') && inSlots(env, 'tap').length === 0 && inSlots(env, 'tchev').length === 0 && find(env.$('slots'), 'tap').length === 0);
    same('... but they show who is who, the team and the marks (the guest is Sam in Red)', [text(find(slotOf(env, 1), 'you')[0]), find(slotOf(env, 0), 'htag').map(text).join(), find(slotOf(env, 1), 'tteam').map((t) => t.className.replace('tteam ', '')).join(), marks(env)], ['You', 'Host', 't1', ['on', 'no', 'on', '']]);
    for (let s = 0; s < 4; s++) { env.$('slots').fire('click', { target: slotOf(env, s), detail: 1, preventDefault() {} }); slotOf(env, s).click(); }
    check('a press on a guest\'s tile opens nothing and sends nothing', !shown(env) && sent(sock, MSG.Plan).length === 0 && sent(sock, MSG.SeatMove).length === 0 && sent(sock, MSG.Remove).length === 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The same commands as the desktop's controls: two pages in the same room, one with the rows and one with the tiles
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
async function twins(names, o) {
    const desk = await open(1280, 800), ph = await open(390, 844);
    const dsock = myRoom(desk, names, PRIVATE, o), psock = myRoom(ph, names, PRIVATE, o);
    return { desk, dsock, ph, psock };
}
const closeSheet = (env) => keyed(env, 'done').click();
{   // the teams: a pair on Team 1
    const t = await twins(NAMES);
    await flush();
    for (const s of [0, 1]) sideButton(t.desk, s, 1).click();
    for (const s of [0, 1]) { tapOf(t.ph, s).click(); keyed(t.ph, 'team:1').click(); closeSheet(t.ph); }
    same('the sheet\'s Team buttons send the plan that the desktop\'s buttons send (the pair Green + Red)', hexes(sent(t.psock, MSG.Plan)), hexes(sent(t.dsock, MSG.Plan)));
    check('... and it is a plan (one was sent, with the pair in it)', sent(t.psock, MSG.Plan).length === 1);
    room(t.psock, NAMES, { teamA: 0, teamB: 1 });
    tapOf(t.ph, 2).click();
    same('the sheet of another color shows the team lit as the room has it (a pair of four: Blue is on the other team)', [1, 2].map((k) => keyed(t.ph, 'team:' + k).getAttribute('aria-pressed')), ['false', 'true']);
    keyed(t.ph, 'team:1').click();
    const refusal = RULES0.pressSide([1, 1, 0, 0], [0, 1, 2, 3], 2, 1).refusal;
    check('a Team that the room refuses is not sent and the sheet says why (the words of the rules, where the person is looking)', sent(t.psock, MSG.Plan).length === 1 && refusal !== '' && text(find(back(t.ph), 'ss-note')[0]) === refusal && keyed(t.ph, 'team:1').getAttribute('aria-disabled') === 'true', text(find(back(t.ph), 'ss-note')[0]));
    keyed(t.ph, 'team:2').click();
    check('the next press that is accepted clears the line', find(back(t.ph), 'ss-note').length === 0);
}
{   // a color moved: the desktop's dots, the sheet's Color row
    const t = await twins(['Maya', 'Sam', 'Tess']);
    await flush();
    pressMove(t.desk, 1, 3);
    tapOf(t.ph, 1).click();
    keyed(t.ph, 'color:3').click();
    same('a move to an open color sends the SeatMove that the dots send', hexes(sent(t.psock, MSG.SeatMove)), hexes(sent(t.dsock, MSG.SeatMove)));
    check('... one message (not zero)', sent(t.psock, MSG.SeatMove).length === 1);
    keyed(t.ph, 'color:1').click();
    check('a press on the person\'s own color sends nothing more', sent(t.psock, MSG.SeatMove).length === 1);
    const t2 = await twins(NAMES);
    await flush();
    pressMove(t2.desk, 1, 2);
    tapOf(t2.ph, 1).click();
    keyed(t2.ph, 'color:2').click();
    same('a move onto a color with a player (a swap) sends the same SeatMove', hexes(sent(t2.psock, MSG.SeatMove)), hexes(sent(t2.dsock, MSG.SeatMove)));
    same('the Color row says what a press does', [0, 1, 2, 3].map((s) => keyed(t2.ph, 'color:' + s).getAttribute('aria-label')), ['Swap places with Maya on Green', 'Red, where Sam is', 'Swap places with Tess on Blue', 'Swap places with Uma on Black']);
}
{   // Remove: the question first, and the same message
    const t = await twins(NAMES);
    await flush();
    t.desk.$('slots').querySelector('[data-remove="1"]').click();
    t.desk.$('slots').querySelector('[data-remove-yes="1"]').click();
    tapOf(t.ph, 1).click();
    keyed(t.ph, 'rm').click();
    check('Remove asks first: the question names the person, and nothing is sent', sent(t.psock, MSG.Remove).length === 0 && !!find(back(t.ph), 'ss-ask')[0] && text(find(back(t.ph), 'ss-ask')[0].findAll((e) => e.tagName === 'p')[0]) === 'Remove Sam from the room?' && keyed(t.ph, 'rm') === null);
    check('... the focus is on Keep, the safe answer', t.ph.focused === keyed(t.ph, 'no'));
    keyed(t.ph, 'no').click();
    check('Keep puts the question away, sends nothing, and the focus is back on Remove', sent(t.psock, MSG.Remove).length === 0 && find(back(t.ph), 'ss-ask').length === 0 && t.ph.focused === keyed(t.ph, 'rm'));
    keyed(t.ph, 'rm').click();
    keyed(t.ph, 'yes').click();
    same('Remove, then Remove, sends the one Remove (with the guard of the seating that the leader saw) that the desktop\'s sends', hexes(sent(t.psock, MSG.Remove)), hexes(sent(t.dsock, MSG.Remove)));
    check('... one message, and the person is named in the toast as on the desktop', sent(t.psock, MSG.Remove).length === 1 && text(t.ph.$('toast')) === text(t.desk.$('toast')) && /Sam was removed/.test(text(t.ph.$('toast'))));
    room(t.psock, ['Maya', '', 'Tess', 'Uma']);
    check('the player is gone: the sheet is gone with the player', !shown(t.ph));
    tapOf(t.ph, 0).click();
    check('the leader\'s own color has no Remove (and the host has the team and color rows)', !!keyed(t.ph, 'color:0') && keyed(t.ph, 'rm') === null && find(back(t.ph), 'ss-remove').length === 0);
}
{   // an open color's choice, and Fog of War
    const t = await twins(['Maya', 'Sam']);
    await flush();
    choose(t.desk, 2, 'hard');
    tapOf(t.ph, 2).click();
    same('the sheet of an open color has the five choices and Open is the chosen', [find(back(t.ph), 'ss-item').map((e) => e.getAttribute('data-word')), find(back(t.ph), 'ss-item').map((e) => e.getAttribute('aria-checked'))], [['open', 'easy', 'medium', 'hard', 'nobody'], ['true', 'false', 'false', 'false', 'false']]);
    keyed(t.ph, 'mode:hard').click();
    same('a choice sends the plan that the drop-down sends', hexes(sent(t.psock, MSG.Plan)), hexes(sent(t.dsock, MSG.Plan)));
    check('... one plan', sent(t.psock, MSG.Plan).length === 1);
    keyed(t.ph, 'mode:open').click();
    check('the choice that the room has already sends nothing, and the sheet shows the room (not the press) until the room says it', sent(t.psock, MSG.Plan).length === 1 && keyed(t.ph, 'mode:open').getAttribute('aria-checked') === 'true' && keyed(t.ph, 'mode:hard').getAttribute('aria-checked') === 'false');
    room(t.psock, ['Maya', 'Sam', '', ''], { plan: [0, 0, 3, 0] });
    check('the room says Hard: the sheet shows the title of a computer player and Hard checked, and has the team buttons only when three play', find(back(t.ph), 'ss-item').map((e) => e.getAttribute('aria-checked')).join() === 'false,false,false,true,false' && !!shown(t.ph) && text(back(t.ph).findAll((e) => e.tagName === 'h3')[0]) === 'Hard bot' && find(back(t.ph), 'ss-team').length === 1);
    closeSheet(t.ph);
    const s0 = sent(t.psock, MSG.Plan).length;
    room(t.psock, ['Maya', 'Sam', '', ''], { fog: true });
    tapOf(t.ph, 3).click();
    same('with Fog of War on the three bots are off in the sheet, and Open and Closed are not', find(back(t.ph), 'ss-item').map((e) => e.getAttribute('aria-disabled') || null), [null, 'true', 'true', 'true', null]);
    keyed(t.ph, 'mode:medium').click();
    check('a bot that is pressed anyway sends nothing and the sheet says why, in the words of the fog switch', sent(t.psock, MSG.Plan).length === s0 && text(find(back(t.ph), 'ss-note')[0]) === FOG.TEXT.refusal, text(find(back(t.ph), 'ss-note')[0]));
    keyed(t.ph, 'mode:nobody').click();
    check('Closed is a plan, and clears the line', sent(t.psock, MSG.Plan).length === s0 + 1 && find(back(t.ph), 'ss-note').length === 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. The sheet and the room
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await open(390, 844);
    const sock = myRoom(env, NAMES);
    await flush();
    tapOf(env, 1).click();
    check('the sheet holds the page behind it (nothing behind it takes a key or a press), has the focus in it, and is a dialog', env.$('lobby').hasAttribute('inert') && env.focused === back(env).children[0] && back(env).children[0].getAttribute('role') === 'dialog');
    check('a person\'s sheet: the team row, the Color row with the own color marked, Remove; no list of choices', find(back(env), 'ss-team').length === 1 && keyed(env, 'color:1').getAttribute('aria-pressed') === 'true' && !!keyed(env, 'rm') && find(back(env), 'ss-list').length === 0);
    same('its words: the name, then the color and whether the person has said Ready', [text(back(env).findAll((e) => e.tagName === 'h3')[0]), text(find(back(env), 'ss-sub')[0])], ['Sam', 'Red · Not ready']);
    room(sock, NAMES, { ready: 2 });
    same('the room says Ready: the sheet says it', text(find(back(env), 'ss-sub')[0]), 'Red · Ready');
    room(sock, ['Maya', 'Sam', 'Tess', 'Uma'], { teamA: 0, teamB: 1 });
    check('a Room that changes a team while the sheet is open is drawn at once', keyed(env, 'team:1').getAttribute('aria-pressed') === 'true');
    // the player moves to another color (the leader's own press, or the player's): the sheet goes with them
    room(sock, ['Maya', 'Uma', 'Tess', 'Sam']);
    check('the player moved to another color (Sam and Uma swapped places): the sheet is for that color now, still open', shown(env) && text(back(env).findAll((e) => e.tagName === 'h3')[0]) === 'Sam' && keyed(env, 'color:3').getAttribute('aria-pressed') === 'true' && /^Black/.test(text(find(back(env), 'ss-sub')[0])) === true);
    closeSheet(env);
    check('Done closes it, lets the page behind go, and the focus is on the tile of the color that the player is in now', !shown(env) && !env.$('lobby').hasAttribute('inert') && env.focused === tapOf(env, 3));
    tapOf(env, 3).click();
    room(sock, ['Maya', 'Uma', 'Tess', '']);
    check('the player left: the sheet is away (a color that nobody holds is not the player\'s sheet)', !shown(env));
    tapOf(env, 3).click();
    check('the color that the player left is open for anybody: its sheet is the open color\'s', find(back(env), 'ss-list').length === 1 && find(back(env), 'ss-team').length === 0 && keyed(env, 'mode:open').getAttribute('aria-checked') === 'true');
    room(sock, ['Maya', 'Uma', 'Tess', 'Sam']);
    check('somebody takes the open color while its sheet is open: the sheet is away (it was for a color with nobody)', !shown(env));
}
{
    const env = await open(390, 844);
    const sock = myRoom(env, ['Maya', 'Sam'], PRIVATE, undefined, true);
    await flush();
    tapOf(env, 3).click();
    room(sock, ['Maya', 'Sam'], { plan: [0, 0, 0, 2] });
    check('an open color\'s sheet stays up when the room changes and shows the new choice (the leader, or the control interface, made the color a Medium bot)', shown(env) && keyed(env, 'mode:medium').getAttribute('aria-checked') === 'true' && text(back(env).findAll((e) => e.tagName === 'h3')[0]) === 'Medium bot');
    sock.receive(roomMessage(seatsOf(['Maya', 'Sam']), { you: 0, leader: 0, flags: STARTING, inGame: 1 }));
    check('START was pressed: the sheet is away and the tiles have nothing to press', !shown(env) && inSlots(env, 'tap').length === 0 && !env.$('lobby').hasAttribute('inert'));
    env.$('slots').fire('click', { target: slotOf(env, 1), detail: 1, preventDefault() {} });
    check('... and a press on a tile then opens nothing', !shown(env));
}
{   // the leader leaves the lead (the old leader is a guest now): the sheet goes
    const env = await open(390, 844);
    const sock = myRoom(env, NAMES);
    await flush();
    tapOf(env, 1).click();
    room(sock, NAMES, { you: 0, leader: 1 });
    check('the lead went to another player: the sheet is away, and the tiles are a guest\'s', !shown(env) && inSlots(env, 'tap').length === 0);
}
{   // a press that comes from a tile that the window has no more (the window turned): nothing opens; and the page has one sheet, however many are opened
    const env = await open(390, 844);
    myRoom(env, NAMES);
    await flush();
    for (let s = 0; s < 4; s++) { tapOf(env, s).click(); closeSheet(env); }
    check('the page has one sheet, however many are opened', find(env.body, 'ss-back').length === 1);
    env.turn(1280, 800);
    env.$('slots').fire('click', { target: { closest: (q) => (q === '[data-tap]' ? { getAttribute: () => '1' } : null) }, detail: 1, preventDefault() {} });
    check('a press that comes from a tile that the window has no more opens nothing', !shown(env));
}

done('Room on a phone upright');
})().catch(broke);
