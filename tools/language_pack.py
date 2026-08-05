#!/usr/bin/env python3
"""Validate or scaffold external language packs for ACGC-PC-Port.

This tool never reads or writes a disc image. It validates already-extracted
language resources placed in languages/<code>/aram.
"""

from __future__ import annotations

import argparse
import configparser
import re
import struct
from pathlib import Path

PAIRS = (
    ("mail_data.bin", "mail_data_table.bin"),
    ("maila_data.bin", "maila_data_table.bin"),
    ("mailb_data.bin", "mailb_data_table.bin"),
    ("mailc_data.bin", "mailc_data_table.bin"),
    ("ps_data.bin", "ps_data_table.bin"),
    ("psz_data.bin", "psz_data_table.bin"),
    ("select_data.bin", "select_data_table.bin"),
    ("string_data.bin", "string_data_table.bin"),
    ("superz_data.bin", "superz_data_table.bin"),
    ("super_data.bin", "super_data_table.bin"),
    ("message_data.bin", "message_data_table.bin"),
)
SINGLES = ("npc_name_str_table.bin",)
CODE_RE = re.compile(r"^[A-Za-z0-9_-]{1,31}$")
MAX_FILE = 64 * 1024 * 1024


def read_table(path: Path, data_size: int) -> tuple[int, int]:
    raw = path.read_bytes()
    if not raw or len(raw) % 4:
        raise ValueError("table size must be a non-zero multiple of 4")
    previous = 0
    active = 0
    for index, (end,) in enumerate(struct.iter_unpack(">I", raw)):
        if end == 0:
            continue
        if end < previous:
            raise ValueError(f"entry {index}: end offset 0x{end:X} goes backwards")
        if end > data_size:
            raise ValueError(
                f"entry {index}: end offset 0x{end:X} exceeds data size 0x{data_size:X}"
            )
        previous = end
        active += 1
    if active == 0:
        raise ValueError("table contains no active entries")
    return len(raw) // 4, active


def validate(pack: Path) -> int:
    errors: list[str] = []
    warnings: list[str] = []
    manifest = pack / "manifest.ini"
    aram = pack / "aram"

    if not manifest.is_file():
        errors.append("missing manifest.ini")
    else:
        cfg = configparser.ConfigParser()
        try:
            cfg.read(manifest, encoding="utf-8")
            code = cfg.get("Language", "code", fallback=pack.name)
            if not CODE_RE.fullmatch(code):
                errors.append(f"invalid language code: {code!r}")
        except (configparser.Error, OSError) as exc:
            errors.append(f"manifest.ini: {exc}")

    if not aram.is_dir():
        errors.append("missing aram/ directory")
    else:
        loaded_pairs = 0
        for data_name, table_name in PAIRS:
            data_path = aram / data_name
            table_path = aram / table_name
            has_data = data_path.is_file()
            has_table = table_path.is_file()
            if has_data != has_table:
                errors.append(f"{data_name} and {table_name} must be supplied together")
                continue
            if not has_data:
                continue
            data_size = data_path.stat().st_size
            if data_size <= 0 or data_size > MAX_FILE:
                errors.append(f"{data_name}: invalid size {data_size}")
                continue
            try:
                total, active = read_table(table_path, data_size)
            except ValueError as exc:
                errors.append(f"{table_name}: {exc}")
                continue
            loaded_pairs += 1
            print(
                f"OK  {data_name} + {table_name}: "
                f"{data_size} bytes, {active}/{total} active IDs"
            )

        for name in SINGLES:
            path = aram / name
            if path.is_file():
                size = path.stat().st_size
                if size <= 0 or size > MAX_FILE:
                    errors.append(f"{name}: invalid size {size}")
                else:
                    print(f"OK  {name}: {size} bytes")

        if loaded_pairs == 0 and not any((aram / n).is_file() for n in SINGLES):
            warnings.append("pack has no usable language resources yet")

    for warning in warnings:
        print(f"WARN {warning}")
    for error in errors:
        print(f"ERROR {error}")
    if errors:
        print(f"Validation failed with {len(errors)} error(s).")
        return 1
    print("Language pack is structurally valid.")
    return 0


def scaffold(root: Path, code: str, name: str) -> int:
    if not CODE_RE.fullmatch(code):
        raise SystemExit("code may contain only letters, numbers, '-' and '_', max 31 chars")
    pack = root / code
    (pack / "aram").mkdir(parents=True, exist_ok=True)
    manifest = pack / "manifest.ini"
    if not manifest.exists():
        manifest.write_text(
            "[Language]\n"
            f"code = {code}\n"
            f"name = {name}\n"
            "version = 1\n"
            "source = user-owned game resources or original translation\n"
            "target = GAFE01_00,GAFU01_00\n",
            encoding="utf-8",
        )
    print(f"Created {pack}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    v = sub.add_parser("validate", help="validate one language pack directory")
    v.add_argument("pack", type=Path)
    c = sub.add_parser("create", help="create an empty language pack scaffold")
    c.add_argument("code")
    c.add_argument("name")
    c.add_argument("--root", type=Path, default=Path("languages"))
    args = parser.parse_args()
    if args.command == "validate":
        return validate(args.pack)
    return scaffold(args.root, args.code, args.name)


if __name__ == "__main__":
    raise SystemExit(main())
