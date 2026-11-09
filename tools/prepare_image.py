#!/usr/bin/env python3
"""
prepare_image.py - make an analysis copy of a game binary with honest section flags.

This hitman2.exe has every section marked 0xC0000040 (initialised data, RW):
no section carries IMAGE_SCN_CNT_CODE or IMAGE_SCN_MEM_EXECUTE, so tools that
find code by section flags (pcrecomp's disasm32.py among them) see an image
with no code at all. The bytes are fine; only the header metadata is wrong.

The original is never touched. A copy is written under build/work/ with the
section characteristics corrected, and a JSON sidecar records the source
SHA-256 and exactly which header fields changed. Nothing else in the file is
altered: every byte outside the section table is identical, and that is
verified before the copy is accepted.

    python tools/prepare_image.py "Hitman 2 Silent Assassin/hitman2.exe"
"""
from __future__ import annotations

import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build" / "work"

CODE = 0x60000020        # CNT_CODE | MEM_EXECUTE | MEM_READ
RDATA = 0x40000040       # CNT_INITIALIZED_DATA | MEM_READ
DATA = 0xC0000040        # CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE

# Conventional MSVC meanings. Sections not listed keep their flags (.cms_t and
# .cms_d are the copy-protection vendor's and are deliberately left as data:
# nothing here analyses or executes them).
WANTED = {".text": CODE, ".rdata": RDATA, ".data": DATA, ".idata": DATA, ".rsrc": RDATA, ".reloc": RDATA}


def prepare(src: Path) -> Path:
    raw = bytearray(src.read_bytes())
    src_sha = hashlib.sha256(raw).hexdigest()
    e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
    if raw[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise SystemExit(f"{src}: not a PE")
    nsec = struct.unpack_from("<H", raw, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", raw, e_lfanew + 20)[0]
    sec_off = e_lfanew + 24 + opt_size
    changes, touched = [], set()
    for i in range(nsec):
        off = sec_off + i * 40
        name = raw[off:off + 8].rstrip(b"\0").decode("latin1")
        old = struct.unpack_from("<I", raw, off + 36)[0]
        new = WANTED.get(name, old)
        if new != old:
            struct.pack_into("<I", raw, off + 36, new)
            touched.update(range(off + 36, off + 40))
            changes.append({"section": name, "field": "Characteristics",
                            "file_offset": off + 36, "old": f"{old:#010x}", "new": f"{new:#010x}"})
    # Proof that only those header dwords differ.
    orig = src.read_bytes()
    diff = [k for k in range(len(orig)) if orig[k] != raw[k]]
    if not set(diff) <= touched:
        raise SystemExit("internal error: bytes outside the section table changed")

    WORK.mkdir(parents=True, exist_ok=True)
    dst = WORK / (src.stem.lower() + ".norm" + src.suffix.lower())
    dst.write_bytes(raw)
    side = {"source": str(src), "source_sha256": src_sha,
            "output": str(dst.relative_to(ROOT)), "output_sha256": hashlib.sha256(raw).hexdigest(),
            "bytes_changed": len(diff), "changes": changes,
            "note": "Header metadata only. Used for static analysis; never executed, never distributed."}
    dst.with_suffix(dst.suffix + ".json").write_text(json.dumps(side, indent=2), encoding="utf-8")
    return dst


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = prepare(Path(sys.argv[1]))
    print(out)
