#include "syscall_dispatch.h"

std::atomic<const char*> g_injection_path{"user32"};

#ifndef MARCO_SYSCALL_DISPATCH_TESTING
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#endif

namespace {
#ifdef MARCO_SYSCALL_DISPATCH_TESTING
NtUserSendInputFn s_native = nullptr;
UINT WINAPI NoDesktopInput(UINT, LPINPUT, int) { return 0; }
User32SendInputFn s_fallback = NoDesktopInput;
NtUserSendInputFn NativeSendInput() { return s_native; }
#else
struct NativeDispatch {
    HMODULE module = LoadLibraryExW(L"win32u.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    NtUserSendInputFn send = nullptr;
    NativeDispatch() {
        if (module) {
            // Copy the address without incompatible function-pointer cast warnings.
            const FARPROC address = GetProcAddress(module, "NtUserSendInput");
            static_assert(sizeof(send) == sizeof(address));
            std::memcpy(&send, &address, sizeof(send));
        }
        g_injection_path.store(send ? "ntuser" : "user32", std::memory_order_release);
    }
    ~NativeDispatch() { if (module) FreeLibrary(module); }
};

const NativeDispatch& Dispatcher() {
    // Resolve once; retain the DLL reference until shutdown, including on fallback.
    static const NativeDispatch dispatch;
    return dispatch;
}
NtUserSendInputFn NativeSendInput() { return Dispatcher().send; }
#endif
}

UINT MarcoSendInput(UINT count, LPINPUT inputs, int inputSize) {
    const auto native = NativeSendInput();
    // Never retry zero/partial sends on another path: that could duplicate input.
    if (native) return native(count, inputs, inputSize);
#ifdef MARCO_SYSCALL_DISPATCH_TESTING
    return s_fallback(count, inputs, inputSize);
#else
    return ::SendInput(count, inputs, inputSize);
#endif
}

void InitializeInjectionDispatch() {
    // Keep DLL resolution out of the first timing-sensitive burst.
    const DWORD error = GetLastError();
    (void)NativeSendInput();
    SetLastError(error);
}

#ifdef MARCO_SYSCALL_DISPATCH_TESTING
void SetInjectionDispatchForTesting(NtUserSendInputFn native, User32SendInputFn fallback) {
    s_native = native;
    s_fallback = fallback ? fallback : NoDesktopInput;
    g_injection_path.store(native ? "ntuser" : "user32", std::memory_order_release);
}
#elif !defined(MARCO_RELEASE)
int BenchmarkInjectionPaths() {
    const auto& dispatch = Dispatcher();
    if (!dispatch.send) {
        std::puts("[INJ] module=win32u.dll resolve=unavailable path=user32");
        return 1;
    }
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(dispatch.module, modulePath, MAX_PATH);
    const auto offset = reinterpret_cast<ULONG_PTR>(dispatch.send) -
                        reinterpret_cast<ULONG_PTR>(dispatch.module);
    std::printf("[INJ] module=%ls offset=0x%llx workload=zero-input\n", modulePath,
                static_cast<unsigned long long>(offset));
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 1;
    constexpr int samples = 9;
    constexpr int calls = 20000;
    std::array<double, samples> nativeNs{}, user32Ns{};
    const auto measure = [&](User32SendInputFn send) {
        LARGE_INTEGER start{}, end{};
        QueryPerformanceCounter(&start);
        for (int i = 0; i < calls; ++i) (void)send(0, nullptr, sizeof(INPUT));
        QueryPerformanceCounter(&end);
        return static_cast<double>(end.QuadPart - start.QuadPart) * 1e9 /
               static_cast<double>(frequency.QuadPart) / calls;
    };
    for (int i = 0; i < 1000; ++i) {
        (void)dispatch.send(0, nullptr, sizeof(INPUT));
        (void)::SendInput(0, nullptr, sizeof(INPUT));
    }
    // Alternate order; report median batch means to limit scheduling outliers.
    for (int i = 0; i < samples; ++i) {
        if (i % 2 == 0) {
            nativeNs[i] = measure(dispatch.send);
            user32Ns[i] = measure(::SendInput);
        } else {
            user32Ns[i] = measure(::SendInput);
            nativeNs[i] = measure(dispatch.send);
        }
    }
    std::sort(nativeNs.begin(), nativeNs.end());
    std::sort(user32Ns.begin(), user32Ns.end());
    std::printf("[INJ] path=syscall avg_ns=%.1f\n", nativeNs[samples / 2]);
    std::printf("[INJ] path=user32 avg_ns=%.1f\n", user32Ns[samples / 2]);
    std::puts("[INJ] zero-input dispatch only; does not measure delivered-key latency");
    return 0;
}
#endif
