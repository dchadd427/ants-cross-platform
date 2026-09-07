# Milestone 2 Iteration 2: Base Lifecycle & Queuing Remediation

**Author**: `explorer_m2_it2_1`  
**Role**: Base Lifecycle & Queuing Remediation Explorer  
**Target Milestone**: Milestone 2 (`libants-sim`)  
**Target Files**:
- `include/ants_sim/sim_engine.hpp`
- `src/ants_sim/sim_engine.cpp`  
**Date**: 2026-09-06  

---

## 1. Executive Summary

During the Milestone 2 review and empirical challenger auditing, three critical defects and integrity violations were identified in base lifecycle, queuing geometry, and game state boundaries:

1. **Dummy Facade in Anthill Queuing (`sim_engine.cpp:711-716`)**:
   `assign_queue_slot` marked `from_pos` as `[[maybe_unused]]` and returned a static coordinate `{bx + 1, by}` regardless of caller approach vector, existing queue occupancy, or concentric rings.
2. **Audio Event Multiplication on Frame 8 Heal (`sim_engine.cpp:730-733`)**:
   `step_base_entry_animation` tested `if (target_frame >= 8)` without a one-shot gate, queuing Sound 36 (`powerupc.wav`) on every single frame from 8 through 16 (9 audio events queued per entering unit).
3. **Simulation Freeze Bypass in Egg Hatching (`sim_engine.cpp:289-307`)**:
   `SimulationEngine::hatch_ant` lacked a game state guard, allowing post-match commands at 0:00 (`GameOver` freeze) to mutate team scores (deducting 200 points), decrement egg inventories, and spawn units.

This document formulates the exact C++ remediation specifications, replacement code, and unified diff patches to resolve all three defects completely.

---

## 2. Issue 1: Concentric Chebyshev Anthill Queuing

### 2.1 Problem Analysis
In `src/ants_sim/sim_engine.cpp`:
```cpp
TileCoord SimulationEngine::assign_queue_slot(uint8_t team_id, [[maybe_unused]] TileCoord from_pos) {
    const auto* a = impl_->grid_.find_anthill(team_id);
    int32_t bx = a ? a->x : 30;
    int32_t by = a ? a->y : 30;
    return TileCoord{bx + 1, by};
}
```
- **Reviewer Finding 1**: Integrates zero ring logic; causes units approaching from North, South, East, and West to all collide onto a single hardcoded tile `{bx + 1, by}`.
- **Challenger Defect 6.5**: `slotN == slotS && slotN == slotW && slotN == slotE` failed because all directions received `{31, 30}`.
- **Specification (`PROJECT.md` Feature 21 & `GAME_REVERSE_ENGINEERING.md` §5.6)**:
  Units approaching the base must be assigned discrete queue slots in concentric Chebyshev rings ($R=1, 2, \dots, 5$) centered at the anthill $(b_x, b_y)$. The assigned slot must be:
  1. On the perimeter of the lowest available ring $R$: $\max(|x - b_x|, |y - b_y|) == R$.
  2. In bounds: `grid.in_bounds(x, y)`.
  3. Passable for standard ground units: `cell.is_passable(false, false)`.
  4. Unoccupied: Not previously reserved and not currently occupied by another unit in `QueuingBase` or `EnteringBase`.
  5. Directionally optimal: Minimizes Manhattan distance to `from_pos`, tie-broken by Euclidean distance and deterministic coordinate order.

### 2.2 Invariants & Lifecycle Management
- `SimulationEngineImpl` maintains `std::vector<TileCoord> reserved_queue_slots_`.
- When an ant is assigned a slot, the slot is immediately appended to `reserved_queue_slots_` to prevent subsequent assignments within the same tick or frame from colliding.
- Slots are cleared upon `init()`, `init_test_world()`, and `reset()`.
- Public helper methods are added to `SimulationEngine`:
  - `void release_queue_slot(TileCoord slot);`
  - `void clear_reserved_queue_slots();`
  - `bool is_queue_slot_reserved(TileCoord slot) const;`

### 2.3 Exact Replacement Code

#### In `include/ants_sim/sim_engine.hpp` (around line 235):
```cpp
    TileCoord assign_queue_slot(uint8_t team_id, TileCoord from_pos);
    void release_queue_slot(TileCoord slot);
    void clear_reserved_queue_slots();
    bool is_queue_slot_reserved(TileCoord slot) const;
```

#### In `src/ants_sim/sim_engine.cpp` (`SimulationEngineImpl` definition around line 27):
```cpp
    std::vector<TileCoord> reserved_queue_slots_;
```

#### In `src/ants_sim/sim_engine.cpp` (`init`, `init_test_world`, `reset`):
```cpp
    impl_->reserved_queue_slots_.clear();
```

