"""
Risen 2 XGFN font builder.

Referenced from Risen2jptools.
https://u10.getuploader.com/gothic3/index/date/desc/2 Risen2jptools

  GAR3 outer archive
  GFN data with GEDXFNT0 LOGFONT header
  character table: count + (u16 codepoint, u16 glyph index)
  metrics table: count + (x1, y1, x2, y2, width, p2, p3, p4, p5)
  DDS payload size + DDS payload

Input is an existing XGFN template plus a TTF/OTF font. The template supplies
the container header, LOGFONT data, default font height, and default DDS style.
"""

import argparse
import math
import struct
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image
import freetype


MAP_COUNT_OFFSET = 0xF6
MAP_START = MAP_COUNT_OFFSET + 4
DDS_HEADER_SIZE = 128


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def i32(data, off):
    return struct.unpack_from("<i", data, off)[0]


def put_u32(data, off, value):
    struct.pack_into("<I", data, off, value)


def find_dds(data):
    off = data.find(b"DDS ")
    if off < 0:
        raise ValueError("missing DDS")
    return off


def parse_dds(data):
    off = find_dds(data)
    fourcc = data[off + 84 : off + 88].rstrip(b"\x00")
    return {
        "off": off,
        "width": u32(data, off + 16),
        "height": u32(data, off + 12),
        "mips": u32(data, off + 28),
        "fourcc": fourcc,
        "payload": data[off:],
    }


