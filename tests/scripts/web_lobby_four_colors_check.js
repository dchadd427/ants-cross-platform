// Runs the Room's four colors of web/lobby.html in the fake browser of tests/scripts/web_home_games_rig.js (the page's own code, a scripted game server; docs/PLAY_IN_BROWSER.md "Your room", docs/HOME_SCREEN.md "The Room"):
//   1. the host's page: a card for each of the four colors in the order of the hills (Black, Green, Red, Blue); the host's own color is the one of "You" and "Host"; a free color is a button with its choice that opens a menu of
//      Open, Easy bot, Medium bot, Hard bot and Closed ("Nobody" of the plan is shown as "Closed"), and every choice goes to the room as a plan (the plan's bytes: Open 0, Easy 1, Medium 2, Hard 3, Nobody 4);
//      the Team 1 and Team 2 buttons under a person (they send a plan too); Remove on another person's color and not on the host's own; the big button says START!;
//   2. a closed color, as the room says it: the host's button and a guest's card both say Closed;
//   3. a guest's page: the host's color says who leads (the guest reads who starts the match: "<host> starts the match."), the guest has no menu, no Remove and no Team press, and the big button is READY (the guest's own mark).
// tests/scripts/test_web_lobby.py runs this with node (the quick tier; tests/scripts/test_ants_server.sh, part options, runs it too).  usage: node web_lobby_four_colors_check.js web/lobby.html     (exit 0: every check holds)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, page, myRoom, guestRoom, find, first, NET0, roomAs, seatsOf, roomMessage, PRIVATE, done, broke } = R;

const PLAN = 32;
const plansOf = (sock) => sock.sent.filter((b) => b[0] === PLAN);
const kindsOf = (bytes) => { const at = 2 + bytes[1]; return Array.from(bytes.slice(at, at + 4)); };
const cardsOf = (env) => env.$('slots').findAll((e) => e.tagName === 'li');
const buttonsOf = (li) => li.findAll((e) => e.tagName === 'button');
const modeOf = (li) => buttonsOf(li).filter((b) => R.classes(b).indexOf('mode') !== -1)[0] || null;
const itemsOf = (env) => env.body.findAll((e) => e.tagName === 'li' && R.classes(e).indexOf('sm-item') !== -1);
const teamButtons = (li) => buttonsOf(li).filter((b) => /^Team [12] for /.test(b.getAttribute('aria-label') || ''));
const removeOf = (li) => buttonsOf(li).filter((b) => /^Remove .* from the room$/.test(b.getAttribute('aria-label') || ''))[0] || null;
const press = (env, b) => env.press(b);
const COLORS = ['Black', 'Green', 'Red', 'Blue'];

