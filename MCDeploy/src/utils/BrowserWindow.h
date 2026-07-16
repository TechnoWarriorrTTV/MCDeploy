#pragma once

// ============================================================
// MCDeploy Native Browser Window (Windows-only)
// ------------------------------------------------------------
// A minimal Win32 host for WebView2 that gives the user a
// standalone "MCDeploy" desktop app: own icon, own title,
// own taskbar entry — no Chrome/Edge branding anywhere.
// ============================================================

#include <string>
#include <functional>

namespace MCDeploy {

// Launches a native window with the given URL. Blocks the calling
// thread inside a Win32 message loop until the window is closed.
// onClosed is invoked (from the message-loop thread) right before
// the function returns so the caller can shut down the server.
//
// Returns true if the window was successfully created, false if
// WebView2 runtime is missing or the window could not be created
// (in which case the caller should fall back to launching a normal
// browser).
bool runMcdeployWindow(const std::string& url, std::function<void()> onClosed);

} // namespace MCDeploy
