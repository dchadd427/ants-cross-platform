#pragma once

// The scripted AGGRESSOR of the bench (docs/audit/B4_1_notes.md, acceptance A4): a TEST-ONLY bot that is never in the registry (make_bot does not know it; the arena and the tests hand
// it to a match through ArenaSpec::factory). It exists to measure what the standard bot keeps of its score against an opponent that attacks:
//
//   it takes a Thief power-up with the nearest idle worker, and up to two Combat power-ups,
//   it raids, again and again, the hill with the highest score box (at least 30 points) that has not shut its thief hole with walls or bombs,
//   it attacks the nearest enemy carrier with every ant that is not on its way to a power-up (workers and Combat Ants), one order for every blow,
//   it never harvests (so that everything it takes from a victim is raid and harassment, not a share of the pot).
//
// It is a client like every bot: the BotView, Orders and the controller's door, the profile of its level. It is deliberately simple (no contest awareness, no leash), and it is the
// opponent of the standard bot, not a model of a good player.

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/standard_tasks.hpp"
#include "ants_ai/tactics.hpp"

namespace ants::ai::bench {

class AggressorBot final : public Bot {
public:
    const char* kind() const noexcept override { return "aggressor"; }
    void start(const BotContext& context) override {
        seat_ = context.seat;
        profile_ = context.profile;
        map_ = context.map;
    }

    void think(const BotView& view, Orders& orders) override {
        const MapInfo* map = view.map() != nullptr ? view.map() : map_;
        if (map == nullptr || !view.has_grid()) return;
        const uint64_t now = view.tick();
        const sim::Grid& grid = view.grid();

        // an invitation: say no (an alliance of all live teams would end the match)
        if (view.invite_from() < sim::MAX_PLAYERS && now >= deny_after_) {
            orders.deny(view.invite_from());
            deny_after_ = now + 100;
        }

        // forget what is gone
        for (auto it = taking_.begin(); it != taking_.end();) {
            const AntView* a = find(view.mine(), it->first);
            if (a == nullptr || a->type != sim::AntType::Worker || now > it->second.since + 600) it = taking_.erase(it);
            else ++it;
        }

        // 1. power-ups: one Thief, two Combat Ants (counting the ants that are on their way)
        size_t thieves = 0;
        size_t combats = 0;
        for (const AntView& a : view.mine()) {
            thieves += a.type == sim::AntType::Thief ? 1u : 0u;
            combats += a.type == sim::AntType::Combat ? 1u : 0u;
        }
        for (const auto& t : taking_) {
            thieves += t.second.kind == sim::AntType::Thief ? 1u : 0u;
            combats += t.second.kind == sim::AntType::Combat ? 1u : 0u;
        }
        const auto send = [&](sim::AntType kind) {
            const PowerUpView* best = nullptr;
            const AntView* who = nullptr;
            int32_t best_d = 0;
            for (const PowerUpView& p : view.powerups()) {
                if (p.kind != kind || p.standing_ant != 0) continue;
                for (const AntView& a : view.mine()) {
                    if (a.type != sim::AntType::Worker || !a.idle() || a.holding || taking_.count(a.id) != 0) continue;
                    if (!reaches(*map, a.tile, p.tile)) continue;
                    const int32_t d = a.tile.chebyshev_dist(p.tile);
                    if (best == nullptr || d < best_d) {
                        best = &p;
                        who = &a;
                        best_d = d;
                    }
                }
            }
            if (best == nullptr) return false;
            orders.pick_up(who->id, best->tile);
            taking_[who->id] = Taking{kind, now};
            return true;
        };
        if (thieves < 1) send(sim::AntType::Thief);
        else if (combats < 2) send(sim::AntType::Combat);

        // 2. the thieves raid
        for (const AntView& a : view.mine()) {
            if (a.type != sim::AntType::Thief || !a.idle() || a.holding || a.carried_points > 0) continue;
            const auto last = raided_.find(a.id);
            if (last != raided_.end() && now < last->second + 80) continue;                // the last order is still on its way
            if (view.ticks_left() < 700) continue;
            int target = -1;
            int32_t best_score = 29;
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                const TeamRow& row = view.rows()[t];
                if (t == seat_ || !row.present || row.dropped || row.ally == seat_ || row.score <= best_score) continue;
                const auto b = black_.find(t);
                if (b != black_.end() && b->second > now) continue;
                const HillInfo& hill = map->hill(t);
                if (!hill.present || east_state(grid, hill).shut()) continue;
                bool reach = false;
                for (const sim::TileCoord& e : east_tiles(hill)) reach = reach || reaches(*map, a.tile, e);
                if (!reach) continue;
                target = t;
                best_score = row.score;
            }
            // a thief that stood idle for a long time after an order did not get through (a hill that was shut): leave that hill alone for a while
            if (last != raided_.end() && now >= last->second + 80 && now < last->second + 400 && raid_target_.count(a.id) != 0 && raid_target_[a.id] >= 0) {
                black_[static_cast<uint8_t>(raid_target_[a.id])] = now + 600;
                raid_target_[a.id] = -1;
                continue;
            }
            if (target < 0) continue;
            const HillInfo& hill = map->hill(static_cast<uint8_t>(target));
            orders.special(a.id, hill.entrance);
            raided_[a.id] = now;
            raid_target_[a.id] = target;
        }

        // 3. every other ant attacks the nearest carrier it can reach (one order is one blow)
        std::vector<const AntView*> carriers;
        for (const AntView& e : view.others()) {
            if (e.holding && attackable(view, e)) carriers.push_back(&e);
        }
        if (!carriers.empty()) {
            std::map<uint32_t, std::vector<uint32_t>> groups;                              // target ant -> attackers
            for (const AntView& a : view.mine()) {
                if (!a.idle() || a.holding || taking_.count(a.id) != 0 || a.hp < 3) continue;
                if (a.type != sim::AntType::Worker && a.type != sim::AntType::Combat) continue;
                const AntView* near = nullptr;
                int32_t near_d = 0;
                for (const AntView* c : carriers) {
                    const int32_t d = a.tile.chebyshev_dist(c->tile);
                    if (near == nullptr || d < near_d) {
                        near = c;
                        near_d = d;
                    }
                }
                if (near != nullptr) groups[near->id].push_back(a.id);
            }
            for (const auto& g : groups) {
                const AntView* target = find(view.others(), g.first);
                if (target != nullptr) orders.attack(g.second, target->tile, Priority::Urgent);
            }
        }
    }

private:
    struct Taking {
        sim::AntType kind{sim::AntType::Thief};
        uint64_t since{0};
    };
    static const AntView* find(const std::vector<AntView>& ants, uint32_t id) {
        const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
        return it != ants.end() && it->id == id ? &*it : nullptr;
    }
    // whether an ant of the seat standing on `from` can walk to a tile next to `to` (the walker components of the start)
    bool reaches(const MapInfo& map, sim::TileCoord from, sim::TileCoord to) const {
        const int32_t mine = map.ant_component(seat_, from);
        if (mine < 0) return false;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (map.component(seat_, sim::TileCoord{to.x + dx, to.y + dy}) == mine) return true;
            }
        }
        return false;
    }

    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    uint64_t deny_after_{0};
    std::map<uint32_t, Taking> taking_;                  // worker -> the power-up it was sent for
    std::map<uint32_t, uint64_t> raided_;                // thief -> the tick of its last raid order
    std::map<uint32_t, int> raid_target_;                // thief -> the team of its last raid order
    std::map<uint8_t, uint64_t> black_;                  // a team whose hill could not be raided -> until when it is left alone
};

}  // namespace ants::ai::bench
