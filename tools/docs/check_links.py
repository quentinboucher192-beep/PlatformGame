#!/usr/bin/env python3
"""Vérifie les liens de la documentation (tous les fichiers .md du dépôt).

1. Chaque lien relatif et chaque image (Markdown `[…](…)`, `![…](…)`, définitions `[x]: …`,
   HTML `src="…"` / `href="…"`) mène à un fichier ou à un dossier qui existe, sans sortir du
   dépôt ; une ancre (`fichier.md#titre`, `#titre`) mène à un titre de ce fichier (règle des
   ancres de GitHub) ou à un `<a id="…">`.
2. Chaque document atteint depuis le README (en suivant les liens vers des `.md`) a son lien
   « ← Retour » en tête (première ligne non vide) et en fin (dernière ligne non vide) : il mène
   à un document qui lui renvoie (son parent) ; un document qui a plusieurs parents mène aussi
   au README. Les lignes « ← Retour » ne comptent pas pour trouver les parents.

Les liens dans les blocs de code et le code en ligne sont ignorés, comme les adresses
externes (`https://…`). Usage, depuis n'importe où :

    python3 tools/docs/check_links.py          # code de sortie 1 s'il y a une erreur
    python3 tools/docs/check_links.py --tree   # affiche aussi l'arbre des documents
"""
import argparse
import os
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
README = "README.md"
BACK = "← Retour"

FENCE = re.compile(r"^\s*(```|~~~)")
INLINE_CODE = re.compile(r"(`+)(?:(?!\1).)+?\1")
COMMENT = re.compile(r"<!--.*?-->", re.S)
MD_LINK = re.compile(r"\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
REF_DEF = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s+\"[^\"]*\")?\s*$")
HTML_ATTR = re.compile(r"\b(?:src|href)\s*=\s*\"([^\"]+)\"")
HTML_ID = re.compile(r"<a\s+(?:[^>]*\s)?(?:id|name)\s*=\s*\"([^\"]+)\"")
HEADING = re.compile(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$")


def md_files():
    """The .md files of the repository (tracked or new, never the ignored ones)."""
    try:
        out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "*.md"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
        files = [f for f in out.splitlines() if f and os.path.isfile(os.path.join(ROOT, f))]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for d, dirs, names in os.walk(ROOT):
            dirs[:] = [x for x in dirs if not x.startswith((".", "build")) and x != "node_modules"]
            files += [os.path.relpath(os.path.join(d, n), ROOT) for n in names if n.endswith(".md")]
    return sorted({f.replace(os.sep, "/") for f in files})  # a file being merged is listed once per stage


def text_lines(path):
    """The lines of a file with code blocks, inline code and comments blanked (numbers kept)."""
    with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
        raw = fh.read()
    raw = COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), raw)
    lines, fenced = [], False
    for line in raw.split("\n"):
        if FENCE.match(line):
            fenced = not fenced
            lines.append("")
            continue
        lines.append("" if fenced else INLINE_CODE.sub("", line))
    return lines


def targets(line):
    found = MD_LINK.findall(line) + HTML_ATTR.findall(line)
    m = REF_DEF.match(line)
    if m:
        found.append(m.group(1))
    return found


def external(t):
    return "://" in t or t.startswith(("mailto:", "data:", "tel:"))


def slug(title, seen):
    """GitHub's anchor of a heading (github-slugger)."""
    t = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", title)  # links and images: their text
    t = re.sub(r"<[^>]+>", "", t)
    t = t.replace("`", "").lower()
    t = re.sub(r"[^\w\- ]", "", t)
    t = t.replace(" ", "-")
    base, n = t, seen.get(t, 0)
    seen[base] = n + 1
    return base if n == 0 else f"{base}-{n}"


def anchors(path, cache={}):
    if path not in cache:
        found, seen = set(), {}
        with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
            fenced = False
            for line in fh:
                if FENCE.match(line):
                    fenced = not fenced
                    continue
                if fenced:
                    continue
                m = HEADING.match(line)
                if m:
                    found.add(slug(m.group(2), seen))
                found.update(HTML_ID.findall(line))
        cache[path] = found
    return cache[path]


def resolve(src, target):
    """(repository path or None when outside, anchor) of a relative link of `src`."""
    path, _, anchor = target.partition("#")
    path = unquote(path.split("?", 1)[0])
    if not path:
        return src, unquote(anchor)
    full = os.path.normpath(os.path.join(ROOT, os.path.dirname(src), path))
    rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
    return (None if rel.startswith("../") or rel == ".." else rel), unquote(anchor)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", action="store_true", help="affiche l'arbre des documents atteints depuis le README")
    args = ap.parse_args()

    files = md_files()
    errors = []
    links = {}  # doc → the .md docs it links to (back links apart)
    backs = {}  # doc → [(line number, [targets])] of its « ← Retour » lines
    for f in files:
        lines = text_lines(f)
        links[f], backs[f] = [], []
        for no, line in enumerate(lines, 1):
            is_back = BACK in line
            resolved = []
            for t in targets(line):
                if external(t):
                    continue
                path, anchor = resolve(f, t)
                where = f"{f}:{no}"
                if path is None:
                    errors.append(f"{where} : « {t} » sort du dépôt")
                    continue
                full = os.path.join(ROOT, path)
                if not os.path.exists(full):
                    errors.append(f"{where} : « {t} » n'existe pas")
                    continue
                if anchor and path.endswith(".md") and anchor not in anchors(path):
                    errors.append(f"{where} : « {t} » : pas de titre « #{anchor} » dans {path}")
                resolved.append(path)
                if path.endswith(".md") and path != f and not is_back:
                    links[f].append(path)
            if is_back:
                backs[f].append((no, resolved))

    # The documents reached from the README, and the parents of each.
    order, parents = [README], {README: []}
    for doc in order:
        for t in links.get(doc, []):
            if t not in parents:
                parents[t] = []
                order.append(t)
            if doc not in parents[t]:
                parents[t].append(doc)
    for doc in order[1:]:
        lines = text_lines(doc)
        nonblank = [i for i, l in enumerate(lines, 1) if l.strip()]
        if not nonblank:
            errors.append(f"{doc} : document vide")
            continue
        mine = dict(backs[doc])
        for where, no in (("en tête", nonblank[0]), ("en fin", nonblank[-1])):
            if no not in mine:
                errors.append(f"{doc}:{no} : pas de lien « {BACK} » {where} (parents : {', '.join(parents[doc])})")
                continue
            got = mine[no]
            if not set(got) & set(parents[doc]):
                errors.append(f"{doc}:{no} : le lien « {BACK} » {where} ne mène à aucun parent ({', '.join(parents[doc])})")
            if len(parents[doc]) > 1 and README not in got:
                errors.append(f"{doc}:{no} : plusieurs parents ({', '.join(parents[doc])}) : le lien « {BACK} » {where} doit aussi mener au README")
            stray = [t for t in got if t not in parents[doc] and t != README]
            if stray:
                errors.append(f"{doc}:{no} : le lien « {BACK} » {where} mène à {', '.join(stray)}, qui ne renvoie pas ici")

    if args.tree:
        for doc in order:
            print(f"{doc}  ← {', '.join(parents[doc]) or '(racine)'}")
    for e in errors:
        print("ERREUR", e)
    print(f"{len(files)} fichiers .md, {len(order)} atteints depuis le README, {len(errors)} erreur(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
