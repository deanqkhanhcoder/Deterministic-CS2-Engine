#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Local IPC Server (Headless Daemon Layer)         ║
// ║  Zero-jitter Winsock HTTP/SSE daemon for Web UI communication       ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>
#include <string>
#include <cstdint>

namespace ipc {

// Start the local loopback IPC server on 127.0.0.1:<port> (default 47650).
// Generates ./marco.token for localhost token authentication.
bool StartServer(uint16_t port = 47650, HWND msgHwnd = nullptr);

// Stop the IPC server and cleanup tokens.
void StopServer();

// Notify connected SSE stream subscribers of an engine state change.
void NotifyStateChanged();

// Get the active session authentication token.
std::string GetAuthToken();

// Check if the IPC server is currently running.
bool IsRunning();

} // namespace ipc
