#!/usr/bin/env python3
"""Extracts a Wallpaper Engine scene.pkg so scene.json, scripts, models and materials can be inspected.

usage: tools/dev/extract-pkg.py <scene.pkg> <output dir>

Every script embedded in scene.json is also written to <output dir>/scripts/NN.js, with a header listing
the layer/property paths that use it.
"""
import json
import os
import struct
import sys


def extract(pkg_path: str, out: str) -> None:
    with open(pkg_path, "rb") as f:
        def u32() -> int:
            return struct.unpack("<I", f.read(4))[0]

        def string() -> str:
            return f.read(u32()).decode()

        version = string()
        entries = [(string(), u32(), u32()) for _ in range(u32())]
        base = f.tell()

        for name, offset, length in entries:
            target = os.path.join(out, name)
            os.makedirs(os.path.dirname(target) or out, exist_ok=True)
            f.seek(base + offset)
            with open(target, "wb") as dst:
                dst.write(f.read(length))

    print(f"{version}: extracted {len(entries)} files to {out}")


def dump_scripts(out: str) -> None:
    scene_path = os.path.join(out, "scene.json")
    if not os.path.exists(scene_path):
        return

    with open(scene_path) as f:
        scene = json.load(f)

    scripts: dict[str, list[str]] = {}

    def walk(node, path: str) -> None:
        if isinstance(node, dict):
            if isinstance(node.get("script"), str):
                scripts.setdefault(node["script"], []).append(path)
            for key, value in node.items():
                walk(value, f"{path}/{key}")
        elif isinstance(node, list):
            for index, value in enumerate(node):
                walk(value, f"{path}[{index}]")

    for obj in scene.get("objects", []):
        walk(obj, str(obj.get("name", obj.get("id"))))

    os.makedirs(os.path.join(out, "scripts"), exist_ok=True)
    for index, (source, paths) in enumerate(scripts.items()):
        with open(os.path.join(out, "scripts", f"{index:02d}.js"), "w") as f:
            f.write("".join(f"// {p}\n" for p in paths) + source)

    print(f"wrote {len(scripts)} unique scripts to {out}/scripts")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    extract(sys.argv[1], sys.argv[2])
    dump_scripts(sys.argv[2])
