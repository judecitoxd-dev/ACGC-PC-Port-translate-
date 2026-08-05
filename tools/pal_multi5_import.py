#!/usr/bin/env python3
"""Convert user-extracted Animal Crossing PAL Multi5 BMG banks to PC language packs.

This tool does not contain copyrighted text. It reads resources extracted from the
user's own GAFP01 disc and converts them to the GAFE01 PC-port resource layout.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

# Keep synchronized with tools/msg_tool.py / include/m_font.h.
from msg_tool import CONT_SIZES, COMMANDS

CUSTOM_GRAMMAR_CMD = COMMANDS.index("AGBDUMMY0")
CMD_SETSEL = [
    COMMANDS.index("SETSELSTR2"), COMMANDS.index("SETSELSTR3"),
    COMMANDS.index("SETSELSTR4"), COMMANDS.index("SETSELSTR5"),
    COMMANDS.index("SETSELSTR6"),
]
CMD_SETNEXT_RND_SECTION = COMMANDS.index("SETNEXTMSGRNDSECTION")
CMD_MALE_FEMALE = COMMANDS.index("MALEFEMALECHK")
CMD_SET_FORCE_MSG = COMMANDS.index("SETFORCEMSG")
CMD_SETNEXT_RND2 = COMMANDS.index("SETNEXTMSGRND2")
CMD_SETNEXT_RND3 = COMMANDS.index("SETNEXTMSGRND3")
CMD_SETNEXT_RND4 = COMMANDS.index("SETNEXTMSGRND4")

BANKS = {
    "mail": ("mail.bin", "mail_data.bin", "mail_data_table.bin", 1500),
    "maila": ("maila.bin", "maila_data.bin", "maila_data_table.bin", 750),
    "mailb": ("mailb.bin", "mailb_data.bin", "mailb_data_table.bin", 750),
    "mailc": ("mailc.bin", "mailc_data.bin", "mailc_data_table.bin", 750),
    "ps": ("ps.bin", "ps_data.bin", "ps_data_table.bin", 1250),
    "psz": ("psz.bin", "psz_data.bin", "psz_data_table.bin", 750),
    "select": ("select.bin", "select_data.bin", "select_data_table.bin", 750),
    "string": ("string.bin", "string_data.bin", "string_data_table.bin", 2500),
    "super": ("super.bin", "super_data.bin", "super_data_table.bin", 1250),
    "superz": ("superz.bin", "superz_data.bin", "superz_data_table.bin", 750),
    "message": ("msg.bin", "message_data.bin", "message_data_table.bin", 17500),
}

LANGS = {
    "en-pal": ("Eng", "English (Europe)"),
    "fr": ("Frn", "Français"),
    "de": ("Gmn", "Deutsch"),
    "it": ("Itl", "Italiano"),
    "es": ("Spn", "Español"),
}

ITEM_NAME_SYMBOLS = {
    "itemName_paper": 0x1000,
    "itemName_money": 0x40,
    "itemName_tool": 0x5C0,
    "itemName_fish": 0x280,
    "itemName_cloth": 0xFF0,
    "itemName_etc": 0x310,
    "itemName_carpet": 0x430,
    "itemName_wall": 0x430,
    "itemName_fruit": 0x80,
    "itemName_plant": 0xB0,
    "itemName_minidisk": 0x370,
    "itemName_dummy": 0x100,
    "itemName_ticket": 0x600,
    "itemName_insect": 0x2D0,
    "itemName_hukubukuro": 0x20,
    "itemName_kabu": 0x40,
    "ftrName_table": 0x4000,
    "ftrName2_table": 0xF20,
}

GRAMMAR_SYMBOLS = {
    "artInfo_Paper": 0x300,
    "artInfo_Tool": 0x114,
    "artInfo_Fish": 0x78,
    "artInfo_Cloth": 0x2FD,
    "artInfo_Carpet": 0xC9,
    "artInfo_Wall": 0xC9,
    "artInfo_Fruit": 0x18,
    "artInfo_Plant": 0x21,
    "artInfo_MiniDisk": 0xA5,
    "artInfo_Diary": 0x30,
    "artInfo_Insect": 0x87,
    "artInfo_Money": 0x0C,
    "artInfo_Etc": 0x93,
    "artInfo_Ticket": 0x120,
    "artInfo_Hukubukuro": 0x06,
    "artInfo_Kabu": 0x0C,
    "ftrArt": 0xED6,
}

@dataclass(frozen=True)
class Token:
    pos: int
    raw: bytes
    control: bool


def parse_bmg(path: Path) -> list[bytes]:
    blob = path.read_bytes()
    inf = blob.find(b"INF1", 0x20)
    dat = blob.find(b"DAT1", inf + 8)
    if inf < 0 or dat < 0:
        raise ValueError(f"{path}: missing INF1/DAT1")
    count, entry_size = struct.unpack_from(">HH", blob, inf + 8)
    if entry_size < 4:
        raise ValueError(f"{path}: invalid INF1 entry size {entry_size}")
    offsets = [struct.unpack_from(">I", blob, inf + 16 + i * entry_size)[0] for i in range(count)]
    payload = blob[dat + 8 :]
    entries: list[bytes] = []
    # BMG offsets are nondecreasing. Duplicate offsets represent shared/empty
    # entries, so find the next strictly greater offset in one reverse pass.
    next_greater = [len(payload)] * len(offsets)
    next_value = len(payload)
    for idx in range(len(offsets) - 1, -1, -1):
        start = offsets[idx]
        next_greater[idx] = next_value
        if idx == 0 or offsets[idx - 1] < start:
            next_value = start
    for idx, start in enumerate(offsets):
        end = next_greater[idx]
        if start > len(payload) or end > len(payload) or end < start:
            raise ValueError(f"{path}: invalid entry {idx} range")
        entries.append(payload[start:end])
    return entries


def parse_us_bank(data_path: Path, table_path: Path) -> list[bytes]:
    data = data_path.read_bytes()
    table = table_path.read_bytes()
    if len(table) % 4:
        raise ValueError(f"{table_path}: table not divisible by four")
    entries: list[bytes] = []
    previous = 0
    for i in range(0, len(table), 4):
        end = struct.unpack_from(">I", table, i)[0]
        if end == 0:
            entries.append(b"")
            continue
        if end < previous or end > len(data):
            raise ValueError(f"{table_path}: invalid table entry {i // 4}")
        entries.append(data[previous:end])
        previous = end
    return entries


def pal_tokens(entry: bytes) -> list[Token]:
    out: list[Token] = []
    i = 0
    text_pos = 0
    while i < len(entry):
        if entry[i] == 0x80 and i + 1 < len(entry):
            size = entry[i + 1]
            if size < 5 or i + size > len(entry):
                raise ValueError(f"bad PAL control at {i}: {entry[i:i+16].hex()}")
            out.append(Token(text_pos, entry[i : i + size], True))
            i += size
        else:
            j = i
            while j < len(entry) and entry[j] != 0x80:
                j += 1
            raw = entry[i:j]
            out.append(Token(text_pos, raw, False))
            text_pos += len(raw)
            i = j
    return out


def us_tokens(entry: bytes) -> list[Token]:
    out: list[Token] = []
    i = 0
    text_pos = 0
    while i < len(entry):
        if entry[i] == 0x7F and i + 1 < len(entry):
            cmd = entry[i + 1]
            size = CONT_SIZES[cmd] if cmd < len(CONT_SIZES) else 2
            if i + size > len(entry):
                raise ValueError(f"bad USA control at {i}: {entry[i:i+16].hex()}")
            out.append(Token(text_pos, entry[i : i + size], True))
            i += size
        else:
            j = i
            while j < len(entry) and entry[j] != 0x7F:
                j += 1
            raw = entry[i:j]
            out.append(Token(text_pos, raw, False))
            text_pos += len(raw)
            i = j
    return out


def plain(tokens: Iterable[Token]) -> bytes:
    return b"".join(t.raw for t in tokens if not t.control)


def learn_exact_map(pal_banks: dict[str, list[bytes]], us_banks: dict[str, list[bytes]]) -> dict[bytes, bytes]:
    votes: dict[bytes, collections.Counter[bytes]] = collections.defaultdict(collections.Counter)
    for bank, pentries in pal_banks.items():
        uentries = us_banks[bank]
        for idx, pentry in enumerate(pentries[: len(uentries)]):
            uentry = uentries[idx]
            if not uentry:
                continue
            pt = pal_tokens(pentry)
            ut = us_tokens(uentry)
            if plain(pt) != plain(ut):
                continue
            pby: dict[int, list[bytes]] = collections.defaultdict(list)
            uby: dict[int, list[bytes]] = collections.defaultdict(list)
            for t in pt:
                if t.control:
                    pby[t.pos].append(t.raw)
            for t in ut:
                if t.control:
                    uby[t.pos].append(t.raw)
            for pos, pcs in pby.items():
                ucs = uby.get(pos, [])
                if len(pcs) == len(ucs):
                    for pc, uc in zip(pcs, ucs):
                        votes[pc][uc] += 1
    result: dict[bytes, bytes] = {}
    for pc, counter in votes.items():
        best, count = counter.most_common(1)[0]
        # Ambiguous controls are handled structurally instead of trusting one sample.
        if count >= 1 and (len(counter) == 1 or count >= counter.most_common(2)[1][1] * 3):
            result[pc] = best
    return result


def ctrl(cmd: int, args: bytes = b"") -> bytes:
    size = CONT_SIZES[cmd]
    body = bytes([0x7F, cmd]) + args
    if len(body) != size:
        raise ValueError(f"command {cmd}/{COMMANDS[cmd]} expects {size}, got {len(body)}")
    return body


def custom_grammar(raw: bytes) -> bytes:
    selector = raw[4]
    payload = raw[5:]
    # String-bank grammar metadata is a fixed three-byte record, not a
    # visible alternative. Preserve it in a custom no-output control.
    if selector == 0x0A and len(raw) == 8:
        return bytes([0x7F, CUSTOM_GRAMMAR_CMD, 8, selector, 0]) + raw[5:8]
    if payload:
        alternatives = payload.split(b"\xFE")
        lengths = bytes(len(x) for x in alternatives)
        total = 5 + len(lengths) + sum(len(x) for x in alternatives)
        if total > 255 or any(len(x) > 255 for x in alternatives):
            return alternatives[0]
        return bytes([0x7F, CUSTOM_GRAMMAR_CMD, total, selector, len(alternatives)]) + lengths + b"".join(alternatives)
    # State/metadata controls. The 0x0A form carries three grammar bytes.
    extra = raw[5:8] if selector == 0x0A and len(raw) >= 8 else b""
    total = 5 + len(extra)
    return bytes([0x7F, CUSTOM_GRAMMAR_CMD, total, selector, 0]) + extra


def structural_control(raw: bytes, color_map: dict[int, bytes]) -> bytes | None:
    group = raw[2]
    sub = raw[4]
    args = raw[5:]

    if group == 0x13:
        return custom_grammar(raw)
    if group == 0x01:
        if sub == 1: return ctrl(0)
        if sub == 2: return ctrl(1)
        if sub == 3: return ctrl(2)
        if sub == 4 and len(args) >= 2: return ctrl(3, bytes([args[-1]]))
        if sub == 9 and args: return ctrl(82, bytes([args[-1]]))
        if sub == 10 and args: return ctrl(103, bytes([args[-1]]))
        if sub == 11 and args: return ctrl(89, bytes([args[-1]]))
    if group == 0x02:
        if sub in (0, 1, 2, 3, 4):
            cmd = CMD_SETSEL[sub]
            return ctrl(cmd, args[: CONT_SIZES[cmd] - 2])
        if 5 <= sub <= 8:
            cmd = 15 + (sub - 5)
            return ctrl(cmd, args[:2])
        if sub == 9 and len(args) >= 2:
            return ctrl(COMMANDS.index("SETNEXTMSG4"), args[-2:])
        if sub == 10 and len(args) >= 2:
            return ctrl(COMMANDS.index("SETNEXTMSG5"), args[-2:])
    if group == 0x03:
        if 0 <= sub <= 4:
            return ctrl(9, bytes([2, 0, sub + 1]))
        if sub == 5:
            return ctrl(9, bytes([2, 0, 7]))
        if sub == 6 and args:
            return ctrl(9, bytes([8, 0, args[-1]]))
    if group == 0x04:
        simple = {
            0x00: 26, 0x01: 27, 0x02: 28, 0x03: 46, 0x04: 64, 0x05: 47,
            0x06: 113, 0x07: 29, 0x08: 30, 0x09: 31, 0x0A: 32, 0x0B: 33,
            0x0C: 34, 0x0D: 35, 0x0E: 48, 0x10: 116, 0x11: 115,
        }
        if sub in simple: return ctrl(simple[sub])
        if 0x12 <= sub <= 0x25:
            return ctrl(36 + (sub - 0x12))
        if 0x26 <= sub <= 0x2A:
            return ctrl(49 + (sub - 0x26))
    if group == 0x05:
        mapping = {
            0:4, 1:85, 2:94, 3:98, 4:6, 5:7, 6:13, 7:25,
            12:97, 13:114, 14:115, 17:4,
        }
        if sub in mapping: return ctrl(mapping[sub])
        if sub == 16:
            return ctrl(9, bytes([3, 0, 0xFF])) + ctrl(87, bytes([5, 2])) + ctrl(86, bytes([6, 2]))
    if group == 0x06:
        value = 0xFF if sub == 0 else sub
        return ctrl(9, bytes([0, 0, value]))
    if group == 0x07:
        mapping = {0:1, 1:2, 2:3, 7:0x0D, 8:0x0A, 9:0x0B, 10:0x0C, 11:0x0E}
        if sub in mapping: return ctrl(9, bytes([1, 0, mapping[sub]]))
    if group == 0x08:
        if 0 <= sub <= 5: return ctrl(65 + sub)
        if sub == 8: return ctrl(8, bytes([0, 0, 0xFE]))
    if group == 0x09:
        if sub == 0: return ctrl(94)
        if sub == 1: return ctrl(93)
        if sub == 2: return ctrl(9, bytes([1, 0, 4]))
        if sub == 3: return ctrl(9, bytes([7, 0, 1]))
        if 4 <= sub <= 8: return ctrl(9, bytes([9, 0, sub - 4]))
        if 9 <= sub <= 11: return ctrl(9, bytes([9, 0, sub - 4]))
    if group == 0x0A:
        if 0 <= sub <= 4: return ctrl(76 + sub)
        if sub == 6: return ctrl(81, b"\x00")
        if sub == 7: return ctrl(81, b"\x01")
        if 0x0B <= sub <= 0x0E: return ctrl(90, bytes([sub - 8]))
        if sub == 0x11: return ctrl(92)
        if sub == 0x12 and len(args) >= 2: return ctrl(87, args[-2:])
        if sub == 0x13 and len(args) >= 2: return ctrl(86, args[-2:])
    if group == 0x0B:
        if sub == 0x18:
            return ctrl(85) + ctrl(12, bytes([8, 0, 1])) + ctrl(4)
        if sub == 0x17:
            return ctrl(12, bytes([7, 0, 1]))
        if sub == 0x06:
            return (ctrl(9, bytes([1, 0, 2])) +
                    ctrl(10, bytes([0, 0x25, 0x1D])) +
                    ctrl(10, bytes([1, 0, 7])) +
                    ctrl(10, bytes([2, 0, 0])) + ctrl(3, bytes([10])))
        if len(raw) == 6 and args:
            kind = {0:2, 1:3, 2:9, 3:5, 4:5}.get(sub)
            value = args[-1] + (0x64 if sub == 4 else 0)
            if kind is not None: return ctrl(12, bytes([kind, 0, value]))
        table = {
            0x05:(0,2), 0x07:(0,1), 0x09:(0,3), 0x0A:(0,4), 0x0B:(0,5),
            0x0C:(0,6), 0x0D:(0,7), 0x0E:(0,8), 0x0F:(0,9), 0x10:(0,10),
            0x11:(0,11), 0x12:(0,12), 0x13:(1,1), 0x14:(1,2), 0x15:(4,1),
            0x16:(6,1), 0x19:(8,2),
        }
        if sub in table:
            k,v=table[sub]; return ctrl(12, bytes([k,0,v]))
    if group == 0x0C:
        if sub == 0 and len(args) >= 2: return ctrl(CMD_SET_FORCE_MSG, args[-2:])
        if sub == 1 and len(args) >= 4: return ctrl(CMD_SETNEXT_RND_SECTION, args[-4:])
        if sub == 2 and len(args) >= 4: return ctrl(CMD_SETNEXT_RND2, args[-4:])
        if sub == 3 and len(args) >= 6: return ctrl(CMD_SETNEXT_RND3, args[-6:])
        if sub == 4 and len(args) >= 8: return ctrl(CMD_SETNEXT_RND4, args[-8:])
        if sub == 7 and len(args) >= 4: return ctrl(CMD_MALE_FEMALE, args[-4:])
    if group == 0xFF:
        if sub == 0 and args:
            rgb = color_map.get(args[-1], b"\xFF\xFF\xFF")
            return ctrl(5, rgb)
        if sub == 1 and len(args) >= 2:
            val = int.from_bytes(args[-2:], "big")
            scale = max(1, min(255, round(val * 0.32)))
            return ctrl(84, bytes([scale]))
    return None


def convert_entry(entry: bytes, exact: dict[bytes, bytes], unknown: collections.Counter[bytes], color_map: dict[int, bytes]) -> bytes:
    out = bytearray()
    for token in pal_tokens(entry):
        if not token.control:
            out += token.raw
            continue
        raw = token.raw
        if raw[2] == 0x13:
            out += custom_grammar(raw)
            continue
        mapped = exact.get(raw)
        if mapped is None:
            mapped = structural_control(raw, color_map)
        if mapped is None:
            unknown[raw] += 1
            # Unknown formatting/demo commands are omitted rather than emitted as malformed USA controls.
            continue
        out += mapped
    return bytes(out)


def write_bank(entries: list[bytes], data_path: Path, table_path: Path, table_count: int, used_count: int | None = None) -> None:
    data_path.parent.mkdir(parents=True, exist_ok=True)
    if used_count is None:
        used_count = len(entries)
    used_count = min(used_count, len(entries), table_count)
    data = bytearray()
    ends: list[int] = []
    for idx in range(table_count):
        if idx < used_count:
            data += entries[idx]
            ends.append(len(data))
        else:
            ends.append(0)
    data_path.write_bytes(data)
    table_path.write_bytes(b"".join(struct.pack(">I", x) for x in ends))


def rel_sections(blob: bytes) -> list[tuple[int,int]]:
    count = struct.unpack_from(">I", blob, 0x0C)[0]
    table = struct.unpack_from(">I", blob, 0x10)[0]
    return [((struct.unpack_from(">I", blob, table+i*8)[0] & ~3), struct.unpack_from(">I", blob, table+i*8+4)[0]) for i in range(count)]


def map_symbol(map_path: Path, name: str) -> tuple[int,int]:
    text = map_path.read_text(encoding="latin-1", errors="replace")
    pat = re.compile(rf"^  ([0-9a-fA-F]{{8}}) ([0-9a-fA-F]{{6}}) [0-9a-fA-F]{{8}}\s+4\s+{re.escape(name)}\s", re.M)
    m = pat.search(text)
    if not m:
        raise KeyError(f"{name} not found in {map_path}")
    return int(m.group(1),16), int(m.group(2),16)


def extract_symbol(rel_path: Path, map_path: Path, section_index: int, name: str, expected: int) -> bytes:
    blob = rel_path.read_bytes()
    sections = rel_sections(blob)
    addr, size = map_symbol(map_path, name)
    if size != expected:
        raise ValueError(f"{name}: map size {size:#x}, expected {expected:#x}")
    file_off = sections[section_index][0] + addr
    raw = blob[file_off:file_off+size]
    if len(raw) != size:
        raise ValueError(f"{name}: truncated REL data")
    return raw


def bank_paths(lang_tag: str, pal_arcs: Path, msg_root: Path) -> dict[str, Path]:
    base = pal_arcs / lang_tag / "script" / "bin_1st_script" / "data"
    result = {name: base / pal_name for name,(pal_name,_,_,_) in BANKS.items() if name != "message"}
    result["message"] = msg_root / lang_tag / "bin_msg" / "data" / "msg.bin"
    return result


def us_paths(us_root: Path) -> dict[str, tuple[Path,Path]]:
    first = us_root / "first" / "bin1" / "data"
    second = us_root / "second" / "bin2" / "data"
    result = {}
    for name,(_,data_name,table_name,_) in BANKS.items():
        base = second if name == "message" else first
        result[name] = (base/data_name, base/table_name)
    return result


def color_learning(exact: dict[bytes,bytes]) -> dict[int,bytes]:
    votes: dict[int, collections.Counter[bytes]] = collections.defaultdict(collections.Counter)
    for p,u in exact.items():
        if len(p) >= 6 and p[2] == 0xFF and p[4] == 0 and len(u) == 5 and u[:2] == bytes([0x7F,5]):
            votes[p[-1]][u[2:5]] += 1
    return {idx: c.most_common(1)[0][0] for idx,c in votes.items()}


def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()


def main() -> None:
    ap=argparse.ArgumentParser()
    ap.add_argument('--pal-arcs',type=Path,required=True)
    ap.add_argument('--pal-msg',type=Path,required=True)
    ap.add_argument('--pal-tgc',type=Path,required=True)
    ap.add_argument('--us-arcs',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()

    upaths=us_paths(args.us_arcs)
    us_banks={name:parse_us_bank(*upaths[name]) for name in BANKS}
    eng_paths=bank_paths('Eng',args.pal_arcs,args.pal_msg)
    eng_banks={name:parse_bmg(path) for name,path in eng_paths.items()}
    exact=learn_exact_map(eng_banks,us_banks)
    colors=color_learning(exact)

    args.out.mkdir(parents=True,exist_ok=True)
    report={'exact_control_mappings':len(exact),'languages':{}}
    lang_order=['en-pal','es','fr','de','it']
    for code in lang_order:
        tag,label=LANGS[code]
        paths=bank_paths(tag,args.pal_arcs,args.pal_msg)
        out_lang=args.out/code
        aram=out_lang/'aram'; items=out_lang/'items'
        aram.mkdir(parents=True,exist_ok=True);items.mkdir(parents=True,exist_ok=True)
        unknown=collections.Counter()
        stats={}
        for name,(_,data_name,table_name,table_count) in BANKS.items():
            pentries=parse_bmg(paths[name])
            converted=[convert_entry(e,exact,unknown,colors) for e in pentries]
            used_count=2039 if name=='string' else len(converted)
            write_bank(converted,aram/data_name,aram/table_name,table_count,used_count)
            stats[name]={'pal_entries':len(pentries),'written_entries':min(used_count,len(converted)),'data_bytes':(aram/data_name).stat().st_size}
        npc=args.pal_arcs/tag/'second'/'bin2'/'data'/'npc_name_str_table.bin'
        shutil.copy2(npc,aram/'npc_name_str_table.bin')

        tgc=args.pal_tgc/f'forest_{tag}_Final_PAL50'/'files'
        drel=tgc/'forestd.rel'; dmap=tgc/'forestd.map'
        arel=tgc/'foresta.rel'; amap=tgc/'foresta.map'
        for sym,size in ITEM_NAME_SYMBOLS.items():
            (items/f'{sym}.bin').write_bytes(extract_symbol(drel,dmap,4,sym,size))
        if code != "en-pal":
            for sym,size in GRAMMAR_SYMBOLS.items():
                (items/f'{sym}.bin').write_bytes(extract_symbol(arel,amap,5,sym,size))

        manifest=(
            '[Language]\n'
            f'code = {code}\n'
            f'name = {label}\n'
            'source_game = GAFP01 PAL Multi5\n'
            'target_game = GAFE01_00\n'
            'format_version = 2\n'
            'grammar = pal-custom-control-v1\n'
        )
        (out_lang/'manifest.ini').write_text(manifest,encoding='utf-8')
        report['languages'][code]={
            'label':label,'banks':stats,'unknown_control_occurrences':sum(unknown.values()),
            'unknown_control_types':len(unknown),
            'unknown_controls':[{'hex':k.hex(' '),'count':v} for k,v in unknown.most_common(100)],
        }

    (args.out/'comparison-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({
        'exact_mappings':len(exact),
        'unknown':{k:v['unknown_control_occurrences'] for k,v in report['languages'].items()},
        'out':str(args.out),
    },ensure_ascii=False,indent=2))

if __name__=='__main__': main()
