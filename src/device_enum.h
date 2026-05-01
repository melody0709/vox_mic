#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <string>

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    int index;
};

IMMDevice* findVBCableDevice(IMMDeviceEnumerator* pEnumerator);
IMMDevice* getDefaultRenderDevice(IMMDeviceEnumerator* pEnumerator);
void listAudioDevices(IMMDeviceEnumerator* pEnumerator);
