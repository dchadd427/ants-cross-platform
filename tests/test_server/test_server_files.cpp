// Tests of the dedicated game server: the site statistics, the replays that the server keeps and the server's own matches of computer players
#include "server_fixtures.hpp"

#include "site_stats_tests.hpp"      // (the site statistics: a file of its own, in this translation unit)
#include "replay_tests.hpp"          // (the replays that the server keeps: likewise)
#include "bot_match_tests.hpp"      // (the server's own matches of computer players: likewise)
#include "state_hash_tests.hpp"      // (the seats whose state differs from the referee's: likewise, after the replay tests, whose helpers it may use)

