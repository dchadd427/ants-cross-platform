// The "people" numbers of bot_watch: see tools/bot_watch_people.hpp and docs/BOT_PEOPLE.md.
#include "bot_watch_people.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <set>
#include <sstream>

namespace ants::watch {

namespace {

constexpr int32_t kStrikerRadius = 2;              // an enemy ant in its strike clip this near a hit ant struck the blow
constexpr uint64_t kFightGapTicks = 300;           // the blows of one fight are at most this many ticks apart ...
constexpr int32_t kFightGapTiles = 10;             // ... and at most this many tiles
constexpr uint64_t kSamplePeriod = 20;             // the walking speed and the crowd at a fight are sampled every this many ticks
constexpr int32_t kCrowdRadius = 4;                // the ants at a fight are the ants within this many tiles of its last blow
constexpr uint16_t kHurtHp = 4;                    // an ant with this many hit points or fewer is hurt: it should be walking home
constexpr uint64_t kWarningTicks = 1200;           // a death looks back at most this many ticks for the first look at the ant hurt
constexpr int32_t kHomeRadius = 10;                // a bomb within this many tiles of its owner's hill entrance is a home mine
constexpr uint64_t kMineEnds[3] = {3000, 6000, 12000};
constexpr int32_t kGateRadius = 4;                 // a fire wall within this many tiles of an enemy hill's entrance is at its gate
constexpr int32_t kBlastRadius = 1;                // a bomb that is gone hit an enemy ant that lost hit points within this many tiles of it ...
constexpr uint64_t kBlastTicks = 2;                // ... in the same tick or in the next two
constexpr uint64_t kFocusGapTicks = 200;           // two blows of one seat less than this many ticks apart are a pair
constexpr uint32_t kMaxAntId = 1u << 20;

int32_t cheb(sim::TileCoord a, sim::TileCoord b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

uint64_t place_key(sim::TileCoord t, uint8_t owner) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(t.y + 1)) << 40) | (static_cast<uint64_t>(static_cast<uint32_t>(t.x + 1)) << 8) | owner;
}
sim::TileCoord key_tile(uint64_t key) { return sim::TileCoord{static_cast<int32_t>((key >> 8) & 0xFFFFFFFFu) - 1, static_cast<int32_t>(key >> 40) - 1}; }
uint8_t key_owner(uint64_t key) { return static_cast<uint8_t>(key & 0xFFu); }

// Whether an ant that was hurt at `dist` tiles from its hill's entrance and lived `lived` more ticks could have walked home in them, at `speed` tiles a tick
bool could_walk_home(int32_t dist, uint64_t lived, double speed) { return speed > 0 && static_cast<double>(dist) / speed < static_cast<double>(lived); }

// The value at nearest rank p percent of a sorted list (the list is not empty)
uint32_t percentile(const std::vector<uint32_t>& sorted, uint32_t percent) {
    const size_t rank = (sorted.size() * percent + 99) / 100;
    return sorted[std::max<size_t>(rank, 1) - 1];
}

std::string number(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}
std::string mean_json(uint64_t sum, uint64_t n) { return n == 0 ? "null" : number(static_cast<double>(sum) / static_cast<double>(n)); }
std::string mean_text(uint64_t sum, uint64_t n) { return n == 0 ? "-" : number(static_cast<double>(sum) / static_cast<double>(n)); }
std::string count_json(bool applicable, uint32_t v) { return applicable ? std::to_string(v) : "null"; }

}  // namespace

// ---- the core ------------------------------------------------------------------------------------------------------------------------------------------------

