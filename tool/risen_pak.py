"""
Risen 1 PAK / P00 / P01 ... pack & unpack.

Each .pak / .p0N file is a self-contained volume with this layout:

    header (48 bytes):
        u32  version       (= 1)
        char[4] magic      ("G3V0")
        16x  reserved zeros
        i64  data_offset   (= 48, files start right after header)
        i64  root_offset   (location of root directory record)
        i64  volume_size   (total file size)

    file data area:
        raw bytes for every file, optionally zlib-compressed

    directory tree at root_offset:
        directory record (no leading attribute):
            i32  name_len
            if name_len != 0: name_len + 1 bytes  (last byte = '\0')
            i64  time_created
            i64  time_accessed
            i64  time_modified
            u32  attributes
            i32  child_count
            for each child:
                u32  attributes        (has Directory bit if child is a directory)
                directory record OR file record (without leading attribute)

        file record (no leading attribute):
            i32  name_len
            if name_len != 0: name_len + 1 bytes  (last byte = '\0')
            i64  data_offset
            i64  time_created
            i64  time_accessed
            i64  time_modified
            u32  attributes
            u32  encryption        (= 0)
            u32  compression       (0 = none, 2 = zlib)
            i32  compressed_size
            i32  uncompressed_size

Usage:
    python risen_unpak.py extract <archive_or_folder> [-o out_dir] [-l]
    python risen_unpak.py pack    <folder>            [-o out.pak]
"""

import argparse
import os
import struct
import sys
import time
import zlib
from pathlib import Path

ATTR_DIRECTORY = 0x00000010
ATTR_ARCHIVE   = 0x00000020
ATTR_PACKED    = 0x00020000

COMPRESSION_NONE = 0x00000000
COMPRESSION_AUTO = 0x00000001
COMPRESSION_ZLIB = 0x00000002

HEADER_SIZE = 48
HEADER_MAGIC = b"\x01\x00\x00\x00G3V0" + b"\x00" * 16


# =========================================================================
# read side
# =========================================================================

class Reader:
    __slots__ = ("f",)

    def __init__(self, f):
        self.f = f

    def seek(self, off):
        self.f.seek(off)

    def read(self, n):
        data = self.f.read(n)
        if len(data) != n:
            raise EOFError(f"short read: wanted {n}, got {len(data)} at {self.f.tell()}")
        return data

    def u32(self): return struct.unpack("<I", self.read(4))[0]
    def i32(self): return struct.unpack("<i", self.read(4))[0]
    def i64(self): return struct.unpack("<q", self.read(8))[0]

    def name(self):
        l = self.i32()
        if l == 0:
            return ""
        raw = self.read(l + 1)
        return raw[:-1].decode("latin-1", errors="replace")


class Entry:
    def __init__(self, name, is_dir):
        self.name = name
        self.is_dir = is_dir
        self.children = [] if is_dir else None
        self.offset = 0
        self.comp_size = 0
        self.uncomp_size = 0
        self.compression = 0
        self.attributes = 0
        self.t_created = 0
        self.t_accessed = 0
        self.t_modified = 0
        # only used when packing
        self.src_path = None


def _read_directory(r, name):
    d = Entry(name, True)
    d.t_created = r.i64()
    d.t_accessed = r.i64()
    d.t_modified = r.i64()
    d.attributes = r.u32()
    count = r.i32()
    for _ in range(count):
        attr = r.u32()
        child_name = r.name()
        if attr & ATTR_DIRECTORY:
            d.children.append(_read_directory(r, child_name))
        else:
            d.children.append(_read_file(r, child_name))
    return d


def _read_file(r, name):
    e = Entry(name, False)
    e.offset = r.i64()
    e.t_created = r.i64()
    e.t_accessed = r.i64()
    e.t_modified = r.i64()
    e.attributes = r.u32()
    e.encryption = r.u32()
    e.compression = r.u32()
    e.comp_size = r.i32()
    e.uncomp_size = r.i32()
    return e


def open_pak(path):
    f = open(path, "rb")
    r = Reader(f)
    r.seek(24)
    data_offset = r.i64()
    root_offset = r.i64()
    volume_size = r.i64()
    r.seek(root_offset)
    root_name = r.name()
    root = _read_directory(r, root_name)
    return f, root, (data_offset, root_offset, volume_size)


def extract_entry_data(f, entry):
    f.seek(entry.offset)
    data = f.read(entry.comp_size)
    if entry.compression & COMPRESSION_ZLIB:
        return zlib.decompress(data)
    return data


def _walk(entry, parents, cb):
    path = parents + [entry.name] if entry.name else parents
    if entry.is_dir:
        for c in entry.children:
            _walk(c, path, cb)
    else:
        cb(path, entry)


def extract_archive(pak_path, out_dir, list_only=False):
    pak_path = Path(pak_path)
    out_dir = Path(out_dir)
    f, root, _hdr = open_pak(pak_path)
    base = out_dir / pak_path.stem
    count = [0]
    total = [0]

    def handle(path_parts, entry):
        rel = Path(*path_parts) if path_parts else Path(entry.name)
        if list_only:
            tag = "ZLIB" if entry.compression & COMPRESSION_ZLIB else "RAW "
            print(f"  {tag} {entry.uncomp_size:>10}  {rel}")
        else:
            target = base / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                data = extract_entry_data(f, entry)
            except Exception as exc:
                print(f"  [SKIP] {rel}: {exc}", file=sys.stderr)
                return
            with open(target, "wb") as out:
                out.write(data)
            total[0] += len(data)
        count[0] += 1

    try:
        _walk(root, [], handle)
    finally:
        f.close()

    action = "listed" if list_only else "extracted"
    print(f"[{pak_path.name}] {action} {count[0]} files, {total[0]:,} bytes")


