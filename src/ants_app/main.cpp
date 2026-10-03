#include "ants_app/application.hpp"
#include <cstring>
#include <iostream>
#if !defined(__EMSCRIPTEN__)
#include "ants_app/lan_list.hpp"
#include "ants_app/version.hpp"
#include "ants_net/protocol.hpp"
#endif

int main(int argc, char* argv[]) {
#if !defined(__EMSCRIPTEN__)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lan-list") == 0) return ants::app::lan_list_main(argc, argv);     // no window, no assets: only what the network offers
        if (std::strcmp(argv[i], "--version") == 0) {                                                  // no window, no assets: which build this is (the corner plate shows the version only)
            std::cout << "ants " << ants::VERSION_STRING << " build " << ants::BUILD_ID << " (network protocol " << ants::net::kProtocolVersion << ")\n";
            return 0;
        }
    }
#endif
#if defined(__EMSCRIPTEN__)
    static ants::app::Application app;
#else
    ants::app::Application app;
#endif
    if (!app.init(argc, argv)) {
        std::cerr << "Failed to initialize Ants Application\n";
        return 1;
    }
    return app.run();
}
