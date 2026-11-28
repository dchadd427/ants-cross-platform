// What the hub answers to the Analytics tab's question, as the admin page's check needs it (tests/scripts/web_admin_check.js: the tab inside the whole page; the numbers of the tab itself are
// web_admin_analytics_check.js's): one day of a page whose clock is 2026-10-18 13:00 Pacific. Made-up numbers.
'use strict';
const small = {
    from: '2026-09-19', to: '2026-10-18', today: '2026-10-18', days: [{ day: '2026-10-18', visitors: 3, visits: 3, nt: 1, signups: 0, errors: 0, perf: {}, vers: {} }], before: { visitors: 0, visits: 0, nt: 0, signups: 0, errors: 0 },
    lists: { visitors: 3, 'new': 3, returning: 0, visits: 3, nt: 1, signups: 0, dur_sum: 60, dur_mid: 20, errors_other: 0, refs: { 'discord.com': 1 }, entry: { '/': 3 }, exit: { '/': 3 }, browsers: { Chrome: 3 }, systems: { Windows: 3 },
             devices: { Desktop: 3 }, langs: { en: 3 }, countries: { US: [3, 1] }, widths: [0, 0, 0, 3, 0], dur: [0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] },
    errors: [], counter: { now: Date.UTC(2026, 9, 18, 20, 0, 0) / 1000, since: 1, first_day: '2026-10-18', last_at: Date.UTC(2026, 9, 18, 19, 59, 0) / 1000, accepted_today: 4, refused_today: 0, crawlers_today: 0, raw_days: 30, oldest_raw: '2026-10-18', geo: null },
    untrusted: true, untrusted_fields: ['lists.refs', 'errors']
};

module.exports = { small };
