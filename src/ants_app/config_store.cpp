#include "ants_app/config_store.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>
#else
  #if defined(__has_include)
    #if __has_include(<SDL.h>)
      #include <SDL.h>
    #elif __has_include(<SDL2/SDL.h>)
      #include <SDL2/SDL.h>
    #endif
  #else
    #include <SDL2/SDL.h>
  #endif
#endif

namespace ants::app {

namespace {

#if defined(__EMSCRIPTEN__)
const char* const kWebStorageKey = "ants.settings";

// The text goes into a JavaScript string literal: the settings hold printable ASCII and line ends only
std::string js_literal(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            default: out.push_back(c); break;
        }
    }
    return out;
}
#endif

bool printable(char c) { return c >= 0x20 && c <= 0x7e; }

}  // anonymous namespace

std::string ConfigStore::default_folder() {
#if defined(__EMSCRIPTEN__)
    return {};
#else
    char* pref = SDL_GetPrefPath("Ants", "Ants");
    if (pref == nullptr) return {};
    std::string folder = pref;
    SDL_free(pref);
    return folder;
#endif
}

std::string ConfigStore::default_location() {
#if defined(__EMSCRIPTEN__)
    return kWebStorageKey;
#else
    const std::string folder = default_folder();
    return folder.empty() ? std::string() : folder + "settings.ini";
#endif
}

void ConfigStore::parse(const std::string& text) {
    values_.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

std::string ConfigStore::serialise() const {
    std::string out;
    for (const auto& kv : values_) out += kv.first + "=" + kv.second + "\n";
    return out;
}

bool ConfigStore::load() {
    values_.clear();
    if (location_.empty()) return true;
#if defined(__EMSCRIPTEN__)
    const std::string script = "(function(){try{return window.localStorage.getItem('" + js_literal(location_) + "')||'';}catch(e){return '';}})()";
    const char* stored = emscripten_run_script_string(script.c_str());
    parse(stored != nullptr ? stored : "");
    return true;
#else
    std::ifstream in(location_, std::ios::binary);
    if (!in) return true;                                   // no file yet: nothing is stored
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return false;
    parse(text);
    return true;
#endif
}

bool ConfigStore::save() const {
    if (location_.empty()) return true;
#if defined(__EMSCRIPTEN__)
    const std::string script = "try{window.localStorage.setItem('" + js_literal(location_) + "','" + js_literal(serialise()) + "');}catch(e){}";
    emscripten_run_script(script.c_str());
    return true;
#else
    std::ofstream out(location_, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << serialise();
    return static_cast<bool>(out);
#endif
}

int32_t ConfigStore::get_int(const std::string& key, int32_t def, uint32_t min, uint32_t max) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return def;
    const std::string& s = it->second;
    if (s.empty() || s.size() > 10) return def;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return def;                 // a DWORD has no sign and no text
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > 0xFFFFFFFFull) return def;
    if (v < min || v >= max) return def;                    // FUN_0102950f: min <= value < max (unsigned)
    return static_cast<int32_t>(v);
}

void ConfigStore::set_int(const std::string& key, int32_t value) {
    values_[key] = std::to_string(value);
    save();
}

std::string ConfigStore::get_string(const std::string& key, const std::string& def, size_t max_chars) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return def;
    std::string out;
    for (char c : it->second) {
        if (!printable(c)) continue;
        if (out.size() >= max_chars) break;
        out.push_back(c);
    }
    return out;
}

void ConfigStore::set_string(const std::string& key, const std::string& value) {
    std::string clean;
    for (char c : value) if (printable(c)) clean.push_back(c);
    values_[key] = clean;
    save();
}

}  // namespace ants::app
