#include "ants_server/submissions.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_ctl/http.hpp"
#include "ants_db/database.hpp"
#include "ants_net/netgame.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/history_store.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/map_preview.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/secret.hpp"

namespace fs = std::filesystem;

namespace ants::server {

namespace {

using ctl::JsonValue;
namespace core = ants::assets::lvl_core;

constexpr size_t kClassicUploadBytes = 512u * 1024u;               // LevelLimits::for_new_maps(): the file that is read as it is
constexpr size_t kExtendedUploadBytes = 4u * 1024u * 1024u;        // LevelLimits::for_extended_maps()
constexpr size_t kListLimit = 500;                                 // rows of one list of the owner page
constexpr size_t kMaxFindingsKept = 20;
constexpr int kOrphanDays = 30;                     // files in submissions/orphans/ are deleted after this many days
constexpr size_t kMaxFindingChars = 240;           // one finding's message at most (a map full of hills would name every one of them: the answer stays small)
constexpr int64_t kDay = 86400;

const char* const kColumns =
    "id, state, kind, name, final_name, maker, note, owner_message, description, hash, file, findings, renamed, file_kept, width, height, minutes, players, size, created_at, decided_at, decided_by";

Failure make_failure(int status, const char* code, const char* message) {
    Failure f;
    f.status = status;
    f.code = code;
    f.message = message;
    return f;
}

bool valid_token_text(const std::string& token) {
    std::string raw;
    return token.size() == 43 && base64url_decode(token, raw) && raw.size() == 32;
}

bool valid_id_text(const std::string& id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string token_hash(const std::string& token) { return hex_encode(sha256(token)); }

uint32_t version_word(const std::string& bytes) {
    if (bytes.size() < 4) return 0;
    const auto b = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])); };
    return b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
}

Submission read_row(db::Statement& st) {
    Submission s;
    s.id = st.column_text(0);
    s.state = st.column_text(1);
    s.kind = st.column_text(2);
    s.name = st.column_text(3);
    s.final_name = st.column_is_null(4) ? std::string() : st.column_text(4);
    s.maker = st.column_text(5);
    s.note = st.column_text(6);
    s.owner_message = st.column_text(7);
    s.description = st.column_text(8);
    s.hash = st.column_text(9);
    s.file = st.column_text(10);
    s.findings_json = st.column_text(11);
    s.renamed = st.column_int64(12) != 0;
    s.file_kept = st.column_int64(13) != 0;
    s.width = st.column_int64(14);
    s.height = st.column_int64(15);
    s.minutes = st.column_int64(16);
    s.players = st.column_int64(17);
    s.size = st.column_int64(18);
    s.created_at = st.column_int64(19);
    s.decided_at = st.column_is_null(20) ? 0 : st.column_int64(20);
    s.decided_by = st.column_text(21);
    return s;
}

// A message cut to kMaxFindingChars bytes, never inside a UTF-8 sequence, with "..." at the cut
std::string shortened(const std::string& text) {
    if (text.size() <= kMaxFindingChars) return text;
    size_t cut = kMaxFindingChars - 3;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "...";
}

// The warnings of a check, as the owner's page shows them (the notes are not kept)
JsonValue kept_findings(const JsonValue& report) {
    JsonValue out = JsonValue::make_array();
    const JsonValue& findings = report.get("findings");
    for (size_t i = 0; i < findings.size() && out.size() < kMaxFindingsKept; ++i) {
        const JsonValue& f = findings.at(i);
        if (f.get("severity").str() == "note") continue;
        JsonValue o = JsonValue::make_object();
        o.set("severity", JsonValue::make_string(f.get("severity").str()));
        o.set("message", JsonValue::make_string(shortened(f.get("message").str())));
        out.push_back(std::move(o));
    }
    return out;
}

bool parse_big_json(const std::string& text, JsonValue& out) {
    ctl::JsonLimits limits;
    limits.max_bytes = 8u * 1024u * 1024u;
    limits.max_elements = 200000;
    return ctl::parse_json(text, out, nullptr, limits);
}

std::string path_of(const SubmissionConfig& c, const std::string& file) { return (fs::path(c.dir) / "files" / file).string(); }

void remove_quietly(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

}  // namespace

SubmissionStore::SubmissionStore(db::Database& database, SubmissionConfig config, std::function<bool(const std::string& key)> library_has)
    : db_(database), config_(std::move(config)), library_has_(std::move(library_has)) {}

size_t SubmissionStore::max_upload_bytes(const std::string& kind) { return kind == "extended" ? kExtendedUploadBytes : kClassicUploadBytes; }