#### In `src/ants_sim/sim_engine.cpp` (replaces lines 711–716):
```cpp
TileCoord SimulationEngine::assign_queue_slot(uint8_t team_id, TileCoord from_pos) {
    const auto* a = impl_->grid_.find_anthill(team_id);
    int32_t bx = a ? a->x : 30;
    int32_t by = a ? a->y : 30;

    for (int32_t r = 1; r <= 5; ++r) {
        std::vector<TileCoord> candidates;
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int32_t x = bx + dx;
                int32_t y = by + dy;

                if (!impl_->grid_.in_bounds(x, y)) continue;

                const auto& cell = impl_->grid_.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                if (!cell.is_passable(false, false)) continue;

                TileCoord cand{x, y};

                bool is_reserved = std::any_of(
                    impl_->reserved_queue_slots_.begin(),
                    impl_->reserved_queue_slots_.end(),
                    [&cand](const TileCoord& slot) { return slot == cand; }
                );
                if (is_reserved) continue;

                bool is_ant_queued = false;
                for (const auto& ant : impl_->ants_) {
                    if (ant && ant->is_alive() &&
                        (ant->state == UnitState::QueuingBase || ant->state == UnitState::EnteringBase) &&
                        ant->pos == cand) {
                        is_ant_queued = true;
                        break;
                    }
                }
                if (is_ant_queued) continue;

                candidates.push_back(cand);
            }
        }

        if (!candidates.empty()) {
            auto best_it = std::min_element(
                candidates.begin(),
                candidates.end(),
                [&from_pos](const TileCoord& c1, const TileCoord& c2) {
                    int32_t m1 = c1.manhattan_dist(from_pos);
                    int32_t m2 = c2.manhattan_dist(from_pos);
                    if (m1 != m2) return m1 < m2;

                    int64_t edx1 = c1.x - from_pos.x;
                    int64_t edy1 = c1.y - from_pos.y;
                    int64_t edx2 = c2.x - from_pos.x;
                    int64_t edy2 = c2.y - from_pos.y;
                    int64_t e1 = edx1 * edx1 + edy1 * edy1;
                    int64_t e2 = edx2 * edx2 + edy2 * edy2;
                    if (e1 != e2) return e1 < e2;

                    if (c1.y != c2.y) return c1.y < c2.y;
                    return c1.x < c2.x;
                }
            );

            TileCoord best_slot = *best_it;
            impl_->reserved_queue_slots_.push_back(best_slot);
            return best_slot;
        }
    }

    return TileCoord{bx + 1, by};
}

void SimulationEngine::release_queue_slot(TileCoord slot) {
    auto it = std::find(impl_->reserved_queue_slots_.begin(), impl_->reserved_queue_slots_.end(), slot);
    if (it != impl_->reserved_queue_slots_.end()) {
        impl_->reserved_queue_slots_.erase(it);
    }
}

void SimulationEngine::clear_reserved_queue_slots() {
    impl_->reserved_queue_slots_.clear();
}

bool SimulationEngine::is_queue_slot_reserved(TileCoord slot) const {
    return std::find(impl_->reserved_queue_slots_.begin(), impl_->reserved_queue_slots_.end(), slot) != impl_->reserved_queue_slots_.end();
}
```

---

## 3. Issue 2: Frame 8 Full Heal Audio Event Multiplication

### 3.1 Problem Analysis
In `src/ants_sim/sim_engine.cpp`:
```cpp
    if (target_frame >= 8) {
        u->heal_full();
        impl_->audio_queue_.push_back(AudioEvent{SoundID::PowerUpHeal, u->pixel_x, u->pixel_y, 1, u->player_id});
    }
```
- **Challenger Defect 6.2**: Stepping an entering ant through animation frames 8 to 16 caused the condition `target_frame >= 8` to evaluate true 9 consecutive times, generating 9 copies of Sound 36 (`powerupc.wav`) in the audio event queue.
- **Specification (`GAME_REVERSE_ENGINEERING.md` §5.6)**:
  Full heal and Sound 36 occur strictly once at Frame 8 of `hgen301` when the ant is submerged underground inside the base.

### 3.2 Exact Replacement Code

#### In `src/ants_sim/sim_engine.cpp` (lines 730–738):
```cpp
    if (target_frame == 8) {
        u->heal_full();
        impl_->audio_queue_.push_back(AudioEvent{SoundID::PowerUpHeal, u->pixel_x, u->pixel_y, 1, u->player_id});
    }

    if (target_frame >= 16) {
        u->state = UnitState::Idle;
        u->anim_subitem = 0;
    }
```

---

## 4. Issue 3: Post-Match Game Freeze on Egg Hatching

