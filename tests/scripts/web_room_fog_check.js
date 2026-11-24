// Runs the Room's FOG OF WAR switch (web/front/room_fog.js with web/lobby.html's own script and web/home/games.js, docs/PLAY_MATCH_SETUP.md "Fog of War", network protocol 28) in the fake browser of
// tests/scripts/web_page_harness.js against a scripted game server:
//   1. the leader's switch: its row in the Rules sheet, its state the Room message's and never the press's, the plan that a press sends (every byte, fog last), a press that is a double press, a guest who has none;
//   2. everybody else: the guest reads the switch (it takes no press) and the Rules button says Fog while fog is on, the line of the room's chat when the leader switches it;
//   3. computer players: the question that turning fog on asks (and what its button sends), the colors' menus with the three bot choices off while fog is on, a bot that is picked anyway;
//   4. every other plan of the page carries the room's fog (a map, a color, the teams) and its game type (also when two colors swap their plans), also before the room has answered the leader's own pick (the pending pick);
//   5. the Room that the server refuses to say (fog with a bot) and the server's refusal of a plan.
// tests/scripts/test_web_room_fog.py runs this with node (the quick tier). usage: node web_room_fog_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, page, wait, find, first, tap, textOf, toastOf, toastTextOf, toastButtons, myRoom, PRIVATE, STARTING, E, C, NET0, KEY, roomMessage, done, broke } = R;

const PLAN = 32;
const plansOf = (sock) => sock.sent.filter((b) => b[0] === PLAN);
const lastPlan = (sock) => { const p = plansOf(sock); return p.length ? p[p.length - 1] : null; };
const fogByte = (bytes) => bytes[bytes.length - 1];
const modeByte = (bytes) => bytes[bytes.length - 2];
const kindsOf = (bytes) => { const at = 2 + bytes[1]; return Array.from(bytes.slice(at, at + 4)); };
const switchOf = (env) => find(env.root, 'dr-switch').filter((e) => e.id === 'fg-switch')[0] || null;
const noteOf = (env) => find(env.root, 'fg-note')[0] || null;
const rowOf = (env) => find(env.root, 'fogsw')[0] || null;
const stateOf = (env) => env.$('rules-state').textContent;       // (what the Rules button says: who can join, and Fog while it is on)
const modeButtons = (env) => find(env.root, 'mode');
const itemsOf = (env) => env.body.findAll((e) => e.tagName === 'li' && String(e.className).split(/\s+/).indexOf('sm-item') !== -1);
const choose = (env, word, which) => { env.press(modeButtons(env)[which || 0]); const item = itemsOf(env).filter((e) => e.getAttribute('data-word') === word)[0]; item.click(); };       // (a press on a color's button opens its menu; an item of it is the choice)
const checked = (env) => switchOf(env).getAttribute('aria-checked');
const seatsOf = (names) => R.seatsOf(names);
const room = (sock, names, o) => sock.receive(roomMessage(seatsOf(names), Object.assign({ you: 0, leader: 0, flags: PRIVATE }, o || {})));
const FOG = require(require('path').resolve(R.lobbyDir, 'front', 'room_fog.js'));
const press = (env) => { switchOf(env).click(); };
const BOTS = [0, 0, 2, 0];                                         // a Medium bot planned in Blue
const advance = async (env, ms) => { await wait(env, ms); };