bool SubmissionStore::open(int64_t now, std::string& why) {
    open_ = false;
    if (!db_.is_open()) {
        why = "the server has no database";
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(config_.dir) / "files", ec);
    if (ec) {
        why = "the folder " + config_.dir + " cannot be made (" + ec.message() + ")";
        return false;
    }
    for (fs::directory_iterator it(fs::path(config_.dir) / "files", ec), end; !ec && it != end; it.increment(ec)) {        // (the leftovers of a crash while a file was written)
        if (it->path().filename().string().compare(0, 5, ".tmp-") == 0) remove_quietly(it->path().string());
    }
    db::Statement st;
    if (!db_.prepare(st, "SELECT value FROM meta WHERE key = 'submission_salt'").ok()) {
        why = "the database has no meta table";
        return false;
    }
    if (st.step() == db::Statement::Step::Row) {
        salt_ = st.column_text(0);
    } else {
        std::string fresh = generate_secret_text();
        if (fresh.empty()) {
            why = "the operating system's random number generator is not available";
            return false;
        }
        db::Statement ins;
        if (!db_.prepare(ins, "INSERT INTO meta (key, value) VALUES ('submission_salt', ?)").ok() || !ins.bind_text(1, fresh).ok() || ins.step() != db::Statement::Step::Done) {
            why = "the database cannot be written";
            return false;
        }
        salt_ = fresh;
    }
    open_ = true;
    remove_orphans();
    approved_changed_ = true;
    maintenance(now);
    return true;
}

std::string SubmissionStore::address_key(const std::string& address, int64_t day) const {
    return hex_encode(hmac_sha256(salt_, address + "|" + std::to_string(day))).substr(0, 32);
}

bool SubmissionStore::database_failed(Failure& why, const char* where) {
    notices_.push_back(std::string("submissions: the database refused ") + where);
    why = make_failure(503, "storage_error", "The server could not save this just now. Please try again later.");
    return false;
}

int64_t SubmissionStore::scalar(const std::string& sql, const std::vector<std::string>& args) {
    db::Statement st;
    if (!db_.prepare(st, sql).ok()) return -1;
    for (size_t i = 0; i < args.size(); ++i) {
        if (!st.bind_text(static_cast<int>(i) + 1, args[i]).ok()) return -1;
    }
    return st.step() == db::Statement::Step::Row ? st.column_int64(0) : -1;
}

bool SubmissionStore::fetch_one(const std::string& where, const std::string& value, Submission& out) {
    db::Statement st;
    if (!db_.prepare(st, std::string("SELECT ") + kColumns + " FROM map_submissions WHERE " + where + " = ?").ok() || !st.bind_text(1, value).ok()) return false;
    if (st.step() != db::Statement::Step::Row) return false;
    out = read_row(st);
    return true;
}

bool SubmissionStore::fetch_many(const std::string& where, const std::string& value, std::vector<Submission>& out) {
    db::Statement st;
    out.clear();
    if (!db_.prepare(st, std::string("SELECT ") + kColumns + " FROM map_submissions WHERE " + where + " = ? ORDER BY created_at DESC, id LIMIT " + std::to_string(kListLimit)).ok() || !st.bind_text(1, value).ok()) return false;
    for (;;) {
        const auto step = st.step();
        if (step == db::Statement::Step::Failed) return false;
        if (step == db::Statement::Step::Done) return true;
        out.push_back(read_row(st));
    }
}

void SubmissionStore::audit(int64_t now, const std::string& actor_id, const std::string& actor_name, const char* action, const std::string& id, const std::string& detail) {
    db::Statement st;
    if (db_.prepare(st, "INSERT INTO review_log (at, actor_id, actor_name, action, submission, detail) VALUES (?, ?, ?, ?, ?, ?)").ok() && st.bind_int64(1, now).ok() && st.bind_text(2, actor_id).ok() &&
        st.bind_text(3, actor_name).ok() && st.bind_text(4, action).ok() && st.bind_text(5, id).ok() && st.bind_text(6, detail).ok()) {
        st.step();
    }
}

bool SubmissionStore::name_taken(const std::string& key) {
    if (library_has_ && library_has_(key)) return true;
    return scalar("SELECT COUNT(*) FROM map_submissions WHERE final_key = ?", {key}) != 0;
}

// ------------------------------------------------------------------------------------------------ the maker's submission

