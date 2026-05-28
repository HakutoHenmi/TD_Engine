#include <iostream>
#include <fstream>
#include <cmath>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

int main() {
    std::ifstream f("Resources/Scenes/Stage1.json");
    if (!f.is_open()) return 1;
    json d;
    f >> d;
    float cx=0, cz=0, sx=0, sz=0;
    for (auto& e : d["entities"]) {
        std::string name = e.value("name", "");
        if (name.find("Core") != std::string::npos) {
            auto t = e["components"]["TransformComponent"]["translate"];
            cx = t[0]; cz = t[2];
        }
        if (name.find("Spawner_W1_1") != std::string::npos || (name.find("Spawner") != std::string::npos && sx == 0)) {
            auto t = e["components"]["TransformComponent"]["translate"];
            sx = t[0]; sz = t[2];
        }
    }
    std::cout << "Core: " << cx << ", " << cz << "\n";
    std::cout << "Spawner: " << sx << ", " << sz << "\n";
    float dx = sx - cx;
    float dz = sz - cz;
    float yaw = std::atan2(dx, dz);
    std::cout << "Yaw: " << yaw << " (" << yaw * 180 / 3.14159 << " deg)\n";
    return 0;
}