def iter_archives(target):
    target = Path(target)
    if target.is_file():
        yield target
        return
    if target.is_dir():
        exts = {".pak"} | {f".p{n:02d}" for n in range(100)}
        for p in sorted(target.rglob("*")):
            if p.is_file() and p.suffix.lower() in exts:
                yield p
        return
    raise FileNotFoundError(target)


# =========================================================================
# write side
# =========================================================================

def _now_filetime():
    return int((time.time() + 11644473600) * 10_000_000)


def _build_tree(folder):
    folder = Path(folder)
    if not folder.is_dir():
        raise NotADirectoryError(folder)
    now = _now_filetime()

    def make_dir(path, name):
        d = Entry(name, True)
        d.attributes = ATTR_DIRECTORY | ATTR_PACKED
        d.t_created = d.t_accessed = d.t_modified = now
        for child in sorted(path.iterdir(), key=lambda p: (not p.is_dir(), p.name.lower())):
            if child.is_dir():
                d.children.append(make_dir(child, child.name))
            elif child.is_file():
                f = Entry(child.name, False)
                f.attributes = ATTR_ARCHIVE | ATTR_PACKED
                f.t_created = f.t_accessed = f.t_modified = now
                f.src_path = child
                f.uncomp_size = child.stat().st_size
                d.children.append(f)
        return d

    root = make_dir(folder, "")  # root has empty name
    return root


def _encode_name(name):
    if not name:
        return struct.pack("<i", 0)
    raw = name.encode("latin-1", errors="replace") + b"\x00"
    return struct.pack("<i", len(name)) + raw


def _write_file_data(out, entry):
    """Walk tree, write file bytes, fill in entry.offset/comp_size/compression."""
    if entry.is_dir:
        for c in entry.children:
            _write_file_data(out, c)
        return
    with open(entry.src_path, "rb") as fh:
        data = fh.read()
    entry.uncomp_size = len(data)
    entry.compression = COMPRESSION_NONE
    entry.offset = out.tell()
    entry.comp_size = len(data)
    out.write(data)


def _write_directory_record(out, entry):
    out.write(_encode_name(entry.name))
    out.write(struct.pack("<qqq", entry.t_created, entry.t_accessed, entry.t_modified))
    out.write(struct.pack("<I", entry.attributes))
    out.write(struct.pack("<i", len(entry.children)))
    for child in entry.children:
        if child.is_dir:
            out.write(struct.pack("<I", child.attributes | ATTR_DIRECTORY))
            _write_directory_record(out, child)
        else:
            out.write(struct.pack("<I", child.attributes & ~ATTR_DIRECTORY))
            _write_file_record(out, child)


def _write_file_record(out, entry):
    out.write(_encode_name(entry.name))
    out.write(struct.pack("<q", entry.offset))
    out.write(struct.pack("<qqq", entry.t_created, entry.t_accessed, entry.t_modified))
    out.write(struct.pack("<I", entry.attributes))
    out.write(struct.pack("<I", 0))                   # encryption
    out.write(struct.pack("<I", entry.compression))
    out.write(struct.pack("<i", entry.comp_size))
    out.write(struct.pack("<i", entry.uncomp_size))


def pack_folder(folder, out_path):
    folder = Path(folder)
    out_path = Path(out_path)
    root = _build_tree(folder)
    file_count = [0]

    def count(e):
        if e.is_dir:
            for c in e.children: count(c)
        else:
            file_count[0] += 1
    count(root)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as out:
        out.write(HEADER_MAGIC)
        out.write(b"\x00" * 24)   # data/root/volume placeholders
        assert out.tell() == HEADER_SIZE

        _write_file_data(out, root)

        root_offset = out.tell()
        _write_directory_record(out, root)
        volume_size = out.tell()

        out.seek(24)
        out.write(struct.pack("<qqq", HEADER_SIZE, root_offset, volume_size))

    print(f"[pack] {folder} -> {out_path}  ({file_count[0]} files, "
          f"{volume_size:,} bytes, zlib=off)")


# =========================================================================
# CLI
# =========================================================================

def cmd_extract(args):
    archives = list(iter_archives(args.input))
    if not archives:
        print("No .pak / .p0N files found.", file=sys.stderr)
        sys.exit(1)
    for pak in archives:
        print(f"\n=== {pak} ===")
        extract_archive(pak, args.out, list_only=args.list)


def cmd_pack(args):
    folder = Path(args.input)
    if not folder.is_dir():
        print(f"pack input must be a folder: {folder}", file=sys.stderr)
        sys.exit(1)
    out = Path(args.out) if args.out else folder.with_suffix(".pak")
    pack_folder(folder, out)


def main():
    ap = argparse.ArgumentParser(description="Risen 1 PAK pack / unpack")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_ex = sub.add_parser("extract", help="unpack .pak / .p00 / .p01 ...")
    p_ex.add_argument("input", help="archive file, or folder of archives")
    p_ex.add_argument("-o", "--out", default="unpacked", help="output dir (default ./unpacked)")
    p_ex.add_argument("-l", "--list", action="store_true", help="list only")
    p_ex.set_defaults(func=cmd_extract)

    p_pk = sub.add_parser("pack", help="pack a folder into a .pak volume")
    p_pk.add_argument("input", help="folder to pack (its contents become the PAK root)")
    p_pk.add_argument("-o", "--out", help="output .pak path (default: <folder>.pak)")
    p_pk.set_defaults(func=cmd_pack)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()