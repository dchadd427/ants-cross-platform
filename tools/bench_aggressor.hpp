#pragma once

// The scripted AGGRESSOR of the bench (docs/audit/B4_1_notes.md, acceptance A4): a TEST-ONLY bot that is never in the registry (make_bot does not know it; the arena and the tests hand
// it to a match through ArenaSpec::factory). It exists to measure what the standard bot keeps of its score against an opponent that attacks:
//
//   it takes a Thief power-up with the nearest idle worker (two of them in the double-thief opening, kind "aggressor2"), and up to two Combat power-ups,
//   it raids, again and again, the hill with the highest score box (at least 30 points) that has not shut its thief hole with walls or bombs,
//   it attacks the nearest enemy carrier with every ant that is not on its way to a power-up (workers and Combat Ants), one order for every blow,
//   it never harvests (so that everything it takes from a victim is raid and harassment, not a share of the pot).
//
// It is a client like every bot: the BotView, Orders and the controller's door, the profile of its level. It is deliberately simple (no contest awareness, no leash), and it is the
// opponent of the standard bot, not a model of a good player.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/standard_tasks.hpp"
#include "ants_ai/tactics.hpp"

namespace ants::ai::bench {

class AggressorBot final : public Bot {
public:
    /// thieves: how many Thief power-ups it takes before it takes the Combat ones (1: the plain aggressor; 2: the DOUBLE-THIEF opening, kind "aggressor2": the thief of
    /// its own side and, with the next idle worker, the nearest other Thief power-up that nobody stands on, then two raids from the start)
    /// max_attackers: how many ants (Combat Ants first) attack carriers at most (the experiments of the harassment's worth: "aggr3" attacks with three ants only; the rest stand idle as a stand-in for an economy)
    explicit AggressorBot(size_t thieves = 1, size_t max_attackers = 1000) : thieves_wanted_(thieves), max_attackers_(max_attackers) {}
    const char* kind() const noexcept override { return max_attackers_ < 1000 ? "aggrN" : thieves_wanted_ >= 2 ? "aggressor2" : "aggressor"; }
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
                bool claimed = false;
                for (const auto& t : taking_) claimed = claimed || (t.second.tile == p.tile);                // another worker is on its way to this one
                if (claimed) continue;
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
            taking_[who->id] = Taking{kind, now, best->tile};
            return true;
        };
        if (thieves < thieves_wanted_) send(sim::AntType::Thief);
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
            std::vector<const AntView*> order;                                             // the attackers in the order of use: Combat Ants first, then by id
            for (const AntView& a : view.mine()) order.push_back(&a);
            std::stable_sort(order.begin(), order.end(), [](const AntView* x, const AntView* y) { return (x->type == sim::AntType::Combat) > (y->type == sim::AntType::Combat); });
            size_t used = 0;
            for (const AntView* ap : order) {
                const AntView& a = *ap;
                if (a.type == sim::AntType::Thief || taking_.count(a.id) != 0) continue;
                if (used >= max_attackers_) break;
                ++used;
                if (!a.idle() || a.holding || a.hp < 3) continue;
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
        sim::TileCoord tile{};
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

    size_t thieves_wanted_{1};
    size_t max_attackers_{1000};
    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    uint64_t deny_after_{0};
    std::map<uint32_t, Taking> taking_;                  // worker -> the power-up it was sent for
    std::map<uint32_t, uint64_t> raided_;                // thief -> the tick of its last raid order
    std::map<uint32_t, int> raid_target_;                // thief -> the team of its last raid order
    std::map<uint8_t, uint64_t> black_;                  // a team whose hill could not be raided -> until when it is left alone
};

// The SABOTEUR (arena-only kind "saboteur", same rules as the aggressor: a bench opponent, never in the registry): it steals the victim's Fire and Bomber power-ups (the ones on the
// victim's side of the map first) with its nearest idle workers, and then
//   its Fire Ant walls in the victim's hill: the eight tiles around the gate (the three tiles of the queue row cannot take a wall), then the tiles around the victim's nearest piles,
//   its Bomber plants bombs on the way between the victim's hill and its nearest piles and around the gate.
// It never harvests and never fights (it is the opponent of the counters and of the denial of the standard bot, not a model of a good player). The victim is the enemy team with the
// highest score box, else the first one.
class SaboteurBot final : public Bot {
public:
    const char* kind() const noexcept override { return "saboteur"; }
    void start(const BotContext& context) override {
        seat_ = context.seat;
        map_ = context.map;
    }

