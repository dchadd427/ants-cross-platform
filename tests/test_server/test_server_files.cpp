// Tests of the dedicated game server: the site statistics, the replays that the server keeps and the server's own matches of computer players
#include "server_fixtures.hpp"

#include "site_stats_tests.hpp"      // (the site statistics: a file of its own, in this translation unit)
#include "replay_tests.hpp"          // (the replays that the server keeps: likewise)
#include "bot_match_tests.hpp"      // (the server's own matches of computer players: likewise)
#include "bot_admin_tests.hpp"      // (what the admin page asks of them and the schedules that start them: likewise, after the tests of the matches, whose fixtures it uses)
#include "bot_rotation_tests.hpp"   // (the catalog of the maps and the lists of the rotation: likewise, after the tests of the admin page, whose fixtures it uses)
#include "bot_stagger_tests.hpp"    // (the staggered start of a batch: likewise, after the tests of the rotation, whose helpers it uses)
#include "bot_queue_tests.hpp"      // (the queue of the batches, the summary and the loop's meter: likewise, after the tests of the staggered start, whose helpers it uses)
#include "state_hash_tests.hpp"      // (the seats whose state differs from the referee's: likewise, after the replay tests, whose helpers it may use)

