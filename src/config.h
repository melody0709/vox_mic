#pragma once

#include <string>

class Config {
public:
    std::string serial;
    std::string host = "127.0.0.1";
    int port = 27183;
    std::string androidSocket = "audiosource";
    std::string androidComponent = "com.voxmic.source/.MainActivity";
    int androidAppPreset = 1;
    float gain = 1.35f;
    bool nsEnabled = false;
    bool aecEnabled = true;
    bool agcEnabled = false;
    bool eqEnabled = true;
    float eqPresence = 3.0f;
    float eqBassCut = -3.0f;
    bool compressorEnabled = true;
    bool nrEnabled = true;
    float nrStrength = 0.6f;
    bool debugConsole = true;
    bool demandMode = true;
    bool alwaysHot = false;

    static Config load();
    void save() const;
};
