#!/usr/bin/env python3
"""Notes of a GitHub release (the « release » job of .github/workflows/build.yml).

    python3 tools/release/notes.py <version> [--since <previous version>] [--exe installer.exe]

Prints the lines of data/news/changelog.json (French) for this version and every version after
--since (the last GitHub release: versions delivered between two releases keep their notes), their
English from data/i18n/en.json, then the installer's SHA-256 when given. Exits with 1 when the
changelog has no entry for this version (no release without notes).
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    args = sys.argv[1:]
    version = args.pop(0)
    since, exe_path = None, None
    while args:
        flag = args.pop(0)
        if flag == "--since" and args:
            since = args.pop(0).lstrip("v")
        elif flag == "--exe" and args:
            exe_path = args.pop(0)
    key = lambda v: tuple(int(x) for x in v.split("."))
    changelog = json.loads((ROOT / "data/news/changelog.json").read_text(encoding="utf-8"))
    if not changelog.get(version):
        print(f"aucune entrée « {version} » dans data/news/changelog.json", file=sys.stderr)
        return 1
    versions = [v for v in changelog if key(v) <= key(version) and (since is None or key(v) > key(since))]
    if since is None:
        versions = [version]
    versions.sort(key=key, reverse=True)
    english = json.loads((ROOT / "data/i18n/en.json").read_text(encoding="utf-8")).get("strings", {})
    out = [f"## Cosmic Platformer {version}"]
    for title, translate in (("Nouveautés", False), ("What's new", True)):
        out += ["", f"### {title}"]
        for v in versions:
            if len(versions) > 1:
                out += ["", f"**{v}**"]
            out += [""] + [f"- {english.get(line, line) if translate else line}" for line in changelog[v]]
    if exe_path:
        exe = Path(exe_path)
        digest = hashlib.sha256(exe.read_bytes()).hexdigest()
        out += ["", f"Installateur / installer : `{exe.name}` — SHA-256 `{digest}`"]
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