SubmitOutcome SubmissionStore::submit(const SubmitInput& in, int64_t now) {
    SubmitOutcome out;
    const auto refuse = [&](int status, const char* code, const char* message) {
        out.fail = make_failure(status, code, message);
        return out;
    };
    if (!open_) return refuse(503, "submissions_off", "Sending maps is not switched on right now.");
    if (in.kind != "classic" && in.kind != "extended") return refuse(400, "bad_kind", "Say whether the map is a Classic map or an Extended map.");
    if (in.bytes.empty()) return refuse(400, "not_a_map", "No map file came with the request.");
    if (in.bytes.size() > max_upload_bytes(in.kind)) return refuse(413, "too_large", "This map file is bigger than a map of this kind may be.");

    const names::Cleaned name = names::clean_map_name(in.name);
    if (!name.ok) {
        out.fail = make_failure(400, "bad_name", name.reason == "too_short" ? "The name is too short (at least 3 characters)." : name.reason == "too_long" ? "The name is too long (at most 60 characters)." :
                                                 name.reason == "reserved" ? "That name cannot be used. Please choose another." : "A name may hold letters, digits, spaces and the marks ' - _ . ! & ( ) , only.");
        out.fail.extra = JsonValue::make_object();
        out.fail.extra.set("reason", JsonValue::make_string(name.reason));
        return out;
    }
    const names::Cleaned maker = names::clean_maker(in.maker);
    if (!maker.ok) {
        out.fail = make_failure(400, "bad_maker", maker.reason == "too_long" ? "The maker name is too long (at most 24 characters)." : maker.reason == "empty" ? "Please give a name to show as the maker." : "The maker name has a character that cannot be shown here.");
        out.fail.extra = JsonValue::make_object();
        out.fail.extra.set("reason", JsonValue::make_string(maker.reason));
        return out;
    }
    const names::Cleaned note = names::clean_note(in.note, names::kMaxNoteChars, false);
    if (!note.ok) {
        out.fail = make_failure(400, "bad_note", note.reason == "too_long" ? "The note is too long (at most 280 characters)." : "The note has a character that cannot be shown here.");
        out.fail.extra = JsonValue::make_object();
        out.fail.extra.set("reason", JsonValue::make_string(note.reason));
        return out;
    }
    if (!in.token.empty() && !valid_token_text(in.token)) return refuse(400, "bad_token", "The token that came with the request is not one that this server made.");

    // the limits, cheapest first and before the map is read
    const int64_t day = now / kDay;
    const std::string akey = address_key(in.address, day);
    const int64_t from_address = scalar("SELECT COALESCE(SUM(submitted), 0) FROM submission_limits WHERE addr_key = ? AND day = " + std::to_string(day), {akey});
    const int64_t from_all = scalar("SELECT COALESCE(SUM(submitted), 0) FROM submission_limits WHERE day = " + std::to_string(day), {});
    const int64_t waiting = scalar("SELECT COUNT(*) FROM map_submissions WHERE state = 'waiting'", {});
    const int64_t waiting_bytes = scalar("SELECT COALESCE(SUM(size), 0) FROM map_submissions WHERE state = 'waiting' AND file_kept = 1", {});
    const int64_t kept_bytes = scalar("SELECT COALESCE(SUM(size), 0) FROM map_submissions WHERE file_kept = 1", {});
    if (from_address < 0 || from_all < 0 || waiting < 0 || waiting_bytes < 0 || kept_bytes < 0) {
        database_failed(out.fail, "a read");
        return out;
    }
    if (static_cast<size_t>(from_address) >= config_.per_address_per_day) {
        return refuse(429, "address_limit", "This address has sent as many maps as it may today. Please try again tomorrow.");
    }
    if (static_cast<size_t>(from_all) >= config_.per_day_total) return refuse(429, "day_limit", "Too many maps came in today. Please try again tomorrow.");
    if (static_cast<size_t>(waiting) >= config_.max_waiting || static_cast<uint64_t>(waiting_bytes) + in.bytes.size() > config_.max_waiting_bytes) {
        return refuse(429, "queue_full", "Too many maps are waiting for the owner. Please try again later.");
    }
    if (static_cast<uint64_t>(kept_bytes) + in.bytes.size() > config_.max_kept_bytes) return refuse(503, "storage_full", "The server has no room for more maps right now. Please try again later.");
    std::error_code ec;
    const fs::space_info space = fs::space(fs::path(config_.dir), ec);
    if (ec || space.available < config_.min_free_bytes + in.bytes.size()) return refuse(503, "storage_full", "The server has no room for more maps right now. Please try again later.");

    // the cheap refusals first (they cost no checking, so they use none of the checking window that every other sender shares): the kind comes from the version word of the file
    const uint32_t version = version_word(in.bytes);
    if (version != 8 && version != 9) return refuse(400, "not_a_map", "This file is not an Ants map.");
    const std::string real_kind = version == 9 ? "extended" : "classic";
    if (real_kind != in.kind) return refuse(400, "kind_mismatch", "The file is not the kind of map that the request says.");
    if (in.allow_check && !in.allow_check()) return refuse(503, "busy", "The server is busy checking other maps. Please try again in a minute.");

    // the check: the real one, whatever the sender says
    const core::Profile profile = real_kind == "extended" ? core::Profile::Extended : core::Profile::NewMap;
    const auto* data = reinterpret_cast<const uint8_t*>(in.bytes.data());
    const core::Result checked = core::check_map(data, in.bytes.size(), profile);
    JsonValue report;
    if (checked.status != 0 || !parse_big_json(checked.payload, report)) return refuse(400, "not_a_map", "This file could not be read as an Ants map.");
    if (!report.get("judged").as_bool_or(false) || !report.get("works").as_bool_or(false) || !report.get("fits_the_place").as_bool_or(false)) {
        const bool too_big = report.get("judged").as_bool_or(false) && !report.get("fits_the_place").as_bool_or(true);
        out.fail = make_failure(422, too_big ? "map_too_big" : "map_fails_checks", too_big ? "This map is bigger than a map of this kind may be." : "This map does not pass the checks, so it cannot be sent.");
        out.fail.extra = JsonValue::make_object();
        out.fail.extra.set("findings", kept_findings(report));
        return out;
    }
    const core::Result rewritten = core::rewrite(data, in.bytes.size(), profile);
    if (rewritten.status != 0 || rewritten.payload.empty()) return refuse(400, "not_a_map", "This map could not be written in its standard form.");
    const std::string& canonical = rewritten.payload;
    const core::Result again = core::check_map(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size(), profile);
    JsonValue report2;
    if (again.status != 0 || !parse_big_json(again.payload, report2) || !report2.get("works").as_bool_or(false) || !report2.get("fits_the_place").as_bool_or(false)) {
        return refuse(400, "not_a_map", "This map could not be written in its standard form.");
    }
    const core::Result info = core::read_report(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size(), profile);
    JsonValue meta;
    if (info.status != 0 || !parse_big_json(info.payload, meta) || !meta.get("loaded").as_bool_or(false)) return refuse(400, "not_a_map", "This map could not be read.");
    int64_t players = 0;
    for (size_t i = 0; i < meta.get("teams").size(); ++i) players += meta.get("teams").at(i).get("start_markers").as_int_or(0) > 0 ? 1 : 0;
    if (players == 0) players = 4;

    // the maker's token: the one the browser has when this server made it, else a new one
    std::string token = in.token;
    bool fresh = true;
    if (!token.empty() && scalar("SELECT COUNT(*) FROM map_submissions WHERE token_hash = ?", {token_hash(token)}) > 0) fresh = false;
    if (fresh) {
        std::string raw(32, '\0');
        if (!random_bytes(reinterpret_cast<uint8_t*>(raw.data()), raw.size())) return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");
        token = base64url_encode(raw);
    }
    std::string id_raw(16, '\0');
    if (!random_bytes(reinterpret_cast<uint8_t*>(id_raw.data()), id_raw.size())) return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");

    Submission s;
    s.id = hex_encode(id_raw);
    s.state = "waiting";
    s.kind = real_kind;
    s.name = name.text;
    s.maker = maker.text;
    s.note = note.text;
    s.width = meta.get("columns").as_int_or(0);
    s.height = meta.get("rows").as_int_or(0);
    s.minutes = meta.get("minutes").as_int_or(0);
    s.players = players;
    s.description = meta.get("description").str();
    s.size = static_cast<int64_t>(canonical.size());
    s.file = s.id + ".lvl";
    s.created_at = now;
    s.findings_json = ctl::to_json(kept_findings(report));
    uint64_t hash = 0;
    const std::string temp = path_of(config_, ".tmp-" + s.id);
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(canonical.data(), static_cast<std::streamsize>(canonical.size()));
        file.flush();
        if (!file) {
            file.close();
            remove_quietly(temp);
            return refuse(503, "storage_full", "The server has no room for more maps right now. Please try again later.");
        }
    }
    if (!net::hash_file(temp, hash)) {
        remove_quietly(temp);
        return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");
    }
    s.hash = map_hash_text(hash);
    fs::rename(temp, path_of(config_, s.file), ec);
    if (ec) {
        remove_quietly(temp);
        return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");
    }
    bool saved = false;
    {
        db::Transaction tx(db_);
        if (tx.active()) {
            db::Statement ins;
            const bool prepared = db_.prepare(ins, "INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, maker, note, width, height, minutes, players, description, size, hash, findings, file, created_at) "
                                                   "VALUES (?, ?, 'waiting', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)").ok();
            bool bound = prepared;
            const std::string thash = token_hash(token);
            const std::string nkey = names::name_key(s.name);
            if (bound) {
                bound = ins.bind_text(1, s.id).ok() && ins.bind_text(2, thash).ok() && ins.bind_text(3, s.kind).ok() && ins.bind_text(4, s.name).ok() && ins.bind_text(5, nkey).ok() && ins.bind_text(6, s.maker).ok() &&
                        ins.bind_text(7, s.note).ok() && ins.bind_int64(8, s.width).ok() && ins.bind_int64(9, s.height).ok() && ins.bind_int64(10, s.minutes).ok() && ins.bind_int64(11, s.players).ok() &&
                        ins.bind_text(12, s.description).ok() && ins.bind_int64(13, s.size).ok() && ins.bind_text(14, s.hash).ok() && ins.bind_text(15, s.findings_json).ok() && ins.bind_text(16, s.file).ok() &&
                        ins.bind_int64(17, now).ok();
            }
            db::Statement lim;
            const bool counted = bound && ins.step() == db::Statement::Step::Done &&
                                 db_.prepare(lim, "INSERT INTO submission_limits (addr_key, day, submitted) VALUES (?, ?, 1) ON CONFLICT (addr_key, day) DO UPDATE SET submitted = submitted + 1").ok() &&
                                 lim.bind_text(1, akey).ok() && lim.bind_int64(2, day).ok() && lim.step() == db::Statement::Step::Done;
            saved = counted && tx.commit().ok();
        }
    }
    if (!saved) {
        remove_quietly(path_of(config_, s.file));
        database_failed(out.fail, "a write");
        return out;
    }
    out.ok = true;
    out.submission = s;
    out.token = token;
    out.token_is_new = fresh;
    out.name_taken = name_taken(names::name_key(s.name));
    return out;
}

