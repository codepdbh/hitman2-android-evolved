#!/usr/bin/env python3
"""
pe_inventory.py - inventory every PE module of the game install.

Writes (all facts read from the files; nothing here is inferred by name):

    analysis/pe_inventory.json     full per-module record
    analysis/modules.json          compact module graph (who imports whom)
    analysis/binary_versions.json  hashes + timestamps + version resources
    analysis/dependencies.md       dependency report
    analysis/imports_report.md     import surface per system DLL

Read-only on the game directory.

    python tools/pe_inventory.py ["Hitman 2 Silent Assassin"]
"""
from __future__ import annotations

import collections
import datetime
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis"

# Section names that copy-protection / packer vendors use. Presence is reported,
# nothing more.
PROTECTION_SECTIONS = {
    ".cms_t": "SecuROM", ".cms_d": "SecuROM", ".securom": "SecuROM",
    "stxt371": "SafeDisc", "stxt774": "SafeDisc", ".sforce": "StarForce",
    "upx0": "UPX", "upx1": "UPX", ".petite": "Petite", ".aspack": "ASPack", ".bind": "SteamStub",
}

SCN_CODE, SCN_EXEC, SCN_WRITE = 0x20, 0x20000000, 0x80000000


def entropy(b: bytes) -> float:
    if not b:
        return 0.0
    counts = collections.Counter(b)
    n = len(b)
    return -sum(c / n * math.log2(c / n) for c in counts.values())


def version_info(pe: pefile.PE) -> dict:
    out = {}
    try:
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
        for fi in getattr(pe, "FileInfo", []) or []:
            for entry in fi:
                for st in getattr(entry, "StringTable", []) or []:
                    for k, v in st.entries.items():
                        out[k.decode("latin1")] = v.decode("latin1")
    except Exception as ex:                      # a malformed resource dir is a fact worth keeping
        out["_error"] = str(ex)
    return out


