#!/usr/bin/env python3
"""Stress fake/pure test binaries on an un-switched Win32 desktop."""

from __future__ import annotations

import argparse
import ctypes
import os
import subprocess
import sys
import time
import uuid
from pathlib import Path

from check_no_input_imports import forbidden_symbols, inspect_imports

SAFE_EXECUTABLES = (
    "marco_test_injection.exe",
    "marco_test_physics.exe",
    "marco_test_hybrid.exe",
    "marco_test_debug_logger.exe",
    "marco_test_runtime_config.exe",
    "marco_test_target_publication.exe",
    "marco_test_bhop_injection_gate.exe",
    "marco_test_bhop_injection_queue.exe",
    "marco_test_timing_lifecycle.exe",
    "marco_test_message_pump.exe",
    "marco_test_engine_dispatch_gate.exe",
    "marco_test_diagnostic_rings.exe",
    "marco_test_routed_input_queue.exe",
    "marco_test_diagonal_engine_stress.exe",
    "marco_test_config_io_atomic.exe",
)


def current_desktop_name() -> str:
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    user32.GetThreadDesktop.argtypes = [ctypes.c_ulong]
    user32.GetThreadDesktop.restype = ctypes.c_void_p
    user32.GetUserObjectInformationW.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_ulong,
        ctypes.POINTER(ctypes.c_ulong),
    ]
    user32.GetUserObjectInformationW.restype = ctypes.c_int
    kernel32.GetCurrentThreadId.restype = ctypes.c_ulong

    handle = user32.GetThreadDesktop(kernel32.GetCurrentThreadId())
    if not handle:
        error = ctypes.get_last_error()
        raise OSError(error, f"GetThreadDesktop failed: {ctypes.FormatError(error)}")

    needed = ctypes.c_ulong()
    user32.GetUserObjectInformationW(handle, 2, None, 0, ctypes.byref(needed))
    if needed.value == 0:
        error = ctypes.get_last_error()
        raise OSError(
            error,
            f"GetUserObjectInformationW(size) failed: {ctypes.FormatError(error)}",
        )
    buffer = ctypes.create_unicode_buffer(
        (needed.value // ctypes.sizeof(ctypes.c_wchar)) + 1
    )
    if not user32.GetUserObjectInformationW(
        handle, 2, buffer, ctypes.sizeof(buffer), ctypes.byref(needed)
    ):
        error = ctypes.get_last_error()
        raise OSError(
            error,
            f"GetUserObjectInformationW(name) failed: {ctypes.FormatError(error)}",
        )
    return buffer.value


def isolated_child(expected_desktop: str, executable: Path) -> int:
    actual_desktop = current_desktop_name()
    if actual_desktop.casefold() != expected_desktop.casefold():
        print(
            f"desktop verification failed: expected {expected_desktop}, "
            f"got {actual_desktop}",
            file=sys.stderr,
        )
        return 125
    environment = os.environ.copy()
    environment["MARCO_EXPECTED_DESKTOP"] = expected_desktop
    result = subprocess.run(
        [str(executable)],
        check=False,
        env=environment,
        creationflags=(
            subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS
        ),
    )
    return result.returncode


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_longlong),
        ("PerJobUserTimeLimit", ctypes.c_longlong),
        ("LimitFlags", ctypes.c_ulong),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", ctypes.c_ulong),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", ctypes.c_ulong),
        ("SchedulingClass", ctypes.c_ulong),
    ]


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("ReadOperationCount", ctypes.c_ulonglong),
        ("WriteOperationCount", ctypes.c_ulonglong),
        ("OtherOperationCount", ctypes.c_ulonglong),
        ("ReadTransferCount", ctypes.c_ulonglong),
        ("WriteTransferCount", ctypes.c_ulonglong),
        ("OtherTransferCount", ctypes.c_ulonglong),
    ]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION),
        ("IoInfo", IO_COUNTERS),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


