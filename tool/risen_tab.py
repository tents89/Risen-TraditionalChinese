"""
Risen 1 & 2 .tab string-table reader / writer (Combined Version).

Formats:
    Risen 1 (TAB0):
        header  : "TAB0" (4B) + int16 version + int16 format + int64 dateTime
        string  : uint16 charCount + charCount * 2 bytes (Always UTF-16 LE)
    
    Risen 2 (TAB1):
        header  : "TAB1" (4B) + uint16 version (=2) + uint16 isUnicode + uint64 fileTime
        string  : uint16 charCount + charCount * N bytes (N=2 if isUnicode else 1)

Usage:
    # Extract (Risen 1 or 2 auto-detected)
    python risen_tab.py extract <file_or_dir> [-o out_dir] [-f csv|json|txt]

    # Pack back to .tab (--game is REQUIRED)
    python risen_tab.py pack    <file_or_dir> -g {1,2} [-o out_dir]
"""

import argparse
import csv
import json
import struct
import sys
import time
from pathlib import Path


# ---------- low-level IO ----------

def _read_exact(f, n):
    b = f.read(n)
    if len(b) != n:
        raise EOFError(f"short read at {f.tell()}: wanted {n}, got {len(b)}")
    return b


def _ru8(f):  return _read_exact(f, 1)[0]
def _ru16(f): return struct.unpack("<H", _read_exact(f, 2))[0]
def _ri16(f): return struct.unpack("<h", _read_exact(f, 2))[0]
def _ru32(f): return struct.unpack("<I", _read_exact(f, 4))[0]
def _ri64(f): return struct.unpack("<q", _read_exact(f, 8))[0]
def _ru64(f): return struct.unpack("<Q", _read_exact(f, 8))[0]


def _rstr(f, is_unicode=True):
    n = _ru16(f)
    if is_unicode:
        return _read_exact(f, n * 2).decode("utf-16-le", errors="replace")
    return _read_exact(f, n).decode("latin-1", errors="replace")


def _wstr(buf, s, is_unicode=True):
    if is_unicode:
        data = s.encode("utf-16-le")
        n_units = len(data) // 2
        if n_units > 0xFFFF:
            raise ValueError(f"String too long ({n_units} code units, max 65535): {s[:40]!r}...")
        buf += struct.pack("<H", n_units)
        buf += data
    else:
        data = s.encode("latin-1", errors="replace")
        if len(data) > 0xFFFF:
            raise ValueError(f"String too long ({len(data)} bytes, max 65535): {s[:40]!r}...")
        buf += struct.pack("<H", len(data))
        buf += data


# ---------- core ----------

def read_tab(path):
    with open(path, "rb") as f:
        magic = _read_exact(f, 4)
        if magic == b"TAB0":
            game = 1
            version = _ri16(f)
            fmt = _ri16(f)
            date_time = _ri64(f)
            is_unicode = True
        elif magic == b"TAB1":
            game = 2
            version = _ru16(f)
            if version != 2:
                raise ValueError(f"{path}: unknown Risen 2 .tab version {version}, expected 2")
            is_unicode = max(_ru16(f), 1)
            date_time = _ru64(f)
            fmt = 1
        else:
            raise ValueError(f"{path}: bad magic {magic!r}, expected b'TAB0' or b'TAB1'")

        col_count = _ru32(f)
        headers, columns = [], []
        
        for _ in range(col_count):
            flag = _ru8(f)
            if flag == 0:
                continue
            _reserved = _ru16(f)
            name = _rstr(f, is_unicode)
            rows = _ru32(f)
            columns.append([_rstr(f, is_unicode) for _ in range(rows)])
            headers.append(name)

    return {
        "game": game,
        "version": version,
        "format": fmt,
        "is_unicode": is_unicode,
        "date_time": date_time,
        "headers": headers,
        "columns": columns,
    }


