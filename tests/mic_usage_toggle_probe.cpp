#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <propkeydef.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {

IMMDevice* findCableOutput(IMMDeviceEnumerator* enumerator) {
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(
            eCapture, DEVICE_STATE_ACTIVE, &collection))) {
        return nullptr;
    }

    IMMDevice* result = nullptr;
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count && !result; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device))) continue;

        IPropertyStore* properties = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
                name.vt == VT_LPWSTR && name.pwszVal &&
                wcsstr(name.pwszVal, L"CABLE Output") != nullptr) {
                std::wprintf(L"Using capture endpoint: %ls\n", name.pwszVal);
                result = device;
            }
            PropVariantClear(&name);
            properties->Release();
        }
        if (!result) device->Release();
    }
    collection->Release();
    return result;
}

bool runCaptureCycle(IMMDevice* device, int activeMs) {
    IAudioClient* audioClient = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(&audioClient));
    if (FAILED(hr)) return false;

    WAVEFORMATEX* format = nullptr;
    hr = audioClient->GetMixFormat(&format);
    if (SUCCEEDED(hr)) {
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, nullptr);
    }
    if (SUCCEEDED(hr)) hr = audioClient->Start();
    if (SUCCEEDED(hr)) {
        Sleep(static_cast<DWORD>(activeMs));
        hr = audioClient->Stop();
    }

    if (format) CoTaskMemFree(format);
    audioClient->Release();
    return SUCCEEDED(hr);
}

} // namespace

int main(int argc, char** argv) {
    const int cycles = argc > 1 ? std::max(1, std::atoi(argv[1])) : 30;
    const int activeMs = argc > 2 ? std::max(20, std::atoi(argv[2])) : 120;
    const int gapMs = argc > 3 ? std::max(0, std::atoi(argv[3])) : 120;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n", hr);
        return 1;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        std::fprintf(stderr, "MMDeviceEnumerator failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }

    IMMDevice* device = findCableOutput(enumerator);
    if (!device) {
        std::fprintf(stderr, "CABLE Output capture endpoint was not found\n");
        enumerator->Release();
        CoUninitialize();
        return 2;
    }

    for (int i = 0; i < cycles; ++i) {
        if (!runCaptureCycle(device, activeMs)) {
            std::fprintf(stderr, "capture cycle %d failed\n", i + 1);
            device->Release();
            enumerator->Release();
            CoUninitialize();
            return 3;
        }
        if (i + 1 < cycles) Sleep(static_cast<DWORD>(gapMs));
    }

    std::printf("mic_usage_toggle_probe passed: cycles=%d active=%dms gap=%dms\n",
        cycles, activeMs, gapMs);
    device->Release();
    enumerator->Release();
    CoUninitialize();
    return 0;
}
