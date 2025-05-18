#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <AudioPolicy.h>
#include <psapi.h>
#include <tchar.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <comdef.h>
#include <cmath>

bool IsSessionReallyPlaying(IAudioSessionControl2* pSession2) {
    IAudioMeterInformation* pMeter = nullptr;

    // Oturuma baðlý ses cihazýný alýn
    ISimpleAudioVolume* pVolume = nullptr;
    IAudioSessionControl* pControl = nullptr;
    pSession2->QueryInterface(__uuidof(IAudioSessionControl), (void**)&pControl);

    if (!pControl) return false;

    IMMDeviceEnumerator* pEnum = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioSessionManager2* pMgr = nullptr;

    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        IID_PPV_ARGS(&pEnum));
    pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pMgr);

    IAudioSessionEnumerator* pEnumSess = nullptr;
    pMgr->GetSessionEnumerator(&pEnumSess);

    // Oturumun ses seviyesi ölçümcüsünü al
    if (SUCCEEDED(pSession2->QueryInterface(__uuidof(IAudioMeterInformation), (void**)&pMeter))) {
        float peak = 0.0f;
        if (SUCCEEDED(pMeter->GetPeakValue(&peak))) {
            pMeter->Release();
            if (peak > 0.05f) return true;  // 0.01 hassasiyet: 1% üzeri ses varsa aktif
        }
    }

    return false;
}

void CheckAndControlSpotifyVolume() {
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionEnumerator* pSessionEnumerator = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        IID_PPV_ARGS(&pEnumerator));
    if (FAILED(hr)) return;

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (FAILED(hr)) return;

    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
        NULL, (void**)&pSessionManager);
    if (FAILED(hr)) return;

    hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
    if (FAILED(hr)) return;

    int sessionCount = 0;
    pSessionEnumerator->GetCount(&sessionCount);

    bool isOtherAppPlaying = false;

    // Ýlk döngüde diðer uygulama ses çýkarýyor mu kontrol edilir
    for (int i = 0; i < sessionCount; ++i) {
        IAudioSessionControl* pSession = nullptr;
        IAudioSessionControl2* pSession2 = nullptr;

        pSessionEnumerator->GetSession(i, &pSession);
        if (!pSession) continue;

        pSession->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSession2);
        if (!pSession2) { pSession->Release(); continue; }

        DWORD pid;
        pSession2->GetProcessId(&pid);

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) { pSession2->Release(); pSession->Release(); continue; }

        TCHAR processName[MAX_PATH] = TEXT("<unknown>");
        GetModuleBaseName(hProcess, NULL, processName, MAX_PATH);

        AudioSessionState state;
        pSession->GetState(&state);

        if (_tcsicmp(processName, _T("Spotify.exe")) != 0) {
            if (IsSessionReallyPlaying(pSession2)) {
                isOtherAppPlaying = true;
            }
        }

        CloseHandle(hProcess);
        pSession2->Release();
        pSession->Release();
    }

    // Ýkinci döngüde Spotify sesi kontrol edilir
    for (int i = 0; i < sessionCount; ++i) {
        IAudioSessionControl* pSession = nullptr;
        IAudioSessionControl2* pSession2 = nullptr;

        pSessionEnumerator->GetSession(i, &pSession);
        if (!pSession) continue;

        pSession->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSession2);
        if (!pSession2) { pSession->Release(); continue; }

        DWORD pid;
        pSession2->GetProcessId(&pid);

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) { pSession2->Release(); pSession->Release(); continue; }

        TCHAR processName[MAX_PATH] = TEXT("<unknown>");
        GetModuleBaseName(hProcess, NULL, processName, MAX_PATH);

        if (_tcsicmp(processName, _T("Spotify.exe")) == 0) {
            ISimpleAudioVolume* pVolume = nullptr;
            pSession->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVolume);

            if (pVolume) {
                float volumeLevel = isOtherAppPlaying ? 0.2f : 1.0f;
                pVolume->SetMasterVolume(volumeLevel, NULL);
                pVolume->Release();
            }
        }

        CloseHandle(hProcess);
        pSession2->Release();
        pSession->Release();
    }

    pSessionEnumerator->Release();
    pSessionManager->Release();
    pDevice->Release();
    pEnumerator->Release();
}

int main() {
    std::cout << "Spotify ses kontrol uygulamasý baþlatýldý...\n";
    CoInitialize(NULL);

    while (true) {
        CheckAndControlSpotifyVolume();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    CoUninitialize();
    return 0;
}
