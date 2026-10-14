#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the FM1 Move device page (GitHub Pages, docs/device/):

  docs/device/index.html          web/device.html with, inlined:
                                    /*PROTO*/   SLOOP's editor protocol code (web/editor.html PROTO-BEGIN..END)
                                    /*LIB*/     web/device_lib.js (FM1 Move: backup objects, projects, USR1..3)
                                    /*OTA*/     web/fm1pkg.js + web/fm1ota.js (the installer's updater, unchanged),
                                                in their own scope (they name Link / pack7 as the protocol does)
                                    /*META*/    the firmware packages offered (version, identity, SHA-256)
                                    /*GROUPS*/  the parameter pages of params.c (ids from core.h)
  docs/index.html                 redirect to device/ (web/site_index.html)
  docs/firmware/fm1move-VER.fwsc  the FM1 Move package (experimental: never run on an FM-1)

  web/make_device.py FM1MOVE_PKG VERSION [SLOOP_PKG]
    FM1MOVE_PKG  build/felucca.fwsc of an FM1MOVE=1 build
    SLOOP_PKG    default docs/firmware/sloop-2.6.fwsc (SLOOP's own stable release, as its installer offers it)

Nothing here talks to a device; the page writes to an FM-1 only when its owner presses Install.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from make_site import product_of, strip_module  # noqa: E402  (the same package checks as SLOOP's installer)


def enum_ids(src, first, last):
    """name -> value of the enum that runs from `first` to `last` in core.h (plain names, no explicit values)"""
    body = src[src.index(first):src.index(last) + len(last)]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    names = [n.strip() for n in body.split(",") if n.strip()]
    return {n: i for i, n in enumerate(names)}


def param_groups():
    core = (ROOT / "firmware/src/core.h").read_text(encoding="utf-8")
    params = (ROOT / "firmware/src/params.c").read_text(encoding="utf-8")
    P = enum_ids(core, "P_LEVEL", "P_COUNT")
    G = enum_ids(core, "G_BPM", "G_COUNT")
    table = params[params.index("static const page_t PAGES[] = {"):]
    table = table[:table.index("};")]
    groups = []
    for m in re.finditer(r'\{"([^"]+)",\s*(FAM_\w+),\s*(SC_\w+),\s*GR_\w+,\s*\{([^}]*)\}\}', table):
        name, fam, scope, ids = m.group(1), m.group(2), m.group(3), [x.strip() for x in m.group(4).split(",")]
        if scope not in ("SC_TRACK", "SC_ENGINE", "SC_GLOBAL"):
            continue
        tab = G if scope == "SC_GLOBAL" else P
        # FM1 Move: track n is MIDI channel n; SLOOP 2.6's CH1..CH3 (its three synth tracks) do not apply
        nums = [tab[i] for i in ids if i in tab and i not in ("G_CH1", "G_CH2", "G_CH3")]
        if not nums:
            continue
        groups.append({"name": name, "fam": fam[4:].lower(), "scope": 1 if scope == "SC_GLOBAL" else 0,
                       "engine": scope == "SC_ENGINE", "ids": nums})
    return {"groups": groups, "P": P, "G": G}


def package(path, name_out, version, label, channel):
    raw = Path(path).read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{path}: identity {product!r} is not a Felucca-family package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{path}: no update loader in it")
    return raw, {"version": version, "label": label, "channel": channel, "product": product,
                 "pkg": "../firmware/" + name_out, "sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)}


def main(move_pkg, version, sloop_pkg=None):
    sloop_pkg = Path(sloop_pkg or ROOT / "docs/firmware/sloop-2.6.fwsc")
    move_name = f"fm1move-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    move_raw, move_meta = package(move_pkg, move_name, version, "FM1 Move", "experimental")
    _, sloop_meta = package(sloop_pkg, sloop_pkg.name, re.sub(r"^sloop-|\.fwsc$", "", sloop_pkg.name), "SLOOP", "stable")
    if move_meta["sha256"] == sloop_meta["sha256"]:      # (the package is scrambled: its version string is not visible)
        raise SystemExit(f"{move_pkg} is the SLOOP package, not an FM1MOVE=1 build")

    editor = (HERE / "editor.html").read_text(encoding="utf-8")
    proto = editor[editor.index("/*PROTO-BEGIN*/"):editor.index("/*PROTO-END*/")]
    lib = (HERE / "device_lib.js").read_text(encoding="utf-8")
    ota = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    ota = ("const OTA = (() => {\n" + ota +
           "\nreturn { Updater, productOf, logicalImage, validateStockPackage, OUR_LOADER, sha256hex };\n})();")
    html = (HERE / "device.html").read_text(encoding="utf-8")
    for mark, text in (("/*PROTO*/", proto), ("/*LIB*/", lib), ("/*OTA*/", ota),
                       ("/*META*/", json.dumps({"move": move_meta, "sloop": sloop_meta})),
                       ("/*GROUPS*/", json.dumps(param_groups()))):
        if html.count(mark) != 1:
            raise SystemExit(f"device.html must contain {mark} once")
        html = html.replace(mark, text)
    wasm = ROOT / "docs/sim/fm1sim.wasm"                 # the virtual FM1 asks for this exact build (never a cached one)
    html = html.replace("fm1sim.wasm?v=WASMHASH", "fm1sim.wasm?v=" + hashlib.sha256(wasm.read_bytes()).hexdigest()[:12])
    out = ROOT / "docs/device"
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(HERE / "site_index.html", ROOT / "docs/index.html")   # the site opens on the device page
    fw = ROOT / "docs/firmware"
    for old in fw.glob("fm1move-*.fwsc"):                   # one FM1 Move package: the current one
        old.unlink()
    shutil.copy(move_pkg, fw / move_name)
    print(f"device page: {out / 'index.html'} ({len(html)} B); firmware/{move_name} ({len(move_raw)} B, "
          f"{move_meta['product']}, sha256 {move_meta['sha256'][:12]}…); SLOOP {sloop_meta['version']}")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
