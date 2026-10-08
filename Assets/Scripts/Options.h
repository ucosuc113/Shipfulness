#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ship {

struct Options {
    float music = 0.7f;
    float sfx = 0.9f;
    bool showFps = false;
    bool cameraFollow = true;
    bool wheelAutoCenter = true;
    bool cameraShake = true;
    std::vector<float> best;

    static std::filesystem::path File() {
        const char* base = std::getenv("LOCALAPPDATA");
        std::filesystem::path p = base ? std::filesystem::path(base) : std::filesystem::temp_directory_path();
        return p / "Shipfulness" / "opciones.txt";
    }

    void Load(size_t levelCount) {
        best.assign(levelCount, 0.0f);
        std::ifstream in(File());
        std::string key;
        while (in >> key) {
            if (key == "musica") in >> music;
            else if (key == "efectos") in >> sfx;
            else if (key == "fps") in >> showFps;
            else if (key == "camara") in >> cameraFollow;
            else if (key == "timon") in >> wheelAutoCenter;
            else if (key == "sacudida") in >> cameraShake;
            else if (key == "record") {
                size_t i;
                float t;
                in >> i >> t;
                if (i < best.size()) best[i] = t;
            }
        }
    }

    void Save() const {
        std::error_code ec;
        std::filesystem::create_directories(File().parent_path(), ec);
        std::ofstream out(File());
        out << "musica " << music << "\nefectos " << sfx << "\nfps " << showFps << "\ncamara " << cameraFollow
            << "\ntimon " << wheelAutoCenter << "\nsacudida " << cameraShake << "\n";
        for (size_t i = 0; i < best.size(); ++i)
            if (best[i] > 0.0f) out << "record " << i << " " << best[i] << "\n";
    }
};

}
