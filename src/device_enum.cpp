#include "device_enum.h"
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
#include <vector>

IMMDevice* findVBCableDevice(IMMDeviceEnumerator* pEnumerator) {
    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        printf("EnumAudioEndpoints failed: 0x%08lx\n", hr);
        return nullptr;
    }

    UINT count = 0;
    pCollection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(i, &pDevice);
        if (FAILED(hr)) continue;

        IPropertyStore* pProps = nullptr;
        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (SUCCEEDED(hr)) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
                if (wcsstr(varName.pwszVal, L"CABLE Input") != nullptr) {
                    printf("Found VB-CABLE: %ls\n", varName.pwszVal);
                    PropVariantClear(&varName);
                    pProps->Release();
                    pCollection->Release();
                    return pDevice;
                }
                PropVariantClear(&varName);
            }
            pProps->Release();
        }
        pDevice->Release();
    }

    pCollection->Release();
    return nullptr;
}

IMMDevice* getDefaultRenderDevice(IMMDeviceEnumerator* pEnumerator) {
    IMMDevice* pDevice = nullptr;
    HRESULT hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) {
        printf("GetDefaultAudioEndpoint failed: 0x%08lx\n", hr);
        return nullptr;
    }

    IPropertyStore* pProps = nullptr;
    hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (SUCCEEDED(hr)) {
        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
        if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
            printf("Using default device: %ls\n", varName.pwszVal);
            PropVariantClear(&varName);
        }
        pProps->Release();
    }

    return pDevice;
}

void listAudioDevices(IMMDeviceEnumerator* pEnumerator) {
    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        printf("EnumAudioEndpoints failed: 0x%08lx\n", hr);
        return;
    }

    UINT count = 0;
    pCollection->GetCount(&count);
    printf("\nAudio output devices:\n");

    for (UINT i = 0; i < count; i++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(i, &pDevice);
        if (FAILED(hr)) continue;

        IPropertyStore* pProps = nullptr;
        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (SUCCEEDED(hr)) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
                printf("  [%u] %ls\n", i, varName.pwszVal);
                PropVariantClear(&varName);
            }
            pProps->Release();
        }
        pDevice->Release();
    }

    pCollection->Release();
    printf("\n");
}