    void think(const BotView& view, Orders& orders) override {
        const MapInfo* map = view.map() != nullptr ? view.map() : map_;
        if (map == nullptr || !view.has_grid()) return;
        const uint64_t now = view.tick();
        if (view.invite_from() < sim::MAX_PLAYERS && now >= deny_after_) {
            orders.deny(view.invite_from());
            deny_after_ = now + 100;
        }
        // the victim
        int victim = -1;
        int32_t best = -1;
        uint8_t present = 0;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) present = static_cast<uint8_t>(present | (view.rows()[t].present ? 1u << t : 0u));
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = view.rows()[t];
            if (t == seat_ || !row.present || row.dropped || !map->hill(t).present) continue;
            if (row.score > best) {
                best = row.score;
                victim = t;
            }
        }
        if (victim < 0) return;
        const HillInfo& hill = map->hill(static_cast<uint8_t>(victim));
        for (auto it = taking_.begin(); it != taking_.end();) {
            const AntView* a = find(view.mine(), it->first);
            if (a == nullptr || a->type != sim::AntType::Worker || now > it->second.since + 700) it = taking_.erase(it);
            else ++it;
        }
        // 1. the power-ups: one Fire, one Bomber
        for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber}) {
            bool have = false;
            for (const AntView& a : view.mine()) have = have || a.type == kind;
            for (const auto& t : taking_) have = have || t.second.kind == kind;
            if (have) continue;
            const PowerUpView* want = nullptr;
            const AntView* who = nullptr;
            int32_t want_d = 0;
            for (const PowerUpView& p : view.powerups()) {
                if (p.kind != kind || p.standing_ant != 0) continue;
                for (const AntView& a : view.mine()) {
                    if (a.type != sim::AntType::Worker || !a.idle() || a.holding || taking_.count(a.id) != 0 || !reaches(*map, a.tile, p.tile)) continue;
                    // the victim's side first (its own power-up), then the nearest
                    const int32_t d = a.tile.chebyshev_dist(p.tile) + (power_up_side(*map, p.tile, present) == victim ? 0 : 1000);
                    if (want == nullptr || d < want_d) {
                        want = &p;
                        who = &a;
                        want_d = d;
                    }
                }
            }
            if (want != nullptr) {
                orders.pick_up(who->id, want->tile);
                taking_[who->id] = Taking{kind, now};
            }
        }
        // 2. the Fire Ant walls in the victim's gate, then its piles
        std::vector<sim::TileCoord> walls;
        for (int32_t dx = -1; dx <= 3; ++dx) walls.push_back(sim::TileCoord{hill.origin.x + dx, hill.origin.y - 2});
        walls.push_back(sim::TileCoord{hill.origin.x - 1, hill.origin.y - 1});
        walls.push_back(sim::TileCoord{hill.origin.x + 3, hill.origin.y - 1});
        walls.push_back(sim::TileCoord{hill.origin.x - 1, hill.origin.y});
        for (const AntView& a : view.mine()) {
            if (a.type != sim::AntType::Fire || !a.idle()) continue;
            std::vector<sim::TileCoord> list = walls;
            for (const PileView& p : nearest_piles(view, hill, 3)) {                                  // then the tiles around the nearest piles
                for (int32_t dy = -2; dy <= 3; ++dy) {
                    for (int32_t dx = -2; dx <= 3; ++dx) {
                        if (dx > -2 && dx < 3 && dy > -2 && dy < 3) continue;
                        list.push_back(sim::TileCoord{p.anchor.x + dx, p.anchor.y + dy});
                    }
                }
            }
            for (const sim::TileCoord& t : list) {
                if (!view.grid().in_bounds(t) || view.grid().has_fire_at(t)) continue;
                const auto last = ordered_.find(key_of(t));
                if (last != ordered_.end() && now < last->second + 150) continue;
                sim::Command probe;
                probe.type = sim::CommandType::GroupSpecial;
                probe.tile_x = static_cast<int16_t>(t.x);
                probe.tile_y = static_cast<int16_t>(t.y);
                probe.ants = {a.id};
                if (view.predict_ack(probe) == 0) continue;
                orders.special(a.id, t);
                ordered_[key_of(t)] = now;
                break;
            }
        }
        // 3. the Bomber plants bombs between the victim's hill and its nearest piles, and around the gate
        std::vector<sim::TileCoord> bombs;
        for (const PileView& p : nearest_piles(view, hill, 3)) {
            for (int32_t k = 3; k <= 9; k += 2) {
                bombs.push_back(sim::TileCoord{hill.queue.x + (p.anchor.x - hill.queue.x) * k / std::max<int32_t>(1, std::abs(p.anchor.x - hill.queue.x) + std::abs(p.anchor.y - hill.queue.y)),
                                               hill.queue.y + (p.anchor.y - hill.queue.y) * k / std::max<int32_t>(1, std::abs(p.anchor.x - hill.queue.x) + std::abs(p.anchor.y - hill.queue.y))});
            }
        }
        for (const sim::TileCoord& t : {sim::TileCoord{hill.origin.x + 1, hill.origin.y - 4}, sim::TileCoord{hill.origin.x - 3, hill.origin.y - 1}, sim::TileCoord{hill.origin.x + 5, hill.origin.y - 2}}) bombs.push_back(t);
        for (const AntView& a : view.mine()) {
            if (a.type != sim::AntType::Bomber || !a.idle()) continue;
            for (const sim::TileCoord& t : bombs) {
                if (!view.grid().in_bounds(t) || view.grid().has_bomb_at(t)) continue;
                const auto last = ordered_.find(key_of(t) + (1 << 30));
                if (last != ordered_.end() && now < last->second + 200) continue;
                sim::Command probe;
                probe.type = sim::CommandType::GroupSpecial;
                probe.tile_x = static_cast<int16_t>(t.x);
                probe.tile_y = static_cast<int16_t>(t.y);
                probe.ants = {a.id};
                if (view.predict_ack(probe) == 0) continue;
                orders.special(a.id, t);
                ordered_[key_of(t) + (1 << 30)] = now;
                break;
            }
        }
    }