// ------------------------------------------------------------------------------------------------ the maker's list

bool SubmissionStore::list_for_token(const std::string& token, std::vector<Submission>& out, Failure& why) {
    out.clear();
    if (!open_) {
        why = make_failure(503, "submissions_off", "Sending maps is not switched on right now.");
        return false;
    }
    if (!valid_token_text(token)) {
        why = make_failure(401, "no_token", "Open this page with the link that you got when you sent a map.");
        return false;
    }
    if (!fetch_many("token_hash", token_hash(token), out)) return database_failed(why, "a read");
    return true;
}

bool SubmissionStore::get_for_token(const std::string& id, const std::string& token, Submission& out, Failure& why) {
    const Failure missing = make_failure(404, "not_found", "There is no such map.");
    if (!open_) {
        why = make_failure(503, "submissions_off", "Sending maps is not switched on right now.");
        return false;
    }
    if (!valid_token_text(token) || !valid_id_text(id) || !fetch_one("id", id, out) || !owns(id, token)) {
        why = missing;                                         // (the same answer whether the id exists or the token is wrong: nothing is learned)
        return false;
    }
    return true;
}

// Whether the token is the one of the submission: the two hashes are compared in constant time, and a missing row is a plain no
bool SubmissionStore::owns(const std::string& id, const std::string& token) {
    db::Statement st;
    if (!db_.prepare(st, "SELECT token_hash FROM map_submissions WHERE id = ?").ok() || !st.bind_text(1, id).ok() || st.step() != db::Statement::Step::Row) return false;
    return ctl::constant_time_equal(st.column_text(0), token_hash(token));
}

