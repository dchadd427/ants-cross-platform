#include "ants_sim/sim_engine.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/combat_ai.hpp"
#include "ants_sim/physics.hpp"
#include "ants_sim/prng.hpp"
#include "ants_sim/match_stats.hpp"

#include <iostream>
#include <cassert>

using namespace ants::sim;

int main() {
    std::cout << "=== RUNNING ADVERSARIAL STRESS TESTS ===\n\n";

    // -------------------------------------------------------------
    // Test 1: Concentric Chebyshev Queuing Dummy Facade
    // -------------------------------------------------------------
    std::cout << "[Test 1] Concentric Chebyshev Queuing Dummy Facade Check:\n";
    {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_anthill(0, {30, 30});

        TileCoord q1 = sim.assign_queue_slot(0, {35, 30});
        TileCoord q2 = sim.assign_queue_slot(0, {25, 30});
        TileCoord q3 = sim.assign_queue_slot(0, {30, 35});
        TileCoord q4 = sim.assign_queue_slot(0, {30, 25});

        std::cout << "  Slot 1 (from 35,30): (" << q1.x << ", " << q1.y << ")\n";
        std::cout << "  Slot 2 (from 25,30): (" << q2.x << ", " << q2.y << ")\n";
        std::cout << "  Slot 3 (from 30,35): (" << q3.x << ", " << q3.y << ")\n";
        std::cout << "  Slot 4 (from 30,25): (" << q4.x << ", " << q4.y << ")\n";

        if (q1 == q2 && q2 == q3 && q3 == q4) {
            std::cout << "  >>> CONFIRMED DUMMY FACADE: assign_queue_slot always returns identical slot (" 
                      << q1.x << ", " << q1.y << ") regardless of caller position or occupancy!\n";
        }
    }

    // -------------------------------------------------------------
    // Test 2: Hardcoded Victim in Thief Infiltration Alert
    // -------------------------------------------------------------
    std::cout << "\n[Test 2] Hardcoded Victim in Thief Infiltration Alert:\n";
    {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_anthill(3, {50, 50}); // Team 3's anthill

        // Team 2's thief infiltrates Team 3
        uint32_t thief = sim.spawn_unit(2, AntType::Thief, {50, 50});
        sim.start_thief_infiltration(thief, 3);
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.step_thief_animation(thief, 19);

        bool target3_alarm = sim.has_targeted_audio_event(3, SoundID::BaseAlarmSiren);
        bool target0_alarm = sim.has_targeted_audio_event(0, SoundID::BaseAlarmSiren);

        std::cout << "  Victim Team 3 got alarm? " << (target3_alarm ? "YES" : "NO") << "\n";
        std::cout << "  Unrelated Team 0 got alarm? " << (target0_alarm ? "YES" : "NO") << "\n";

        if (!target3_alarm && target0_alarm) {
            std::cout << "  >>> CONFIRMED INTEGRITY / LOGIC VIOLATION: Victim is hardcoded to Team 0/1 via (u->player_id == 0) ? 1 : 0!\n";
        }
    }

    // -------------------------------------------------------------
    // Test 3: Combat Ant AI Knockback Out-of-Bounds Teleportation
    // -------------------------------------------------------------
    std::cout << "\n[Test 3] Combat Ant AI Knockback Boundary Safety:\n";
    {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Place Combat Ant at (58, 30) and enemy at (59, 30)
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {58, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {59, 30});

        // Trigger AI update
        sim.tick();

        AntUnit& enemy_unit = sim.get_unit(enemy);
        std::cout << "  Enemy pos after punch: (" << enemy_unit.pos.x << ", " << enemy_unit.pos.y << ")\n";
        std::cout << "  Grid width: " << sim.grid().width() << ", height: " << sim.grid().height() << "\n";

        if (enemy_unit.pos.x >= static_cast<int32_t>(sim.grid().width())) {
            std::cout << "  >>> CONFIRMED CRITICAL BUG: Enemy ant knocked OUT OF BOUNDS to x=" 
                      << enemy_unit.pos.x << " without boundary or obstacle clipping!\n";
        }
    }

    // -------------------------------------------------------------
    // Test 4: InfiltrateAnthill and ReturnToBase Order Inactivity in tick()
    // -------------------------------------------------------------
    std::cout << "\n[Test 4] InfiltrateAnthill & ReturnToBase Order Execution via tick():\n";
    {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_anthill(1, {40, 40});
        sim.set_player_score(1, 100);

        uint32_t thief = sim.spawn_unit(0, AntType::Thief, {10, 10});
        AntOrder order{thief, OrderType::InfiltrateAnthill, 40, 40, 1};
        sim.issue_order(order);

        std::cout << "  Thief state immediately after order: " << static_cast<int>(sim.get_unit(thief).state) << "\n";
        for (int i = 0; i < 50; ++i) {
            sim.tick();
        }

        std::cout << "  Thief pos after 50 ticks: (" << sim.get_unit(thief).pos.x << ", " << sim.get_unit(thief).pos.y << ")\n";
        std::cout << "  Team 1 score after 50 ticks: " << sim.get_player_score(1) << "\n";

        if (sim.get_unit(thief).pos.x == 10 && sim.get_player_score(1) == 100) {
            std::cout << "  >>> CONFIRMED DUMMY / BROKEN WORKFLOW: InfiltrateAnthill order leaves ant frozen; never executes infiltration in tick()!\n";
        }
    }

    std::cout << "\n=== ADVERSARIAL STRESS TESTS FINISHED ===\n";
    return 0;
}
