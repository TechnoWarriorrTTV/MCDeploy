// ============================================================
// MCDeploy Native Browser Window - Windows implementation
// ============================================================
#include "BrowserWindow.h"
#include "AppPaths.h"

#ifdef _WIN32

#include <windows.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <objbase.h>       // CoInitializeEx
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>

#include <atomic>
#include <string>
#include <iostream>
#include <filesystem>

using namespace Microsoft::WRL;

namespace MCDeploy {

// ------------------------------------------------------------
// Window state - kept in the window's user data.
// ------------------------------------------------------------
struct WindowState {
    wil::com_ptr<ICoreWebView2Controller> controller;
    wil::com_ptr<ICoreWebView2>           webview;
    std::function<void()>                 onClosed;
    std::wstring                          navigateUrl;
    std::atomic<bool>                     initialized{false};
};

static const wchar_t* kWindowClass = L"MCDeployAppWindow";
static const wchar_t* kWindowTitle = L"MCDeploy";

// Convert UTF-8 std::string to std::wstring (Windows APIs expect UTF-16).
static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int cch = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)cch, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), cch);
    return out;
}

// The WM_ dispatcher for our host window.
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
        // Keep the WebView2 filling the client area on resize.
        if (state && state->controller) {
            RECT bounds;
            ::GetClientRect(hWnd, &bounds);
            state->controller->put_Bounds(bounds);
        }
        return 0;

    case WM_CLOSE:
        ::DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (state && state->onClosed) state->onClosed();
        ::PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND:
        // We render the whole client area with the WebView; skip the flash.
        return 1;

    default: break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Register the window class once per process.
static void ensureWindowClass(HINSTANCE hInst) {
    static bool registered = false;
    if (registered) return;
    registered = true;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kWindowClass;
    wc.hCursor       = ::LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    // Solid dark background so the window doesn't flash white before the WebView paints.
    wc.hbrBackground = ::CreateSolidBrush(RGB(12, 15, 12));

    // Load our custom icon from the exe's embedded resources.
    // Resource id 1 is our IDI_ICON_MAIN from mcdeploy.rc.
    wc.hIcon   = ::LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = ::LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon)   wc.hIcon   = ::LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;

    ::RegisterClassExW(&wc);
}

// WebView2 stores its writable profile under the per-user MCDeploy data directory.
static std::wstring computeUserDataFolder() {
    try {
        auto p = ::MCDeployPaths::dataDirectory() / "mcdeploy_webview_data";
        std::filesystem::create_directories(p);
        return p.wstring();
    } catch (...) {
        return L"";
    }
}

