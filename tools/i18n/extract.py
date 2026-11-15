#!/usr/bin/env python3
"""Textes du jeu à traduire : extraction, rapport, fusion des traductions.

Le français reste la langue source : ce script relit tout ce que le jeu affiche — les
chaînes du code (ui/, game/, engine/, editors/, packaging/, saves/, main.cpp), les données
(data/ : niveaux, planètes, ennemis, boss, pouvoirs, succès, actualités, avatars, pays,
conditions d'utilisation) et les messages du serveur Cosmic Online (server/src) — et écrit le
catalogue `tools/i18n/catalog.json`. Le dictionnaire `data/i18n/en.json` (français → anglais)
doit donner une traduction à chaque texte du catalogue (vérifié par tests/test_i18n.cpp).

Les textes composés deviennent des modèles à emplacements :
    "Niveau " + std::to_string(n)        → « Niveau {0} »
    std::format("{} fichiers", n)          → « {0} fichiers »
    "Voir les " + x + (n > 1 ? " réponses" : " réponse")
                                           → « Voir les {0} réponses », « Voir les {0} réponse »
Dans la traduction, {0}… reprennent les parties insérées (traduites à leur tour) ; un mot
peut suivre le nombre d'un emplacement : « {0} {0|level|levels} ».

Usage (depuis la racine du dépôt) :
    python3 tools/i18n/extract.py                 # catalogue + rapport (textes non traduits)
    python3 tools/i18n/extract.py --todo a.json   # écrit les textes à traduire (valeurs vides)
    python3 tools/i18n/extract.py --merge a.json  # ajoute les traductions remplies au dictionnaire
    python3 tools/i18n/extract.py --check         # code de sortie 1 s'il manque des traductions

Ce qui n'est pas affiché (clés JSON, chemins, journaux…) est écarté par des règles simples ;
les exceptions vont dans tools/i18n/ignore.txt (une chaîne exacte par ligne). Les contenus créés
par les joueurs (niveaux de la communauté, pseudonymes, commentaires) ne passent jamais ici.
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CATALOG = os.path.join(ROOT, "tools", "i18n", "catalog.json")
DICTIONARY = os.path.join(ROOT, "data", "i18n", "en.json")
IGNORE = os.path.join(ROOT, "tools", "i18n", "ignore.txt")

CODE_DIRS = ["ui", "game", "engine", "editors", "packaging", "saves"]
CODE_FILES = ["main.cpp"]

# ----------------------------------------------------------------------------- C++ tokens

TOKEN = re.compile(
    r"""
    (?P<ws>\s+)
  | (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<raw>(?:u8|u|U|L)?R"(?P<delim>[^()\\\s]{0,16})\((?P<rawbody>.*?)\)(?P=delim)")
  | (?P<str>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
  | (?P<chr>(?:u8|u|U|L)?'(?:[^'\\\n]|\\.)+')
  | (?P<num>\.?\d(?:[\w.']|[eEpP][+-])*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<op>::|->\*?|\+\+|--|<<=|>>=|<=>|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\.\.\.|[^\s])
    """,
    re.S | re.X,
)

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'", "?": "?", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


def unescape(body):
    out, i = [], 0
    raw = bytearray()

    def flush():
        if raw:
            out.append(raw.decode("utf-8", "replace"))
            raw.clear()

    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            n = body[i + 1]
            if n == "x":
                m = re.match(r"[0-9a-fA-F]+", body[i + 2:])
                raw.append(int(m.group(0), 16) & 0xFF)
                i += 2 + len(m.group(0))
                continue
            if n in "01234567" and n != "0" or (n == "0" and i + 2 < len(body) and body[i + 2] in "01234567"):
                m = re.match(r"[0-7]{1,3}", body[i + 1:])
                raw.append(int(m.group(0), 8) & 0xFF)
                i += 1 + len(m.group(0))
                continue
            if n in "uU":
                size = 4 if n == "u" else 8
                flush()
                out.append(chr(int(body[i + 2:i + 2 + size], 16)))
                i += 2 + size
                continue
            flush()
            out.append(ESCAPES.get(n, n))
            i += 2
            continue
        flush()
        out.append(c)
        i += 1
    flush()
    return "".join(out)


class Tok:
    __slots__ = ("kind", "text", "value", "line")

    def __init__(self, kind, text, value, line):
        self.kind, self.text, self.value, self.line = kind, text, value, line


def tokenize(src):
    toks, line = [], 1
    pos = 0
    # Preprocessor lines (#include "x.h", #define …) are not code to translate.
    src = re.sub(r"(?m)^[ \t]*#(?:[^\n\\]|\\\n|\\.)*", lambda m: "\n" * m.group(0).count("\n"), src)
    while pos < len(src):
        m = TOKEN.match(src, pos)
        if not m:
            pos += 1
            continue
        kind = m.lastgroup
        text = m.group(0)
        if kind == "delim" or kind == "rawbody":
            kind = "raw"
        if m.group("raw") is not None:
            toks.append(Tok("str", text, m.group("rawbody"), line))
        elif m.group("str") is not None:
            body = text[text.index('"') + 1:-1]
            toks.append(Tok("str", text, unescape(body), line))
        elif kind not in ("ws", "comment"):
            toks.append(Tok(kind, text, None, line))
        line += text.count("\n")
        pos = m.end()
    return toks


# ----------------------------------------------------------------------------- what is shown

LETTER = re.compile(r"[A-Za-zÀ-ÖØ-öø-ÿŒœ]")
FR_ACCENT = re.compile(r"[À-ÖØ-öø-ÿŒœ]")
FILE_LIKE = re.compile(r"^[\w\-./]+\.(png|wav|ogg|json|cprz|cplanet|ttf|txt|md|ppm|zip|ico|exe|dll|so|log|tmp|mjs|ts|cpp|h)$", re.I)
IDENT = re.compile(r"^[a-z0-9_\-.:/#=&?%+]+$")
CAMEL = re.compile(r"^[a-z]+(?:[A-Z][a-z0-9]*)+$")
CODE_CONST = re.compile(r"^[A-Z0-9]+(?:_[A-Z0-9]+)+$")
# Calls whose text is never shown to the player.
SKIP_CALLS = {
    "printf", "fprintf", "puts", "SDL_Log", "SDL_SetHint", "getenv", "SDL_GetHint",
    "playSound", "texture", "soundPath", "loadJson", "readText", "playMusic", "uiSound",
    "CHECK", "REQUIRE", "TEST_CASE", "static_assert", "assert",
    "get", "post", "send", "del", "patch", "put",  # HTTP paths of OnlineService
    "pushId", "id", "focused", "setFocus", "setScroll", "editText",
    "contains", "find", "rfind", "count", "erase", "starts_with", "ends_with", "jstr", "jget", "propStr", "propNum",
    "propBool", "param", "setHeader", "icon", "fromUtf8", "path",
}
# First argument only is a widget id / key (the second is shown).
ID_FIRST_ARG = {"button", "toggle", "slider", "sliderInt", "cycle", "textField", "passwordField", "numberField", "hotspot", "listItem",
                "tabs", "beginScroll", "value", "jstr", "jget", "header"}


def load_ignore():
    out = set()
    if os.path.exists(IGNORE):
        with open(IGNORE, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if line and not line.startswith("#"):
                    out.add(line.replace("\\n", "\n"))
    return out


def letters(s):
    return len(LETTER.findall(s))


def displayable(text, words):
    """True when a literal looks like words for the player (not a key, a path, a log…)."""
    core = re.sub(r"\{\d+\}", "", text)
    if not LETTER.search(core):
        return False
    if any(ord(c) < 32 and c not in "\n\t" for c in text):
        return False
    stripped = core.strip()
    if FILE_LIKE.match(stripped) or stripped.startswith(("http://", "https://", "/api", "cosmic.", "sounds/", "ui/", "music/")):
        return False
    if CODE_CONST.match(stripped) or CAMEL.match(stripped):
        return False
    if " " not in stripped and "\n" not in stripped:
        if IDENT.match(stripped):
            # A lowercase word: shown when the same word is used in French sentences.
            w = stripped.lower()
            return w in words and len(w) > 1 and "_" not in w and "/" not in w and "." not in w
        if "_" in stripped and not FR_ACCENT.search(stripped):
            return False
    return True


# ----------------------------------------------------------------------------- expressions

STOP = {",", ";", "?", ":", "=", "+=", "<<", ">>", "==", "!=", "&&", "||", "return", "case", "{", "}", "co_return", "throw",
        "<=", ">=", "!", "-=", "|=", "&="}
OPEN, CLOSE = {"(": ")", "[": "]", "{": "}"}, {")": "(", "]": "[", "}": "{"}


def span_of(toks, i):
    """[a, b) of the expression around token i (stops at , ; ? : = << and the enclosing brackets)."""
    a, depth = i, 0
    while a > 0:
        t = toks[a - 1].text
        if t in CLOSE:
            depth += 1
        elif t in OPEN:
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and t in STOP:
            break
        a -= 1
    b, depth = (matching(toks, i) if toks[i].text in OPEN else i) + 1, 0
    while b < len(toks):
        t = toks[b].text
        if t in OPEN:
            depth += 1
        elif t in CLOSE:
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and t in STOP:
            break
        b += 1
    return a, b


def split_top(toks, a, b, sep):
    parts, depth, start = [], 0, a
    for k in range(a, b):
        t = toks[k].text
        if t in OPEN:
            depth += 1
        elif t in CLOSE:
            depth -= 1
        elif depth == 0 and t == sep:
            parts.append((start, k))
            start = k + 1
    parts.append((start, b))
    return parts


def find_top(toks, a, b, what):
    depth = 0
    for k in range(a, b):
        t = toks[k].text
        if t in OPEN:
            depth += 1
        elif t in CLOSE:
            depth -= 1
        elif depth == 0 and t == what:
            return k
    return -1


def matching(toks, k):
    depth = 0
    for j in range(k, len(toks)):
        if toks[j].text in OPEN:
            depth += 1
        elif toks[j].text in CLOSE:
            depth -= 1
            if depth == 0:
                return j
    return len(toks) - 1


STRING_CTORS = {("std", "string"), ("std", "string_view")}


def operand_alternatives(toks, a, b, depth=0):
    """Pieces of one operand of a '+' chain: a list of alternatives, each a list of pieces
    (str: literal text, None: a slot)."""
    if a >= b:
        return [[None]]
    # "a" "b": adjacent literals.
    if all(toks[k].kind == "str" for k in range(a, b)):
        return [["".join(toks[k].value for k in range(a, b))]]
    # std::string("x"), std::string{"x"}
    if b - a >= 4 and toks[a].text == "std" and toks[a + 1].text == "::" and toks[a + 2].text in ("string", "string_view"):
        k = a + 3
        if k < b and toks[k].text in ("(", "{") and matching(toks, k) == b - 1:
            inner = [toks[j] for j in range(k + 1, b - 1)]
            if all(t.kind == "str" for t in inner):  # std::string() is an empty text
                return [["".join(t.value for t in inner)]]
            if find_top(toks, k + 1, b - 1, "?") >= 0 and depth < 4:  # std::string(c ? "" : ", ")
                return expression_alternatives(toks, k + 1, b - 1, depth + 1)
    # ( … ): a ternary or a chain inside.
    if toks[a].text == "(" and matching(toks, a) == b - 1 and depth < 4:
        return expression_alternatives(toks, a + 1, b - 1, depth + 1)
    return [[None]]


def expression_alternatives(toks, a, b, depth):
    """« c ? A : B » (B may be another ternary): the alternatives of A and B; else a chain."""
    q = find_top(toks, a, b, "?")
    if q >= 0:
        c = find_top(toks, q + 1, b, ":")
        if c >= 0:
            alts = chain_alternatives(toks, q + 1, c, depth + 1) + expression_alternatives(toks, c + 1, b, depth + 1)
            return alts[:8]
    return chain_alternatives(toks, a, b, depth)


def chain_alternatives(toks, a, b, depth=0):
    parts = split_top(toks, a, b, "+")
    combos = [[]]
    for (pa, pb) in parts:
        alts = operand_alternatives(toks, pa, pb, depth)
        combos = [c + alt for c in combos for alt in alts][:16]
    return combos


SEPARATORS = ["  ·  ", " · ", " — ", " – ", " | ", " • ", ", ", " ; "]


def render(pieces):
    """Model text of a list of pieces: « Niveau {0} »."""
    out, n = [], 0
    for p in pieces:
        if p is None:
            out.append("{%d}" % n)
            n += 1
        else:
            out.append(p)
    text = "".join(out)
    # Separators at the ends belong to the next piece of a longer text (« A  ·  » + « B »).
    changed = True
    while changed:
        changed = False
        s2 = text.strip(" ")
        for sep in SEPARATORS:
            ss = sep.strip(" ")
            if s2.endswith(ss) and len(s2) > len(ss):
                s2 = s2[: -len(ss)].rstrip(" ")
            if s2.startswith(ss) and len(s2) > len(ss):
                s2 = s2[len(ss):].lstrip(" ")
        if s2 != text:
            text, changed = s2, True
    return renumber(text.strip())


def renumber(text):
    """Slots numbered in order of appearance from 0."""
    mapping = {}

    def sub(m):
        k = m.group(1)
        if k not in mapping:
            mapping[k] = str(len(mapping))
        return "{" + mapping[k] + "}"

    return re.sub(r"\{(\d+)\}", sub, text)


def format_to_model(fmt, style):
    """std::format("{} x {:.1f}") → « {0} x {1} »; printf "%d %s %%" → « {0} {1} % »."""
    if style == "format":
        n = [0]

        def sub(m):
            t = m.group(0)
            if t in ("{{", "}}"):
                return t[0]
            idx = m.group(1)
            if idx:
                return "{" + idx + "}"
            r = "{%d}" % n[0]
            n[0] += 1
            return r

        return re.sub(r"\{\{|\}\}|\{(\d*)(?::[^{}]*)?\}", sub, fmt)
    n = [0]

    def sub2(m):
        if m.group(0) == "%%":
            return "%"
        r = "{%d}" % n[0]
        n[0] += 1
        return r

    return re.sub(r"%%|%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGcsp]", sub2, fmt)


PRINTF = re.compile(r"%[-+#0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[dfsgucx]")


def ternary_in_chain(toks, a, b):
    """When the expression [a, b) is a branch of a ternary in parentheses that is itself an
    operand of a '+' chain: the index of that parenthesis (else -1)."""
    if not ((a > 0 and toks[a - 1].text in ("?", ":")) or (b < len(toks) and toks[b].text in ("?", ":"))):
        return -1
    # The enclosing parenthesis.
    depth, k = 0, a - 1
    while k >= 0:
        t = toks[k].text
        if t in CLOSE:
            depth += 1
        elif t in OPEN:
            if depth == 0:
                break
            depth -= 1
        k -= 1
    if k < 0 or toks[k].text != "(":
        return -1
    if k > 0 and toks[k - 1].kind == "id":  # a call: f(c ? "a" : "b")
        return -1
    end = matching(toks, k)
    inside = (k > 0 and toks[k - 1].text == "+") or (end + 1 < len(toks) and toks[end + 1].text == "+")
    return k if inside else -1


def call_name(toks, a):
    """Name of the call whose '(' is just before token a (if any) and the argument index."""
    if a == 0 or toks[a - 1].text != "(":
        return None, 0
    k = a - 1
    name = None
    j = k - 1
    if j >= 0 and toks[j].text == ">":  # jget<int>(
        depth = 0
        while j >= 0:
            if toks[j].text == ">":
                depth += 1
            elif toks[j].text == "<":
                depth -= 1
                if depth == 0:
                    j -= 1
                    break
            j -= 1
    if j >= 0 and toks[j].kind == "id":
        name = toks[j].text
    return name, k


def argument_index(toks, open_paren, a):
    depth, idx = 0, 0
    for k in range(open_paren + 1, a):
        t = toks[k].text
        if t in OPEN:
            depth += 1
        elif t in CLOSE:
            depth -= 1
        elif depth == 0 and t == ",":
            idx += 1
    return idx


def enclosing_call(toks, a):
    """Innermost call around token a: (name, argument index) or (None, 0)."""
    depth = 0
    k = a - 1
    while k >= 0:
        t = toks[k].text
        if t in CLOSE:
            depth += 1
        elif t in OPEN:
            if depth == 0:
                if t != "(":
                    return None, 0, k
                j = k - 1
                if j >= 0 and toks[j].text == ">":
                    d2 = 0
                    while j >= 0:
                        if toks[j].text == ">":
                            d2 += 1
                        elif toks[j].text == "<":
                            d2 -= 1
                            if d2 == 0:
                                j -= 1
                                break
                        j -= 1
                name = toks[j].text if j >= 0 and toks[j].kind == "id" else None
                qual = toks[j - 2].text if name and j >= 2 and toks[j - 1].text == "::" else None
                return (qual + "::" + name if qual else name), argument_index(toks, k, a), k
            depth -= 1
        k -= 1
    return None, 0, -1


def mark_done(toks, a, b, done):
    """Literals of the text [a, b) (not those given to a call inside it, which are texts of
    their own: plural(n, "favori", "favoris"))."""
    stack = []
    for k in range(a, b):
        t = toks[k].text
        if t in OPEN:
            call = t == "(" and k > a and (toks[k - 1].kind == "id" or toks[k - 1].text == ">")
            stack.append(call)
        elif t in CLOSE:
            if stack:
                stack.pop()
        elif toks[k].kind == "str" and not any(stack):
            done.add(k)


def extract_cpp(path, words, out):
    with open(path, encoding="utf-8", errors="replace") as f:
        toks = tokenize(f.read())
    rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
    done = set()
    seen_spans = set()
    for i, t in enumerate(toks):
        if t.kind != "str" or i in done:
            continue
        a, b = span_of(toks, i)
        # std::string("Réglages : ") + x: the text is the whole expression around the call.
        if a >= 4 and b < len(toks) and toks[a - 1].text in ("(", "{") and toks[b].text in (")", "}") \
                and toks[a - 2].text in ("string", "string_view") and toks[a - 3].text == "::" and toks[a - 4].text == "std":
            a, b = span_of(toks, a - 4)
        mark_done(toks, a, b, done)
        name, arg, paren = enclosing_call(toks, a)
        base = name.split("::")[-1] if name else None
        if name and (name.startswith("Log::") or base in SKIP_CALLS and not (base in ID_FIRST_ARG and arg >= 1)):
            if not (base in ("jstr", "jget", "value") and arg >= 2):  # jstr(j, "key", "défaut affiché")
                continue
        if base in ID_FIRST_ARG and arg == 0:
            continue
        # plural(n, "favori", "favoris"): « {0} favori », « {0} favoris ».
        if base == "plural" and arg in (1, 2) and b - a == 1:
            out.setdefault("{0} " + toks[i].value, {"where": set()})["where"].add(rel)
            continue
        # j["key"], a == "x"
        if a > 0 and toks[a - 1].text == "[" and b < len(toks) and toks[b].text == "]":
            continue
        if (a > 0 and toks[a - 1].text in ("==", "!=")) or (b < len(toks) and toks[b].text in ("==", "!=")):
            continue
        style = None
        if base == "format" and arg == 0:
            style = "format"
        elif base in ("snprintf", "sprintf") and arg == 2:
            style = "printf"
        # A branch of « (n > 1 ? " réponses" : " réponse") » inside a longer text: only the
        # whole text (« Voir les {0} réponses ») is shown.
        for _ in range(4):
            k = ternary_in_chain(toks, a, b)
            if k < 0:
                break
            a, b = span_of(toks, k)
        if any(k in seen_spans for k in [(a, b)]):
            continue
        seen_spans.add((a, b))
        mark_done(toks, a, b, done)
        models = []
        for pieces in chain_alternatives(toks, a, b):
            if style and len(pieces) == 1 and isinstance(pieces[0], str):
                models.append(renumber(format_to_model(pieces[0], style)).strip())
            else:
                m = render(pieces)
                # A printf format handed to a widget (« %.0f morts »): its value is inserted.
                if PRINTF.search(m) and not re.search(r"%[YmdHMS]", m):
                    m = renumber(format_to_model(m, "printf")).strip()
                models.append(m)
        for m in models:
            add(out, m, rel, words)


def add(out, text, where, words, source=None):
    if not text or text in ("{0}",):
        return
    # "\x1F" "contexte|texte": a word whose translation depends on the place (« édition|Annuler »).
    if text.startswith("\x1f") and "|" in text:
        out.setdefault(text[1:], {"where": set()})["where"].add(where)
        return
    # Two slots side by side (« {0}{1} ») cannot be told apart at display: not a model.
    if re.search(r"\}\{\d", text):
        return
    if not displayable(text, words):
        return
    e = out.setdefault(text, {"where": set()})
    e["where"].add(where)
    if source:
        e["en"] = source


# ----------------------------------------------------------------------------- data

DATA_KEYS = {"name", "text", "description", "label", "message", "subtitle", "title", "enterText", "exitText", "speaker", "hint",
             "detail", "prompt", "caption", "tooltip", "summary", "question", "answer", "unlockText", "objective",
             "where"}
DATA_SKIP_FILES = {"data/online/config.json", "data/online/flags.json", "data/levels/ratings.json", "data/news/new_features.json"}


def extract_data(words, out):
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "**", "*.json"), recursive=True)):
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        if rel in DATA_SKIP_FILES or rel.startswith("data/i18n/"):
            continue
        with open(path, encoding="utf-8") as f:
            try:
                d = json.load(f)
            except json.JSONDecodeError:
                continue
        news = rel.startswith("data/news/")

        def walk(o, key):
            if isinstance(o, dict):
                if isinstance(o.get("name_fr"), str):
                    # Bilingual data (countries, avatars): the English name is given.
                    add(out, o["name_fr"], rel, words, o.get("name_en") or None)
                for k, v in o.items():
                    if k in ("name_fr", "name_en"):
                        continue
                    walk(v, k)
            elif isinstance(o, list):
                for v in o:
                    walk(v, key)
            elif isinstance(o, str):
                if news or key in DATA_KEYS:
                    if LETTER.search(o) and not FILE_LIKE.match(o) and not o.startswith(("http", "#")):
                        e = out.setdefault(o, {"where": set()})
                        e["where"].add(rel)

        walk(d, None)
    # Terms of use: one entry per paragraph (the text is translated line by line).
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "online", "*_fr.txt"))):
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        with open(path, encoding="utf-8") as f:
            for line in f.read().split("\n"):
                line = line.strip()
                if LETTER.search(line):
                    out.setdefault(line, {"where": set()})["where"].add(rel)


# ----------------------------------------------------------------------------- server

def extract_server(words, out):
    """Messages of the Cosmic Online server shown by the game (errors, refusals…)."""
    lit = re.compile(r"\"((?:[^\"\\\n]|\\.)*)\"|'((?:[^'\\\n]|\\.)*)'|`((?:[^`\\]|\\.)*)`")
    for path in sorted(glob.glob(os.path.join(ROOT, "server", "src", "*.ts"))):
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        with open(path, encoding="utf-8") as f:
            src = f.read()
        src = re.sub(r"//[^\n]*", "", src)
        for m in lit.finditer(src):
            s = m.group(1) if m.group(1) is not None else m.group(2) if m.group(2) is not None else m.group(3)
            if m.group(3) is not None:
                s = re.sub(r"\$\{[^}]*\}", "{0}", s)
                s = renumber(re.sub(r"\{0\}", lambda _m, c=[0]: "{%d}" % (c.__setitem__(0, c[0] + 1) or c[0] - 1), s))
            s = s.replace("\\n", "\n").replace("\\'", "'").replace('\\"', '"')
            # French messages only (the server's logs and keys are in English).
            line_start = src.rfind("\n", 0, m.start())
            line = src[line_start + 1: m.start()]
            if "log(" in line or "console." in line:
                continue
            if (FR_ACCENT.search(s) or re.search(r"\b(le|la|les|du|des|une?|est|pas|aucun|introuvable|invalide)\b", s)) and " " in s:
                add(out, s.strip(), rel, words)


# ----------------------------------------------------------------------------- main

# Lowercase French words shown alone that no sentence of the game contains.
EXTRA_WORDS = {"argent", "brouillon", "gratuit", "informations", "normale", "oui", "non", "profondeur", "retournement", "recaler",
               "vitesse", "lecture", "image", "point", "caméra"}


def french_words():
    """Lowercase words used in sentences of the code and the data (to tell a shown word
    such as « portail » from a key such as « username »)."""
    words = set()
    sources = []
    for d in CODE_DIRS:
        sources += glob.glob(os.path.join(ROOT, d, "**", "*.cpp"), recursive=True)
        sources += glob.glob(os.path.join(ROOT, d, "**", "*.h"), recursive=True)
    sources += [os.path.join(ROOT, f) for f in CODE_FILES]
    sources += glob.glob(os.path.join(ROOT, "data", "**", "*.json"), recursive=True)
    for p in sources:
        with open(p, encoding="utf-8", errors="replace") as f:
            for m in re.finditer(r"\"((?:[^\"\\\n]|\\.)*)\"", f.read()):
                s = m.group(1)
                if " " in s.strip() and (FR_ACCENT.search(s) or re.search(r"\b(le|la|les|de|du|des|un|une|et|est|pour|sur|dans)\b", s)):
                    for w in re.findall(r"[A-Za-zÀ-ÿœ']+", s):
                        words.add(w.lower())
    return words | EXTRA_WORDS


def extract():
    words = french_words()
    out = {}
    files = []
    for d in CODE_DIRS:
        files += glob.glob(os.path.join(ROOT, d, "**", "*.cpp"), recursive=True)
        files += glob.glob(os.path.join(ROOT, d, "**", "*.h"), recursive=True)
    files += [os.path.join(ROOT, f) for f in CODE_FILES]
    for p in sorted(files):
        extract_cpp(p, words, out)
    extract_data(words, out)
    extract_server(words, out)
    ignore = load_ignore()
    for k in list(out):
        if k in ignore or k.strip() != k and k.strip() in out:
            del out[k]
    return out


def placeholders(s):
    return sorted(set(re.findall(r"\{(\d+)(?:\|[^{}]*)?\}", s)))


def load_dictionary():
    if not os.path.exists(DICTIONARY):
        return {"format": "cosmic.i18n", "language": "en", "name": "English", "strings": {}}
    with open(DICTIONARY, encoding="utf-8") as f:
        return json.load(f)


def save_dictionary(d):
    d["strings"] = dict(sorted(d["strings"].items(), key=lambda kv: (kv[0].lower(), kv[0])))
    with open(DICTIONARY, "w", encoding="utf-8", newline="\n") as f:
        json.dump(d, f, ensure_ascii=False, indent=1)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--todo", help="écrit les textes sans traduction dans ce fichier JSON (valeurs à remplir)")
    ap.add_argument("--merge", help="ajoute au dictionnaire les traductions remplies de ce fichier JSON")
    ap.add_argument("--check", action="store_true", help="code de sortie 1 s'il manque une traduction")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if args.merge:
        d = load_dictionary()
        with open(args.merge, encoding="utf-8") as f:
            todo = json.load(f)
        n = 0
        for k, v in todo.items():
            if isinstance(v, str) and v.strip():
                d["strings"][k] = v
                n += 1
        save_dictionary(d)
        print(f"{n} traduction(s) ajoutée(s) à {os.path.relpath(DICTIONARY, ROOT)}")
        return 0

    found = extract()
    catalog = {
        "format": "cosmic.i18n.catalog",
        "comment": "Textes français affichés par le jeu (généré par tools/i18n/extract.py : ne pas éditer).",
        "strings": [{"fr": k, "where": sorted(v["where"])} for k, v in sorted(found.items(), key=lambda kv: (kv[0].lower(), kv[0]))],
    }
    with open(CATALOG, "w", encoding="utf-8", newline="\n") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=1)
        f.write("\n")

    d = load_dictionary()
    strings = d["strings"]
    # Translations given by the data itself (name_en of countries and avatars).
    auto = 0
    for k, v in found.items():
        if v.get("en") and not strings.get(k):
            strings[k] = v["en"]
            auto += 1
    if auto:
        save_dictionary(d)
    missing = [k for k in found if not strings.get(k)]
    bad = [k for k in found if strings.get(k) and placeholders(k) != placeholders(strings[k])]
    unused = [k for k in strings if k not in found]
    total = len(found)
    if not args.quiet:
        print(f"Catalogue : {total} textes ({os.path.relpath(CATALOG, ROOT)})")
        print(f"Traduits : {total - len(missing)} / {total} ({100.0 * (total - len(missing)) / max(1, total):.1f} %)")
        if auto:
            print(f"{auto} traduction(s) reprise(s) des données (name_en)")
        for k in sorted(missing)[:60]:
            print("  À TRADUIRE :", json.dumps(k, ensure_ascii=False), "—", ", ".join(sorted(found[k]["where"])[:2]))
        if len(missing) > 60:
            print(f"  … et {len(missing) - 60} autres (--todo fichier.json pour la liste complète)")
        for k in bad:
            print("  EMPLACEMENTS DIFFÉRENTS :", json.dumps(k, ensure_ascii=False), "→", json.dumps(strings[k], ensure_ascii=False))
        if unused:
            print(f"{len(unused)} entrée(s) du dictionnaire ne sont plus dans le catalogue (gardées : textes composés ou anciens)")
    if args.todo:
        with open(args.todo, "w", encoding="utf-8", newline="\n") as f:
            json.dump({k: "" for k in sorted(missing)}, f, ensure_ascii=False, indent=1)
            f.write("\n")
        print(f"{len(missing)} texte(s) à traduire écrits dans {args.todo}")
    if args.check and (missing or bad):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