struct People::Impl {
    struct AntState {
        bool seen{false};
        bool dead{false};
        bool was_fire{false};
        bool hurt{false};
        uint16_t hp{0};
        sim::AntType type{sim::AntType::Worker};
        uint64_t hurt_since{0};
        sim::TileCoord hurt_tile{};
        bool sample_valid{false};
        bool sample_walking{false};
        uint64_t sample_tick{0};
        sim::TileCoord sample_tile{};
    };
    struct Fight {
        uint8_t side_a{0};                         // the side of the first victim
        uint8_t side_b{0};
        uint8_t first_victim_seat{0};
        uint32_t first_victim{0};
        uint64_t start{0};
        uint64_t last{0};
        sim::TileCoord last_at{};
        bool open{true};
        std::set<uint32_t> a_strikers;
        std::set<uint32_t> b_strikers;
        bool helped{false};
        uint64_t help_after{0};
        uint32_t a_lost{0};
        uint32_t b_lost{0};
        uint32_t local_a{0};
        uint32_t local_b{0};
    };
    struct Death {
        uint8_t seat{0};
        bool warned{false};
        uint64_t lived{0};                         // the ticks from the first look at 4 hit points or fewer (at most 1,200 back) to the death
        int32_t dist{INT_MAX};                     // the tiles from where the ant stood then to its hill's entrance
    };
    struct Last {
        bool has{false};
        uint64_t tick{0};
        uint32_t victim{0};
    };
    struct Pending {                               // a bomb that is gone: the enemy ant that it hit is looked for in the next ticks
        sim::TileCoord tile{};
        uint8_t owner{0};
        uint64_t until{0};
    };

