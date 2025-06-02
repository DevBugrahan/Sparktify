#include "SpotifyPopup.h"
#include <thread>
#include <chrono>

LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::wstring text;
    static int animStep = 0;
    static int animMax = 20;
    static bool closing = false;

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 10, NULL);
        break;
    case WM_TIMER:
        if (!closing) {
            if (animStep < animMax) {
                animStep++;
                int width = 100 + animStep * 15;
                SetWindowPos(hwnd, HWND_TOPMOST, (GetSystemMetrics(SM_CXSCREEN) - width) / 2, 30, width, 60, SWP_SHOWWINDOW);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        } else {
            if (animStep > 0) {
                animStep--;
                int width = 100 + animStep * 15;
                SetWindowPos(hwnd, HWND_TOPMOST, (GetSystemMetrics(SM_CXSCREEN) - width) / 2, 30, width, 60, SWP_SHOWWINDOW);
                InvalidateRect(hwnd, NULL, TRUE);
            } else {
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
            }
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_CLOSE:
        closing = true;
        SetTimer(hwnd, 1, 10, NULL);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowSpotifyPopup(const std::wstring& song, const std::wstring& artist) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = PopupWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"SpotifyPopupClass";
    RegisterClass(&wc);

    std::wstring text = L"?? " + song + L" - " + artist;

    int width = 100;
    int height = 60;
    HWND hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - width) / 2, 30, width, height, NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    // 3 saniye göster, sonra kapat
    std::thread([hwnd]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }).detach();

    // Mesaj döngüsü
    MSG msg;
    while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}