bool SubmissionStore::withdraw(const std::string& id, const std::string& token, int64_t now, Submission& out, Failure& why) {
    if (!get_for_token(id, token, out, why)) return false;
    if (out.state != "waiting") {
        why = make_failure(409, "not_waiting", "Only a map that is still waiting can be withdrawn.");
        return false;
    }
    db::Statement st;
    if (!db_.prepare(st, "UPDATE map_submissions SET state = 'withdrawn', decided_at = ?, file_kept = 0 WHERE id = ? AND state = 'waiting'").ok() || !st.bind_int64(1, now).ok() || !st.bind_text(2, id).ok() ||
        st.step() != db::Statement::Step::Done) {
        return database_failed(why, "a write");
    }
    remove_quietly(path_of(config_, out.file));             // (the place in the queue and the room on the disk are free at once)
    out.state = "withdrawn";
    out.decided_at = now;
    out.file_kept = false;
    return true;
}

// ------------------------------------------------------------------------------------------------ the owner's side

bool SubmissionStore::list(const std::string& state, std::vector<Submission>& out, Failure& why) {
    out.clear();
    if (!open_) {
        why = make_failure(503, "submissions_off", "Sending maps is not switched on right now.");
        return false;
    }
    if (state != "waiting" && state != "approved" && state != "declined" && state != "taken_down") {
        why = make_failure(400, "bad_state", "The state is waiting, approved, declined or taken_down.");
        return false;
    }
    db::Statement st;
    const std::string order = state == "waiting" ? "created_at, id" : "decided_at DESC, id";
    if (!db_.prepare(st, std::string("SELECT ") + kColumns + " FROM map_submissions WHERE state = ? ORDER BY " + order + " LIMIT " + std::to_string(kListLimit)).ok() || !st.bind_text(1, state).ok()) return database_failed(why, "a read");
    for (;;) {
        const auto step = st.step();
        if (step == db::Statement::Step::Failed) return database_failed(why, "a read");
        if (step == db::Statement::Step::Done) return true;
        out.push_back(read_row(st));
    }
}

ReviewCounts SubmissionStore::counts() {
    ReviewCounts c;
    const auto n = [&](const char* state) { return static_cast<size_t>(std::max<int64_t>(0, scalar("SELECT COUNT(*) FROM map_submissions WHERE state = ?", {state}))); };
    c.waiting = n("waiting");
    c.approved = n("approved");
    c.declined = n("declined");
    c.taken_down = n("taken_down");
    return c;
}

bool SubmissionStore::get(const std::string& id, Submission& out) { return open_ && valid_id_text(id) && fetch_one("id", id, out); }