def write_tab(path, tab, target_game):
    headers = tab["headers"]
    columns = tab["columns"]
    if len(headers) != len(columns):
        raise ValueError("headers/columns length mismatch")

    game = tab.get("game", target_game)
    buf = bytearray()
    
    # Calculate a fresh filetime fallback only if missing from the imported structure
    default_filetime = int((time.time() + 11644473600) * 10_000_000)
    ts = tab.get("date_time", default_filetime)

    if game == 1:
        buf += b"TAB0"
        buf += struct.pack("<hhq", tab.get("version", 1), tab.get("format", 1), ts)
        is_unicode = True
    elif game == 2:
        buf += b"TAB1"
        is_unicode = int(tab.get("is_unicode", 1)) or 1
        buf += struct.pack("<HHQ", tab.get("version", 2), is_unicode, ts)
    else:
        raise ValueError(f"Unsupported game version: {game}")

    buf += struct.pack("<I", len(headers))
    for name, col in zip(headers, columns):
        buf += struct.pack("<BH", 1, 1)
        _wstr(buf, name, is_unicode)
        buf += struct.pack("<I", len(col))
        for s in col:
            _wstr(buf, s, is_unicode)

    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(buf)


def columns_to_rows(columns):
    if not columns:
        return []
    n = max(len(c) for c in columns)
    return [[(c[i] if i < len(c) else "") for c in columns] for i in range(n)]


def rows_to_columns(rows, ncols):
    cols = [[] for _ in range(ncols)]
    for r in rows:
        for i in range(ncols):
            cols[i].append(r[i] if i < len(r) else "")
    return cols


# ---------- file format writers / readers ----------

def dump_csv(path, tab):
    rows = columns_to_rows(tab["columns"])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8-sig", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(tab["headers"])
        w.writerows(rows)


def load_csv(path):
    with open(path, "r", encoding="utf-8-sig", newline="") as fh:
        r = list(csv.reader(fh))
    if not r:
        raise ValueError(f"{path}: empty csv")
    headers = r[0]
    rows = r[1:]
    return {
        "headers": headers,
        "columns": rows_to_columns(rows, len(headers)),
    }


def dump_json(path, tab):
    rows = columns_to_rows(tab["columns"])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({
            "game": tab["game"],
            "version": tab["version"],
            "format": tab.get("format", 1),
            "is_unicode": tab.get("is_unicode", 1),
            "date_time": tab["date_time"],
            "headers": tab["headers"],
            "rows": rows,
        }, fh, ensure_ascii=False, indent=2)


def load_json(path):
    with open(path, "r", encoding="utf-8") as fh:
        j = json.load(fh)
    headers = j["headers"]
    if "rows" in j:
        rows = j["rows"]
        if rows and isinstance(rows[0], dict):
            rows = [[r.get(h, "") for h in headers] for r in rows]
        cols = rows_to_columns(rows, len(headers))
    elif "columns" in j:
        cols = j["columns"]
    else:
        raise ValueError(f"{path}: json must contain 'rows' or 'columns'")
    
    ret = {
        "headers": headers,
        "columns": cols,
    }
    for k in ("game", "version", "format", "is_unicode", "date_time"):
        if k in j:
            ret[k] = j[k]
    return ret


def dump_txt(path, tab):
    rows = columns_to_rows(tab["columns"])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\t".join(tab["headers"]) + "\n")
        for r in rows:
            fh.write("\t".join(r) + "\n")


DUMPERS = {"csv": dump_csv, "json": dump_json, "txt": dump_txt}
LOADERS = {".csv": load_csv, ".json": load_json}


# ---------- batch helpers ----------

def iter_files(root, exts):
    root = Path(root)
    if root.is_file():
        if root.suffix.lower() in exts:
            yield root, root.name
        return
    if root.is_dir():
        for p in sorted(root.rglob("*")):
            if p.is_file() and p.suffix.lower() in exts:
                yield p, str(p.relative_to(root))
        return
    raise FileNotFoundError(root)


# ---------- commands ----------

def cmd_extract(args):
    out_dir = Path(args.out) if args.out else None
    files = list(iter_files(args.input, {".tab"}))
    if not files:
        print("No .tab files found.", file=sys.stderr)
        sys.exit(1)

    ok = fail = 0
    for path, rel in files:
        try:
            tab = read_tab(path)
        except Exception as e:
            print(f"[FAIL] {rel}: {e}", file=sys.stderr)
            fail += 1
            continue

        if out_dir is None:
            print(f"\n=== {rel} (Risen {tab['game']}) ===")
            print(f"v{tab['version']}  cols={len(tab['headers'])}  "
                  f"rows={len(tab['columns'][0]) if tab['columns'] else 0}")
            print("\t".join(tab["headers"]))
            for r in columns_to_rows(tab["columns"])[: args.preview]:
                print("\t".join(r))
        else:
            target = out_dir / (rel[:-4] + "." + args.format)
            DUMPERS[args.format](target, tab)
            print(f"[ok]   {rel} -> {target}")
        ok += 1

    print(f"\nExtract summary: {ok} successful, {fail} failed", file=sys.stderr)