(async () => {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The leader's switch
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    check('before the page has a room the switch cannot be pressed', rowOf(env) !== null && switchOf(env).disabled === true);
    const sock = myRoom(env, ['Maya', 'Sage']);
    const sw = switchOf(env);
    check('the leader of a room has the switch: a button with the role switch, named Fog of War, off', sw.getAttribute('role') === 'switch' && sw.getAttribute('aria-labelledby') === 'fg-label' && checked(env) === 'false' && sw.disabled === false);
    check('the switch is the fog row of the Rules sheet, and its line says what it does', rowOf(env) === env.$('r-fog') && textOf(noteOf(env)) === FOG.TEXT.note);
    check('a room that is fog-free says nothing of fog on the Rules button', !/Fog/.test(stateOf(env)));
    // a press sends a plan that has fog on, and nothing else of the plan moves; the switch shows what the room said, not the press
    const before = plansOf(sock).length;
    press(env);
    await flush();
    const sent = lastPlan(sock);
    check('a press sends one plan with fog on (the last byte), the room\'s map, colors and mode as they were', plansOf(sock).length === before + 1 && fogByte(sent) === 1 && modeByte(sent) === 0 && kindsOf(sent).join() === '0,0,0,0');
    check('the switch still shows Off: the Room message has not come', checked(env) === 'false');
    room(sock, ['Maya', 'Sage'], { fog: true });
    check('the Room message says fog: the switch is on and the Rules button says Fog', checked(env) === 'true' && /Fog/.test(stateOf(env)));
    check('the line is the honest one: the map is hidden from the screens, it is not cheat-proof yet, and there are no bots', /Not cheat-proof yet/.test(FOG.TEXT.note) && /screens/.test(FOG.TEXT.note) && /No bots/.test(FOG.TEXT.note));
    check('the line is the switch\'s description (aria-describedby names it)', switchOf(env).getAttribute('aria-describedby') === noteOf(env).id);
    await advance(env, 1500);
    press(env);
    await flush();
    check('a press on a switch that is on sends a plan with fog off', fogByte(lastPlan(sock)) === 0 && kindsOf(lastPlan(sock)).join() === '0,0,0,0');
    room(sock, ['Maya', 'Sage'], { fog: false });
    check('the Room message says it is off: the switch is off and the Rules button says no more of fog', checked(env) === 'false' && !/Fog/.test(stateOf(env)));
    // a double press: the plan is sent with the page's pace (it is the same request twice, and the second goes with the room's own fog, which has not changed)
    const n = plansOf(sock).length;
    press(env); press(env);
    await flush();
    check('two quick presses send what the page may send and no more than its pace (two plans at most), each a plan with a fog byte of 0 or 1', plansOf(sock).length - n <= 2 && plansOf(sock).slice(n).every((p) => fogByte(p) === 0 || fogByte(p) === 1));
}
{   // a room that is starting does not take a press (the leader's START waits for the games): the switch is off
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], STARTING, undefined, true);
    check('while the START waits the switch cannot be pressed', switchOf(env).disabled === true);
    const n = plansOf(sock).length;
    switchOf(env).click();
    await flush();
    check('... and a press that gets through sends nothing', plansOf(sock).length === n);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. A guest
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = R.guestRoom(env, 'Sage');
    check('a guest reads the switch: it is off and takes no press', checked(env) === 'false' && switchOf(env).disabled === true);
    check('... and the Rules button says nothing of fog while it is off', !/Fog/.test(stateOf(env)));
    sock.receive(roomMessage(seatsOf(['Sage', 'Maya']), { you: 1, leader: 0, flags: PRIVATE, fog: true }));
    check('the room says fog: the guest reads the switch as on, and the Rules button says Fog', checked(env) === 'true' && /Fog/.test(stateOf(env)));
    check('a guest still has nothing to press with fog on', switchOf(env).disabled === true);
    const n = plansOf(sock).length;
    switchOf(env).click();
    await flush();
    check('a click on the guest\'s switch sends nothing', plansOf(sock).length === n);
    sock.receive(roomMessage(seatsOf(['Sage', 'Maya']), { you: 1, leader: 0, flags: PRIVATE, fog: false }));
    check('fog off again: the switch is off and the Rules button says no more of it', checked(env) === 'false' && !/Fog/.test(stateOf(env)));
    // the line of the chat: everybody is told who switched it (the room's chat box is the page's own)
    sock.receive(roomMessage(seatsOf(['Sage', 'Maya']), { you: 1, leader: 0, flags: PRIVATE, fog: true }));
    const said = find(env.root, 'sys').map(textOf);
    check('the room\'s chat says it: the leader\'s name, turned Fog of War on', said.indexOf('Sage turned Fog of War on.') !== -1 || said.some((t) => /turned Fog of War on\.$/.test(t)), JSON.stringify(said));
}
{   // a guest who becomes the leader finds the room as it was and has the switch
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = R.guestRoom(env, 'Sage');
    sock.receive(roomMessage(seatsOf(['Sage', 'Maya']), { you: 1, leader: 0, flags: PRIVATE, fog: true }));
    sock.receive(roomMessage(seatsOf(['', 'Maya']), { you: 1, leader: 1, flags: PRIVATE, fog: true }));
    check('the guest leads now: the switch takes presses, and is on', switchOf(env).disabled === false && checked(env) === 'true');
    switchOf(env).click();
    await flush();
    check('... and a press sends fog off', lastPlan(sock) !== null && fogByte(lastPlan(sock)) === 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. Computer players
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], PRIVATE, { plan: BOTS });
    await flush();
    check('a room with a planned bot has fog off and the switch can be pressed', checked(env) === 'false' && switchOf(env).disabled === false);
    const n = plansOf(sock).length;
    press(env);
    await flush();
    check('a press with a bot planned sends nothing yet: it asks', plansOf(sock).length === n && toastOf(env).hidden === false);
    check('the question names the color of the bot and asks to set it open and turn fog on', toastTextOf(env) === FOG.question(['Blue']) && /Set Blue to Open and turn fog on\?/.test(toastTextOf(env)) && /no computer players/.test(toastTextOf(env)));
    check('the buttons are Turn fog on and OK (the Games list\'s own notice: OK is always last)', toastButtons(env).map(textOf).join('|') === FOG.TEXT.yes + '|OK');
    toastButtons(env)[1].click();
    await flush();
    check('OK sends nothing and the question goes', plansOf(sock).length === n && toastOf(env).hidden === true);
    await advance(env, 1000);
    press(env);
    await flush();
    toastButtons(env)[0].click();
    await flush();
    const yes = lastPlan(sock);
    check('Turn fog on sends ONE plan: the bot\'s color open and fog on, the map and the mode as they were', plansOf(sock).length === n + 1 && fogByte(yes) === 1 && kindsOf(yes).join() === '0,0,0,0' && modeByte(yes) === 0);
    room(sock, ['Maya', 'Sage'], { fog: true });
    check('the Room says it: fog is on and no color is a bot', checked(env) === 'true');
    await advance(env, 1000);
    press(env);
    await flush();
    room(sock, ['Maya', 'Sage'], { fog: false, plan: [0, 0, 2, 3] });
    press(env);
    await flush();
    check('two bots planned: the question names both colors in order', toastTextOf(env) === FOG.question(['Blue', 'Black']) && /Set Blue and Black to Open/.test(toastTextOf(env)));
    toastButtons(env)[0].click();
    await flush();
    check('... and Turn fog on opens both', kindsOf(lastPlan(sock)).join() === '0,0,0,0' && fogByte(lastPlan(sock)) === 1);
}
{   // Open and Nobody are no bots: fog goes on at once
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], PRIVATE, { plan: [0, 0, 4, 4] });
    press(env);
    await flush();
    check('with Open and Nobody colors a press sends fog on at once, and asks nothing', fogByte(lastPlan(sock)) === 1 && kindsOf(lastPlan(sock)).join() === '0,0,4,4' && toastOf(env).hidden === true);
}
{   // fog on: the menus of the colors have the three bot choices off, and a key that picks one anyway is told
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], PRIVATE, { fog: true });
    await flush();
    const menus = modeButtons(env);
    check('the empty colors have a menu (a button that opens it)', menus.length >= 2);
    env.press(menus[0]);
    same('with fog on the menu offers Open and Closed and shows Easy, Medium and Hard off', itemsOf(env).map((o) => o.getAttribute('data-word') + (o.getAttribute('aria-disabled') === 'true' ? ':off' : '')).join(), 'open,easy:off,medium:off,hard:off,nobody');
    const n = plansOf(sock).length;
    itemsOf(env).filter((o) => o.getAttribute('data-word') === 'medium')[0].click();
    await flush();
    check('a bot that is picked anyway is refused with the page\'s words and no plan is sent', plansOf(sock).length === n && env.$('toast').hidden === false && env.$('toast').textContent === FOG.TEXT.refusal);
    env.press(modeButtons(env)[0]);
    room(sock, ['Maya', 'Sage'], { fog: false });
    check('the menu that was open when fog went off is closed (its choices were those of the fog)', env.body.findAll((e) => String(e.className).split(/\s+/).indexOf('sm-menu') !== -1)[0].hidden === true);
    env.press(modeButtons(env)[0]);
    same('with fog off the menu has all five choices', itemsOf(env).map((o) => o.getAttribute('data-word') + (o.getAttribute('aria-disabled') === 'true' ? ':off' : '')).join(), 'open,easy,medium,hard,nobody');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. Every plan carries the room's fog
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], PRIVATE, { fog: true });
    await flush();
    const n = plansOf(sock).length;
    choose(env, 'nobody');
    await flush();
    check('a color that the leader sets is a plan that keeps the room\'s fog (on)', plansOf(sock).length === n + 1 && fogByte(lastPlan(sock)) === 1 && kindsOf(lastPlan(sock)).indexOf(4) !== -1);
}
{   // the leader's pick before the room's answer: the next plan carries it
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage']);
    await flush();
    press(env);
    await flush();
    check('the press sends fog on', fogByte(lastPlan(sock)) === 1);
    await advance(env, 400);
    choose(env, 'nobody');
    await flush();
    check('a color set before the Room has answered carries the pending fog (on), not the room\'s old (off)', fogByte(lastPlan(sock)) === 1 && kindsOf(lastPlan(sock)).indexOf(4) !== -1);
    room(sock, ['Maya', 'Sage'], { fog: true, plan: [4, 0, 0, 0] });
    await advance(env, 800);
    choose(env, 'open');
    await flush();
    check('once the room says fog the pending pick is gone and a plan carries the room\'s own', fogByte(lastPlan(sock)) === 1);
    room(sock, ['Maya', 'Sage'], { fog: false });
    await advance(env, 800);
    choose(env, 'nobody');
    await flush();
    check('a room that says fog is off carries off in the next plan: nothing is held back that the room does not say', fogByte(lastPlan(sock)) === 0);
}