    Setup setup;
    std::array<uint8_t, kPeopleSeats> ally{sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
    uint64_t now{0};
    bool finished{false};
    std::vector<AntState> ants;
    std::vector<Fight> fights;
    std::vector<Death> deaths;
    std::vector<Blow> blows;
    std::array<SeatPeople, kPeopleSeats> seats{};
    std::array<Last, kPeopleSeats> last_blow{};
    std::vector<uint64_t> prev_bombs;
    std::vector<uint64_t> prev_fires;
    std::vector<Pending> pending;
    uint64_t speed_sum{0};
    uint64_t speed_count{0};

    uint8_t side_of(uint8_t seat) const { return seat < kPeopleSeats && ally[seat] < kPeopleSeats ? std::min(seat, ally[seat]) : seat; }
    bool enemies(uint8_t a, uint8_t b) const { return side_of(a) != side_of(b); }

    void on_blow(const Blow& b);
    void sample(const Snapshot& s);
    void places(const Snapshot& s, const std::vector<const AntSnap*>& fell);
};

People::People(const Setup& setup) : impl_(std::make_unique<Impl>()) { impl_->setup = setup; }
People::~People() = default;
const std::vector<Blow>& People::blows() const noexcept { return impl_->blows; }
double People::walking_speed() const noexcept {
    return impl_->speed_count == 0 ? 0.0 : static_cast<double>(impl_->speed_sum) / static_cast<double>(kSamplePeriod * impl_->speed_count);
}

void People::finish() { impl_->finished = true; }

void People::tick(const Snapshot& s) {
    Impl& m = *impl_;
    if (m.finished) return;
    m.now = s.tick;
    m.ally = s.ally;
    for (Impl::Fight& f : m.fights) f.open = f.open && m.now - f.last <= kFightGapTicks;
    // 1. what became of every ant since the last snapshot: hit points that fell or rose, a new Fire Ant, a death
    std::vector<const AntSnap*> fell;
    for (const AntSnap& a : s.ants) {
        if (a.id > kMaxAntId || a.seat >= kPeopleSeats) continue;
        if (a.id >= m.ants.size()) m.ants.resize(static_cast<size_t>(a.id) + 64);
        Impl::AntState& st = m.ants[a.id];
        SeatPeople& mine = m.seats[a.seat];
        if (st.seen) {
            if (a.hp < st.hp) fell.push_back(&a);
            if (a.hp > st.hp && a.hp > 0) ++mine.heals;
            if (a.type == sim::AntType::Fire && st.type != sim::AntType::Fire) {
                ++mine.fire_ants;
                st.was_fire = true;
            }
        }
        if (a.hp >= 1 && a.hp <= kHurtHp) {
            if (!st.hurt) {
                st.hurt = true;
                st.hurt_since = m.now;
                st.hurt_tile = a.tile;
            }
        } else if (a.hp > kHurtHp) {
            st.hurt = false;
        }
        if (!st.dead && (a.hp == 0 || a.state == sim::UnitState::Dead || a.state == sim::UnitState::Drowning)) {
            st.dead = true;
            ++mine.deaths;
            if (st.was_fire) ++mine.fire_ants_died;
            Impl::Death d;
            d.seat = a.seat;
            d.warned = st.hurt;
            if (st.hurt) {
                d.lived = m.now - std::max<uint64_t>(st.hurt_since, m.now > kWarningTicks ? m.now - kWarningTicks : 0);
                const sim::TileCoord home = m.setup.entrance[a.seat];
                d.dist = home.x < 0 ? INT_MAX : cheb(st.hurt_tile, home);
            }
            m.deaths.push_back(d);
        }
        st.seen = true;
        st.hp = a.hp;
        st.type = a.type;
    }
    std::sort(fell.begin(), fell.end(), [](const AntSnap* x, const AntSnap* y) { return x->id < y->id; });
    // 2. the blows: a hit ant with an enemy ant in its strike clip within two tiles; the nearest such ant struck it
    for (const AntSnap* v : fell) {
        const AntSnap* striker = nullptr;
        int32_t best = INT_MAX;
        for (const AntSnap& e : s.ants) {
            if (e.state != sim::UnitState::Attacking || e.seat >= kPeopleSeats || !m.enemies(e.seat, v->seat) || e.hp == 0) continue;
            const int32_t d = cheb(e.tile, v->tile);
            if (d > kStrikerRadius || d > best || (d == best && striker != nullptr && striker->id < e.id)) continue;
            striker = &e;
            best = d;
        }
        if (striker == nullptr) continue;
        Blow b;
        b.tick = m.now;
        b.victim = v->id;
        b.victim_seat = v->seat;
        b.striker = striker->id;
        b.striker_seat = striker->seat;
        b.at = v->tile;
        b.killed = v->hp == 0;
        m.blows.push_back(b);
        m.on_blow(b);
    }
    m.places(s, fell);
    if (m.now % kSamplePeriod == 0) m.sample(s);
}

void People::Impl::on_blow(const Blow& b) {
    SeatPeople& striker = seats[b.striker_seat];
    ++striker.blows_struck;
    Last& last = last_blow[b.striker_seat];
    if (last.has && b.tick - last.tick < kFocusGapTicks) {
        ++striker.focus_pairs;
        if (b.victim == last.victim) ++striker.focus_same;
    }
    last = Last{true, b.tick, b.victim};
    // the fight: the open one of the two sides whose last blow is near enough in time and in tiles (the nearest), else a new one
    const uint8_t victim_side = side_of(b.victim_seat);
    const uint8_t striker_side = side_of(b.striker_seat);
    Fight* fight = nullptr;
    int32_t nearest = INT_MAX;
    for (Fight& f : fights) {
        const bool same_sides = (f.side_a == victim_side && f.side_b == striker_side) || (f.side_a == striker_side && f.side_b == victim_side);
        if (!f.open || !same_sides || b.tick - f.last > kFightGapTicks) continue;
        const int32_t d = cheb(b.at, f.last_at);
        if (d > kFightGapTiles || (fight != nullptr && (d > nearest || (d == nearest && f.last < fight->last)))) continue;
        fight = &f;
        nearest = d;
    }
    if (fight == nullptr) {
        fights.emplace_back();
        fight = &fights.back();
        fight->side_a = victim_side;
        fight->side_b = striker_side;
        fight->first_victim_seat = b.victim_seat;
        fight->first_victim = b.victim;
        fight->start = b.tick;
    }
    fight->last = b.tick;
    fight->last_at = b.at;
    const bool by_a = striker_side == fight->side_a;
    (by_a ? fight->a_strikers : fight->b_strikers).insert(b.striker);
    if (by_a && b.striker != fight->first_victim && !fight->helped) {
        fight->helped = true;
        fight->help_after = b.tick - fight->start;
    }
    if (b.killed) ++(victim_side == fight->side_a ? fight->a_lost : fight->b_lost);
}

// The fire walls and bombs that are new, the bombs that are gone, and what the gone ones hit
void People::Impl::places(const Snapshot& s, const std::vector<const AntSnap*>& fell) {
    std::vector<uint64_t> bombs_now;
    std::vector<uint64_t> fires_now;
    for (const PlaceSnap& b : s.bombs) bombs_now.push_back(place_key(b.tile, b.owner));
    for (const PlaceSnap& f : s.fires) fires_now.push_back(place_key(f.tile, f.owner));
    std::sort(bombs_now.begin(), bombs_now.end());
    std::sort(fires_now.begin(), fires_now.end());
    std::vector<uint64_t> fresh;
    std::set_difference(fires_now.begin(), fires_now.end(), prev_fires.begin(), prev_fires.end(), std::back_inserter(fresh));
    for (const uint64_t key : fresh) {
        const uint8_t owner = key_owner(key);
        if (owner >= kPeopleSeats) continue;
        for (uint8_t h = 0; h < kPeopleSeats; ++h) {
            if (!setup.present[h] || setup.entrance[h].x < 0 || !enemies(owner, h) || cheb(key_tile(key), setup.entrance[h]) > kGateRadius) continue;
            ++seats[owner].walls_at_enemy_gates;
            break;
        }
    }
    fresh.clear();
    std::set_difference(bombs_now.begin(), bombs_now.end(), prev_bombs.begin(), prev_bombs.end(), std::back_inserter(fresh));
    for (const uint64_t key : fresh) {
        const uint8_t owner = key_owner(key);
        if (owner >= kPeopleSeats) continue;
        if (setup.entrance[owner].x < 0 || cheb(key_tile(key), setup.entrance[owner]) > kHomeRadius) {
            ++seats[owner].away_mines;
            continue;
        }
        for (size_t k = 0; k < 3; ++k) {
            if (now <= kMineEnds[k]) ++seats[owner].home_mines[k];
        }
    }
    fresh.clear();
    std::set_difference(prev_bombs.begin(), prev_bombs.end(), bombs_now.begin(), bombs_now.end(), std::back_inserter(fresh));
    for (const uint64_t key : fresh) {
        if (key_owner(key) < kPeopleSeats) pending.push_back(Pending{key_tile(key), key_owner(key), now + kBlastTicks});
    }
    prev_bombs = std::move(bombs_now);
    prev_fires = std::move(fires_now);
    for (auto it = pending.begin(); it != pending.end();) {
        bool hit = false;
        for (const AntSnap* v : fell) {
            if (it->until >= now && enemies(it->owner, v->seat) && cheb(it->tile, v->tile) <= kBlastRadius) hit = true;
        }
        if (hit) ++seats[it->owner].mine_hits_enemy;
        if (hit || it->until <= now) it = pending.erase(it);
        else ++it;
    }
}

// Every kSamplePeriod ticks: how fast the ants that walk go, and how many ants stand round the last blow of a fight that is going on
void People::Impl::sample(const Snapshot& s) {
    for (const AntSnap& a : s.ants) {
        if (a.id > kMaxAntId || a.seat >= kPeopleSeats || a.id >= ants.size()) continue;
        AntState& st = ants[a.id];
        const bool walking = a.state == sim::UnitState::Walking;
        if (st.sample_valid && st.sample_walking && walking && st.sample_tick + kSamplePeriod == now) {
            speed_sum += static_cast<uint64_t>(cheb(a.tile, st.sample_tile));
            ++speed_count;
        }
        st.sample_valid = true;
        st.sample_walking = walking;
        st.sample_tick = now;
        st.sample_tile = a.tile;
    }
    for (Fight& f : fights) {
        if (!f.open || now - f.last > kSamplePeriod) continue;
        uint32_t near_a = 0;
        uint32_t near_b = 0;
        for (const AntSnap& a : s.ants) {
            if (a.hp == 0 || a.seat >= kPeopleSeats || cheb(a.tile, f.last_at) > kCrowdRadius) continue;
            const uint8_t side = side_of(a.seat);
            if (side == f.side_a) ++near_a;
            else if (side == f.side_b) ++near_b;
        }
        f.local_a = std::max(f.local_a, near_a);
        f.local_b = std::max(f.local_b, near_b);
    }
}

SeatPeople People::seat(uint8_t seat) const {
    const Impl& m = *impl_;
    SeatPeople out = seat < kPeopleSeats ? m.seats[seat] : SeatPeople{};
    out.matches = 1;
    out.walk_applicable = !m.setup.hill_closed;
    for (const Impl::Fight& f : m.fights) {
        if (f.first_victim_seat != seat) continue;
        ++out.struck_first;
        out.hit_back += f.a_strikers.empty() ? 0u : 1u;
        if (f.helped) {
            ++out.helped;
            out.help_ticks.push_back(static_cast<uint32_t>(f.help_after));
        }
        out.strikers_own += static_cast<uint32_t>(f.a_strikers.size());
        out.strikers_enemy += static_cast<uint32_t>(f.b_strikers.size());
        out.local_own += f.local_a;
        out.local_enemy += f.local_b;
        out.lost_in_fights += f.a_lost;
        out.killed_in_fights += f.b_lost;
    }
    const double speed = walking_speed();
    for (const Impl::Death& d : m.deaths) {
        if (d.seat != seat) continue;
        if (!d.warned) {
            ++out.died_without_warning;
        } else if (out.walk_applicable) {                                            // (with the hill closed nobody could walk home: the two counts are not made)
            if (could_walk_home(d.dist, d.lived, speed)) ++out.could_walk_home;
            else ++out.died_far;
        }
    }
    return out;
}

// ---- the numbers of a seat -----------------------------------------------------------------------------------------------------------------------------------

void SeatPeople::merge(const SeatPeople& o) {
    matches += o.matches;
    struck_first += o.struck_first;
    hit_back += o.hit_back;
    helped += o.helped;
    help_ticks.insert(help_ticks.end(), o.help_ticks.begin(), o.help_ticks.end());
    strikers_own += o.strikers_own;
    strikers_enemy += o.strikers_enemy;
    local_own += o.local_own;
    local_enemy += o.local_enemy;
    lost_in_fights += o.lost_in_fights;
    killed_in_fights += o.killed_in_fights;
    deaths += o.deaths;
    could_walk_home += o.could_walk_home;
    died_far += o.died_far;
    died_without_warning += o.died_without_warning;
    walk_applicable = walk_applicable && o.walk_applicable;
    heals += o.heals;
    fire_ants += o.fire_ants;
    fire_ants_died += o.fire_ants_died;
    walls_at_enemy_gates += o.walls_at_enemy_gates;
    for (size_t k = 0; k < home_mines.size(); ++k) home_mines[k] += o.home_mines[k];
    away_mines += o.away_mines;
    mine_hits_enemy += o.mine_hits_enemy;
    blows_struck += o.blows_struck;
    focus_pairs += o.focus_pairs;
    focus_same += o.focus_same;
}

std::string SeatPeople::json() const {
    std::vector<uint32_t> sorted = help_ticks;
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream o;
    o << "{\"matches\":" << matches << ",\"struck_first\":" << struck_first << ",\"hit_back\":" << hit_back << ",\"helped\":" << helped
      << ",\"help_ticks_median\":" << (sorted.empty() ? std::string("null") : std::to_string(percentile(sorted, 50)))
      << ",\"help_ticks_p90\":" << (sorted.empty() ? std::string("null") : std::to_string(percentile(sorted, 90)))
      << ",\"strikers_own_mean\":" << mean_json(strikers_own, struck_first) << ",\"strikers_enemy_mean\":" << mean_json(strikers_enemy, struck_first)
      << ",\"local_own_max_mean\":" << mean_json(local_own, struck_first) << ",\"local_enemy_max_mean\":" << mean_json(local_enemy, struck_first)
      << ",\"lost_in_fights\":" << lost_in_fights << ",\"killed_in_fights\":" << killed_in_fights << ",\"deaths\":" << deaths
      << ",\"could_walk_home\":" << count_json(walk_applicable, could_walk_home) << ",\"died_far\":" << count_json(walk_applicable, died_far)
      << ",\"died_without_warning\":" << died_without_warning << ",\"heals\":" << heals << ",\"fire_ants\":" << fire_ants << ",\"fire_ants_died\":" << fire_ants_died
      << ",\"walls_at_enemy_gates\":" << walls_at_enemy_gates << ",\"fire_ants_lost_per_wall\":" << number(static_cast<double>(fire_ants_died) / std::max<uint32_t>(1u, walls_at_enemy_gates))
      << ",\"home_mines_3000\":" << home_mines[0] << ",\"home_mines_6000\":" << home_mines[1] << ",\"home_mines_12000\":" << home_mines[2] << ",\"away_mines\":" << away_mines
      << ",\"mine_hits_enemy\":" << mine_hits_enemy << ",\"blows_struck\":" << blows_struck << ",\"focus_pct\":" << (focus_pairs == 0 ? std::string("null") : number(100.0 * focus_same / focus_pairs)) << "}";
    return o.str();
}

std::string SeatPeople::line() const {
    std::vector<uint32_t> sorted = help_ticks;
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream o;
    o << "struck first " << struck_first << ", hit back " << hit_back << " (" << (struck_first == 0 ? std::string("-") : std::to_string(100u * hit_back / struck_first) + "%") << "), helped " << helped;
    if (!sorted.empty()) o << " (median " << percentile(sorted, 50) << ", p90 " << percentile(sorted, 90) << " ticks)";
    o << "; strikers " << mean_text(strikers_own, struck_first) << " own / " << mean_text(strikers_enemy, struck_first) << " enemy; at the fight " << mean_text(local_own, struck_first) << " own / "
      << mean_text(local_enemy, struck_first) << " enemy; lost " << lost_in_fights << " killed " << killed_in_fights << " | deaths " << deaths;
    if (walk_applicable) o << ": " << could_walk_home << " could walk home, " << died_far << " far, " << died_without_warning << " without warning";
    else o << " (hill closed)";
    o << " | heals " << heals << " | fire ants " << fire_ants << " died " << fire_ants_died << " walls at gates " << walls_at_enemy_gates << " | home mines " << home_mines[0] << "/" << home_mines[1] << "/"
      << home_mines[2] << " away " << away_mines << " hits " << mine_hits_enemy << " | blows " << blows_struck << " focus "
      << (focus_pairs == 0 ? std::string("-") : std::to_string(100u * focus_same / focus_pairs) + "%");
    return o.str();
}

// ---- the observer of an engine -------------------------------------------------------------------------------------------------------------------------------

PeopleObserver::PeopleObserver() = default;
PeopleObserver::~PeopleObserver() = default;

void PeopleObserver::tick(const sim::SimulationEngine& engine) {
    const sim::WorldState& ws = engine.get_world_state();
    const sim::Grid& grid = engine.grid();
    if (people_ == nullptr) {
        Setup setup;
        for (uint8_t seat = 0; seat < kPeopleSeats; ++seat) {
            const assets::AnthillSpawn* hill = grid.find_anthill(seat);
            if (hill == nullptr) continue;
            setup.present[seat] = true;
            setup.entrance[seat] = sim::TileCoord{hill->x + 1, hill->y + 1};
        }
        setup.hill_closed = !engine.own_hill_enterable();
        people_ = std::make_unique<People>(setup);
    }
    snap_.tick = engine.current_tick();
    for (size_t seat = 0; seat < kPeopleSeats; ++seat) snap_.ally[seat] = ws.player_alliances[seat];
    snap_.ants.clear();
    for (const sim::AntSnapshot& a : ws.ants) {
        AntSnap s;
        s.id = a.id;
        s.seat = a.player_id;
        s.type = a.type;
        s.tile = sim::TileCoord{a.tile_x, a.tile_y};
        s.hp = a.hp;
        s.state = a.state;
        s.holding = a.is_holding;
        snap_.ants.push_back(s);
    }
    snap_.bombs.clear();
    snap_.fires.clear();
    for (uint32_t y = 0; y < grid.height(); ++y) {
        for (uint32_t x = 0; x < grid.width(); ++x) {
            const sim::TileCell& cell = grid.get_cell(x, y);
            if (cell.has_bomb()) snap_.bombs.push_back(PlaceSnap{sim::TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}, cell.interactive_owner});
            else if (cell.has_fire()) snap_.fires.push_back(PlaceSnap{sim::TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}, cell.interactive_owner});
        }
    }
    people_->tick(snap_);
}

void PeopleObserver::finish() {
    if (people_ != nullptr) people_->finish();
}

// ---- the sums over matches -----------------------------------------------------------------------------------------------------------------------------------

void PeopleTotals::add(const std::string& label, const SeatPeople& seat) {
    size_t i = 0;
    while (i < labels_.size() && labels_[i] != label) ++i;
    if (i == labels_.size()) {
        labels_.push_back(label);
        sums_.emplace_back();
    }
    sums_[i].merge(seat);
}

void PeopleTotals::merge(const PeopleTotals& other) {
    for (size_t i = 0; i < other.labels_.size(); ++i) add(other.labels_[i], other.sums_[i]);
}

std::vector<std::string> PeopleTotals::lines() const {
    std::vector<std::string> out;
    for (size_t i = 0; i < labels_.size(); ++i) out.push_back("people " + labels_[i] + " (" + std::to_string(sums_[i].matches) + " seats): " + sums_[i].line());
    return out;
}

std::string PeopleTotals::json() const {
    std::string out = "{";
    for (size_t i = 0; i < labels_.size(); ++i) out += (i ? ",\"" : "\"") + labels_[i] + "\":" + sums_[i].json();
    return out + "}";
}

}  // namespace ants::watch