bool SubmissionStore::approve(const std::string& id, const std::string& name, const std::string& message, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why) {
    if (!get(id, out)) {
        why = make_failure(404, "not_found", "There is no such map.");
        return false;
    }
    if (out.state != "waiting") {
        why = make_failure(409, "not_waiting", "This map is not waiting any more.");
        return false;
    }
    if (!out.file_kept || !fs::is_regular_file(path_of(config_, out.file))) {
        why = make_failure(409, "file_gone", "The file of this map is gone, so it cannot be approved.");
        return false;
    }
    std::string chosen = out.name;
    if (!name.empty()) {
        const names::Cleaned c = names::clean_map_name(name);
        if (!c.ok) {
            why = make_failure(400, "bad_name", "A name may hold letters, digits, spaces and the marks ' - _ . ! & ( ) , only (3 to 60 characters).");
            return false;
        }
        chosen = c.text;
    }
    const names::Cleaned note = names::clean_note(message, names::kMaxReasonChars, false);
    if (!note.ok) {
        why = make_failure(400, "bad_message", "The message is too long or has a character that cannot be shown.");
        return false;
    }
    const std::string key = names::name_key(chosen);
    if (name_taken(key)) {
        why = make_failure(409, "name_taken", "A map with this name exists already. A name that is taken is never replaced.");
        const std::string next = names::next_free_name(chosen, [&](const std::string& k) { return name_taken(k); });
        why.extra = JsonValue::make_object();
        why.extra.set("suggestion", JsonValue::make_string(next));
        return false;
    }
    db::Statement st;
    const bool renamed = chosen != out.name;
    if (!db_.prepare(st, "UPDATE map_submissions SET state = 'approved', final_name = ?, final_key = ?, renamed = ?, owner_message = ?, decided_at = ?, decided_by = ? WHERE id = ? AND state = 'waiting'").ok() ||
        !st.bind_text(1, chosen).ok() || !st.bind_text(2, key).ok() || !st.bind_int64(3, renamed ? 1 : 0).ok() || !st.bind_text(4, note.text).ok() || !st.bind_int64(5, now).ok() || !st.bind_text(6, actor_name).ok() ||
        !st.bind_text(7, id).ok()) {
        return database_failed(why, "a write");
    }
    if (st.step() != db::Statement::Step::Done) {
        why = make_failure(409, "name_taken", "A map with this name exists already. A name that is taken is never replaced.");     // (the unique index: the name was taken a moment ago)
        return false;
    }
    audit(now, actor_id, actor_name, "approve", id, renamed ? "renamed" : "same name");
    approved_changed_ = true;
    return fetch_one("id", id, out) || database_failed(why, "a read");
}

bool SubmissionStore::decline(const std::string& id, const std::string& reason, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why) {
    if (!get(id, out)) {
        why = make_failure(404, "not_found", "There is no such map.");
        return false;
    }
    if (out.state != "waiting") {
        why = make_failure(409, "not_waiting", "This map is not waiting any more.");
        return false;
    }
    const names::Cleaned text = names::clean_note(reason, names::kMaxReasonChars, true);
    if (!text.ok) {
        why = make_failure(400, "bad_reason", text.reason == "empty" ? "Please give a short reason that the maker can read." : "The reason is too long (at most 200 characters) or has a character that cannot be shown.");
        return false;
    }
    db::Statement st;
    if (!db_.prepare(st, "UPDATE map_submissions SET state = 'declined', owner_message = ?, decided_at = ?, decided_by = ?, file_kept = 0 WHERE id = ? AND state = 'waiting'").ok() || !st.bind_text(1, text.text).ok() ||
        !st.bind_int64(2, now).ok() || !st.bind_text(3, actor_name).ok() || !st.bind_text(4, id).ok() || st.step() != db::Statement::Step::Done) {
        return database_failed(why, "a write");
    }
    remove_quietly(path_of(config_, out.file));                // (a declined map is never shown again: its file does not use the kept budget for 30 days)
    audit(now, actor_id, actor_name, "decline", id, "");
    return fetch_one("id", id, out) || database_failed(why, "a read");
}

// approved -> taken_down and back: the name keeps its protection in both (the unique index holds it), the file is never touched
bool SubmissionStore::decide(const std::string& id, const char* from_state, Submission& out, Failure& why) {
    if (!get(id, out)) {
        why = make_failure(404, "not_found", "There is no such map.");
        return false;
    }
    if (out.state != from_state) {
        why = make_failure(409, std::string(from_state) == "approved" ? "not_approved" : "not_taken_down", std::string(from_state) == "approved" ? "This map is not approved." : "This map is not taken down.");
        return false;
    }
    return true;
}

bool SubmissionStore::take_down(const std::string& id, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why) {
    if (!decide(id, "approved", out, why)) return false;
    db::Statement st;
    if (!db_.prepare(st, "UPDATE map_submissions SET state = 'taken_down', decided_at = ?, decided_by = ? WHERE id = ? AND state = 'approved'").ok() || !st.bind_int64(1, now).ok() || !st.bind_text(2, actor_name).ok() ||
        !st.bind_text(3, id).ok() ||
        st.step() != db::Statement::Step::Done) {
        return database_failed(why, "a write");
    }
    audit(now, actor_id, actor_name, "take_down", id, "");
    approved_changed_ = true;
    return fetch_one("id", id, out) || database_failed(why, "a read");
}