### 4.1 Problem Analysis
In `src/ants_sim/sim_engine.cpp`:
```cpp
bool SimulationEngine::hatch_ant(uint8_t player_id, AntType type) {
    if (player_id >= MAX_PLAYERS) return false;
    if (impl_->stats_.get_individual_score(player_id) < static_cast<int32_t>(HATCH_COST_POINTS)) return false;
    if (impl_->stats_.get_egg_count(player_id) < 1) return false;
```
- **Challenger Defect 6.4**: `hatch_ant` did not verify if match time had expired (`match_time_remaining_ms_ == 0`) or if `match_state_ == MatchState::GameOver`. Calling `hatch_ant` after 0:00 deducted 200 points, consumed an egg, and altered scorecard metrics (`new_hatched++`) during simulation freeze.
- **Specification (`PROJECT.md` Feature 31)**:
  Immediate simulation freeze at 0:00; no unit spawning or economic mutations are permitted after game over.

### 4.2 Exact Replacement Code

#### In `src/ants_sim/sim_engine.cpp` (lines 289–294):
```cpp
bool SimulationEngine::hatch_ant(uint8_t player_id, AntType type) {
    if (is_match_over() || impl_->match_state_ == MatchState::GameOver) return false;
    if (player_id >= MAX_PLAYERS) return false;
    if (impl_->stats_.get_individual_score(player_id) < static_cast<int32_t>(HATCH_COST_POINTS)) return false;
    if (impl_->stats_.get_egg_count(player_id) < 1) return false;
```

---

## 5. Consolidated Unified Git Diff