// two colors that nobody holds swap their plans: the plan that goes out says the room's game type and fog (it said no game type, so a room of 187 went back to Highest score at every swap)
const gripOf = (env, seat) => find(env.root, 'grip').filter((e) => e.getAttribute('data-grip') === String(seat))[0];
const swap = async (env, from, to) => {                                                             // (the keyboard's way: Enter on the dots of one color, then of the other)
    for (const seat of [from, to]) env.$('slots').fire('click', { target: gripOf(env, seat), detail: 0, preventDefault() {} });
    await flush();
};
{
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage']);
    room(sock, ['Maya', 'Sage'], { mode: 1, fog: true, plan: [0, 0, 0, 4] });
    await flush();
    const n = plansOf(sock).length;
    await swap(env, 2, 3);
    const sent = lastPlan(sock);
    check('two unused colors swap: one plan, the two plans exchanged (Blue is nobody, Black is open), and the room\'s game type (187) and fog (on) with it', plansOf(sock).length === n + 1 && kindsOf(sent).join() === '0,0,4,0' && modeByte(sent) === 1 && fogByte(sent) === 1);
}
{   // the leader's picks that no Room has confirmed yet go with the swap too
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], PRIVATE, { plan: [0, 0, 0, 4] });
    await flush();
    find(env.root, 'gt-opt').filter((e) => e.getAttribute('data-mode') === '1')[0].click();
    press(env);
    await flush();
    check('the picks are sent: the game type 187 and fog on', modeByte(lastPlan(sock)) === 1 && fogByte(lastPlan(sock)) === 1);
    await advance(env, 1500);
    const n = plansOf(sock).length;
    await swap(env, 2, 3);
    const sent = lastPlan(sock);
    check('a swap before the Room has answered carries the pending game type and fog, not the room\'s old ones (Highest score, off)', plansOf(sock).length === n + 1 && kindsOf(sent).join() === '0,0,4,0' && modeByte(sent) === 1 && fogByte(sent) === 1);
}
{   // a room of Highest score with fog off is what it was
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage'], PRIVATE, { plan: [0, 0, 0, 4] });
    await flush();
    await swap(env, 2, 3);
    const sent = lastPlan(sock);
    check('a swap in a room of Highest score without fog sends mode 0 and fog off', kindsOf(sent).join() === '0,0,4,0' && modeByte(sent) === 0 && fogByte(sent) === 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. What the room refuses
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage']);
    await flush();
    const before = switchOf(env).getAttribute('aria-checked');
    sock.receive(roomMessage(seatsOf(['Maya', 'Sage']), { you: 0, leader: 0, flags: PRIVATE, fog: true, plan: [0, 0, 2, 0] }));       // (a Room message that has fog and a bot: no server says it)
    check('a Room message with fog and a planned bot is no Room message: the page keeps the room it had', switchOf(env).getAttribute('aria-checked') === before);
    sock.receive(roomMessage(seatsOf(['Maya', 'Sage']), { you: 0, leader: 0, flags: PRIVATE, fog: false }));
    const refusal = 'Bots cannot play with Fog of War.';
    sock.receive(Uint8Array.from([9, 255, 0, refusal.length].concat(Array.from(refusal, (c) => c.charCodeAt(0)))));
    check('the server\'s refusal of a plan is the page\'s notice (a banner), the switch is as the room says', switchOf(env).getAttribute('aria-checked') === 'false');
}

done('the Room\'s Fog of War switch');
})().catch(broke);