(async () => {
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. the host's page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage', 'Wren']);
    await flush();
    const cards = cardsOf(env);
    same('a card for each of the four colors, in the order of the hills', cards.map((li) => (li.getAttribute('aria-label') || '').split(':')[0]), COLORS);
    check('the host\'s own color is the one that says You and Host (the first player in the room leads it)', /you/i.test(cards[1].getAttribute('aria-label')) && /host/i.test(cards[1].getAttribute('aria-label')) && !/host/i.test(cards[2].getAttribute('aria-label')));
    check('the big button says START! for the host', env.$('start').textContent === 'START!');
    check('the free color (Black) is a button with its choice, Open', !!modeOf(cards[0]) && /Open/.test(modeOf(cards[0]).textContent + modeOf(cards[0]).getAttribute('aria-label')));
    check('a color that a person holds has no menu button', cards.slice(1).every((li) => modeOf(li) === null));
    // the menu: five choices, with "Closed" for Nobody; every choice is a plan
    press(env, modeOf(cards[0]));
    const items = itemsOf(env);
    same('a press on the free color opens a menu of Open, Easy bot, Medium bot, Hard bot and Closed', items.map((i) => [i.getAttribute('data-word'), i.children[0].textContent]), [['open', 'Open'], ['easy', 'Easy bot'], ['medium', 'Medium bot'], ['hard', 'Hard bot'], ['nobody', 'Closed']]);
    items.find((i) => i.getAttribute('data-word') === 'open').click();       // (the choice that is the color's already: no plan)
    check('the choice that the color has already sends no plan', plansOf(sock).length === 0);
    // Black is the fourth seat of the room (the colors lie in the order of the hills, the seats in the order of the room): its kind is the plan's last byte
    for (const [word, byte] of [['easy', NET0.PLAN.Easy], ['medium', NET0.PLAN.Medium], ['hard', NET0.PLAN.Hard], ['nobody', NET0.PLAN.Nobody]]) {
        const had = plansOf(sock).length;
        press(env, modeOf(cardsOf(env)[0]));
        itemsOf(env).find((i) => i.getAttribute('data-word') === word).click();
        await flush();
        const plans = plansOf(sock);
        check('Black ' + word + ' goes to the room as a plan (the kind of Black is ' + byte + ')', plans.length === had + 1 && kindsOf(plans[plans.length - 1])[3] === byte, JSON.stringify(plans.length ? kindsOf(plans[plans.length - 1]) : null));
        sock.receive(roomAs(['Maya', 'Sage', 'Wren'], 0, PRIVATE, { plan: [0, 0, 0, byte] }));       // (the room tells everybody: nothing is remembered by the page)
        await flush();
    }
    // the Team buttons and Remove
    const people = cardsOf(env).slice(1);
    check('under each person (three play) the Team 1 and Team 2 buttons', people.every((li) => teamButtons(li).length === 2));
    const plansBefore = plansOf(sock).length;
    press(env, teamButtons(cardsOf(env)[1])[0]);       // (a press on one color's Team 1 is the page's own; a second color on Team 1 makes a pair, and the pair is what the room holds)
    press(env, teamButtons(cardsOf(env)[2])[0]);
    await flush();
    check('two colors on Team 1 go to the room as a plan', plansOf(sock).length === plansBefore + 1);
    check('Remove is on another person\'s color (Sage and Wren) and not on the host\'s own', !!removeOf(cardsOf(env)[2]) && !!removeOf(cardsOf(env)[3]) && removeOf(cardsOf(env)[1]) === null);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. a closed color, as the room says it
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    const sock = myRoom(env, ['Maya']);
    await flush();
    sock.receive(roomAs(['Maya'], 0, PRIVATE, { plan: [0, NET0.PLAN.Nobody, 0, NET0.PLAN.Nobody] }));       // (Red and Black are out of the match)
    await flush();
    const cards = cardsOf(env);
    check('Nobody of the plan is Closed on the host\'s button (Black and Red), and Blue is still Open', /Closed/.test(modeOf(cards[0]).textContent) && /Closed/.test(modeOf(cards[2]).textContent) && !/Closed/.test(modeOf(cards[3]).textContent) && /Open/.test(modeOf(cards[3]).textContent));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. a guest's page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page({ search: '?room=k7m2xq' });
    const sock = guestRoom(env, 'Hazel');
    await flush();
    sock.receive(roomAs(['Hazel', 'Maya', 'Sage'], 1, PRIVATE, { plan: [0, 0, 0, NET0.PLAN.Nobody] }));
    await flush();
    const cards = cardsOf(env);
    same('a guest sees the same four colors', cards.map((li) => (li.getAttribute('aria-label') || '').split(':')[0]), COLORS);
    check('the host\'s color (Green, seat 0) says Host and the guest\'s own (Red) says You', /host/i.test(cards[1].getAttribute('aria-label')) && /you/i.test(cards[2].getAttribute('aria-label')) && !/host/i.test(cards[2].getAttribute('aria-label')), cards.map((li) => li.getAttribute('aria-label')).join(' | '));
    check('the guest reads who starts the match (the status line names the host)', /Hazel starts the match/.test(env.$('go-status').textContent), env.$('go-status').textContent);
    check('a guest has no menu on a color, no Remove and no Team press', cards.every((li) => modeOf(li) === null && removeOf(li) === null) && cards.every((li) => teamButtons(li).every((b) => b.disabled || b.getAttribute('aria-disabled') === 'true' || b.tagName !== 'button')));
    check('the big button is the guest\'s READY', env.$('start').textContent === 'READY');
    check('a closed color reads Closed for a guest too', /closed/i.test(cards[0].getAttribute('aria-label') + cards[0].textContent), cards[0].getAttribute('aria-label'));
}
done('the Room\'s four colors');
})().catch(broke);