private:
    struct Taking {
        sim::AntType kind{sim::AntType::Fire};
        uint64_t since{0};
    };
    static int64_t key_of(sim::TileCoord t) { return static_cast<int64_t>(t.y) * 4096 + t.x; }
    static const AntView* find(const std::vector<AntView>& ants, uint32_t id) {
        const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
        return it != ants.end() && it->id == id ? &*it : nullptr;
    }
    static std::vector<PileView> nearest_piles(const BotView& view, const HillInfo& hill, size_t n) {
        std::vector<PileView> piles = view.piles();
        std::stable_sort(piles.begin(), piles.end(), [&](const PileView& x, const PileView& y) { return x.anchor.chebyshev_dist(hill.queue) < y.anchor.chebyshev_dist(hill.queue); });
        if (piles.size() > n) piles.resize(n);
        return piles;
    }
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
    const MapInfo* map_{nullptr};
    uint64_t deny_after_{0};
    std::map<uint32_t, Taking> taking_;
    std::map<int64_t, uint64_t> ordered_;
};

// The INVITER (arena-only: a wrapper, not a kind): the bots of the game never invite (docs/BOTS.md, the accept rule of the standard bot), so the arena seats an alliance by letting one seat
// invite another at its first look, once, through the controller like any command, and the invited standard bot answers by its accept rule. The wrapped bot plays on as before and the
// wrapper reports its kind.
class InviterBot final : public Bot {
public:
    InviterBot(std::unique_ptr<Bot> inner, uint8_t invitee, uint64_t at_tick = 8) : inner_(std::move(inner)), invitee_(invitee), at_tick_(at_tick) {}
    const char* kind() const noexcept override { return inner_->kind(); }
    void start(const BotContext& context) override { inner_->start(context); }
    void think(const BotView& view, Orders& orders) override {
        if (!sent_ && view.tick() >= at_tick_ && view.ally() >= sim::MAX_PLAYERS) {
            orders.invite(invitee_);
            sent_ = true;
        }
        inner_->think(view, orders);
    }
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override { inner_->on_command(command, fate, tick); }

private:
    std::unique_ptr<Bot> inner_;
    uint8_t invitee_;
    uint64_t at_tick_;
    bool sent_{false};
};

