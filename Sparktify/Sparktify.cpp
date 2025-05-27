#include <windows.h>
#include <shellapi.h>
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
#include "resource.h"
#include <fstream>
#include <string>
#include <shlobj.h> // SHGetFolderPathW için
#include <filesystem>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_AUTOSTART 1002

// Ayar dosyasý yolu
const wchar_t* SETTINGS_FILE = L"settings.ini";
const wchar_t* AUTOSTART_KEY = L"AutoStart";

// Uygulamanýn tam yolu
std::wstring GetAppPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
}

// Belgeler\Sparktify\settings.ini yolunu döndürür
std::wstring GetSettingsFilePath() {
    wchar_t docPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docPath);
    std::wstring folder = std::wstring(docPath) + L"\\Sparktify";
    std::filesystem::create_directories(folder); // Klasörü oluþtur (varsa bir þey yapmaz)
    return folder + L"\\settings.ini";
}

// Ayar dosyasýndan autostart durumunu oku
bool ReadAutoStartSetting() {
    std::wstring settingsPath = GetSettingsFilePath();
    std::wifstream fin(settingsPath);
    if (!fin) return true; // Dosya yoksa default açýk
    std::wstring line;
    while (std::getline(fin, line)) {
        if (line.find(AUTOSTART_KEY) == 0) {
            return line.find(L"1") != std::wstring::npos;
        }
    }
    return true;
}

// Ayar dosyasýna autostart durumunu yaz
void WriteAutoStartSetting(bool enabled) {
    std::wstring settingsPath = GetSettingsFilePath();
    std::wofstream fout(settingsPath);
    fout << AUTOSTART_KEY << L"=" << (enabled ? L"1" : L"0") << std::endl;
}

// Kayýt defterine ekle/kaldýr
void SetAutoStartRegistry(bool enabled) {
    HKEY hKey;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    if (enabled) {
        std::wstring path = GetAppPath();
        RegSetValueExW(hKey, L"Sparktify", 0, REG_SZ, (BYTE*)path.c_str(), (DWORD)((path.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"Sparktify");
    }
    RegCloseKey(hKey);
}

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

void FadeToVolume(ISimpleAudioVolume* pVolume, float targetVolume, float step = 0.05f, int delayMs = 50) {
    if (!pVolume) return;

    float currentVolume = 0.0f;
    if (FAILED(pVolume->GetMasterVolume(&currentVolume))) return;

    while (abs(currentVolume - targetVolume) > 0.01f) {
        if (currentVolume < targetVolume) {
            currentVolume = min(currentVolume + step, targetVolume);
        } else {
            currentVolume = max(currentVolume - step, targetVolume);
        }

        pVolume->SetMasterVolume(currentVolume, NULL);
        Sleep(delayMs);
    }
}

void CheckAndControlSpotifyVolume() {
    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        IID_PPV_ARGS(&pEnumerator));
    if (FAILED(hr)) return;

    IMMDeviceCollection* pDeviceCollection = nullptr;
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDeviceCollection);
    if (FAILED(hr)) { pEnumerator->Release(); return; }

    UINT deviceCount = 0;
    pDeviceCollection->GetCount(&deviceCount);

    // 1. Tüm cihazlarda Spotify dýþý bir uygulama ses çýkarýyor mu?
    bool isOtherAppPlaying = false;
    for (UINT devIdx = 0; devIdx < deviceCount; ++devIdx) {
        IMMDevice* pDevice = nullptr;
        hr = pDeviceCollection->Item(devIdx, &pDevice);
        if (FAILED(hr)) continue;

        IAudioSessionManager2* pSessionManager = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
        if (FAILED(hr)) { pDevice->Release(); continue; }

        IAudioSessionEnumerator* pSessionEnumerator = nullptr;
        hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
        if (FAILED(hr)) { pSessionManager->Release(); pDevice->Release(); continue; }

        int sessionCount = 0;
        pSessionEnumerator->GetCount(&sessionCount);

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

            if (_tcsicmp(processName, _T("Spotify.exe")) != 0) {
                if (IsSessionReallyPlaying(pSession2)) {
                    isOtherAppPlaying = true;
                }
            }

            CloseHandle(hProcess);
            pSession2->Release();
            pSession->Release();

            if (isOtherAppPlaying) break; // Bir tane bulunca yeter
        }

        pSessionEnumerator->Release();
        pSessionManager->Release();
        pDevice->Release();

        if (isOtherAppPlaying) break; // Bir tane bulunca yeter
    }

    // 2. Tüm cihazlarda Spotify'ýn sesini ayarla
    for (UINT devIdx = 0; devIdx < deviceCount; ++devIdx) {
        IMMDevice* pDevice = nullptr;
        hr = pDeviceCollection->Item(devIdx, &pDevice);
        if (FAILED(hr)) continue;

        IAudioSessionManager2* pSessionManager = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
        if (FAILED(hr)) { pDevice->Release(); continue; }

        IAudioSessionEnumerator* pSessionEnumerator = nullptr;
        hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
        if (FAILED(hr)) { pSessionManager->Release(); pDevice->Release(); continue; }

        int sessionCount = 0;
        pSessionEnumerator->GetCount(&sessionCount);

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
                    FadeToVolume(pVolume, volumeLevel);
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
    }

    pDeviceCollection->Release();
    pEnumerator->Release();
}

// Sað týk menüsüne ekle
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static bool autostart = ReadAutoStartSetting();

    if (uMsg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, -1, MF_BYPOSITION | (autostart ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_AUTOSTART, L"Baþlangýçta Aç");
            InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_EXIT, L"Çýkýþ");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            if (cmd == ID_TRAY_EXIT) {
                PostQuitMessage(0);
            } else if (cmd == ID_TRAY_AUTOSTART) {
                autostart = !autostart;
                WriteAutoStartSetting(autostart);
                SetAutoStartRegistry(autostart);
            }
            DestroyMenu(hMenu);
        }
    } else if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void RunVolumeControl() {
    CoInitialize(NULL);
    while (true) {
        CheckAndControlSpotifyVolume();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    CoUninitialize();
}

// wWinMain içinde ilk baþlatmada autostart ayarýný uygula
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Ayar dosyasýný ilk çalýþtýrmada oluþtur
    std::wstring settingsPath = GetSettingsFilePath();
    if (!std::filesystem::exists(settingsPath)) {
        WriteAutoStartSetting(true); // Default açýk olarak oluþtur
    }

    // Pencere sýnýfý tanýmý
    const wchar_t CLASS_NAME[] = L"SparktifyTrayClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    // Gizli pencere oluþtur
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"Sparktify", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;

    // System tray simgesi ekle
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SPARKTIFY));
    wcscpy_s(nid.szTip, L"Sparktify");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Autostart ayarýný uygula (ilk baþlatmada)
    bool autostart = ReadAutoStartSetting();
    SetAutoStartRegistry(autostart);

    // Ana iþlevi thread olarak baþlat
    std::thread worker(RunVolumeControl);
    worker.detach();

    // Mesaj döngüsü
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Tray simgesini kaldýr
    Shell_NotifyIcon(NIM_DELETE, &nid);
    return 0;
}