```diff
diff --git a/include/ants_sim/sim_engine.hpp b/include/ants_sim/sim_engine.hpp
--- a/include/ants_sim/sim_engine.hpp
+++ b/include/ants_sim/sim_engine.hpp
@@ -235,3 +235,6 @@ public:
     TileCoord assign_queue_slot(uint8_t team_id, TileCoord from_pos);
+    void release_queue_slot(TileCoord slot);
+    void clear_reserved_queue_slots();
+    bool is_queue_slot_reserved(TileCoord slot) const;
     void step_base_entry_animation(uint32_t ant_id, uint16_t target_frame);
 
diff --git a/src/ants_sim/sim_engine.cpp b/src/ants_sim/sim_engine.cpp
--- a/src/ants_sim/sim_engine.cpp
+++ b/src/ants_sim/sim_engine.cpp
@@ -2,4 +2,5 @@
 #include <unordered_map>
 #include <memory>
 #include <algorithm>
+#include <cmath>
 #include <iostream>
@@ -26,2 +27,3 @@ public:
     std::vector<NewsEvent>  news_queue_;
+    std::vector<TileCoord>  reserved_queue_slots_;
 
@@ -101,2 +103,3 @@ void SimulationEngine::init(const ants::assets::LevelData& level, uint32_t rando
     impl_->world_state_dirty_ = true;
+    impl_->reserved_queue_slots_.clear();
 
@@ -122,2 +125,3 @@ void SimulationEngine::init_test_world(uint32_t width, uint32_t height, uint32_
     impl_->world_state_dirty_ = true;
+    impl_->reserved_queue_slots_.clear();
 
@@ -139,2 +143,3 @@ void SimulationEngine::reset() {
     impl_->world_state_dirty_ = true;
+    impl_->reserved_queue_slots_.clear();
 }
@@ -289,2 +294,3 @@ void SimulationEngine::issue_order(const AntOrder& order) {
 bool SimulationEngine::hatch_ant(uint8_t player_id, AntType type) {
+    if (is_match_over() || impl_->match_state_ == MatchState::GameOver) return false;
     if (player_id >= MAX_PLAYERS) return false;
@@ -711,6 +717,76 @@ void SimulationEngine::set_anthill(uint8_t team_id, TileCoord pos) {
-TileCoord SimulationEngine::assign_queue_slot(uint8_t team_id, [[maybe_unused]] TileCoord from_pos) {
-    const auto* a = impl_->grid_.find_anthill(team_id);
-    int32_t bx = a ? a->x : 30;
-    int32_t by = a ? a->y : 30;
-    return TileCoord{bx + 1, by};
-}
+TileCoord SimulationEngine::assign_queue_slot(uint8_t team_id, TileCoord from_pos) {
+    const auto* a = impl_->grid_.find_anthill(team_id);
+    int32_t bx = a ? a->x : 30;
+    int32_t by = a ? a->y : 30;
+
+    for (int32_t r = 1; r <= 5; ++r) {
+        std::vector<TileCoord> candidates;
+        for (int32_t dy = -r; dy <= r; ++dy) {
+            for (int32_t dx = -r; dx <= r; ++dx) {
+                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
+                int32_t x = bx + dx;
+                int32_t y = by + dy;
+
+                if (!impl_->grid_.in_bounds(x, y)) continue;
+
+                const auto& cell = impl_->grid_.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
+                if (!cell.is_passable(false, false)) continue;
+
+                TileCoord cand{x, y};
+
+                bool is_reserved = std::any_of(
+                    impl_->reserved_queue_slots_.begin(),
+                    impl_->reserved_queue_slots_.end(),
+                    [&cand](const TileCoord& slot) { return slot == cand; }
+                );
+                if (is_reserved) continue;
+
+                bool is_ant_queued = false;
+                for (const auto& ant : impl_->ants_) {
+                    if (ant && ant->is_alive() &&
+                        (ant->state == UnitState::QueuingBase || ant->state == UnitState::EnteringBase) &&
+                        ant->pos == cand) {
+                        is_ant_queued = true;
+                        break;
+                    }
+                }
+                if (is_ant_queued) continue;
+
+                candidates.push_back(cand);
+            }
+        }
+
+        if (!candidates.empty()) {
+            auto best_it = std::min_element(
+                candidates.begin(),
+                candidates.end(),
+                [&from_pos](const TileCoord& c1, const TileCoord& c2) {
+                    int32_t m1 = c1.manhattan_dist(from_pos);
+                    int32_t m2 = c2.manhattan_dist(from_pos);
+                    if (m1 != m2) return m1 < m2;
+
+                    int64_t edx1 = c1.x - from_pos.x;
+                    int64_t edy1 = c1.y - from_pos.y;
+                    int64_t edx2 = c2.x - from_pos.x;
+                    int64_t edy2 = c2.y - from_pos.y;
+                    int64_t e1 = edx1 * edx1 + edy1 * edy1;
+                    int64_t e2 = edx2 * edx2 + edy2 * edy2;
+                    if (e1 != e2) return e1 < e2;
+
+                    if (c1.y != c2.y) return c1.y < c2.y;
+                    return c1.x < c2.x;
+                }
+            );
+
+            TileCoord best_slot = *best_it;
+            impl_->reserved_queue_slots_.push_back(best_slot);
+            return best_slot;
+        }
+    }
+
+    return TileCoord{bx + 1, by};
+}
+
+void SimulationEngine::release_queue_slot(TileCoord slot) {
+    auto it = std::find(impl_->reserved_queue_slots_.begin(), impl_->reserved_queue_slots_.end(), slot);
+    if (it != impl_->reserved_queue_slots_.end()) {
+        impl_->reserved_queue_slots_.erase(it);
+    }
+}
+
+void SimulationEngine::clear_reserved_queue_slots() {
+    impl_->reserved_queue_slots_.clear();
+}
+
+bool SimulationEngine::is_queue_slot_reserved(TileCoord slot) const {
+    return std::find(impl_->reserved_queue_slots_.begin(), impl_->reserved_queue_slots_.end(), slot) != impl_->reserved_queue_slots_.end();
+}
@@ -730,3 +806,3 @@ void SimulationEngine::step_base_entry_animation(uint32_t ant_id, uint16_t targe
-    if (target_frame >= 8) {
+    if (target_frame == 8) {
         u->heal_full();
@@ -736,2 +812,3 @@ void SimulationEngine::step_base_entry_animation(uint32_t ant_id, uint16_t targe
     if (target_frame >= 16) {
         u->state = UnitState::Idle;
+        u->anim_subitem = 0;
     }
```

---

## 6. Verification Results

A standalone verification test suite (`verify_remediation.cpp`) was executed against the exact proposed implementation. All scenarios passed:
```
=== RUNNING REMEDIATION VERIFICATION TESTS ===
Slot N (from 30,25): (30, 29)
Slot S (from 30,35): (30, 31)
Slot W (from 25,30): (29, 30)
Slot E (from 35,30): (31, 30)
PASS: Directional 4-way slots are distinct and optimal.
PASS: Entire Ring 1 (8 slots) completely filled.
Slot R2 E (from 35,30): (32, 30)
PASS: 9th unit successfully expands to Ring 2 at (32, 30).
Slot Corner (from 5,5, base 0,0): (1, 1)
PASS: Anthill at grid corner does not generate out-of-bounds slots.
Slot Avoid Obstacle (from 35,30, obstacle at 31,30): (31, 29)
PASS: Solid obstacle at (31, 30) bypassed, candidate picked from remaining Ring 1.
PASS: Frame 8 heal audio event triggers strictly once in [8..16].
PASS: Post-game match over prevents ant hatching.

ALL VERIFICATION CHECKS PASSED PERFECTLY!
```
