// The web build's MapFetcher: the page's AntsMapFiles (web/map_files.js, created in web/shell.html as window.antsMapFiles) does the fetching; these calls only ask and poll.
#include "ants_app/map_fetch.hpp"

#include <cstdio>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

extern "C" {
EM_JS(void, ants_map_request, (const char* folder, const char* name, unsigned hash_high, unsigned hash_low), {
    try {
        var h = (hash_high >>> 0).toString(16).padStart(8, "0") + (hash_low >>> 0).toString(16).padStart(8, "0");
        window.antsMapFiles.request(UTF8ToString(folder), UTF8ToString(name), (hash_high | hash_low) ? h : "");
    } catch (e) { console.error("map request", e); }
});
EM_JS(int, ants_map_status, (const char* name, char* text, int capacity), {
    try {
        var n = UTF8ToString(name), s = window.antsMapFiles.status(n);
        if (s === 2) stringToUTF8(window.antsMapFiles.message(n), text, capacity);
        return s;
    } catch (e) { stringToUTF8("The page cannot fetch maps.", text, capacity); return 2; }
});
}
#endif

namespace ants::app {

namespace {

#if defined(__EMSCRIPTEN__)
class WebMapFetcher final : public MapFetcher {
public:
    void request(const std::string& folder, const std::string& name, uint64_t hash) override {
        ants_map_request(folder.c_str(), name.c_str(), static_cast<unsigned>(hash >> 32), static_cast<unsigned>(hash & 0xFFFFFFFFu));
    }
    Status status(const std::string& name) override {
        char text[512] = {0};
        const int s = ants_map_status(name.c_str(), text, static_cast<int>(sizeof(text)));
        Status out;
        out.state = s == 1 ? State::Ready : s == 2 ? State::Failed : State::Pending;
        if (out.state == State::Failed) out.message = text;
        return out;
    }
};
#endif

}  // namespace

std::unique_ptr<MapFetcher> make_web_map_fetcher() {
#if defined(__EMSCRIPTEN__)
    return std::make_unique<WebMapFetcher>();
#else
    return nullptr;
#endif
}

}  // namespace ants::app
