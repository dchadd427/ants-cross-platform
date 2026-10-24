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
