#include "ants_ai/brawl_task.hpp"

#include <algorithm>

#include "ants_ai/tactics.hpp"

namespace ants::ai {

namespace {

constexpr int32_t kNoTile = -1;
constexpr uint64_t kCantGoWindow = 24;     // an attack that shows "can't go" within this many ticks of leaving was refused by the path finder
constexpr uint64_t kShunTicks = 300;       // ... twice within kRefusalWindow ticks, and the pair is left alone this long
constexpr uint64_t kRefusalWindow = 160;
constexpr uint64_t kAttackTicks = 600;     // an ant on its way to a victim is given this long before it is sent to another

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

bool ally_of(const BotView& v, uint8_t team) noexcept { return v.ally() < sim::MAX_PLAYERS && team == v.ally(); }

}  // namespace

bool BrawlTask::pending(const Job& job, const Profile& profile, uint64_t now) const noexcept {
    if (job.kind == Kind::None || job.sent != kPending) return false;
    const uint64_t longest = profile.reaction_delay + profile.reaction_delay * profile.jitter_percent / 100u + profile.intent_ttl;
    return now <= job.decided + longest + 2u;
}

bool BrawlTask::shunned(uint32_t ant, uint32_t target, uint64_t now) const noexcept {
    const auto it = shun_.find({ant, target});
    return it != shun_.end() && now < it->second;
}

void BrawlTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    for (const uint32_t ant : command.ants) {
        const auto it = jobs_.find(ant);
        if (it == jobs_.end() || it->second.kind == Kind::None) continue;
        Job& job = it->second;
        if (job.sent != kPending) continue;
        if (command.tile_x != job.tile.x || command.tile_y != job.tile.y) continue;       // not this job's order
        if (fate == Bot::Fate::Sent) job.sent = tick;
        else job.kind = Kind::None;                                                        // it never left: the ant is asked again at the next look
    }
}

void BrawlTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const HillInfo& home = c.map.hill(c.seat);
    // the analysis of the map is made for plain walkers: on a level whose ants swim or walk through fire it says nothing about who can meet whom
    const bool walkers = v.default_ant_type() != sim::AntType::Swimmer && v.default_ant_type() != sim::AntType::Fire;

    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (find_ant(v.mine(), it->first) == nullptr) it = jobs_.erase(it);
        else ++it;
    }

    // an attack that left and was followed by "can't go" within a few ticks was refused: either no walk joins the two ants, or the victim was in a blow of another ant (engaged, in its hit clip), which
    // passes. A pair that is refused twice in a short time is left alone for a while
    for (const AntView& a : v.mine()) {
        const auto it = jobs_.find(a.id);
        if (it == jobs_.end()) continue;
        Job& job = it->second;
        if (job.kind != Kind::Attack || job.sent == kPending || a.state != sim::UnitState::CantGo || now > job.sent + kCantGoWindow || job.refused) continue;
        job.refused = true;
        const AntView* t = find_ant(v.others(), job.target);
        if (t != nullptr && (t->state == sim::UnitState::Flinch || t->state == sim::UnitState::Knockback || t->state == sim::UnitState::Attacking)) continue;
        Refusal& r = refusals_[{a.id, job.target}];
        r.count = now > r.last + kRefusalWindow ? 1u : r.count + 1u;
        r.last = now;
        if (r.count >= 2) shun_[{a.id, job.target}] = now + kShunTicks;
    }
    for (auto it = shun_.begin(); it != shun_.end();) {
        if (now >= it->second) it = shun_.erase(it);
        else ++it;
    }
    for (auto it = refusals_.begin(); it != refusals_.end();) {
        if (now > it->second.last + kRefusalWindow) it = refusals_.erase(it);
        else ++it;
    }

    // ---- the enemy ants that a blow can reach (one that was struck stands again after its hit clip)
    std::vector<const AntView*> foes;
    for (const AntView& e : v.others()) {
        if (ally_of(v, e.team)) continue;
        AntView probe = e;
        if (e.state == sim::UnitState::Flinch || e.state == sim::UnitState::Knockback) probe.state = sim::UnitState::Idle;
        if (attackable(v, probe)) foes.push_back(&e);
    }
    const auto foe_by_id = [&](uint32_t id) -> const AntView* {
        const AntView* e = find_ant(v.others(), id);
        return e != nullptr && std::binary_search(foes.begin(), foes.end(), e, [](const AntView* x, const AntView* y) { return x->id < y->id; }) ? e : nullptr;
    };

    // ---- my ants: the wounded go home; those that strike a victim stay in its pack; the rest are ready
    std::map<uint32_t, std::vector<uint32_t>> pack;                        // victim -> the ants that strike it
    std::vector<const AntView*> ready;
    std::vector<uint32_t> going_home;
    for (const AntView& a : v.mine()) {
        Job& job = jobs_[a.id];
        if (a.hp <= params_.retreat_hp) {
            if (!a.takes_orders()) continue;
            if (job.kind != Kind::Home || (!pending(job, c.profile, now) && a.idle() && a.tile.chebyshev_dist(home.entrance) > 1)) going_home.push_back(a.id);
            continue;
        }
        if (job.kind == Kind::Home && a.hp < params_.return_hp && !a.idle()) continue;
        if (job.kind == Kind::Attack) {
            if (foe_by_id(job.target) != nullptr && now < job.decided + kAttackTicks && !shunned(a.id, job.target, now)) {
                pack[job.target].push_back(a.id);
                continue;
            }
            job.kind = Kind::None;
        }
        if (!a.takes_orders()) continue;
        if (job.kind == Kind::March && (a.state == sim::UnitState::Walking || pending(job, c.profile, now))) continue;
        ready.push_back(&a);
    }
    if (!going_home.empty() && home.present) {
        for (const uint32_t id : going_home) {
            Job& job = jobs_[id];
            job = Job{Kind::Home, 0, home.entrance, now, kPending, false};
            ++retreats_ordered_;
        }
        c.orders.move(going_home, home.entrance, Priority::Urgent);
    }

    // ---- the victims, the wounded and the near first
    struct Foe {
        const AntView* ant;
        int64_t score;
    };
    std::vector<Foe> order;
    std::map<uint32_t, int32_t> comp_of;                                   // own ant -> its walker component
    if (walkers) {
        for (const AntView* a : ready) comp_of[a->id] = c.map.ant_component(c.seat, a->tile);
    }
    const auto meets = [&](const AntView& a, const AntView& f) {           // whether a walk can join the ant to a tile beside the victim (the components of the start: a hint, the engine decides)
        if (!walkers) return true;
        const int32_t mine = comp_of[a.id];
        if (mine < 0) return false;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (c.map.component(c.seat, sim::TileCoord{f.tile.x + dx, f.tile.y + dy}) == mine) return true;
            }
        }
        return false;
    };
    for (const AntView* f : foes) {
        int32_t nearest = 1 << 20;
        for (const AntView* a : ready) {
            if (shunned(a->id, f->id, now) || !meets(*a, *f)) continue;
            nearest = std::min(nearest, a->tile.chebyshev_dist(f->tile));
        }
        if (pack.find(f->id) != pack.end()) nearest = 0;
        if (nearest > params_.reach_tiles) continue;
        // an enemy on the move is not caught by an order that names the tile it stands on: it is only struck when it is about to pass an ant
        if (f->state == sim::UnitState::Walking && nearest > 2) continue;
        order.push_back(Foe{f, -static_cast<int64_t>(nearest) * 10 - static_cast<int64_t>(f->hp) * static_cast<int64_t>(params_.hp_weight)});
    }
    std::stable_sort(order.begin(), order.end(), [](const Foe& x, const Foe& y) { return x.score != y.score ? x.score > y.score : x.ant->id < y.ant->id; });

    std::set<uint32_t> fresh;                                              // ants given a victim at this look (their pack is ordered at once)
    const auto assign = [&](const AntView& a, const AntView& f) {
        pack[f.id].push_back(a.id);
        jobs_[a.id] = Job{Kind::Attack, f.id, f.tile, now, kPending, false};
        fresh.insert(a.id);
    };
    const auto load_of = [&](uint32_t victim) {
        const auto p = pack.find(victim);
        return static_cast<uint32_t>(p == pack.end() ? 0u : p->second.size());
    };

    // the packs: the victims in order, each with the nearest ready ants up to its cap
    for (const Foe& fo : order) {
        while (load_of(fo.ant->id) < params_.per_target) {
            const AntView* pick = nullptr;
            int32_t pick_d = 1 << 20;
            for (const AntView* a : ready) {
                if (fresh.count(a->id) != 0 || shunned(a->id, fo.ant->id, now) || !meets(*a, *fo.ant)) continue;
                const int32_t d = a->tile.chebyshev_dist(fo.ant->tile);
                if (d <= params_.reach_tiles && d < pick_d) {
                    pick = a;
                    pick_d = d;
                }
            }
            if (pick == nullptr) break;
            assign(*pick, *fo.ant);
        }
    }
    // the ready ants that found no place support the nearest fight (another blow on the same victim finishes it, and the ants round it shut its way home)
    std::vector<uint32_t> idle_left;
    for (const AntView* a : ready) {
        if (fresh.count(a->id) != 0) continue;
        const Foe* near = nullptr;
        int32_t near_d = 1 << 20;
        for (const Foe& fo : order) {
            if (shunned(a->id, fo.ant->id, now) || !meets(*a, *fo.ant)) continue;
            const int32_t d = a->tile.chebyshev_dist(fo.ant->tile);
            if (d < near_d && load_of(fo.ant->id) < params_.crowd) {
                near_d = d;
                near = &fo;
            }
        }
        if (near != nullptr) assign(*a, *near->ant);
        else idle_left.push_back(a->id);
    }

    // ---- the packs are ordered onto their victims: at once when an ant joined, and again when the victim was thrown to another tile (the orders of the pack name the tile it stood on) or an ant of the
    // pack stands idle after its blow
    for (auto& entry : pack) {
        const AntView* f = foe_by_id(entry.first);
        if (f == nullptr) continue;
        Fight& fight = fights_[entry.first];
        bool joined = false;
        bool idle_member = false;
        bool close = false;                                                // an enemy on the move is only chased by ants within two tiles of it: with a lead it is not caught
        for (const uint32_t id : entry.second) {
            joined = joined || fresh.count(id) != 0;
            const AntView* m = find_ant(v.mine(), id);
            if (m == nullptr) continue;
            idle_member = idle_member || (m->idle() && !pending(jobs_[id], c.profile, now));
            close = close || m->tile.chebyshev_dist(f->tile) <= 2;
        }
        const bool moved = f->tile != fight.tile;
        const bool due = now >= fight.ordered + params_.regap_ticks;
        const bool settled = f->state != sim::UnitState::Walking || (f->hp <= 1 && close);
        if (!joined && !(due && settled && (moved || idle_member))) continue;
        std::vector<uint32_t> ants;
        for (const uint32_t id : entry.second) {
            const AntView* m = find_ant(v.mine(), id);
            if (m != nullptr && m->takes_orders() && m->state != sim::UnitState::Stunned) ants.push_back(id);
        }
        if (ants.empty()) continue;
        for (const uint32_t id : ants) {
            Job& job = jobs_[id];
            const uint64_t decided = job.kind == Kind::Attack && job.target == entry.first ? job.decided : now;
            job = Job{Kind::Attack, entry.first, f->tile, decided, kPending, false};
        }
        c.orders.attack(ants, f->tile, Priority::Urgent);
        fight.ordered = now;
        fight.tile = f->tile;
        ++attacks_ordered_;
    }
    for (auto it = fights_.begin(); it != fights_.end();) {
        if (pack.find(it->first) == pack.end()) it = fights_.erase(it);
        else ++it;
    }

    // ---- nobody to strike: march to the nearest enemy hill that a walk reaches and wait at its door
    if (!idle_left.empty() && foes.empty()) {
        sim::TileCoord dest{kNoTile, kNoTile};
        int32_t best = 1 << 20;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = v.rows()[t];
            if (t == c.seat || !row.present || row.dropped || ally_of(v, t)) continue;
            const HillInfo& h = c.map.hill(t);
            if (!h.present || !home.present || (walkers && c.map.hill_component(t) != c.map.hill_component(c.seat))) continue;     // (a hill that no walk joins to the own is no goal)
            const int32_t d = home.origin.chebyshev_dist(h.origin);
            if (d < best) {
                best = d;
                dest = h.tile42;
            }
        }
        if (dest.x != kNoTile) {
            std::vector<uint32_t> go;
            for (const uint32_t id : idle_left) {
                Job& job = jobs_[id];
                const AntView* a = find_ant(v.mine(), id);
                if (a == nullptr || a->tile.chebyshev_dist(dest) <= 2) continue;
                if (job.kind == Kind::March && job.sent != kPending && now < job.sent + params_.march_gap_ticks) continue;
                job = Job{Kind::March, 0, dest, now, kPending, false};
                go.push_back(id);
                ++marches_ordered_;
            }
            if (!go.empty()) c.orders.move(go, dest, Priority::Normal);
        }
    }
}

}  // namespace ants::ai
