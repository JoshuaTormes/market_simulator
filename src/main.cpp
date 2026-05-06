#include "engine/Engine.h"
#include "visual/VisualApp.h"
#include <thread>
#include <string>

int main() {
    std::vector<std::string> tickers = {"AAPL"};
    Engine engine(1'000'000, tickers);

    std::thread simThread([&]() {
        engine.run(0.0005);
    });

    VisualApp app(engine);
    app.run();

    simThread.join();
    return 0;
}
