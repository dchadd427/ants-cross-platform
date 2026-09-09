#include "ants_app/application.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
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