bool SubmissionStore::put_back(const std::string& id, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why) {
    if (!decide(id, "taken_down", out, why)) return false;
    db::Statement st;
    if (!db_.prepare(st, "UPDATE map_submissions SET state = 'approved', decided_at = ?, decided_by = ? WHERE id = ? AND state = 'taken_down'").ok() || !st.bind_int64(1, now).ok() || !st.bind_text(2, actor_name).ok() ||
        !st.bind_text(3, id).ok() ||
        st.step() != db::Statement::Step::Done) {
        return database_failed(why, "a write");
    }
    audit(now, actor_id, actor_name, "put_back", id, "");
    approved_changed_ = true;
    return fetch_one("id", id, out) || database_failed(why, "a read");
}

bool SubmissionStore::next_name(const std::string& name, std::string& out, Failure& why) {
    const names::Cleaned c = names::clean_map_name(name);
    if (!c.ok) {
        why = make_failure(400, "bad_name", "A name may hold letters, digits, spaces and the marks ' - _ . ! & ( ) , only (3 to 60 characters).");
        return false;
    }
    out = names::next_free_name(c.text, [&](const std::string& key) { return name_taken(key); });
    if (out.empty()) {
        why = make_failure(409, "no_free_name", "No free number was found for this name.");
        return false;
    }
    return true;
}

std::string SubmissionStore::preview_png(const std::string& id) {
    Submission s;
    if (!get(id, s) || !s.file_kept) return std::string();
    std::ifstream in(path_of(config_, s.file), std::ios::binary);
    if (!in) return std::string();
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assets::LevelData level;
    const assets::LevelLimits limits = s.kind == "extended" ? assets::LevelLimits::for_extended_maps() : assets::LevelLimits::for_new_maps();
    if (bytes.empty() || !assets::LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), level, nullptr, limits)) return std::string();
    return render_preview_png(level);
}

std::vector<ApprovedMap> SubmissionStore::approved_maps() {
    std::vector<ApprovedMap> out;
    if (!open_) return out;
    db::Statement st;
    if (!db_.prepare(st, std::string("SELECT ") + kColumns + " FROM map_submissions WHERE state = 'approved' ORDER BY final_key").ok()) return out;
    for (;;) {
        if (st.step() != db::Statement::Step::Row) break;
        const Submission s = read_row(st);
        ApprovedMap a;
        a.name = names::file_name_of(s.final_name);
        a.path = path_of(config_, s.file);
        a.size = static_cast<uint64_t>(s.size);
        a.width = static_cast<uint32_t>(s.width);
        a.height = static_cast<uint32_t>(s.height);
        a.minutes = static_cast<uint32_t>(s.minutes);
        a.players = static_cast<uint32_t>(s.players);
        a.description = s.description;
        a.maker = s.maker;
        a.kind = s.kind;
        if (!net::hash_file(a.path, a.hash)) {
            notices_.push_back("submissions: the file of an approved map cannot be read, so it is not listed");
            continue;
        }
        out.push_back(std::move(a));
    }
    return out;
}

bool SubmissionStore::take_approved_changed() {
    const bool changed = approved_changed_;
    approved_changed_ = false;
    return changed;
}

