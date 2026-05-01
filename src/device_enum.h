#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <string>

IMMDevice* findVBCableDevice(IMMDeviceEnumerator* pEnumerator);
IMMDevice* getDefaultRenderDevice(IMMDeviceEnumerator* pEnumerator);
void listAudioDevices(IMMDeviceEnumerator* pEnumerator);
