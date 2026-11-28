"""The tables of tests/scripts/run_python_tests.py for the tests that only the whole tree has: the game server's, the hub's, the site's pages' and the stack's. The runner reads this file when it
is there and adds the entries to its own COSTS, SHARDS and LINUX_ONLY (what they mean is explained there); a tree without those tests has no such file and no entry for them."""

COSTS = {
    "test_web_home_games.TheChecksAreNotVacuous": 490, "test_web_home_chat.TheChecksAreNotVacuous": 355, "test_web_home_players.TheChecksAreNotVacuous": 68,
    "test_nginx_history.TheBlocksRun": 60, "test_web_home_screen.TheChecksAreNotVacuous": 44, "test_web_death_map.TheCards": 35, "test_web_admin.ThePagesOwnCode": 30,
    "test_nginx_stats.TheCacheRuns": 29, "test_web_lobby_chat.TheChecksAreNotVacuous": 27, "test_nginx_live.TheBlocksRun": 20, "test_web_home_talk.TheChecksAreNotVacuous": 19,
    "test_web_home_mod.TheChecksAreNotVacuous": 18, "test_e2e_deadlines.Waits": 18, "test_hub_login.TheCallbackWhenDiscordGoesWrong": 18,
    "test_web_lobby_net.TheBudgetChecksAreNotVacuous": 15, "test_web_lobby_rules.TheCheckIsNotVacuous": 15, "test_e2e_deadlines.Watchdog": 14, "test_web_home_chat.TheCodeRuns": 14,
    "test_web_rematch.TheChecksAreNotVacuous": 14, "test_nginx_lobbies.TheCacheRuns": 11, "test_nginx_replays.TheBlocksRun": 11, "test_changelog_to_html.TheScriptRunByNode": 10,
    "test_e2e_deadlines.TheScriptCounts": 10, "test_hub_login.TheCallback": 10, "test_hub_login.TheSocket": 10, "test_measure_tick_rate.Program": 10,
    "test_nginx_lobbies.TheServerHangsRuns": 10, "test_hub_ws.TheFuzz": 9, "test_web_lobby.TheCheckIsNotVacuous": 9, "test_hub_login.TheDisplayName": 8, "test_hub_login.TheForget": 8,
    "test_nginx_hub_login.TheBlocksRun": 8, "test_nginx_maps.TheBlocksRun": 8, "test_web_lobby_net.ThePublicRoomChecksAreNotVacuous": 8, "test_web_map_picker.ThePickersOwnCode": 8,
    "test_hub_login.TheDiscordClient": 7, "test_hub_login.TheProcess": 7, "test_hub_server.TheProcess": 7, "test_hub_server.TheTimers": 7, "test_nginx_lobbies.TheServerFailsRuns": 7,
    "test_nginx_stats.TheRealServerBehindTheFile": 7, "test_web_without_docker.TheRealDockerfileReplayed": 7, "test_nginx_stats.TheBlocksRun": 6,
    "test_web_map_maker_link.TheChecksAreNotVacuous": 6, "test_hub_roles.TheLiveRoles": 30, "test_hub_roles.TheLookup": 6, "test_hub_roles.TheSecrets": 5,
    "test_web_home_cmds.TheChecksAreNotVacuous": 6, "test_hub_powers.TheLiveHub": 45, "test_hub_powers.TheRealProcess": 31, "test_hub_powers.TheFreshCheck": 6,
    "test_hub_powers.TheIdIsLikeAPassword": 5, "test_web_mapmaker_ai.TheChecksAreNotVacuous": 40, "test_web_plays.TheChecksAreNotVacuous": 6, "test_web_likes.TheChecksAreNotVacuous": 7,
}

SHARDS = {"test_web_home_games.TheChecksAreNotVacuous": 4, "test_web_home_chat.TheChecksAreNotVacuous": 4}

LINUX_ONLY = {
    "test_web_admin.ThePagesOwnCode", "test_web_death_map.TheCards", "test_web_home_chat.TheChecksAreNotVacuous", "test_web_home_games.TheChecksAreNotVacuous",
    "test_web_home_screen.TheChecksAreNotVacuous", "test_web_home_screen.TheMapCardChecksAreNotVacuous", "test_web_lobby.TheCheckIsNotVacuous",
    "test_web_lobby_chat.TheChecksAreNotVacuous", "test_web_lobby_net.TheBudgetChecksAreNotVacuous", "test_web_lobby_net.ThePublicRoomChecksAreNotVacuous",
    "test_web_lobby_rules.TheCheckIsNotVacuous", "test_web_map_maker_link.TheChecksAreNotVacuous", "test_web_mapmaker_ai.TheChecksAreNotVacuous",
    "test_web_rematch.TheChecksAreNotVacuous",
}