namespace {
// A name this server makes for a file: 32 lowercase hex digits and ".lvl"
bool made_file_name(const std::string& name) {
    if (name.size() != 36 || name.compare(32, 4, ".lvl") != 0) return false;
    for (size_t i = 0; i < 32; ++i) {
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    }
    return true;
}
// ... or such a name that the quarantine gave a number when its place was taken: "<32 hex>.lvl.<1 to 3 digits>"
bool quarantined_name(const std::string& name) {
    if (made_file_name(name)) return true;
    if (name.size() < 38 || name.size() > 40 || !made_file_name(name.substr(0, 36)) || name[36] != '.') return false;
    for (size_t i = 37; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return true;
}
}  // namespace

void SubmissionStore::remove_orphans() {
    // A file that no row owns is MOVED to submissions/orphans/, never deleted: a database that was reset, restored from an older copy or replaced would otherwise take the files of every approved
    // map with it. The quarantine is in no budget. A moved file gets the time of the move as its time (a rename keeps the old one), and what has been there for kOrphanDays is deleted, but only a
    // regular file with a name this server makes (a link, a folder or a stranger's file is never touched), and only what was there before this start: the purge comes first.
    std::error_code ec;
    const fs::path files = fs::path(config_.dir) / "files";
    const fs::path quarantine = fs::path(config_.dir) / "orphans";
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * kOrphanDays);
    for (fs::directory_iterator it(quarantine, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code stamp;
        if (!quarantined_name(it->path().filename().string()) || !fs::is_regular_file(fs::symlink_status(it->path(), stamp)) || stamp) continue;
        const auto written = fs::last_write_time(it->path(), stamp);
        if (!stamp && written < old) remove_quietly(it->path().string());
    }
    size_t moved = 0;
    for (fs::directory_iterator it(files, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!made_file_name(name)) continue;                                              // (only a name this server makes)
        if (scalar("SELECT COUNT(*) FROM map_submissions WHERE file = ? AND file_kept = 1", {name}) != 0) continue;          // (a read that fails is -1: nothing is moved on a doubt)
        std::error_code made, renamed, probe;
        fs::create_directories(quarantine, made);
        if (made) continue;
        fs::path target = quarantine / name;
        for (int n = 1; n < 1000 && fs::symlink_status(target, probe).type() != fs::file_type::not_found; ++n) target = quarantine / (name + "." + std::to_string(n));          // (a second file of the same name never replaces the first)
        if (fs::symlink_status(target, probe).type() != fs::file_type::not_found) continue;
        fs::rename(it->path(), target, renamed);
        if (renamed) continue;
        fs::last_write_time(target, fs::file_time_type::clock::now(), probe);
        ++moved;
    }
    if (moved > 0) {
        notices_.push_back("submissions: " + std::to_string(moved) + " file(s) that no submission owned were moved to " + quarantine.string() +
                           " at the start (a database that was reset or restored from an older copy? move them back to " + files.string() + " if their rows are in the database again)");
    }
}

void SubmissionStore::maintenance(int64_t now) {
    if (!open_) return;
    const int64_t file_cutoff = now - static_cast<int64_t>(config_.declined_file_days) * kDay;
    db::Statement old;
    if (db_.prepare(old, "SELECT id, file FROM map_submissions WHERE state = 'declined' AND file_kept = 1 AND decided_at < ?").ok() && old.bind_int64(1, file_cutoff).ok()) {
        std::vector<std::pair<std::string, std::string>> gone;
        while (old.step() == db::Statement::Step::Row) gone.emplace_back(old.column_text(0), old.column_text(1));
        for (const auto& g : gone) {
            remove_quietly(path_of(config_, g.second));
            db::Statement mark;
            if (db_.prepare(mark, "UPDATE map_submissions SET file_kept = 0 WHERE id = ?").ok() && mark.bind_text(1, g.first).ok()) mark.step();
        }
    }
    db::Statement rows;
    if (db_.prepare(rows, "DELETE FROM map_submissions WHERE state IN ('declined', 'withdrawn') AND file_kept = 0 AND decided_at < ?").ok() &&
        rows.bind_int64(1, now - static_cast<int64_t>(config_.old_row_days) * kDay).ok()) {
        rows.step();
    }
    db::Statement days;
    if (db_.prepare(days, "DELETE FROM submission_limits WHERE day < ?").ok() && days.bind_int64(1, now / kDay - 2).ok()) days.step();
}

std::vector<std::string> SubmissionStore::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

// ------------------------------------------------------------------------------------------------ what the pages are told

JsonValue maker_view(const Submission& s) {
    JsonValue o = JsonValue::make_object();
    o.set("id", JsonValue::make_string(s.id));
    o.set("state", JsonValue::make_string(s.state));
    o.set("kind", JsonValue::make_string(s.kind));
    o.set("name", JsonValue::make_string(s.name));
    o.set("width", JsonValue::make_int(s.width));
    o.set("height", JsonValue::make_int(s.height));
    o.set("sent_at", JsonValue::make_int(s.created_at));
    const bool decided = s.state == "approved" || s.state == "taken_down" || s.state == "declined";
    if (decided) o.set("decided_at", JsonValue::make_int(s.decided_at));
    if (s.state == "approved" || s.state == "taken_down") {
        o.set("approved_name", JsonValue::make_string(s.final_name));
        o.set("renamed_by_owner", JsonValue::make_bool(s.renamed));
    }
    if (decided && !s.owner_message.empty()) o.set("message", JsonValue::make_string(s.owner_message));
    return o;
}

JsonValue owner_view(const Submission& s, bool name_is_taken) {
    JsonValue o = maker_view(s);
    o.set("maker", JsonValue::make_string(s.maker));
    o.set("note", JsonValue::make_string(s.note));
    o.set("minutes", JsonValue::make_int(s.minutes));
    o.set("players", JsonValue::make_int(s.players));
    o.set("description", JsonValue::make_string(s.description));
    o.set("size", JsonValue::make_int(s.size));
    o.set("name_taken", JsonValue::make_bool(name_is_taken));
    o.set("has_picture", JsonValue::make_bool(s.file_kept));
    JsonValue findings;
    if (!ctl::parse_json(s.findings_json, findings, nullptr)) findings = JsonValue::make_array();
    o.set("findings", std::move(findings));
    if (s.state != "approved" && s.state != "taken_down") o.set("message", JsonValue::make_string(s.owner_message));
    if (s.state != "waiting" && s.state != "withdrawn") o.set("decided_by", JsonValue::make_string(s.decided_by));          // (the account that decided: an Admin typed that name, so it is text to show, never to obey)
    return o;
}

SubmissionStore::~SubmissionStore() = default;

}  // namespace ants::server
