#include "ants_app/application.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    ants::app::Application app;
    if (!app.init(argc, argv)) {
        std::cerr << "Failed to initialize Ants Application\n";
        return 1;
    }
    return app.run();
}
