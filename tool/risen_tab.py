"""
Risen 1 .tab string-table reader / writer.

Format (see Code/Risen FileSystem/tabFile.cs):
    header  : "TAB0" (4B) + int16 version + int16 format + int64 dateTime
    body    : uint32 columnCount
              per column:
                  uint8  flag        (0 -> skip column on read)
                  uint16 (reserved, usually 1)
                  string columnName
                  uint32 rowCount
                  rowCount * string
    string  : uint16 charCount + charCount * 2 bytes (UTF-16 LE)

Column 0 is the key column; other columns hold localised text.

Usage:
    # extract (one file or whole folder, recursive)
    python risen_tab.py extract <file_or_dir> [-o out_dir] [-f csv|json|txt]

    # pack back to .tab (one file or whole folder, recursive)
    python risen_tab.py pack    <file_or_dir> [-o out_dir]
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


def _rstr(f):
    n = _ru16(f)
    return _read_exact(f, n * 2).decode("utf-16-le", errors="replace")


def _wstr(buf, s):
    if len(s) > 0xFFFF:
        raise ValueError(f"string too long ({len(s)} chars, max 65535): {s[:40]!r}...")
    buf += struct.pack("<H", len(s))
    buf += s.encode("utf-16-le")


# ---------- core ----------

def read_tab(path):
    with open(path, "rb") as f:
        magic = _read_exact(f, 4)
        if magic != b"TAB0":
            raise ValueError(f"{path}: bad magic {magic!r}, expected b'TAB0'")
        version = _ri16(f)
        fmt = _ri16(f)
        date_time = _ri64(f)
        col_count = _ru32(f)

        headers, columns = [], []
        for _ in range(col_count):
            flag = _ru8(f)
            if flag == 0:
                continue
            _reserved = _ru16(f)
            name = _rstr(f)
            rows = _ru32(f)
            columns.append([_rstr(f) for _ in range(rows)])
            headers.append(name)

    return {
        "version": version,
        "format": fmt,
        "date_time": date_time,
        "headers": headers,
        "columns": columns,
    }


def write_tab(path, tab):
    headers = tab["headers"]
    columns = tab["columns"]
    if len(headers) != len(columns):
        raise ValueError("headers/columns length mismatch")

    buf = bytearray()
    buf += b"TAB0"
    buf += struct.pack("<hhq",
                       tab.get("version", 1),
                       tab.get("format", 1),
                       tab.get("date_time", _now_filetime()))
    buf += struct.pack("<I", len(headers))
    for name, col in zip(headers, columns):
        buf += struct.pack("<BH", 1, 1)
        _wstr(buf, name)
        buf += struct.pack("<I", len(col))
        for s in col:
            _wstr(buf, s)

    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(buf)


def _now_filetime():
    # Windows FILETIME: 100-ns ticks since 1601-01-01 UTC
    return int((time.time() + 11644473600) * 10_000_000)


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


# ---------- file format writers / readers (csv / json / txt) ----------

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
        "version": 1,
        "format": 1,
        "date_time": _now_filetime(),
        "headers": headers,
        "columns": rows_to_columns(rows, len(headers)),
    }


def dump_json(path, tab):
    rows = columns_to_rows(tab["columns"])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({
            "version": tab["version"],
            "format": tab["format"],
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
        # accept either list-of-lists or list-of-dicts
        if rows and isinstance(rows[0], dict):
            rows = [[r.get(h, "") for h in headers] for r in rows]
        cols = rows_to_columns(rows, len(headers))
    elif "columns" in j:
        cols = j["columns"]
    else:
        raise ValueError(f"{path}: json must contain 'rows' or 'columns'")
    return {
        "version": j.get("version", 1),
        "format": j.get("format", 1),
        "date_time": j.get("date_time", _now_filetime()),
        "headers": headers,
        "columns": cols,
    }


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
            print(f"\n=== {rel} ===")
            print(f"v{tab['version']} fmt{tab['format']}  "
                  f"cols={len(tab['headers'])}  rows={len(tab['columns'][0]) if tab['columns'] else 0}")
            print("\t".join(tab["headers"]))
            for r in columns_to_rows(tab["columns"])[: args.preview]:
                print("\t".join(r))
        else:
            target = out_dir / (rel[:-4] + "." + args.format)
            DUMPERS[args.format](target, tab)
            print(f"[ok]   {rel} -> {target}")
        ok += 1

    print(f"\nextract: {ok} ok, {fail} failed", file=sys.stderr)


def cmd_pack(args):
    out_dir = Path(args.out) if args.out else None
    files = list(iter_files(args.input, set(LOADERS.keys())))
    if not files:
        print("No .csv / .json files found.", file=sys.stderr)
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
            write_tab(target, tab)
        except Exception as e:
            print(f"[FAIL] {rel}: {e}", file=sys.stderr)
            fail += 1
            continue
        print(f"[ok]   {rel} -> {target}")
        ok += 1

    print(f"\npack: {ok} ok, {fail} failed", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description="Risen 1 .tab reader / writer")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_ex = sub.add_parser("extract", help=".tab -> csv/json/txt")
    p_ex.add_argument("input", help=".tab file or folder")
    p_ex.add_argument("-o", "--out", help="output folder (omit to preview to stdout)")
    p_ex.add_argument("-f", "--format", choices=("csv", "json", "txt"), default="csv")
    p_ex.add_argument("-n", "--preview", type=int, default=10)
    p_ex.set_defaults(func=cmd_extract)

    p_pk = sub.add_parser("pack", help="csv/json -> .tab")
    p_pk.add_argument("input", help=".csv / .json file or folder")
    p_pk.add_argument("-o", "--out", help="output folder (default: alongside source)")
    p_pk.set_defaults(func=cmd_pack)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
