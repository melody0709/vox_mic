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
    bool eqEnabled = false;
    float eqPresence = 3.0f;
    float eqBassCut = -3.0f;
    bool compressorEnabled = false;
    bool nrEnabled = true;
    float nrStrength = 0.6f;
    std::string denoiseBackend = "dpdfnet";
    bool debugConsole = false;
    bool demandMode = true;
    bool alwaysHot = false;

    static Config load();
    bool save() const;
};
