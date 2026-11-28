// Whose slide it is (include/ants_app/view_glide.hpp). An ant that the picture draws from the confirmed engine (every other player's, with others = confirmed) is never corrected by the prediction, so the
// only slide that it can have is the picture's own hop of a snap that the engine itself made (ViewGlide::follow: a new order, a stop): the slide begins in the frame in which the engine moved the ant by
// about as much as the slide takes back. A correction of a rebuild would begin with no such move. Shared by the tests that hold "no correction of another player's ant is ever drawn" (test_prediction_link,
// test_prediction_app).
#pragma once

#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>

namespace shaped {

class SlideCause {
public:
    // The look at an ant in a frame, with the slide that it has now (gx, gy; none: slid false). True: its slide is one that no move of the engine's own ant explains. `started` is true in the frame in which
    // a slide begins or grows
    bool unexplained(const ants::sim::AntSnapshot& engine_ant, int32_t gx, int32_t gy, bool slid, bool& started) {
        Track& t = tracks_[engine_ant.id];
        const double mag = std::hypot(static_cast<double>(gx), static_cast<double>(gy));
        started = slid && (t.mag == 0.0 || mag > t.mag + 1.5);
        if (started) {
            const int32_t jump_x = t.seen ? t.px - engine_ant.px : 0;                   // (what the engine did to the ant since the last look)
            const int32_t jump_y = t.seen ? t.py - engine_ant.py : 0;
            t.explained = std::max(std::abs(gx - t.gx - jump_x), std::abs(gy - t.gy - jump_y)) <= 8;      // (a few pixels more than the clip's frames and a walk's step between two looks can add)
        }
        if (!slid) t.explained = false;
        t.mag = slid ? mag : 0.0;
        t.gx = slid ? gx : 0;
        t.gy = slid ? gy : 0;
        t.px = engine_ant.px;
        t.py = engine_ant.py;
        t.seen = true;
        return slid && !t.explained;
    }

private:
    struct Track {
        bool seen{false};
        bool explained{false};
        double mag{0.0};
        int32_t gx{0};
        int32_t gy{0};
        int32_t px{0};
        int32_t py{0};
    };
    std::map<uint32_t, Track> tracks_;
};

}  // namespace shaped