// ------------------------------------------------------------
// Public entry point.
// ------------------------------------------------------------
bool runMcdeployWindow(const std::string& url, std::function<void()> onClosed) {
    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    ensureWindowClass(hInst);

    // WebView2 dispatches its async COM callbacks (environment created,
    // controller created, navigation completed) through the current
    // apartment's STA message pump. If we skip this, the callbacks
    // never fire and the WebView never attaches — you just see the
    // window's dark fallback background. Learned the hard way.
    HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
        std::cerr << "[MCDeploy Window] CoInitializeEx failed: 0x"
                  << std::hex << hrCo << std::dec << std::endl;
    }

    // Enable per-monitor DPI awareness so the WebView is crisp on HiDPI displays.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Own AppUserModelID → Windows gives us our own taskbar entry (own icon
    // + own group), separate from any other Chromium instances.
    ::SetCurrentProcessExplicitAppUserModelID(L"MCDeploy.Panel");

    // Center on the primary monitor at ~1280x820.
    int screenW = ::GetSystemMetrics(SM_CXSCREEN);
    int screenH = ::GetSystemMetrics(SM_CYSCREEN);
    int winW = 1280, winH = 820;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    auto* state = new WindowState;
    state->onClosed = std::move(onClosed);
    state->navigateUrl = widen(url);

    HWND hWnd = ::CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        winX, winY, winW, winH,
        nullptr, nullptr, hInst, nullptr
    );

    if (!hWnd) {
        std::cerr << "[MCDeploy Window] Failed to create window: " << ::GetLastError() << std::endl;
        delete state;
        return false;
    }

    ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)state);

    // Explicit icon set on the window itself (belt-and-suspenders — the class
    // icon usually covers it, but this also updates when a themed shell rebuilds).
    HICON hIcon = ::LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (hIcon) {
        ::SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        ::SendMessageW(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
    }

    ::ShowWindow(hWnd, SW_SHOW);
    ::UpdateWindow(hWnd);

    // Kick off WebView2 environment creation. This is async — the browser
    // widget attaches to the window in the completed handler.
    std::wstring userData = computeUserDataFolder();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.empty() ? nullptr : userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd, state](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    std::cerr << "[MCDeploy Window] WebView2 environment creation failed: 0x"
                              << std::hex << result << std::endl;
                    // Close the native host; the caller will stop the local service.
                    ::PostMessageW(hWnd, WM_CLOSE, 0, 0);
                    return result;
                }
                env->CreateCoreWebView2Controller(
                    hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hWnd, state](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(r) || !controller) {
                                std::cerr << "[MCDeploy Window] WebView2 controller creation failed: 0x"
                                          << std::hex << r << std::endl;
                                ::PostMessageW(hWnd, WM_CLOSE, 0, 0);
                                return r;
                            }
                            state->controller = controller;

                            RECT bounds;
                            ::GetClientRect(hWnd, &bounds);
                            controller->put_Bounds(bounds);

                            wil::com_ptr<ICoreWebView2> webview;
                            controller->get_CoreWebView2(&webview);
                            state->webview = webview;

                            // Turn off the default context menu + dev tools binding for a
                            // more app-like feel. F12 still opens devtools if the user
                            // really wants them (kept as an escape hatch for support).
                            wil::com_ptr<ICoreWebView2Settings> settings;
                            webview->get_Settings(&settings);
                            if (settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            // Log navigation results (successful or otherwise)
                            // so we can diagnose blank-screen issues.
                            EventRegistrationToken navToken;
                            webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (success) {
                                            std::cout << "[MCDeploy Window] Navigation completed successfully." << std::endl;
                                        } else {
                                            COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                            args->get_WebErrorStatus(&status);
                                            std::cerr << "[MCDeploy Window] Navigation FAILED (WebErrorStatus="
                                                      << status << "). If this is 'ConnectionAborted' or "
                                                      << "'ConnectionReset', the drogon server wasn't ready. "
                                                      << "The window will retry in 1s..." << std::endl;
                                            // Small retry with delay - sometimes drogon takes a beat to bind.
                                            ::Sleep(1000);
                                            LPWSTR uri = nullptr;
                                            sender->get_Source(&uri);
                                            if (uri) {
                                                sender->Navigate(uri);
                                                ::CoTaskMemFree(uri);
                                            }
                                        }
                                        return S_OK;
                                    }
                                ).Get(),
                                &navToken);

                            // Navigate to the MCDeploy dashboard.
                            HRESULT hrNav = webview->Navigate(state->navigateUrl.c_str());
                            if (FAILED(hrNav)) {
                                std::cerr << "[MCDeploy Window] Navigate() returned 0x"
                                          << std::hex << hrNav << std::dec << std::endl;
                            } else {
                                std::wcout << L"[MCDeploy Window] Navigating to: "
                                           << state->navigateUrl << std::endl;
                            }
                            state->initialized = true;
                            return S_OK;
                        }
                    ).Get()
                );
                return S_OK;
            }
        ).Get()
    );

    if (FAILED(hr)) {
        std::cerr << "[MCDeploy Window] Failed to start WebView2 env creation: 0x"
                  << std::hex << hr << ". Is the WebView2 Runtime installed?"
                  << std::endl;
        ::DestroyWindow(hWnd);
        delete state;
        return false;
    }

    // Standard Win32 message loop. Blocks until WM_QUIT (posted from WM_DESTROY).
    // COM async callbacks (WebView2 env/controller/navigation) fire during
    // message dispatch, which is why CoInitializeEx above is critical.
    MSG msg = {};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    delete state;
    ::CoUninitialize();
    return true;
}

} // namespace MCDeploy

#else // !_WIN32

#include <iostream>
namespace MCDeploy {
bool runMcdeployWindow(const std::string&, std::function<void()> onClosed) {
    std::cerr << "[MCDeploy Window] Native window is Windows-only in this build.\n";
    if (onClosed) onClosed();
    return false;
}
} // namespace MCDeploy

#endif
