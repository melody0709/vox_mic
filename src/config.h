#pragma once

#include <string>

class Config {
public:
    std::string serial;
    std::string host = "127.0.0.1";
    int port = 27183;
    std::string androidSocket = "audiosource";
    std::string androidComponent = "fr.dzx.audiosource/.MainActivity";
    int androidAppPreset = 0;
    float gain = 1.0f;
    bool nsEnabled = false;
    bool aecEnabled = true;
    bool agcEnabled = false;
    bool eqEnabled = true;
    float eqPresence = 3.0f;
    float eqBassCut = -3.0f;
    bool compressorEnabled = true;
    bool nrEnabled = true;

    static Config load();
    void save() const;
};
