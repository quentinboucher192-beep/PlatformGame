#!/usr/bin/env python3
"""Assets nommés par le code du jeu : écrit data/assets/code_refs.json.

ADMINISTRATION › Assets (2.63) dit qui utilise chaque fichier de assets/. Les définitions
(data/) se lisent dans le jeu installé, mais pas le code : ce script relit les sources C++
(engine/, game/, ui/, editors/, packaging/, saves/, main.cpp) et note, pour chaque asset
installé, les fichiers sources qui le nomment et combien de fois :
  · une chaîne littérale égale au chemin d'un asset : "sounds/jump.wav" ;
  · uiSound("ui_confirm") : le son d'interface sounds/ui_confirm.wav ;
  · sheetOf("lava_flood") (les dangers des boss) : la planche animations/lava_flood.json ;
  · un nom de fichier seul, quand un seul asset le porte : Paths::assetsDir() / "online" /
    "earth_countries.png", font("Orbitron.ttf").
Le catalogue lui-même (AssetCatalog.cpp, AdminAssets.cpp, AssetPreview.cpp) n'est pas lu : il
nomme des assets pour décrire leurs utilisations, sans s'en servir. Les commentaires et les chaînes brutes (R"(…)") sont ignorés. Les chemins construits par
morceaux ("textures/tiles/" + m + ".json") ne se devinent pas : le catalogue les tire des
données (niveaux, ennemis…) et des règles du jeu.

Le test « asset catalogue: code_refs.json is up to date » (tests/test_asset_catalog.cpp)
refait la même lecture et échoue si ce fichier n'est pas à jour.

Le même lancement met à jour (ou vérifie) data/assets/asset_versions.json, la version où chaque
asset est apparu (tools/assets/asset_versions.py) : après une fusion qui apporte des assets, le
lancement habituel leur donne leur pastille « NOUVEAU ».

Usage (depuis la racine du dépôt) :
    python3 tools/assets/code_refs.py           # écrit data/assets/code_refs.json (et asset_versions.json)
    python3 tools/assets/code_refs.py --check   # code de sortie 1 si l'un des deux n'est pas à jour
"""
import argparse
import json
import os
import sys

import asset_versions  # tools/assets/asset_versions.py (this script's folder)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "data", "assets", "code_refs.json")
CODE_DIRS = ["engine", "game", "ui", "editors", "packaging", "saves"]
CODE_FILES = ["main.cpp"]
# The catalogue itself names assets to describe their uses, without using them.
EXCLUDED = {"game/content/AssetCatalog.cpp", "ui/screens/AdminAssets.cpp", "ui/widgets/AssetPreview.cpp"}


def installed_assets():
    out = set()
    base = os.path.join(ROOT, "assets")
    for root, _dirs, files in os.walk(base):
        for f in files:
            out.add(os.path.relpath(os.path.join(root, f), base).replace(os.sep, "/"))
    return out


def literals(src):
    """(literal, previous identifier, '(' between them) of each ordinary string literal.

    Same rules as scanCodeRefs() in tests/test_asset_catalog.cpp."""
    i, n = 0, len(src)
    last_id, paren = "", False
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '"':
            j, out = i + 1, []
            while j < n and src[j] != '"' and src[j] != "\n":
                if src[j] == "\\" and j + 1 < n:
                    out.append(src[j + 1])
                    j += 2
                    continue
                out.append(src[j])
                j += 1
            yield "".join(out), last_id, paren
            i = j + 1
            last_id, paren = "", False
            continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'" and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
            last_id, paren = "", False
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            if src[i:j] in ("R", "u8R", "uR", "UR", "LR") and j < n and src[j] == '"':
                # A raw string: skipped up to its closing delimiter.
                k = src.find("(", j + 1)
                if k < 0:
                    break
                end = src.find(")" + src[j + 1:k] + '"', k + 1)
                i = n if end < 0 else end + (k - j) + 1
                last_id, paren = "", False
                continue
            last_id, paren = src[i:j], False
            i = j
            continue
        if c.isdigit():
            j = i
            while j < n and (src[j].isalnum() or src[j] in "_.'"):
                j += 1
            i = j
            last_id, paren = "", False
            continue
        if c.isspace():
            i += 1
            continue
        if c == "(" and last_id and not paren:
            paren = True
        else:
            last_id, paren = "", False
        i += 1


def scan(assets):
    refs = {}
    by_name = {}
    for a in sorted(assets):
        by_name.setdefault(a.rsplit("/", 1)[-1], []).append(a)
    files = []
    for d in CODE_DIRS:
        for root, _dirs, names in os.walk(os.path.join(ROOT, d)):
            for f in names:
                if f.endswith((".cpp", ".h")):
                    files.append(os.path.join(root, f))
    files += [os.path.join(ROOT, f) for f in CODE_FILES]
    for path in sorted(files):
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        if rel in EXCLUDED:
            continue
        with open(path, encoding="utf-8") as fh:
            src = fh.read()
        for lit, ident, paren in literals(src):
            name = lit if lit in assets else None
            if not name and ident == "uiSound" and paren:
                cand = "sounds/" + lit + ".wav"
                name = cand if cand in assets else None
            if not name and ident == "sheetOf" and paren:
                cand = "animations/" + lit + ".json"
                name = cand if cand in assets else None
            if not name and "/" not in lit and "." in lit and len(by_name.get(lit, ())) == 1:
                name = by_name[lit][0]
            if name:
                refs.setdefault(name, {})
                refs[name][rel] = refs[name].get(rel, 0) + 1
    return {k: dict(sorted(v.items())) for k, v in sorted(refs.items())}


def render(refs):
    doc = {
        "format": "cosmic.asset_code_refs",
        "comment": "Assets named by the game's code, with the source files naming them (generated by tools/assets/code_refs.py: do not edit).",
        "refs": refs,
    }
    return json.dumps(doc, ensure_ascii=False, indent=1) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="vérifie sans écrire")
    args = ap.parse_args()
    text = render(scan(installed_assets()))
    old = None
    if os.path.exists(OUT):
        with open(OUT, encoding="utf-8") as fh:
            old = fh.read()
    # 2.69: the version in which each asset appeared (« NOUVEAU » in ADMINISTRATION › Assets)
    # follows the same files: refreshed (or checked) in the same run, after a merge too.
    versions = asset_versions.run(check=args.check)
    if args.check:
        if old != text:
            print("data/assets/code_refs.json n'est pas à jour : lancez python3 tools/assets/code_refs.py")
            return 1
        print("data/assets/code_refs.json est à jour.")
        return versions
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("data/assets/code_refs.json : %d assets nommés par le code." % len(json.loads(text)["refs"]))
    return versions


if __name__ == "__main__":
    sys.exit(main())
