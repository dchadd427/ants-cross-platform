// What a browser answers to a media query, for the fake windows of the node checks: matches(query, width, height) is true when the window is that wide and that high. A query is a list (commas: any one
// holds) of chains (`and`: all hold) of (min-width: Npx), (max-width: Npx), (min-height: Npx) and (max-height: Npx); a condition of another kind does not hold (a check names the features that it needs).
'use strict';
function matches(query, width, height) {
    return String(query).split(',').some((chain) => chain.split(/\band\b/).every((condition) => {
        const m = /\((min|max)-(width|height): (\d+)px\)/.exec(condition);
        if (!m) return false;
        const size = m[2] === 'width' ? width : height;
        return m[1] === 'min' ? size >= Number(m[3]) : size <= Number(m[3]);
    }));
}
module.exports = { matches };
