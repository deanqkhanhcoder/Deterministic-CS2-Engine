#!/usr/bin/env python3
"""Fail closed if a safe-test PE imports real desktop input APIs."""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

IMPORT_SYMBOL = re.compile(
    r"^\s*(?:[0-9a-f]+\s+)?(sendinput|keybd_event|mouse_event)\s*$",
    re.IGNORECASE,
)


def forbidden_symbols(import_dump: str) -> list[str]:
    return sorted({
        match.group(1).lower()
        for line in import_dump.splitlines()
        if (match := IMPORT_SYMBOL.match(line))
    })


def _u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("truncated PE uint16")
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError("truncated PE uint32")
    return struct.unpack_from("<I", data, offset)[0]


def inspect_imports(executable: Path) -> str:
    data = executable.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise SystemExit(f"invalid PE DOS header: {executable}")

    try:
        pe_offset = _u32(data, 0x3C)
        if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("invalid PE signature")

        section_count = _u16(data, pe_offset + 6)
        optional_size = _u16(data, pe_offset + 20)
        optional_offset = pe_offset + 24
        section_offset = optional_offset + optional_size
        if section_count == 0 or section_count > 96:
            raise ValueError("invalid PE section count")
        if section_offset + section_count * 40 > len(data):
            raise ValueError("truncated PE section table")

        magic = _u16(data, optional_offset)
        if magic == 0x10B:  # PE32
            directory_offset = optional_offset + 96
            thunk_size = 4
            ordinal_mask = 1 << 31
        elif magic == 0x20B:  # PE32+
            directory_offset = optional_offset + 112
            thunk_size = 8
            ordinal_mask = 1 << 63
        else:
            raise ValueError(f"unsupported PE optional-header magic 0x{magic:x}")
        if directory_offset + 16 > optional_offset + optional_size:
            raise ValueError("missing PE import directory")

        import_rva = _u32(data, directory_offset + 8)
        import_size = _u32(data, directory_offset + 12)
        if import_rva == 0 or import_size == 0:
            return ""

        size_of_headers = _u32(data, optional_offset + 60)
        sections: list[tuple[int, int, int, int]] = []
        for index in range(section_count):
            offset = section_offset + index * 40
            virtual_size = _u32(data, offset + 8)
            virtual_address = _u32(data, offset + 12)
            raw_size = _u32(data, offset + 16)
            raw_offset = _u32(data, offset + 20)
            sections.append((virtual_address, virtual_size, raw_offset, raw_size))

        def rva_to_offset(rva: int, needed: int = 1) -> int:
            if rva < size_of_headers and rva + needed <= len(data):
                return rva
            for va, virtual_size, raw_offset, raw_size in sections:
                span = max(virtual_size, raw_size)
                if va <= rva and rva + needed <= va + span:
                    delta = rva - va
                    if delta + needed > raw_size:
                        break
                    result = raw_offset + delta
                    if result + needed <= len(data):
                        return result
                    break
            raise ValueError(f"unmapped PE RVA 0x{rva:x}")

        imports: list[str] = []
        descriptor_offset = rva_to_offset(import_rva, 20)
        descriptor_limit = min(4096, max(1, import_size // 20 + 1))
        for descriptor_index in range(descriptor_limit):
            offset = descriptor_offset + descriptor_index * 20
            if offset + 20 > len(data):
                raise ValueError("truncated PE import descriptor")
            original_thunk = _u32(data, offset)
            name_rva = _u32(data, offset + 12)
            first_thunk = _u32(data, offset + 16)
            if original_thunk == 0 and name_rva == 0 and first_thunk == 0:
                break

            thunk_rva = original_thunk or first_thunk
            thunk_offset = rva_to_offset(thunk_rva, thunk_size)
            for thunk_index in range(65536):
                entry_offset = thunk_offset + thunk_index * thunk_size
                if entry_offset + thunk_size > len(data):
                    raise ValueError("truncated PE thunk table")
                if thunk_size == 8:
                    value = struct.unpack_from("<Q", data, entry_offset)[0]
                else:
                    value = _u32(data, entry_offset)
                if value == 0:
                    break
                if value & ordinal_mask:
                    continue
                name_offset = rva_to_offset(value, 3) + 2  # skip hint
                end = data.find(b"\0", name_offset, min(len(data), name_offset + 4096))
                if end < 0:
                    raise ValueError("unterminated PE import name")
                imports.append(data[name_offset:end].decode("ascii", errors="strict"))
            else:
                raise ValueError("PE thunk table exceeds safety bound")
        else:
            raise ValueError("PE import descriptor table exceeds safety bound")

        return "\n".join(imports)
    except (UnicodeDecodeError, ValueError, struct.error) as exc:
        raise SystemExit(f"invalid PE import table in {executable}: {exc}") from exc


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} <test-executable>")
    executable = Path(sys.argv[1])
    if not executable.is_file():
        raise SystemExit(f"test executable not found: {executable}")

    found = forbidden_symbols(inspect_imports(executable))
    if found:
        raise SystemExit(
            f"unsafe desktop-input import(s) in {executable}: {', '.join(found)}"
        )
    print(f"safe PE imports: {executable.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
