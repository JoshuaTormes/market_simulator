#include "engine/Engine.h"
#include <vector>
#include <string>

int main() {
    std::vector<std::string> tickers = {"AAPL"}; 
    Engine simulation(1'000'000, tickers);
    simulation.run(1); 
}
