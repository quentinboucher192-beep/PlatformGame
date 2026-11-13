#!/usr/bin/env python3
"""Characters / objects / VFX generator for Cosmic Platformer.

Run from the repository root:
    python3 tools/assetgen/characters/generate.py [only ...]

Regenerates (deterministically) everything in docs/ASSET_SPEC.md sections
3, 4, 5, 6 and 10, plus ui/app_icon.png and installer/app.ico.
Optional arguments restrict the run to some groups:
    nova enemies boss projectiles objects fx icon
"""
import os
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))  # tools/assetgen -> `common`


def main(argv):
    import boss
    import enemies
    import fx
    import icon
    import nova
    import objects
    import projectiles
    import validate

    groups = [
        ("nova", nova.build),
        ("enemies", enemies.build),
        ("boss", boss.build),
        ("projectiles", projectiles.build),
        ("objects", objects.build),
        ("fx", fx.build),
        ("icon", icon.build),
    ]
    only = set(argv)
    for name, fn in groups:
        if only and name not in only:
            continue
        t = time.time()
        fn()
        print(f"  characters/{name}: done in {time.time() - t:.1f}s", flush=True)
    if not only:
        errors = validate.run()
        if errors:
            for e in errors:
                print("  VALIDATION:", e)
            sys.exit(1)
        print("  characters: validation OK")


if __name__ == "__main__":
    main(sys.argv[1:])
