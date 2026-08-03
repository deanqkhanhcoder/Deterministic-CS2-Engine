#!/usr/bin/env python3
from pathlib import Path

source = (Path(__file__).parents[2] / "src" / "core" / "input_capture.cpp").read_text(
    encoding="utf-8"
)
engine_api = (
    Path(__file__).parents[2] / "include" / "core" / "state_engine.h"
).read_text(encoding="utf-8")
bhop_source = (
    Path(__file__).parents[2] / "src" / "core" / "bhop.cpp"
).read_text(encoding="utf-8")
main_source = (
    Path(__file__).parents[2] / "src" / "core" / "main.cpp"
).read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


for signature in (
    "static LRESULT CALLBACK KeyboardProc",
    "static LRESULT CALLBACK MouseProc",
):
    body = function_body(signature)
    for forbidden in (
        "engine::HandleKey",
        "engine::OnSysKeyChange",
        "engine::OnShiftChange",
        "engine::OnSpace",
        "engine::OnLButton",
        "bhop::",
        "SendNotifyMessageW",
        "DLOG_TRACE",
        "GetAsyncKeyState",
        "std::lock_guard",
        "std::unique_lock",
    ):
        assert forbidden not in body, f"{signature} contains blocking path {forbidden}"
    if signature.endswith("KeyboardProc"):
        assert "QueueRoutedEvent" in body
    else:
        assert "QueueRoutedEvent" not in body, (
            "Mouse shooting must not enter counter-strafe routing"
        )

assert "RoutedKeyEvent::Kind::Mouse" not in source
assert "engine::OnLButton" not in source
assert "OnLButton" not in engine_api

# Timer and bhop workers must never invoke synthetic input while the hook-owner
# message thread can be waiting on the same engine/injection serialization path.
assert "BhopInjectionQueue" in bhop_source
assert "s_injectionQueue.Submit" in bhop_source
assert "WM_BHOP_INJECTION_READY" in main_source
assert "bhop::DrainInjectionRequests();" in main_source
assert "GetWindowThreadProcessId(injectionWindow, nullptr)" in bhop_source
assert "s_injectionOwnerThreadId" in bhop_source
shutdown_body = bhop_source.split("void Shutdown()", 1)[1].split(
    "void DrainInjectionRequests()", 1
)[0]
assert shutdown_body.index("s_workerThread.join()") < shutdown_body.index(
    "s_initialized.store(false"
)

print("test_hook_deferral: hook, timer, and bhop work defer to owner thread")
