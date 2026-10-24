// Preloaded (node --require) by the Python tests that run a map maker check on a BROKEN copy of a script (test_web_mapmaker.py and test_web_mapmaker_play.py, run_check): the run ends with status 1 at the
// first line in which the check says that something failed ("    FAILED: ...", "    ERROR: ...", "    CRASHED: ..."), instead of going on through the cases that follow (the sweeps of the 540 maps are most
// of the time of a check). A broken copy is refused when ANY check fails, which is what the test asks, so the verdict is the same and only the time that is spent after it is saved. The real scripts are
// always held to the whole check (the tests that expect a pass never preload this file). The line is written with writeSync because process.exit can cut what a pipe has not yet taken.
'use strict';
const fs = require('fs');
const util = require('util');
const log = console.log;
console.log = function (...args) {
    if (typeof args[0] === 'string' && /^\s*(FAILED|ERROR|CRASHED)\b/.test(args[0])) {
        try { fs.writeSync(1, util.format(...args) + '\n'); } catch (e) { /* (the status says it) */ }
        process.exit(1);
    }
    return log.apply(console, args);
};

// A watchdog for a run that must not take long (ANTS_BAIL_WATCHDOG_S = seconds, set by test_web_mapmaker_play.py: the check of a broken copy takes a third of a second, and on a macOS runner of CI a few runs of it
// were once found still alive after 600 s, with no way to tell where). A thread of its own, so that it still runs when the main thread is stuck (in the exit, or anywhere else): after the time it writes one line
// to standard error and kills the process, and the test then has the output that the run had made (the failing line, when it got that far) and the line, in about a minute and not ten.
const watch = Number(process.env.ANTS_BAIL_WATCHDOG_S || 0);
if (watch > 0) {
    const { Worker } = require('worker_threads');
    const code = "setTimeout(() => { try { require('fs').writeSync(2, 'WATCHDOG: the run was still alive after " + watch + " s\\n'); } catch (e) {} process.kill(" + process.pid + ", 'SIGKILL'); }, " + Math.round(watch * 1000) + ");";
    new Worker(code, { eval: true }).unref();
}
