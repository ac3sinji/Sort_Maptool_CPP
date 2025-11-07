// ========================= src/main.cpp =========================
#include "ui/App.hpp"
#include <SDL.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    ws::AppUI app;
    return app.run();
}