class WindowsDesktopRunner:
    """Launch processes on a private desktop that is never made interactive."""

    DESKTOP_ACCESS = 0x0001 | 0x0002 | 0x0080
    CREATE_SUSPENDED = 0x00000004
    CREATE_NO_WINDOW = 0x08000000
    BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
    WAIT_OBJECT_0 = 0
    WAIT_TIMEOUT = 258
    INFINITE = 0xFFFFFFFF
    UOI_NAME = 2
    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000

    class STARTUPINFOW(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("lpReserved", ctypes.c_wchar_p),
            ("lpDesktop", ctypes.c_wchar_p),
            ("lpTitle", ctypes.c_wchar_p),
            ("dwX", ctypes.c_ulong),
            ("dwY", ctypes.c_ulong),
            ("dwXSize", ctypes.c_ulong),
            ("dwYSize", ctypes.c_ulong),
            ("dwXCountChars", ctypes.c_ulong),
            ("dwYCountChars", ctypes.c_ulong),
            ("dwFillAttribute", ctypes.c_ulong),
            ("dwFlags", ctypes.c_ulong),
            ("wShowWindow", ctypes.c_ushort),
            ("cbReserved2", ctypes.c_ushort),
            ("lpReserved2", ctypes.POINTER(ctypes.c_ubyte)),
            ("hStdInput", ctypes.c_void_p),
            ("hStdOutput", ctypes.c_void_p),
            ("hStdError", ctypes.c_void_p),
        ]

    class PROCESS_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("hProcess", ctypes.c_void_p),
            ("hThread", ctypes.c_void_p),
            ("dwProcessId", ctypes.c_ulong),
            ("dwThreadId", ctypes.c_ulong),
        ]

    def __init__(self) -> None:
        if sys.platform != "win32":
            raise RuntimeError("private Win32 desktop isolation requires Windows")

        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.name = f"MarcoSafeStress-{uuid.uuid4().hex}"

        self.user32.CreateDesktopW.argtypes = [
            ctypes.c_wchar_p,
            ctypes.c_wchar_p,
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.c_void_p,
        ]
        self.user32.CreateDesktopW.restype = ctypes.c_void_p
        self.user32.GetThreadDesktop.argtypes = [ctypes.c_ulong]
        self.user32.GetThreadDesktop.restype = ctypes.c_void_p
        self.user32.GetUserObjectInformationW.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
        ]
        self.user32.GetUserObjectInformationW.restype = ctypes.c_int
        self.user32.CloseDesktop.argtypes = [ctypes.c_void_p]
        self.user32.CloseDesktop.restype = ctypes.c_int

        self.kernel32.CreateProcessW.argtypes = [
            ctypes.c_wchar_p,
            ctypes.c_wchar_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_ulong,
            ctypes.c_void_p,
            ctypes.c_wchar_p,
            ctypes.POINTER(self.STARTUPINFOW),
            ctypes.POINTER(self.PROCESS_INFORMATION),
        ]
        self.kernel32.CreateProcessW.restype = ctypes.c_int
        self.kernel32.ResumeThread.argtypes = [ctypes.c_void_p]
        self.kernel32.ResumeThread.restype = ctypes.c_ulong
        self.kernel32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.kernel32.WaitForSingleObject.restype = ctypes.c_ulong
        self.kernel32.GetExitCodeProcess.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_ulong),
        ]
        self.kernel32.GetExitCodeProcess.restype = ctypes.c_int
        self.kernel32.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        self.kernel32.TerminateProcess.restype = ctypes.c_int
        self.kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        self.kernel32.CloseHandle.restype = ctypes.c_int
        self.kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
        self.kernel32.CreateJobObjectW.restype = ctypes.c_void_p
        self.kernel32.SetInformationJobObject.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_ulong,
        ]
        self.kernel32.SetInformationJobObject.restype = ctypes.c_int
        self.kernel32.AssignProcessToJobObject.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
        ]
        self.kernel32.AssignProcessToJobObject.restype = ctypes.c_int

        self.handle = self.user32.CreateDesktopW(
            self.name, None, None, 0, self.DESKTOP_ACCESS, None
        )
        if not self.handle:
            self._raise_last_error("CreateDesktopW")

        self.job = self.kernel32.CreateJobObjectW(None, None)
        if not self.job:
            self.user32.CloseDesktop(self.handle)
            self.handle = None
            self._raise_last_error("CreateJobObjectW")

        limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        limits.BasicLimitInformation.LimitFlags = self.JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.kernel32.SetInformationJobObject(
            self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)
        ):
            self.kernel32.CloseHandle(self.job)
            self.job = None
            self.user32.CloseDesktop(self.handle)
            self.handle = None
            self._raise_last_error("SetInformationJobObject")

    @staticmethod
    def _raise_last_error(operation: str) -> None:
        error = ctypes.get_last_error()
        raise OSError(error, f"{operation} failed: {ctypes.FormatError(error)}")

    def run(self, executable: Path, timeout_seconds: float) -> int:
        startup = self.STARTUPINFOW()
        startup.cb = ctypes.sizeof(startup)
        startup.lpDesktop = self.name
        process = self.PROCESS_INFORMATION()
        command_line = ctypes.create_unicode_buffer(
            subprocess.list2cmdline(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--isolated-child",
                    "--desktop-name",
                    self.name,
                    "--executable",
                    str(executable.resolve()),
                ]
            )
        )
        flags = (
            self.CREATE_SUSPENDED
            | self.CREATE_NO_WINDOW
            | self.BELOW_NORMAL_PRIORITY_CLASS
        )

        if not self.kernel32.CreateProcessW(
            None,
            command_line,
            None,
            None,
            False,
            flags,
            None,
            str(executable.parent.resolve()),
            ctypes.byref(startup),
            ctypes.byref(process),
        ):
            self._raise_last_error(f"CreateProcessW({executable.name})")

        try:
            if not self.kernel32.AssignProcessToJobObject(self.job, process.hProcess):
                self.kernel32.TerminateProcess(process.hProcess, 126)
                self._raise_last_error("AssignProcessToJobObject")
            if self.kernel32.ResumeThread(process.hThread) == self.INFINITE:
                self.kernel32.TerminateProcess(process.hProcess, 126)
                self._raise_last_error("ResumeThread")

            timeout_ms = max(1, min(int(timeout_seconds * 1000), 0xFFFFFFFE))
            wait_result = self.kernel32.WaitForSingleObject(process.hProcess, timeout_ms)
            if wait_result == self.WAIT_TIMEOUT:
                self.kernel32.TerminateProcess(process.hProcess, 124)
                self.kernel32.WaitForSingleObject(process.hProcess, 5000)
                raise TimeoutError(
                    f"{executable.name} exceeded {timeout_seconds:.3f}s timeout"
                )
            if wait_result != self.WAIT_OBJECT_0:
                self._raise_last_error("WaitForSingleObject")

            exit_code = ctypes.c_ulong()
            if not self.kernel32.GetExitCodeProcess(
                process.hProcess, ctypes.byref(exit_code)
            ):
                self._raise_last_error("GetExitCodeProcess")
            return int(exit_code.value)
        finally:
            self.kernel32.CloseHandle(process.hThread)
            self.kernel32.CloseHandle(process.hProcess)

    def close(self) -> None:
        if getattr(self, "job", None):
            if not self.kernel32.CloseHandle(self.job):
                self._raise_last_error("CloseHandle(job)")
            self.job = None
        if self.handle:
            if not self.user32.CloseDesktop(self.handle):
                self._raise_last_error("CloseDesktop")
            self.handle = None

    def __enter__(self) -> WindowsDesktopRunner:
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=float, default=60.0)
    parser.add_argument("--isolated-child", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--desktop-name", help=argparse.SUPPRESS)
    parser.add_argument("--executable", type=Path, help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.isolated_child:
        if not args.desktop_name or args.executable is None:
            raise SystemExit("isolated child arguments are incomplete")
        return isolated_child(args.desktop_name, args.executable)
    if args.build_dir is None:
        raise SystemExit("--build-dir is required")
    if args.iterations < 1:
        raise SystemExit("--iterations must be positive")
    if args.timeout_seconds <= 0:
        raise SystemExit("--timeout-seconds must be positive")

    executables = [args.build_dir / name for name in SAFE_EXECUTABLES]
    for executable in executables:
        if not executable.is_file():
            raise SystemExit(f"safe test executable not found: {executable}")
        imports = inspect_imports(executable)
        found = forbidden_symbols(imports)
        if found:
            raise SystemExit(
                f"refusing isolated stress: {executable.name} imports {', '.join(found)}"
            )

    completed = 0
    started = time.perf_counter()
    with WindowsDesktopRunner() as desktop:
        desktop_name = desktop.name
        for _ in range(args.iterations):
            for executable in executables:
                exit_code = desktop.run(executable, args.timeout_seconds)
                if exit_code != 0:
                    raise SystemExit(
                        f"{executable.name} failed with exit {exit_code} "
                        f"on isolated desktop {desktop_name}"
                    )
                completed += 1

    elapsed = time.perf_counter() - started
    print(
        f"isolated-desktop fake stress passed: {completed}/{completed} runs; "
        f"forbidden input imports: 0/{len(executables)}; "
        f"desktop={desktop_name}; switched=false; elapsed={elapsed:.3f}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
