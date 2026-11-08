#pragma once

// The GANG of the bench: a TEST-ONLY bot that is never in the registry (make_bot does not know it; bot_arena, bot_watch and the tests hand it to a match through a factory). It plays the way a good
// person plays against the standard bot, and it exists to measure how the standard bot answers a small group that hits one ant at a time (docs/BOT_PEOPLE.md). At every look, in this order:
//
//   1. the economy   it harvests with the standard bot's own HarvestTask (every type of ant harvests when it has nothing else to do) and takes power-ups with the standard bot's PowerUpTask:
//                    Thief power-ups first (up to three, the other sides' too), then one Combat Ant, then one Fire Ant. It has none of the standard bot's fight tasks.
//   2. the raids     every Thief raids the hill whose score box is highest (at least 30 points) that is not shut, again and again, and harvests between raids.
//   3. the gang      it keeps a gang of `gang_size` ants (four by default; every type of ant but a Thief; never an ant that carries food). The target is the enemy ant that carries nothing within eight
//                    tiles of the gang's centre: Thieves, Fire Ants and Bombers first, then the one with the fewest hit points. One strike order names every gang ant that is idle after its blow, again and
//                    again, until the target is dead or more than ten tiles from the gang's centre; then it takes the next one. A gang ant with three hit points or fewer goes to its hill and rejoins
//                    when it is healed. With no target in reach the gang walks to the pile that is nearest the victim's hill (the victim is the enemy with the highest score box) and waits there.
//   4. the hunter    one Combat Ant (else the Fire Ant; it is not of the gang) stands four tiles from the victim's hill entrance, on the way to the victim's nearest pile. It strikes any enemy ant within
//                    three tiles that is not a Combat Ant, again after every blow, and goes home at three hit points or fewer.
//   5. no fire walls of its own, no mines, no eggs.
//
// It is a client like every bot: the BotView, Orders and the controller's door, the profile of its level. It is deliberately simple and it is the opponent of the standard bot, not a model of a
// good player.

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/standard_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai::bench {

class GangBot final : public Bot {
public:
    explicit GangBot(Level level, size_t gang_size = 4)
        : gang_size_(gang_size), tactics_(make_tactics(level)), harvest_(StandardBot::kHarvest, harvest_params(level)), powerups_(StandardBot::kPowerUps, tactics_) {}

    const char* kind() const noexcept override { return "gang"; }
    void start(const BotContext& context) override {
        seat_ = context.seat;
        profile_ = context.profile;
        map_ = context.map;
        ledger_.set_rank(StandardBot::kHarvest, 1);                                        // (a task of a higher rank takes ants from a lower one: the economy's pick-up trips come before the gang)
        ledger_.set_rank(kRaid, 3);
        ledger_.set_rank(kGang, 5);
        ledger_.set_rank(kHunter, 5);
        ledger_.set_rank(kHeal, 6);
        ledger_.set_rank(StandardBot::kPowerUps, 7);
    }
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override {
        harvest_.on_command(command, fate, tick);
        powerups_.on_command(command, fate, tick);
    }

    void think(const BotView& view, Orders& orders) override {
        const MapInfo* map = view.map() != nullptr ? view.map() : map_;
        if (map == nullptr || !view.has_grid()) return;
        now_ = view.tick();
        if (view.invite_from() < sim::MAX_PLAYERS && now_ >= deny_after_) {                  // an invitation: say no (an alliance of all live teams would end the match)
            orders.deny(view.invite_from());
            deny_after_ = now_ + 100;
        }
        ledger_.forget_missing(view.mine());
        TaskContext context{view, orders, ledger_, profile_, *map, seat_};
        victim_ = pick_victim(view, *map);
        set_wants(view);
        powerups_.step(context);
        raids(view, orders, *map);
        heal(view, orders, *map);
        hunter(view, orders, *map);
        gang(view, orders, *map);
        harvest_.step(context);
    }

    /// For the tests: the gang's ants now, and the hunter (0: none)
    std::vector<uint32_t> gang_ants() const { return ledger_.ants_of(kGang); }
    uint32_t hunter_ant() const noexcept { return hunter_; }

    static constexpr TaskId kGang = 30;
    static constexpr TaskId kHunter = 31;
    static constexpr TaskId kHeal = 32;
    static constexpr TaskId kRaid = 33;

private:
    static constexpr uint64_t kGangFromTick = 600;       // the opening (the power-up trips, the first loads of food) is the economy's
    static constexpr uint64_t kOpeningTicks = 1200;      // after this the Combat Ant and the Fire Ant are wanted whatever the Thief power-ups do
    static constexpr int32_t kTargetReach = 8;           // an enemy within this many tiles of the gang's centre is a target ...
    static constexpr int32_t kTargetLeash = 10;          // ... and stays one until it is farther than this
    static constexpr int32_t kHunterReach = 3;
    static constexpr int32_t kHunterPost = 4;            // tiles from the victim's hill entrance
    static constexpr uint8_t kHomeHp = 3;                // this many hit points or fewer: go home to heal
    static constexpr uint8_t kRejoinHp = 8;              // healed: at least this many
    static constexpr uint64_t kBlowGap = 12;             // an ant that is idle again sooner than this after an order of ours has not even begun it: not ordered twice
    static constexpr uint64_t kHealTicks = 1500;

    static Tactics make_tactics(Level level) {
        Tactics t;
        t.plan = plan_for(level);
        t.plan.steals = true;                            // the Thief power-ups of the other sides are taken too
        t.plan.secure_side = false;
        t.plan.flower_sides = false;
        t.plan.flower_fire = false;
        t.plan.flower_recover = false;
        t.plan.flower_recall = false;
        t.plan.flower_watch = false;
        t.plan.opening_order = {sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Swimmer};
        return t;
    }
    // The standard bot's economy as its plan makes it, with every type of ant in the pool and the rescue of idle carriers on (no gate task guides them here)
    static HarvestTask::Params harvest_params(Level level) {
        const StandardBot probe(plan_for(level));
        HarvestTask::Params p = probe.harvest().params();
        p.extra_types = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Bomber)) |
                                             (1u << static_cast<unsigned>(sim::AntType::Thief)) | (1u << static_cast<unsigned>(sim::AntType::Combat)));
        p.rescue = true;
        p.gate_gap_ticks = HarvestTask::Params{}.gate_gap_ticks;
        return p;
    }
    static const AntView* find(const std::vector<AntView>& ants, uint32_t id) {
        const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
        return it != ants.end() && it->id == id ? &*it : nullptr;
    }
    static bool carries(const AntView& a) { return a.holding || a.carried_points > 0; }
    // whether an ant of the seat standing on `from` can walk to a tile next to `to`
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
    bool enemy_of(const BotView& view, const AntView& e) const { return e.team != seat_ && !(view.ally() < sim::MAX_PLAYERS && e.team == view.ally()); }

    // The victim: the enemy team with the highest score box that has a hill (the lowest seat for equal boxes); -1 when there is none
    int pick_victim(const BotView& view, const MapInfo& map) const {
        int victim = -1;
        int32_t best = -1;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = view.rows()[t];
            if (t == seat_ || !row.present || row.dropped || row.ally == seat_ || !map.hill(t).present) continue;
            if (row.score > best) {
                best = row.score;
                victim = t;
            }
        }
        return victim;
    }
    // The pile with units left that the victim's hill reaches at the least cost (null: none)
    const PileView* victim_pile(const BotView& view, const MapInfo& map) const {
        const PileView* best = nullptr;
        int32_t best_cost = 0;
        for (const PileView& p : view.piles()) {
            const PileInfo* info = map.pile(p.index);
            if (info == nullptr || !info->approach[static_cast<size_t>(victim_)].reachable()) continue;
            if (best == nullptr || info->approach[static_cast<size_t>(victim_)].cost < best_cost) {
                best = &p;
                best_cost = info->approach[static_cast<size_t>(victim_)].cost;
            }
        }
        return best;
    }
    // The free tile nearest to `want` (within `reach` tiles of it) that a walk may end on: no obstacle, food, power-up, fire wall or bomb; false when none is
    bool free_tile_near(const BotView& view, sim::TileCoord want, int32_t reach, sim::TileCoord& out) const {
        const sim::Grid& grid = view.grid();
        int32_t best = 1 << 20;
        for (int32_t dy = -reach; dy <= reach; ++dy) {
            for (int32_t dx = -reach; dx <= reach; ++dx) {
                const sim::TileCoord t{want.x + dx, want.y + dy};
                if (!grid.in_bounds(t) || !MapInfo::walkable(grid, seat_, t, view.walk_context()) || grid.has_bomb_at(t) || grid.has_fire_at(t) || view.powerup_at(t) != nullptr) continue;
                const int32_t d = std::max(std::abs(dx), std::abs(dy));
                if (d < best) {
                    best = d;
                    out = t;
                }
            }
        }
        return best < (1 << 20);
    }

    // ---- 1. the economy: what the power-up task is asked for ---------------------------------------------------------------------------------------------------------
    void set_wants(const BotView& view) {
        tactics_.wants.fill(0);
        std::array<size_t, 6> have{};
        std::array<bool, 6> lies{};
        for (const AntView& a : view.mine()) ++have[static_cast<size_t>(a.type)];
        for (const auto& trip : powerups_.on_their_way()) ++have[static_cast<size_t>(trip.second)];
        for (const PowerUpView& p : view.powerups()) lies[static_cast<size_t>(p.kind)] = lies[static_cast<size_t>(p.kind)] || p.standing_ant == 0;
        const size_t thief = static_cast<size_t>(sim::AntType::Thief);
        const size_t combat = static_cast<size_t>(sim::AntType::Combat);
        const size_t fire = static_cast<size_t>(sim::AntType::Fire);
        const bool late = now_ >= kOpeningTicks;
        tactics_.wants[thief] = 3;
        const bool thieves_done = have[thief] >= 3 || !lies[thief];
        if (late || thieves_done) tactics_.wants[combat] = 1;
        const bool combat_done = have[combat] >= 1 || !lies[combat];
        if (late || (thieves_done && combat_done)) tactics_.wants[fire] = 1;
    }

    // ---- 2. the raids ------------------------------------------------------------------------------------------------------------------------------------------------
    void raids(const BotView& view, Orders& orders, const MapInfo& map) {
        if (view.ticks_left() < 700) return;
        const sim::Grid& grid = view.grid();
        for (const AntView& a : view.mine()) {
            if (a.type != sim::AntType::Thief) continue;
            const TaskId owner = ledger_.owner(a.id);
            if (carries(a)) {
                if (owner == kRaid) paid_.insert(a.id);                                  // it has taken loot: the raid got through
                continue;
            }
            if (!a.idle() || (owner != kNoTask && owner != StandardBot::kHarvest && owner != kRaid)) continue;
            const auto last = raided_.find(a.id);
            if (last != raided_.end() && now_ < last->second + 80) continue;            // the last order is still on its way
            // idle and empty soon after an order, with no loot: the hill was shut. It is left alone for a while.
            if (last != raided_.end() && now_ < last->second + 400 && paid_.count(a.id) == 0 && raid_target_[a.id] >= 0) {
                black_[static_cast<uint8_t>(raid_target_[a.id])] = now_ + 600;
                raid_target_[a.id] = -1;
                continue;
            }
            int target = -1;
            int32_t best_score = 29;
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                const TeamRow& row = view.rows()[t];
                if (t == seat_ || !row.present || row.dropped || row.ally == seat_ || row.score <= best_score) continue;
                const auto b = black_.find(t);
                if (b != black_.end() && b->second > now_) continue;
                const HillInfo& hill = map.hill(t);
                if (!hill.present || east_state(grid, hill).shut()) continue;
                bool reach = false;
                for (const sim::TileCoord& e : east_tiles(hill)) reach = reach || reaches(map, a.tile, e);
                if (!reach) continue;
                target = t;
                best_score = row.score;
            }
            if (target < 0) {
                if (owner == kRaid) ledger_.release(a.id, kRaid);                        // nothing to raid now: it harvests
                continue;
            }
            const sim::TileCoord click = raid_click(view, map.hill(static_cast<uint8_t>(target)));
            if (click.x < 0 || !ledger_.take(a.id, kRaid)) continue;                     // (the whole mound is covered by ants: the next look)
            orders.special(a.id, click);
            raided_[a.id] = now_;
            raid_target_[a.id] = target;
            paid_.erase(a.id);
        }
    }

    // The tile of a victim's mound that a raid is clicked on: its entrance, or when an ant stands on that (or dies on it) another tile of the mound that no ant stands on, because the cursor shows
    // the target cursor only where no ant is under the pointer; (-1, -1) when the whole mound is covered
    static sim::TileCoord raid_click(const BotView& view, const HillInfo& hill) {
        const auto occupied = [&](sim::TileCoord t) {
            for (const AntView& a : view.mine()) {
                if (a.tile == t) return true;
            }
            for (const AntView& a : view.others()) {
                if (a.tile == t) return true;
            }
            return view.dying_at(t);
        };
        for (const sim::TileCoord& t : {hill.entrance, sim::TileCoord{hill.origin.x + 2, hill.origin.y + 1}, sim::TileCoord{hill.origin.x + 1, hill.origin.y + 2},
                                        sim::TileCoord{hill.origin.x + 2, hill.origin.y + 2}, hill.origin, sim::TileCoord{hill.origin.x + 3, hill.origin.y + 3}}) {
            if (!occupied(t)) return t;
        }
        return sim::TileCoord{-1, -1};
    }

    // ---- hurt ants go home ---------------------------------------------------------------------------------------------------------------------------------------------
    void send_home(const BotView& view, Orders& orders, const MapInfo& map, const AntView& a) {
        if (!view.hill_enterable() || !map.hill(seat_).present) {                        // (a closed hill heals nobody: the ant goes back to the economy)
            ledger_.release(a.id, ledger_.owner(a.id));
            return;
        }
        if (!ledger_.take(a.id, kHeal)) return;
        orders.move({a.id}, map.hill(seat_).entrance, Priority::Urgent);
        healing_[a.id] = now_;
        homed_[a.id] = now_;
    }
    void heal(const BotView& view, Orders& orders, const MapInfo& map) {
        for (auto it = healing_.begin(); it != healing_.end();) {
            const AntView* a = find(view.mine(), it->first);
            if (a == nullptr || a->hp >= kRejoinHp || now_ >= it->second + kHealTicks) {
                if (a != nullptr) ledger_.release(a->id, kHeal);
                it = healing_.erase(it);
                continue;
            }
            if (a->idle() && now_ >= homed_[a->id] + 60) {                               // idle and still hurt: the walk home did not happen
                orders.move({a->id}, map.hill(seat_).entrance, Priority::Urgent);
                homed_[a->id] = now_;
            }
            ++it;
        }
    }

    // ---- 4. the hunter -------------------------------------------------------------------------------------------------------------------------------------------------
    void hunter(const BotView& view, Orders& orders, const MapInfo& map) {
        const AntView* h = hunter_ != 0 ? find(view.mine(), hunter_) : nullptr;
        if (h != nullptr && ledger_.owner(h->id) != kHunter) h = nullptr;
        if (h == nullptr) hunter_ = 0;
        if (h != nullptr && h->hp <= kHomeHp) {
            send_home(view, orders, map, *h);
            hunter_ = 0;
            return;
        }
        if (victim_ < 0 || now_ < kGangFromTick) return;
        if (h == nullptr) {                                                               // the first Combat Ant, else the first Fire Ant, that is healthy and has no job
            for (const sim::AntType want : {sim::AntType::Combat, sim::AntType::Fire}) {
                for (const AntView& a : view.mine()) {
                    const TaskId owner = ledger_.owner(a.id);
                    if (a.type != want || a.hp < kRejoinHp || carries(a) || !a.takes_orders() || (owner != kNoTask && owner != StandardBot::kHarvest)) continue;
                    if (view.powerup_at(a.tile) != nullptr || !ledger_.take(a.id, kHunter)) continue;
                    h = &a;
                    hunter_ = a.id;
                    break;
                }
                if (h != nullptr) break;
            }
            if (h == nullptr) return;
        }
        const AntView* near = nullptr;                                                    // an enemy within three tiles that is not a Combat Ant
        int32_t near_d = 0;
        for (const AntView& e : view.others()) {
            if (!enemy_of(view, e) || e.type == sim::AntType::Combat || !attackable(view, e)) continue;
            const int32_t d = e.tile.chebyshev_dist(h->tile);
            if (d > kHunterReach || (near != nullptr && (d > near_d || (d == near_d && e.hp >= near->hp)))) continue;
            near = &e;
            near_d = d;
        }
        const auto last = ordered_.find(h->id);
        if (!h->idle() || (last != ordered_.end() && now_ < last->second + kBlowGap)) return;
        if (near != nullptr) {
            orders.attack({h->id}, near->tile, Priority::Urgent);
            ordered_[h->id] = now_;
            return;
        }
        const PileView* pile = victim_pile(view, map);                                    // its post: four tiles from the victim's entrance, on the way to its nearest pile
        const sim::TileCoord door = map.hill(static_cast<uint8_t>(victim_)).entrance;
        sim::TileCoord want = door;
        if (pile != nullptr) {
            const int32_t dx = pile->anchor.x - door.x;
            const int32_t dy = pile->anchor.y - door.y;
            const int32_t m = std::max(std::abs(dx), std::abs(dy));
            if (m > 0) want = sim::TileCoord{door.x + dx * kHunterPost / m, door.y + dy * kHunterPost / m};
        }
        sim::TileCoord post;
        if (free_tile_near(view, want, 2, post) && h->tile.chebyshev_dist(post) > 1 && reaches(map, h->tile, post)) {
            orders.move({h->id}, post, Priority::Normal);
            ordered_[h->id] = now_ + 48;                                                  // (not again for a while: the walk is long)
        }
    }

    // ---- 3. the gang ---------------------------------------------------------------------------------------------------------------------------------------------------
    void gang(const BotView& view, Orders& orders, const MapInfo& map) {
        std::vector<const AntView*> members;
        for (const uint32_t id : ledger_.ants_of(kGang)) {
            const AntView* a = find(view.mine(), id);
            if (a == nullptr) continue;
            if (a->hp <= kHomeHp) send_home(view, orders, map, *a);
            else members.push_back(a);
        }
        if (now_ >= kGangFromTick && victim_ >= 0 && members.size() < gang_size_) recruit(view, map, members);
        if (members.empty()) {
            target_ = 0;
            return;
        }
        int32_t sx = 0;
        int32_t sy = 0;
        for (const AntView* a : members) {
            sx += a->tile.x;
            sy += a->tile.y;
        }
        const sim::TileCoord centre{sx / static_cast<int32_t>(members.size()), sy / static_cast<int32_t>(members.size())};
        // the target stays until it is dead or more than ten tiles from the centre
        const AntView* target = target_ != 0 ? find(view.others(), target_) : nullptr;
        if (target != nullptr && (!enemy_of(view, *target) || target->tile.chebyshev_dist(centre) > kTargetLeash || target_since_ + 400 < now_)) target = nullptr;
        if (target == nullptr) {
            for (const AntView& e : view.others()) {                                      // the next one: Thieves, Fire Ants and Bombers, then the fewest hit points, then the nearest
                if (!enemy_of(view, e) || carries(e) || !attackable(view, e) || e.tile.chebyshev_dist(centre) > kTargetReach || !reaches(map, members.front()->tile, e.tile)) continue;
                const auto skip = skipped_.find(e.id);
                if (skip != skipped_.end() && skip->second > now_) continue;
                if (target == nullptr || prefers(e, *target, centre)) target = &e;
            }
            target_ = target != nullptr ? target->id : 0;
            target_since_ = now_;
        }
        if (target != nullptr) {
            if (!attackable(view, *target)) {                                             // in its hit clip, or on a power-up or a hill: not now (and not for ever)
                if (target->state != sim::UnitState::Flinch && target->state != sim::UnitState::Knockback && now_ >= target_since_ + 60) skipped_[target->id] = now_ + 300;
                return;
            }
            std::vector<uint32_t> strikers;
            for (const AntView* a : members) {
                const auto last = ordered_.find(a->id);
                if (a->idle() && (last == ordered_.end() || now_ >= last->second + kBlowGap)) strikers.push_back(a->id);
            }
            if (strikers.empty()) return;
            orders.attack(strikers, target->tile, Priority::Urgent);
            for (const uint32_t id : strikers) ordered_[id] = now_;
            return;
        }
        // no target in reach: to the pile that is nearest the victim's hill, and wait there
        const PileView* pile = victim_pile(view, map);
        sim::TileCoord stand;
        if (pile == nullptr || !free_tile_near(view, rally_point(map, *pile), 3, stand)) return;
        std::vector<uint32_t> walkers;
        for (const AntView* a : members) {
            const auto last = ordered_.find(a->id);
            if (a->idle() && a->tile.chebyshev_dist(stand) > 3 && reaches(map, a->tile, stand) && (last == ordered_.end() || now_ >= last->second + 40)) walkers.push_back(a->id);
        }
        if (walkers.empty()) return;
        orders.move(walkers, stand, Priority::Normal);
        for (const uint32_t id : walkers) ordered_[id] = now_;
    }
    // the point three tiles from the pile on the side of the victim's hill
    sim::TileCoord rally_point(const MapInfo& map, const PileView& pile) const {
        const sim::TileCoord door = map.hill(static_cast<uint8_t>(victim_)).entrance;
        const int32_t dx = door.x - pile.anchor.x;
        const int32_t dy = door.y - pile.anchor.y;
        const int32_t m = std::max(std::abs(dx), std::abs(dy));
        return m == 0 ? pile.anchor : sim::TileCoord{pile.anchor.x + dx * 3 / m, pile.anchor.y + dy * 3 / m};
    }
    // whether enemy `a` is a better target than `b`
    static bool prefers(const AntView& a, const AntView& b, sim::TileCoord centre) {
        const auto wanted = [](const AntView& e) { return e.type == sim::AntType::Thief || e.type == sim::AntType::Fire || e.type == sim::AntType::Bomber; };
        if (wanted(a) != wanted(b)) return wanted(a);
        if (a.hp != b.hp) return a.hp < b.hp;
        return a.tile.chebyshev_dist(centre) < b.tile.chebyshev_dist(centre);
    }
    // the free ants that fill the gang: nearest to the gang (or to the victim's hill, with no gang yet)
    void recruit(const BotView& view, const MapInfo& map, std::vector<const AntView*>& members) {
        sim::TileCoord from = map.hill(static_cast<uint8_t>(victim_)).entrance;
        if (!members.empty()) from = members.front()->tile;
        std::vector<const AntView*> pool;
        for (const AntView& a : view.mine()) {
            const TaskId owner = ledger_.owner(a.id);
            if (a.type == sim::AntType::Thief || a.id == hunter_ || carries(a) || a.hp < kRejoinHp || !a.takes_orders() || a.state == sim::UnitState::Stunned) continue;
            if ((owner != kNoTask && owner != StandardBot::kHarvest) || view.powerup_at(a.tile) != nullptr) continue;
            pool.push_back(&a);
        }
        std::stable_sort(pool.begin(), pool.end(), [&](const AntView* x, const AntView* y) { return x->tile.chebyshev_dist(from) < y->tile.chebyshev_dist(from); });
        for (const AntView* a : pool) {
            if (members.size() >= gang_size_) break;
            if (ledger_.take(a->id, kGang)) members.push_back(a);
        }
    }

    size_t gang_size_;
    Tactics tactics_;
    HarvestTask harvest_;
    PowerUpTask powerups_;
    AntLedger ledger_;
    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    uint64_t now_{0};
    uint64_t deny_after_{0};
    int victim_{-1};
    uint32_t hunter_{0};
    uint32_t target_{0};
    uint64_t target_since_{0};
    std::map<uint32_t, uint64_t> raided_;                // Thief -> the tick of its last raid order
    std::map<uint32_t, int> raid_target_;                // Thief -> the team of its last raid order
    std::set<uint32_t> paid_;                            // Thieves that took loot after their last raid order
    std::map<uint8_t, uint64_t> black_;                  // a team whose hill could not be raided -> until when it is left alone
    std::map<uint32_t, uint64_t> healing_;               // hurt ants on their way home -> since when
    std::map<uint32_t, uint64_t> homed_;                 // ... the tick of the last order home
    std::map<uint32_t, uint64_t> ordered_;               // ant -> the tick of the last blow or move that it was ordered
    std::map<uint32_t, uint64_t> skipped_;               // enemy ant -> until when it is not a target (no order reaches it)
};

}  // namespace ants::ai::bench
