"""Run: python tests/integration/test_ipc_dashboard.py (port 47650 must be free)."""
import copy
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
BASE = "http://127.0.0.1:47650"
TOKEN = ""
USER32 = ctypes.WinDLL("user32", use_last_error=True)
USER32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
USER32.FindWindowW.restype = wintypes.HWND
USER32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER32.PostMessageW.restype = wintypes.BOOL
USER32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
USER32.GetWindowThreadProcessId.restype = wintypes.DWORD
KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
KERNEL32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
KERNEL32.LoadLibraryExW.restype = wintypes.HMODULE
KERNEL32.FindResourceW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR]
KERNEL32.FindResourceW.restype = wintypes.HANDLE
KERNEL32.FreeLibrary.argtypes = [wintypes.HMODULE]
KERNEL32.FreeLibrary.restype = wintypes.BOOL



def request(path, body=None, expected=200, authenticated=True):
    headers = {"Content-Type": "application/json"}
    if authenticated:
        headers["X-Marco-Token"] = TOKEN
    req = urllib.request.Request(BASE + path, headers=headers,
        data=None if body is None else json.dumps(body).encode())
    try:
        response = urllib.request.urlopen(req, timeout=5)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        assert response.status == expected, (path, response.status, response.read())
        return json.load(response)


def launch(exe):
    global TOKEN
    process = subprocess.Popen([str(exe)], cwd=exe.parent)
    for _ in range(100):
        try:
            TOKEN = request("/api/token", authenticated=False)["token"]
            return process
        except (OSError, AssertionError):
            time.sleep(0.05)
    process.terminate()
    raise AssertionError("Daemon did not start")


with socket.socket() as probe:
    assert probe.connect_ex(("127.0.0.1", 47650)) != 0, "Stop the existing daemon before this isolated test"
with tempfile.TemporaryDirectory(prefix="marco-ipc-test-") as directory:
    sandbox = Path(directory).resolve()
    assert sandbox.parent == Path(tempfile.gettempdir()).resolve()
    exe = sandbox / "marco.exe"
    shutil.copy2(ROOT / "marco.exe", exe)
    process = launch(exe)
    try:
        window = USER32.FindWindowW("CS2MsgClass", "CS2Msg")
        assert window, "Missing tray owner window"
        owner = wintypes.DWORD()
        USER32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        assert owner.value == process.pid, "Tray window belongs to another process"
        module = KERNEL32.LoadLibraryExW(str(exe), None, 2)  # LOAD_LIBRARY_AS_DATAFILE
        assert module, "Cannot load embedded resources"
        try:
            assert KERNEL32.FindResourceW(module, ctypes.cast(101, wintypes.LPCWSTR),
                                         ctypes.cast(14, wintypes.LPCWSTR)), "Missing RT_GROUP_ICON 101"
        finally:
            assert KERNEL32.FreeLibrary(module)
        request("/api/config", expected=401, authenticated=False)
        with urllib.request.urlopen(BASE + "/api/events?token=" + TOKEN, timeout=5) as events:
            assert events.readline().decode().strip() == "event: telemetry"
            initial = events.readline().decode().strip()
            assert json.loads(initial.removeprefix("data: "))["suspended"] is False
            request("/api/config", {"activeProfileIndex": 2})
            for _ in range(500):
                line = events.readline().decode().strip()
                if line == "event: state_changed":
                    changed = json.loads(events.readline().decode().strip().removeprefix("data: "))
                    # Startup/hook events may already be buffered before this mutation.
                    if changed["profile"]["name"] == "PISTOL":
                        break
            else:
                raise AssertionError("Missing state_changed SSE event")
        for suspended in (True, True, False, False):
            assert request("/api/state", {"suspended": suspended})["suspended"] is suspended
            assert request("/api/telemetry")["suspended"] is suspended
        for desired in (True, False):
            # Menu command uses the same message path as the real tray selection.
            assert USER32.PostMessageW(window, 0x111, 40002, 0)  # WM_COMMAND, ID_TRAY_TOGGLE
            deadline = time.monotonic() + 2
            while request("/api/telemetry")["suspended"] is not desired:
                assert time.monotonic() < deadline, "Tray toggle did not publish state"
                time.sleep(0.02)
        for invalid in ({}, {"suspended": "true"}, {"suspended": 1}):
            request("/api/state", invalid, expected=400)
        for index in range(1, 5):
            assert request("/api/config", {"activeProfileIndex": index})["activeBrakeProfileIndex"] == index
        cfg = request("/api/config")
        for index in range(1, 5):
            cfg["brakeProfiles"][index].update(accuracyThreshold=20 + index,
                overlapDurationUs=500 * index, brakeBiasMultiplier=1.1,
                momentumMemoryMs=30 + index)
        assert request("/api/config", cfg) == cfg
        assert request("/api/config") == cfg
        # Send a valid JSON body split across two TCP writes.
        body = json.dumps({"brakeProfiles": cfg["brakeProfiles"]}).encode()
        with socket.create_connection(("127.0.0.1", 47650), timeout=5) as client:
            header = ("POST /api/config HTTP/1.1\r\nHost: localhost\r\n"
                f"X-Marco-Token: {TOKEN}\r\nContent-Length: {len(body)}\r\n\r\n").encode()
            client.sendall(header + body[:30])
            time.sleep(0.05)
            client.sendall(body[30:])
            assert b"200 OK" in client.recv(8192)
        request("/api/config", {"activeProfileIndex": 5}, expected=400)
        request("/api/config", {"brakeProfiles": [{}]}, expected=400)
        assert request("/api/config") == cfg
        snapshot = copy.deepcopy(cfg)
        assert request("/api/safemode", {"enabled": True})["safeModeEnabled"]
        request("/api/config", {"profile_3": {"accuracyThreshold": 50}})
        assert request("/api/revert", {}) == snapshot
        assert request("/api/config") == snapshot
        assert request("/api/safemode", {"enabled": True})["safeModeEnabled"]
        assert request("/api/safemode", {"enabled": False}) == snapshot
        for index, memory in ((1, 35), (2, 25), (3, 40), (4, 25)):
            default = dict(overlapDurationUs=0, brakeBiasMultiplier=1.0,
                authorityBiasMs=0.0, aggressivenessCurve=1.0,
                momentumMemoryMs=memory, accuracyThreshold=17 if index == 3 else 34)
            cfg = request("/api/config", {f"profile_{index}": default})
            assert cfg["brakeProfiles"][index] == default
        # A half-sent HTTP client must not outlive tray shutdown or Winsock.
        with socket.create_connection(("127.0.0.1", 47650), timeout=5) as incomplete:
            incomplete.sendall(b"POST /api/config HTTP/1.1\r\n")
            assert USER32.PostMessageW(window, 0x111, 40003, 0)  # ID_TRAY_EXIT
            process.wait(timeout=10)
        assert not (sandbox / "marco.token").exists(), "Session token leaked after tray exit"
        process = launch(exe)
        assert request("/api/config")["brakeProfiles"] == cfg["brakeProfiles"], "INI roundtrip failed"
        print("PASS: embedded icon, tray toggle/exit cleanup, SSE, power, auth, profiles, fragmented POST, validation, safe mode, snapshot, reset, INI roundtrip")
    finally:
        if process.poll() is None:
            try:
                request("/api/quit", {})
                process.wait(timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                process.terminate()
                process.wait(timeout=5)