def analyse(path: Path, game_root: Path) -> dict:
    raw = path.read_bytes()
    pe = pefile.PE(data=raw, fast_load=False)
    oh, fh = pe.OPTIONAL_HEADER, pe.FILE_HEADER
    sections, protections, anomalies = [], set(), []
    for s in pe.sections:
        name = s.Name.rstrip(b"\0").decode("latin1")
        ch = s.Characteristics
        sections.append({
            "name": name, "rva": s.VirtualAddress, "vsize": s.Misc_VirtualSize,
            "raw_offset": s.PointerToRawData, "raw_size": s.SizeOfRawData,
            "characteristics": f"{ch:#010x}",
            "code": bool(ch & SCN_CODE), "execute": bool(ch & SCN_EXEC), "write": bool(ch & SCN_WRITE),
            "entropy": round(entropy(s.get_data()), 4),
        })
        if name.lower() in PROTECTION_SECTIONS:
            protections.add(PROTECTION_SECTIONS[name.lower()])
    if not any(s["execute"] or s["code"] for s in sections):
        anomalies.append("no section is flagged code/executable (header metadata does not describe the code)")
    ep = oh.AddressOfEntryPoint
    ep_sec = next((s["name"] for s in sections if s["rva"] <= ep < s["rva"] + max(s["vsize"], s["raw_size"])), None)
    end_of_sections = max((s.PointerToRawData + s.SizeOfRawData for s in pe.sections), default=0)
    overlay = len(raw) - end_of_sections if len(raw) > end_of_sections else 0

    imports = {}
    for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        imports[d.dll.decode("latin1")] = [
            (i.name.decode("latin1") if i.name else f"ordinal#{i.ordinal}") for i in d.imports]
    delay = {}
    for d in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
        delay[d.dll.decode("latin1")] = [
            (i.name.decode("latin1") if i.name else f"ordinal#{i.ordinal}") for i in d.imports]
    exports = []
    ex = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    if ex:
        exports = [{"ordinal": s.ordinal, "rva": s.address,
                    "name": s.name.decode("latin1") if s.name else None,
                    "forwarder": s.forwarder.decode("latin1") if s.forwarder else None} for s in ex.symbols]
    n_relocs = sum(len(b.entries) for b in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []))

    # __FILE__ strings surviving in the image: evidence of which source files
    # were linked in (used for subsystem mapping, see analysis/glacier_map.md).
    src_files = sorted(set(m.group().decode("latin1").lstrip("D") for m in
                           re.finditer(rb"[A-Za-z]:\\[\x20-\x7e]{4,120}?\.(?:cpp|c|h|inl)\b", raw)))

    return {
        "file": str(path.relative_to(game_root)).replace("\\", "/"),
        "size": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "md5": hashlib.md5(raw).hexdigest(),
        "machine": pefile.MACHINE_TYPE.get(fh.Machine, hex(fh.Machine)),
        "pe32_plus": oh.Magic == 0x20B,
        "is_dll": bool(fh.Characteristics & 0x2000),
        "subsystem": pefile.SUBSYSTEM_TYPE.get(oh.Subsystem, str(oh.Subsystem)),
        "image_base": f"{oh.ImageBase:#010x}",
        "size_of_image": oh.SizeOfImage,
        "entry_point_rva": f"{ep:#010x}",
        "entry_point_section": ep_sec,
        "entry_point_bytes": pe.get_data(ep, 16).hex() if ep else None,
        "linker": f"{oh.MajorLinkerVersion}.{oh.MinorLinkerVersion:02d}",
        "timestamp": fh.TimeDateStamp,
        "timestamp_utc": datetime.datetime.fromtimestamp(fh.TimeDateStamp, datetime.UTC).strftime("%Y-%m-%d %H:%M:%S"),
        "has_relocations": n_relocs > 0,
        "relocation_count": n_relocs,
        "overlay_bytes": overlay,
        "sections": sections,
        "protection_indicators": sorted(protections),
        "anomalies": anomalies,
        "imports": imports,
        "delay_imports": delay,
        "exports": exports,
        "version_info": version_info(pe),
        "source_file_strings": src_files,
    }