def read_text_auto(path):
    raw = Path(path).read_bytes()
    if raw.startswith(b"\xfe\xff"):
        return raw[2:].decode("utf-16-be", errors="ignore")
    if raw.startswith(b"\xff\xfe"):
        return raw[2:].decode("utf-16-le", errors="ignore")
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw[3:].decode("utf-8", errors="ignore")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw[: len(raw) // 2 * 2].decode("utf-16-be", errors="ignore")


def template_chars(data):
    count = u32(data, MAP_COUNT_OFFSET)
    chars = []
    for i in range(count):
        cp, _gid = struct.unpack_from("<HH", data, MAP_START + i * 4)
        chars.append(cp)
    return chars


def collect_chars(template_data, chars_file, text, keep_template):
    out = []
    seen = set()

    def add(cp):
        if cp > 0xFFFF:
            return
        if cp not in seen:
            seen.add(cp)
            out.append(cp)

    if keep_template:
        for cp in template_chars(template_data):
            add(cp)
    if chars_file:
        for ch in read_text_auto(chars_file):
            if ch not in "\r\n\t\ufeff":
                add(ord(ch))
    for ch in text or "":
        add(ord(ch))
    if not out:
        raise ValueError("no characters selected")
    return out


def default_font_px(template_data):
    # GEDXFNT0 starts at 0x6E in all observed files; height is signed i32 at +12.
    ged = template_data.find(b"GEDXFNT0")
    if ged < 0:
        return 18
    h = i32(template_data, ged + 12)
    return max(1, abs(h))


ATLAS_CANDIDATES = [
    (256, 256),
    (512, 512),
    (1024, 1024),
    (1024, 2048),
    (1024, 4096),
    (2048, 2048),
    (4096, 4096),
]


def choose_atlas(chars_count, font_px, width, height):
    if width and height:
        return width, height
    rough_area = chars_count * (font_px + 2) * (font_px + 2) * 1.35
    min_w = width or 0
    min_h = height or 0
    for w, h in ATLAS_CANDIDATES:
        if w >= min_w and h >= min_h and w * h >= rough_area:
            return w, h
    return 4096, 4096


def patch_dds_header(header, width, height, dxt5):
    h = bytearray(header[:DDS_HEADER_SIZE])
    put_u32(h, 12, height)
    put_u32(h, 16, width)
    put_u32(h, 28, 0)
    if dxt5:
        put_u32(h, 8, 0x81007)
        put_u32(h, 20, width * height)
        put_u32(h, 76, 32)
        put_u32(h, 80, 4)
        h[84:88] = b"DXT5"
        put_u32(h, 88, 0)
        put_u32(h, 92, 0)
        put_u32(h, 96, 0)
        put_u32(h, 100, 0)
        put_u32(h, 104, 0)
        put_u32(h, 108, 0x1000)
    else:
        put_u32(h, 8, 0x81007)
        put_u32(h, 20, width * height)
        put_u32(h, 76, 32)
        put_u32(h, 80, 0x41)
        h[84:88] = b"\x00\x00\x00\x00"
        put_u32(h, 88, 32)
        put_u32(h, 92, 0x00FF0000)
        put_u32(h, 96, 0x0000FF00)
        put_u32(h, 100, 0x000000FF)
        put_u32(h, 104, 0xFF000000)
        put_u32(h, 108, 0x1000)
    return bytes(h)


def render_glyph(face, cp):
    face.load_char(chr(cp), freetype.FT_LOAD_RENDER)
    glyph = face.glyph
    bmp = glyph.bitmap
    w = max(1, bmp.width)
    h = max(1, bmp.rows)
    img = Image.new("L", (w, h), 0)
    if bmp.width and bmp.rows:
        img = Image.frombytes("L", (bmp.width, bmp.rows), bytes(bmp.buffer))
    advance = max(1, glyph.advance.x >> 6)
    return {
        "img": img,
        "left": glyph.bitmap_left,
        "top": glyph.bitmap_top,
        "advance": advance,
        "width": w,
        "height": h,
    }


def pack_atlas(font_path, chars, font_px, width, height, padding, y_shift=0):
    face = freetype.Face(str(font_path))
    face.set_pixel_sizes(0, font_px)
    ascender = face.size.ascender >> 6
    line_height = max(1, face.size.height >> 6)
    atlas = Image.new("RGBA", (width, height), (255, 255, 255, 0))
    metrics = []
    x = padding
    y = padding
    row_h = 0
    for cp in chars:
        glyph = render_glyph(face, cp)
        glyph_img = glyph["img"]
        cell_w = max(1, glyph["advance"])
        cell_h = line_height
        draw_x = max(0, glyph["left"])
        draw_y = max(0, ascender - glyph["top"] + y_shift)
        if glyph_img.getbbox() is None:
            gw = cell_w
            gh = cell_h
            rgba = Image.new("RGBA", (gw, gh), (255, 255, 255, 0))
        else:
            gw = max(cell_w, draw_x + glyph_img.width)
            gh = max(cell_h, draw_y + glyph_img.height)
            rgba = Image.new("RGBA", (gw, gh), (255, 255, 255, 0))
            glyph_rgba = Image.new("RGBA", glyph_img.size, (255, 255, 255, 0))
            glyph_rgba.putalpha(glyph_img)
            rgba.alpha_composite(glyph_rgba, (draw_x, draw_y))
        if x + gw + padding > width:
            x = padding
            y += row_h + padding
            row_h = 0
        if y + gh + padding > height:
            raise ValueError(f"atlas overflow at U+{cp:04X}; increase atlas size or reduce font px")
        atlas.alpha_composite(rgba, (x, y))
        metrics.append((x, y, x + gw, y + gh, glyph["advance"], 0, 0, 0, 0))
        x += gw + padding
        row_h = max(row_h, gh)
    return atlas, metrics


def encode_dxt5(atlas):
    width, height = atlas.size
    pix = atlas.convert("RGBA").load()
    out = bytearray()
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            alphas = []
            for yy in range(4):
                for xx in range(4):
                    alphas.append(pix[min(width - 1, bx + xx), min(height - 1, by + yy)][3])
            a0, a1 = max(alphas), min(alphas)
            pal = [a0, a1]
            if a0 > a1:
                pal += [(6*a0+a1)//7, (5*a0+2*a1)//7, (4*a0+3*a1)//7, (3*a0+4*a1)//7, (2*a0+5*a1)//7, (a0+6*a1)//7]
            else:
                pal += [(4*a0+a1)//5, (3*a0+2*a1)//5, (2*a0+3*a1)//5, (a0+4*a1)//5, 0, 255]
            bits = 0
            for i, a in enumerate(alphas):
                idx = min(range(8), key=lambda n: abs(pal[n] - a))
                bits |= idx << (3 * i)
            out += bytes((a0, a1)) + bits.to_bytes(6, "little")
            out += struct.pack("<HHI", 0xFFFF, 0xFFFF, 0)
    return bytes(out)


def encode_bgra(atlas):
    raw = atlas.convert("RGBA").tobytes()
    out = bytearray(len(raw))
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i : i + 4]
        out[i : i + 4] = bytes((b, g, r, a))
    return bytes(out)


def make_dds(template_dds, atlas, force_dxt5):
    dxt5 = force_dxt5 or template_dds[84:88] == b"DXT5"
    header = patch_dds_header(template_dds, atlas.width, atlas.height, dxt5)
    return header + (encode_dxt5(atlas) if dxt5 else encode_bgra(atlas))


def build_xgfn(template, font, out_path, chars_file=None, text="", font_px=None, width=None, height=None, padding=1, dds_template=None, preview_dir=None, keep_template=True, y_shift=0):
    template = Path(template)
    font = Path(font)
    data = template.read_bytes()
    dds_source = Path(dds_template).read_bytes() if dds_template else data
    dds = parse_dds(dds_source)
    chars = collect_chars(data, chars_file, text, keep_template)
    px = font_px or default_font_px(data)
    w, h = choose_atlas(len(chars), px, width or dds["width"], height or dds["height"])
    while True:
        try:
            atlas, metrics = pack_atlas(font, chars, px, w, h, padding, y_shift)
            break
        except ValueError:
            bigger = [(cw, ch) for cw, ch in ATLAS_CANDIDATES if cw * ch > w * h and cw >= (width or 0) and ch >= (height or 0)]
            if not bigger:
                raise
            w, h = bigger[0]
    new_dds = make_dds(dds["payload"], atlas, True)

    mapping = bytearray()
    for gid, cp in enumerate(chars):
        mapping += struct.pack("<HH", cp, gid)
    metric_blob = bytearray(struct.pack("<I", len(metrics)))
    for rec in metrics:
        metric_blob += struct.pack("<9i", *rec)

    new_dds_off = MAP_START + len(mapping) + len(metric_blob) + 4
    out = bytearray()
    out += data[:MAP_COUNT_OFFSET]
    out += struct.pack("<I", len(chars))
    out += data[MAP_COUNT_OFFSET + 4 : MAP_START]
    out += mapping
    out += metric_blob
    out += struct.pack("<I", len(new_dds))
    out += new_dds
    put_u32(out, 0x1C, len(out) - 102)

    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(out)
    if preview_dir:
        preview = Path(preview_dir) / (template.name.replace(" ", "_") + ".png")
        preview.parent.mkdir(parents=True, exist_ok=True)
        bg = Image.new("RGBA", atlas.size, (0, 0, 0, 255))
        bg.alpha_composite(atlas)
        bg.save(preview)
    print(f"[OK] {template.name} -> {out_path}")
    print(f"  chars={len(chars)} font_px={px} atlas={w}x{h} dds=DXT5 offset=0x{new_dds_off:X}")


def run_gui():
    root = tk.Tk()
    root.title("XGFN Builder")
    root.geometry("760x520")
    font_v = tk.StringVar()
    chars_v = tk.StringVar()
    out_v = tk.StringVar(value="xgfn_output")
    px_v = tk.StringVar()
    y_v = tk.StringVar(value="-2")
    w_v = tk.StringVar()
    h_v = tk.StringVar()
    templates = []
    per_px = {}
    per_y = {}

    def pick_font():
        p = filedialog.askopenfilename(filetypes=[("Fonts", "*.ttf *.otf"), ("All", "*.*")])
        if p:
            font_v.set(p)

    def pick_chars():
        p = filedialog.askopenfilename(filetypes=[("Text", "*.txt"), ("All", "*.*")])
        if p:
            chars_v.set(p)

    def pick_out():
        p = filedialog.askdirectory()
        if p:
            out_v.set(p)

    def add_files():
        for p in filedialog.askopenfilenames(filetypes=[("XGFN", "*._xgfn *.xgfn"), ("All", "*.*")]):
            if p not in templates:
                templates.append(p)
                per_px[p] = default_font_px(Path(p).read_bytes())
                per_y[p] = int(y_v.get() or 0)
                tree.insert("", tk.END, iid=p, values=(p, per_px[p], per_y[p]))

    def add_folder():
        folder = filedialog.askdirectory()
        if folder:
            for p in sorted(Path(folder).rglob("*")):
                if p.is_file() and (p.name.endswith("._xgfn") or p.suffix.lower() == ".xgfn"):
                    s = str(p)
                    if s not in templates:
                        templates.append(s)
                        per_px[s] = default_font_px(p.read_bytes())
                        per_y[s] = int(y_v.get() or 0)
                        tree.insert("", tk.END, iid=s, values=(s, per_px[s], per_y[s]))

    def build():
        try:
            if not font_v.get() or not templates:
                raise ValueError("select font and XGFN files")
            out_dir = Path(out_v.get() or "xgfn_output")
            for t in templates:
                px = int(px_v.get()) if px_v.get() else per_px.get(t)
                ys = int(y_v.get()) if y_v.get() else per_y.get(t, 0)
                build_xgfn(
                    t,
                    font_v.get(),
                    out_dir / Path(t).name,
                    chars_file=chars_v.get() or None,
                    font_px=px,
                    width=int(w_v.get()) if w_v.get() else None,
                    height=int(h_v.get()) if h_v.get() else None,
                    y_shift=ys,
                    preview_dir=out_dir.parent / (out_dir.name + "_preview"),
                )
            messagebox.showinfo("Done", f"Built {len(templates)} files")
        except Exception as exc:
            messagebox.showerror("Error", str(exc))

    frm = ttk.Frame(root, padding=10)
    frm.pack(fill=tk.BOTH, expand=True)
    frm.columnconfigure(1, weight=1)
    frm.rowconfigure(6, weight=1)
    ttk.Label(frm, text="Font").grid(row=0, column=0, sticky="w")
    ttk.Entry(frm, textvariable=font_v).grid(row=0, column=1, sticky="ew")
    ttk.Button(frm, text="Browse", command=pick_font).grid(row=0, column=2)
    ttk.Label(frm, text="Chars").grid(row=1, column=0, sticky="w")
    ttk.Entry(frm, textvariable=chars_v).grid(row=1, column=1, sticky="ew")
    ttk.Button(frm, text="Browse", command=pick_chars).grid(row=1, column=2)
    ttk.Label(frm, text="Output").grid(row=2, column=0, sticky="w")
    ttk.Entry(frm, textvariable=out_v).grid(row=2, column=1, sticky="ew")
    ttk.Button(frm, text="Browse", command=pick_out).grid(row=2, column=2)
    opts = ttk.Frame(frm)
    opts.grid(row=3, column=1, sticky="w")
    for label, var in (("Font px", px_v), ("Y shift", y_v), ("Width", w_v), ("Height", h_v)):
        ttk.Label(opts, text=label).pack(side=tk.LEFT)
        ttk.Entry(opts, textvariable=var, width=8).pack(side=tk.LEFT, padx=6)
    def edit_cell(event):
        item = tree.focus()
        if not item:
            return
        col = tree.identify_column(event.x)
        if col not in ("#2", "#3"):
            return
        field = "px" if col == "#2" else "y"
        x, y, w, h = tree.bbox(item, col)
        entry = ttk.Entry(tree, width=8)
        entry.insert(0, tree.set(item, field))
        entry.place(x=x, y=y, width=w, height=h)
        entry.focus()
        def save(_event=None):
            try:
                val = int(entry.get())
                if field == "px":
                    per_px[item] = val
                else:
                    per_y[item] = val
                tree.set(item, field, val)
            finally:
                entry.destroy()
        entry.bind("<Return>", save)
        entry.bind("<FocusOut>", save)

    buttons = ttk.Frame(frm)
    buttons.grid(row=4, column=1, sticky="w", pady=5)
    ttk.Button(buttons, text="Add Files", command=add_files).pack(side=tk.LEFT)
    ttk.Button(buttons, text="Add Folder", command=add_folder).pack(side=tk.LEFT, padx=6)
    ttk.Button(buttons, text="Clear", command=lambda: (templates.clear(), per_px.clear(), per_y.clear(), tree.delete(*tree.get_children()))).pack(side=tk.LEFT)
    tree = ttk.Treeview(frm, columns=("file", "px", "y"), show="headings")
    tree.heading("file", text="XGFN")
    tree.heading("px", text="Font px")
    tree.heading("y", text="Y shift")
    tree.column("file", width=580)
    tree.column("px", width=80, anchor="center")
    tree.column("y", width=80, anchor="center")
    tree.bind("<Double-1>", edit_cell)
    tree.grid(row=6, column=0, columnspan=3, sticky="nsew")
    ttk.Button(frm, text="Build", command=build).grid(row=7, column=2, sticky="e", pady=8)
    root.mainloop()


def main():
    ap = argparse.ArgumentParser(
        description=(
            "Rebuild Risen XGFN font files from an existing XGFN template and a TTF/OTF font.\n\n"
            "template means one or more source XGFN files, usually files ending with ._xgfn or .xgfn.\n"
            "The template supplies the original container/header layout, LOGFONT data, default font size,\n"
            "DDS style, and existing character list unless --no-template-chars is used."
        ),
        epilog=(
            "Examples:\n"
            "  python xgfn_rebuild.py NotoSansTC-Regular.ttf src_xgfn\\font._xgfn\n"
            "  python xgfn_rebuild.py NotoSansTC-Regular.ttf src_xgfn\\*.xgfn --chars-file unique_characters.txt\n"
            "  python xgfn_rebuild.py NotoSansTC-Regular.ttf src_xgfn\\font._xgfn --font-px 22 --y-shift -2 -o out\\font._xgfn\n"
            "  python xgfn_rebuild.py --gui\n\n"
            "Notes:\n"
            "  - Multiple template files are written to --out-dir with the same file names.\n"
            "  - A PNG atlas preview is also written beside --out-dir, named <out-dir>_preview.\n"
            "  - Use --chars-file or --text to add extra characters to the rebuilt XGFN.\n"
            "  - Use --no-template-chars only if you want to discard all characters from the template."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "font",
        nargs="?",
        metavar="FONT",
        help="TTF/OTF font used to render glyphs, for example NotoSansTC-Regular.ttf.",
    )
    ap.add_argument(
        "template",
        nargs="*",
        metavar="XGFN_TEMPLATE",
        help="Source XGFN template file(s), usually ._xgfn or .xgfn. Multiple files are supported.",
    )
    ap.add_argument(
        "--chars-file",
        metavar="TXT",
        help="Text file containing extra characters to include. UTF-8, UTF-8 BOM, UTF-16 LE, and UTF-16 BE are detected.",
    )
    ap.add_argument(
        "--text",
        default="",
        help="Extra characters supplied directly on the command line.",
    )
    ap.add_argument(
        "--font-px",
        type=int,
        metavar="PX",
        help="Override rendered font size in pixels. Defaults to the template LOGFONT height.",
    )
    ap.add_argument(
        "--atlas-width",
        type=int,
        metavar="PX",
        help="Force DDS atlas width. If omitted, the script uses the template size or auto-grows when needed.",
    )
    ap.add_argument(
        "--atlas-height",
        type=int,
        metavar="PX",
        help="Force DDS atlas height. If omitted, the script uses the template size or auto-grows when needed.",
    )
    ap.add_argument(
        "--y-shift",
        type=int,
        default=-2,
        metavar="PX",
        help="Vertical glyph offset in pixels. Negative values move glyphs up; default: -2.",
    )
    ap.add_argument(
        "--out-dir",
        default="xgfn_output",
        metavar="DIR",
        help="Output folder used when rebuilding one or more templates; default: xgfn_output.",
    )
    ap.add_argument(
        "-o",
        "--out",
        metavar="XGFN",
        help="Output file path for a single template. Cannot be used with multiple templates.",
    )
    ap.add_argument(
        "--dds-template",
        metavar="XGFN_OR_DDS",
        help="Optional file whose DDS header/style is used instead of the main XGFN template DDS.",
    )
    ap.add_argument(
        "--no-template-chars",
        action="store_true",
        help="Do not keep the original character map from the template; only use --chars-file and --text.",
    )
    ap.add_argument(
        "--gui",
        action="store_true",
        help="Open the graphical builder. FONT and XGFN_TEMPLATE arguments are not required in GUI mode.",
    )
    args = ap.parse_args()
    if args.gui:
        run_gui()
        return
    if not args.font or not args.template:
        ap.error("font and template are required unless --gui is used")
    if args.out and len(args.template) != 1:
        ap.error("--out only supports one template")
    for t in args.template:
        build_xgfn(
            t,
            args.font,
            Path(args.out) if args.out else Path(args.out_dir) / Path(t).name,
            chars_file=args.chars_file,
            text=args.text,
            font_px=args.font_px,
            width=args.atlas_width,
            height=args.atlas_height,
            y_shift=args.y_shift,
            dds_template=args.dds_template,
            preview_dir=Path(args.out_dir + "_preview"),
            keep_template=not args.no_template_chars,
        )


if __name__ == "__main__":
    main()
