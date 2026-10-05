#pragma once
#include <windows.h>
#include <atomic>

// Published once at resolution; telemetry reads without invoking the dispatcher.
extern std::atomic<const char*> g_injection_path;

// The native export returns an inserted-event count, not an NTSTATUS.
using NtUserSendInputFn = UINT (NTAPI *)(UINT, LPINPUT, int);
using User32SendInputFn = UINT (WINAPI *)(UINT, LPINPUT, int);

UINT MarcoSendInput(UINT count, LPINPUT inputs, int inputSize);
void InitializeInjectionDispatch();

#if !defined(MARCO_RELEASE) && !defined(MARCO_SYSCALL_DISPATCH_TESTING)
int BenchmarkInjectionPaths(); // Zero-input calls only; no desktop events.
#endif

#ifdef MARCO_SYSCALL_DISPATCH_TESTING
// Test setup only: install fakes before starting any dispatch threads.
void SetInjectionDispatchForTesting(NtUserSendInputFn native, User32SendInputFn fallback);
#endif