def main() -> int:
    game = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "Hitman 2 Silent Assassin"
    if not game.is_dir():
        print(f"game directory not found: {game}", file=sys.stderr)
        return 2
    mods = []
    for p in sorted(game.rglob("*")):
        if p.is_file() and p.suffix.lower() in (".exe", ".dll"):
            try:
                mods.append(analyse(p, game))
            except pefile.PEFormatError as ex:
                mods.append({"file": str(p.relative_to(game)), "error": str(ex)})
    OUT.mkdir(exist_ok=True)
    local = {m["file"].lower(): m for m in mods if "error" not in m}

    (OUT / "pe_inventory.json").write_text(json.dumps({"game_dir": game.name, "modules": mods}, indent=1), encoding="utf-8")

    # ---- module graph -------------------------------------------------------
    graph = []
    for m in local.values():
        deps = sorted(m["imports"], key=str.lower)
        graph.append({
            "module": m["file"], "kind": "dll" if m["is_dll"] else "exe",
            "imports_local": [d for d in deps if d.lower() in local],
            "imports_system": [d for d in deps if d.lower() not in local],
            "imported_by": sorted(o["file"] for o in local.values()
                                  if any(d.lower() == m["file"].lower() for d in o["imports"])),
            "export_count": len(m["exports"]),
            "import_count": sum(len(v) for v in m["imports"].values()),
        })
    (OUT / "modules.json").write_text(json.dumps(graph, indent=1), encoding="utf-8")

    (OUT / "binary_versions.json").write_text(json.dumps([
        {k: m[k] for k in ("file", "size", "sha256", "md5", "timestamp_utc", "linker", "image_base",
                           "entry_point_rva", "entry_point_section", "has_relocations",
                           "protection_indicators", "anomalies", "version_info")}
        for m in local.values()], indent=1), encoding="utf-8")

    # ---- dependencies.md ----------------------------------------------------
    L = ["# Module dependencies", "",
         f"Generated by `tools/pe_inventory.py` from `{game.name}/`. Facts read from PE headers only.", "",
         "| Module | Kind | Size | Linker | Built (UTC) | Base | Relocs | Imports | Exports | Protection indicators |",
         "|---|---|--:|---|---|---|---|--:|--:|---|"]
    for m in local.values():
        L.append(f"| `{m['file']}` | {'DLL' if m['is_dll'] else 'EXE'} | {m['size']:,} | {m['linker']} | "
                 f"{m['timestamp_utc'][:10]} | {m['image_base']} | {'yes' if m['has_relocations'] else 'no'} | "
                 f"{sum(len(v) for v in m['imports'].values())} | {len(m['exports'])} | "
                 f"{', '.join(m['protection_indicators']) or '—'} |")
    L += ["", "## Who imports whom (static import tables)", ""]
    for g in graph:
        L.append(f"- `{g['module']}`")
        L.append(f"  - local: {', '.join('`%s`' % d for d in g['imports_local']) or '—'}")
        L.append(f"  - system: {', '.join('`%s`' % d for d in g['imports_system']) or '—'}")
        if g["imported_by"]:
            L.append(f"  - imported by: {', '.join('`%s`' % d for d in g['imported_by'])}")
    L += ["", "## Anomalies", ""]
    for m in local.values():
        for a in m["anomalies"]:
            L.append(f"- `{m['file']}`: {a}")
        if m["overlay_bytes"]:
            L.append(f"- `{m['file']}`: {m['overlay_bytes']:,} bytes of overlay after the last section")
    L += ["", "## Source files named inside each module (`__FILE__` strings)", ""]
    for m in local.values():
        if m["source_file_strings"]:
            L.append(f"### `{m['file']}` ({len(m['source_file_strings'])})")
            L.append("")
            L += [f"- `{s}`" for s in m["source_file_strings"]]
            L.append("")
    (OUT / "dependencies.md").write_text("\n".join(L) + "\n", encoding="utf-8")

    # ---- imports_report.md --------------------------------------------------
    by_dll = collections.defaultdict(lambda: collections.defaultdict(set))
    for m in local.values():
        for dll, names in m["imports"].items():
            for n in names:
                by_dll[dll.lower()][n].add(m["file"])
    R = ["# Import surface", "",
         "Every function the game's modules import, grouped by provider. Functions from",
         "system DLLs are the Win32/DirectX surface the Android platform layer must answer;",
         "functions from local modules are engine-internal links resolved by recompiling both sides.", ""]
    R += ["| Provider | Local? | Distinct functions | Used by |", "|---|---|--:|---|"]
    for dll in sorted(by_dll):
        users = sorted({u for us in by_dll[dll].values() for u in us})
        R.append(f"| `{dll}` | {'yes' if dll in local else 'no'} | {len(by_dll[dll])} | {', '.join('`%s`' % u for u in users)} |")
    for dll in sorted(by_dll):
        R += ["", f"## `{dll}` ({len(by_dll[dll])})", "", "| Function | Imported by |", "|---|---|"]
        for n in sorted(by_dll[dll], key=str.lower):
            R.append(f"| `{n}` | {', '.join(sorted(by_dll[dll][n]))} |")
    (OUT / "imports_report.md").write_text("\n".join(R) + "\n", encoding="utf-8")

    print(f"{len(mods)} modules -> {OUT}")
    for m in local.values():
        print(f"  {m['file']:<20} {m['sha256'][:16]}  {m['timestamp_utc'][:10]}  "
              f"imports={sum(len(v) for v in m['imports'].values()):<4} exports={len(m['exports']):<4} "
              f"{','.join(m['protection_indicators'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