// The COUNTING wrapper of the experiments (arena-only; BOT_DIAG=1): counts what a bot proposes by kind of command and prints the numbers when the bot is destroyed
class CountBot final : public Bot {
public:
    CountBot(std::unique_ptr<Bot> inner, std::string label) : inner_(std::move(inner)), label_(std::move(label)) {}
    ~CountBot() override {
        std::fprintf(stderr, "COUNT seat %u %s: attack %u move %u special %u other %u (intents)\n", static_cast<unsigned>(seat_), label_.c_str(), attack_, move_, special_, other_);
    }
    const char* kind() const noexcept override { return inner_->kind(); }
    void start(const BotContext& context) override {
        seat_ = context.seat;
        inner_->start(context);
    }
    void think(const BotView& view, Orders& orders) override {
        const size_t before = orders.intents().size();
        inner_->think(view, orders);
        for (size_t i = before; i < orders.intents().size(); ++i) {
            switch (orders.intents()[i].command.type) {
                case sim::CommandType::GroupAttack: attack_ += static_cast<uint32_t>(orders.intents()[i].command.ants.size()); break;
                case sim::CommandType::GroupMove: move_ += static_cast<uint32_t>(orders.intents()[i].command.ants.size()); break;
                case sim::CommandType::GroupSpecial: ++special_; break;
                default: ++other_; break;
            }
        }
    }
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override { inner_->on_command(command, fate, tick); }

private:
    std::unique_ptr<Bot> inner_;
    std::string label_;
    uint8_t seat_{0};
    uint32_t attack_{0};
    uint32_t move_{0};
    uint32_t special_{0};
    uint32_t other_{0};
};

// The DIAGNOSTIC wrapper of the experiments (arena-only; set BOT_DIAG=1): runs a standard bot and prints, when the match is over and the bot is destroyed, what its tasks did and how many attack
// orders it proposed (one line of key=value pairs)
class DiagBot final : public Bot {
public:
    DiagBot(std::unique_ptr<StandardBot> inner, std::string label) : inner_(std::move(inner)), label_(std::move(label)) {}
    ~DiagBot() override {
        const StandardBot& b = *inner_;
        std::fprintf(stderr,
                     "DIAG label=%s seat=%u style=%s attack_cmds=%u attack_ants=%u harass=%u fight=%u raids=%u walls=%u sabwalls=%u strikes=%u hatches=%u pickups=%u gate=%u squad_end=%zu recruited=%u\n",
                     label_.c_str(), static_cast<unsigned>(seat_), style_name(b.style()), attack_cmds_, attack_ants_, b.harass().attacks_ordered(), b.fight().attacks_ordered(), b.raids().raids_ordered(), b.walls().walls_ordered(),
                     b.sabotage().walls_ordered(), b.strike().strikes_started(), b.hatch().hatches_ordered(), b.powerups().taken(), b.gate().entrance_clicks(), b.harass().squad(), b.harass().recruited());
    }
    const char* kind() const noexcept override { return inner_->kind(); }
    void start(const BotContext& context) override {
        seat_ = context.seat;
        inner_->start(context);
    }
    void think(const BotView& view, Orders& orders) override {
        const size_t before = orders.intents().size();
        inner_->think(view, orders);
        for (size_t i = before; i < orders.intents().size(); ++i) {
            if (orders.intents()[i].command.type == sim::CommandType::GroupAttack) {
                ++attack_cmds_;
                attack_ants_ += static_cast<uint32_t>(orders.intents()[i].command.ants.size());
            }
        }
    }
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override { inner_->on_command(command, fate, tick); }

private:
    std::unique_ptr<StandardBot> inner_;
    std::string label_;
    uint8_t seat_{0};
    uint32_t attack_cmds_{0};
    uint32_t attack_ants_{0};
};

}  // namespace ants::ai::bench