def cmd_pack(args):
    out_dir = Path(args.out) if args.out else None
    files = list(iter_files(args.input, set(LOADERS.keys())))
    if not files:
        print("No compatible .csv or .json files found.", file=sys.stderr)
        sys.exit(1)

    ok = fail = 0
    for path, rel in files:
        try:
            tab = LOADERS[path.suffix.lower()](path)
        except Exception as e:
            print(f"[FAIL] {rel}: {e}", file=sys.stderr)
            fail += 1
            continue

        target = (out_dir if out_dir else path.parent) / (Path(rel).with_suffix(".tab").name
                                                          if out_dir is None
                                                          else Path(rel).with_suffix(".tab"))
        try:
            write_tab(target, tab, target_game=args.game)
        except Exception as e:
            print(f"[FAIL] {rel}: {e}", file=sys.stderr)
            fail += 1
            continue
        print(f"[ok]   {rel} -> {target} (Game {tab.get('game', args.game)})")
        ok += 1

    print(f"\nPack summary: {ok} successful, {fail} failed", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(
        description="Unified Tool for Reading and Writing Risen 1 and Risen 2 .tab String Tables.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               "  python risen_tab.py extract localization.tab -o extracted_texts/ -f csv\n"
               "  python risen_tab.py pack extracted_texts/ -g 1 -o packed_out/\n"
               "  python risen_tab.py pack edited_file.csv --game 2"
    )
    sub = ap.add_subparsers(
        dest="cmd", 
        required=True, 
        help="Available operation modes. Type '[command] -h' for specific options."
    )

    # Extract command help parser
    p_ex = sub.add_parser(
        "extract", 
        help="Convert binary .tab files into editable text formats (CSV, JSON, or TXT).",
        description="Extracts data from .tab files. Supports processing a single file or a whole directory recursively. "
                    "The script automatically detects whether a file belongs to Risen 1 (TAB0) or Risen 2 (TAB1)."
    )
    p_ex.add_argument(
        "input", 
        help="Path to a single .tab file or a directory containing .tab files to extract."
    )
    p_ex.add_argument(
        "-o", "--out", 
        help="Path to the output directory where extracted files will be saved. "
             "If this flag is omitted, the script will dump a raw preview directly to stdout instead of saving files."
    )
    p_ex.add_argument(
        "-f", "--format", 
        choices=("csv", "json", "txt"), 
        default="csv",
        help="The target file format to generate. 'csv' (default) preserves the grid natively with UTF-8 BOM, "
             "'json' saves complete layout structure including internal file metadata, "
             "and 'txt' outputs a plain tab-separated matrix layout."
    )
    p_ex.add_argument(
        "-n", "--preview", 
        type=int, 
        default=10,
        help="Maximum number of rows to print per file when performing a command line preview (when -o is omitted)."
    )
    p_ex.set_defaults(func=cmd_extract)

    # Pack command help parser
    p_pk = sub.add_parser(
        "pack", 
        help="Compile editable text formats (CSV or JSON) back into binary .tab files.",
        description="Compiles text files back into engine-compatible binary string tables. "
                    "Supports single files or batch folder conversion recursively."
    )
    p_pk.add_argument(
        "input", 
        help="Path to a single source file (.csv/.json) or a directory containing them to pack."
    )
    p_pk.add_argument(
        "-o", "--out", 
        help="Path to the destination output folder. If omitted, the generated .tab files "
             "will be saved in the exact same directory alongside their corresponding source files."
    )
    p_pk.add_argument(
        "-g", "--game", 
        choices=(1, 2), 
        type=int, 
        required=True,
        help="'1' to Risen 1 format (TAB0) or '2' to Risen 2 format (TAB1). "
             "Note: If using JSON files containing embedded 'game' metadata keys, the JSON property will take priority."
    )
    p_pk.set_defaults(func=cmd_pack)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()