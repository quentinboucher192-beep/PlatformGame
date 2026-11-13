#!/usr/bin/env python3
"""Regenerates every procedural asset of Cosmic Platformer.

Usage: python3 tools/assetgen/generate_all.py
Each sub-generator is deterministic (fixed seeds) and writes into assets/.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS = ["characters/generate.py", "environment/generate.py", "audio/generate.py"]


def main():
    for step in STEPS:
        path = os.path.join(HERE, step)
        print(f"== {step}", flush=True)
        subprocess.check_call([sys.executable, path], cwd=os.path.dirname(os.path.dirname(HERE)))
    print("All assets generated.")


if __name__ == "__main__":
    main()
