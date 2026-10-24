// What the map maker's checks use instead of the wall clock, which on a runner that is busy with other test processes measures the wait for a core and not the work:
//   now()     the CPU time that this process has used, in milliseconds: the clock of the speed budgets ("a 512 x 512 map is planned in under 3 seconds"). The budgets are the same; what is held to them is the work.
//   settle()  a promise that comes after a thousand turns of the event loop: everything that the fakes do later is a promise or a setImmediate (the fake database and server, `later` of the page's env), so
//             that settles every chain of them whatever the load, because the order of the turns does not depend on the clock (a wait of 15 or 25 ms for each of 150 calls took 3 s of every run, and what
//             it waited for was the clock).
'use strict';
module.exports = {
    now() { const u = process.cpuUsage(); return (u.user + u.system) / 1000; },
    async settle() { for (let i = 0; i < 1000; i++) await new Promise((r) => setImmediate(r)); }
};
