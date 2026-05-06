#pragma once

#include "../engine/Engine.h"
#include <SFML/Graphics.hpp>

class VisualApp {
public:
    VisualApp(Engine& engine);
    void run();

private:
    Engine& engine;
    sf::RenderWindow window;
    uint64_t tick;
};
