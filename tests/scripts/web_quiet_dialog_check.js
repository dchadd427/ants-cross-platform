// Runs the game page's OWN code for the browser's boxes (the block of web/shell.html between ANTS_QUIET_BEGIN and ANTS_QUIET_END: antsQuietly) on a page of fakes, and checks:
//   * a playing sound context is paused before the box opens and resumed after it closes, and the box's answer is passed back (Yes, No, a typed text);
//   * a box that throws still resumes the sound, and the error goes on to the caller;
//   * a context that does not play (suspended: not let start yet; closed) is neither paused nor resumed, and no context at all (no game, no SDL, no Module) asks the box all the same;
//   * a suspend that throws asks the box and resumes nothing; a resume or a suspend that is refused leaves no rejection unhandled.
// tests/scripts/test_web_quiet_dialog.py runs this with node (the quick tier). usage: node web_quiet_dialog_check.js web/shell.html     (exit 0: every check holds; every failure is printed)
'use strict';

const fs = require('fs');
const vm = require('vm');

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const shellPath = process.argv[2];
if (!shellPath) { console.log('usage: web_quiet_dialog_check.js shell.html'); process.exit(2); }
const text = fs.readFileSync(shellPath, 'utf8');
function between(begin, end) {
    const at = text.indexOf(begin);
    if (at < 0 || text.indexOf(begin, at + 1) >= 0) { console.log('FAIL ' + begin + ' is not in ' + shellPath + ' exactly once'); process.exit(1); }
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) { console.log('FAIL no ' + end + ' after ' + begin); process.exit(1); }
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}
const block = between('ANTS_QUIET_BEGIN', 'ANTS_QUIET_END');

let unhandled = 0;
process.on('unhandledRejection', () => { unhandled++; });

// A Web Audio context as the browser makes it: suspend() and resume() change its state and answer with a promise (or refuse, or throw)
function context(state, opts) {
    opts = opts || {};
    const c = { state: state, log: [] };
    c.suspend = () => {
        c.log.push('suspend');
        if (opts.suspend === 'throw') throw new Error('InvalidStateError');
        if (opts.suspend === 'reject') return Promise.reject(new Error('refused'));
        c.state = 'suspended';
        return Promise.resolve();
    };
    c.resume = () => {
        c.log.push('resume');
        if (opts.resume === 'throw') throw new Error('InvalidStateError');
        if (opts.resume === 'reject') return Promise.reject(new Error('refused'));
        c.state = 'running';
        return Promise.resolve();
    };
    return c;
}

// The page's block in a sandbox whose Module is `module` (undefined: no Module at all); the box records the state of the sound while it is open
function page(module) {
    const sandbox = {};
    if (module !== undefined) sandbox.Module = module;
    vm.createContext(sandbox);
    vm.runInContext(block + '\nthis.antsQuietly = antsQuietly;', sandbox);
    return sandbox.antsQuietly;
}

async function main() {
    // a playing context: paused while the box is open, resumed after, the answer passed back
    for (const answer of [true, false, 'a typed text', null]) {
        const ctx = context('running');
        const quietly = page({ SDL2: { audioContext: ctx } });
        let during = null;
        const got = quietly(() => { during = ctx.state; ctx.log.push('box'); return answer; });
        expect('playing, answer ' + JSON.stringify(answer) + ': the answer', got, answer);
        expect('playing, answer ' + JSON.stringify(answer) + ': silent while the box is open', during, 'suspended');
        expect('playing, answer ' + JSON.stringify(answer) + ': the order', ctx.log, ['suspend', 'box', 'resume']);
        expect('playing, answer ' + JSON.stringify(answer) + ': playing again', ctx.state, 'running');
    }

    // a box that throws: the sound comes back and the error reaches the caller
    {
        const ctx = context('running');
        const quietly = page({ SDL2: { audioContext: ctx } });
        let error = null;
        try { quietly(() => { throw new Error('the box failed'); }); } catch (e) { error = e.message; }
        expect('a box that throws: the error goes on', error, 'the box failed');
        expect('a box that throws: the sound is resumed', ctx.log, ['suspend', 'resume']);
    }

    // a context that does not play: left as it is
    for (const state of ['suspended', 'closed', 'interrupted']) {
        const ctx = context(state);
        const quietly = page({ SDL2: { audioContext: ctx } });
        let asked = 0;
        const got = quietly(() => { asked++; return 7; });
        expect('a context that is ' + state + ': asked, answered', [asked, got], [1, 7]);
        expect('a context that is ' + state + ': neither paused nor resumed', ctx.log, []);
        expect('a context that is ' + state + ': its state is kept', ctx.state, state);
    }

    // no context: the box is asked all the same
    for (const [label, module] of [['no Module', undefined], ['a Module without SDL2', {}], ['SDL2 without a context', { SDL2: {} }], ['a context of null', { SDL2: { audioContext: null } }]]) {
        const quietly = page(module);
        let asked = 0;
        expect(label + ': the answer', quietly(() => { asked++; return 'yes'; }), 'yes');
        expect(label + ': asked once', asked, 1);
    }

    // a suspend that throws: the box is asked, nothing is resumed
    {
        const ctx = context('running', { suspend: 'throw' });
        const quietly = page({ SDL2: { audioContext: ctx } });
        const got = quietly(() => { ctx.log.push('box'); return true; });
        expect('a suspend that throws: asked, answered', got, true);
        expect('a suspend that throws: nothing resumed', ctx.log, ['suspend', 'box']);
    }

    // refusals: no rejection is left unhandled, and a resume that throws does not reach the caller
    for (const opts of [{ suspend: 'reject' }, { resume: 'reject' }, { resume: 'throw' }]) {
        const ctx = context('running', opts);
        const quietly = page({ SDL2: { audioContext: ctx } });
        let error = null, got = null;
        try { got = quietly(() => 'ok'); } catch (e) { error = e.message; }
        expect('refused ' + JSON.stringify(opts) + ': answered, no error', [got, error], ['ok', null]);
    }

    await new Promise((resolve) => setImmediate(resolve));
    await new Promise((resolve) => setTimeout(resolve, 10));
    expect('no rejection is left unhandled', unhandled, 0);

    console.log('quiet_dialog: ' + checks + ' checks, ' + failed + ' failed');
    process.exit(failed ? 1 : 0);
}

main();
