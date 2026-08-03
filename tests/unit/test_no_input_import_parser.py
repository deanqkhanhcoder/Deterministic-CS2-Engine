#!/usr/bin/env python3
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

module_path = Path(__file__).parents[2] / "scripts" / "test" / "check_no_input_imports.py"
spec = spec_from_file_location("check_no_input_imports", module_path)
assert spec and spec.loader
module = module_from_spec(spec)
spec.loader.exec_module(module)

assert module.forbidden_symbols("C:/tmp/sendinput-safe-test.exe") == []
assert module.forbidden_symbols("0000abcd SendInput\n keybd_event\n") == [
    "keybd_event",
    "sendinput",
]
assert module.forbidden_symbols("DLL Name: USER32.dll\nmouse_eventual\n") == []
print("test_no_input_import_parser: exact symbols only")
