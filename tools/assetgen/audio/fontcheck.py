"""Minimal TrueType inspection (no fontTools needed): magic bytes, table directory, cmap
coverage. Used by fetch_fonts.py and validate.py to prove the fonts are real TTFs that contain
the glyphs the game's French/English UI needs."""
import struct

TTF_MAGIC = (b'\x00\x01\x00\x00', b'true')

# Characters every font must render: letters, digits, common punctuation, French accents.
FRENCH = 'àâäçéèêëîïôöùûüÀÂÇÉÈÊËÎÏÔÙÛÜ'
REQUIRED_TITLE = ('ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'
                  " .,:;!?'\"-+/()%&*#" + FRENCH + '’')
# UI/body fonts must cover all of printable ASCII plus French quotes.
REQUIRED_UI = ''.join(chr(c) for c in range(0x20, 0x7F)) + FRENCH + '’«»'
NICE_TO_HAVE = ''.join(chr(c) for c in range(0x20, 0x7F)) + '«»œŒæÆÿŸ€…–—“”'


def tables(data):
    if len(data) < 12 or data[:4] not in TTF_MAGIC:
        raise ValueError('not a TrueType font (bad sfnt magic %r)' % data[:4])
    num = struct.unpack('>H', data[4:6])[0]
    out = {}
    for i in range(num):
        rec = data[12 + 16 * i:28 + 16 * i]
        tag, _, off, length = struct.unpack('>4sIII', rec)
        if off + length > len(data):
            raise ValueError(f'table {tag!r} extends past end of file')
        out[tag.decode('latin-1')] = (off, length)
    return out


def _format4(data, o):
    seg2 = struct.unpack('>H', data[o + 6:o + 8])[0]
    seg = seg2 // 2
    ends = struct.unpack('>%dH' % seg, data[o + 14:o + 14 + seg2])
    starts_o = o + 16 + seg2
    starts = struct.unpack('>%dH' % seg, data[starts_o:starts_o + seg2])
    deltas = struct.unpack('>%dh' % seg, data[starts_o + seg2:starts_o + 2 * seg2])
    ro_o = starts_o + 2 * seg2
    ranges = struct.unpack('>%dH' % seg, data[ro_o:ro_o + seg2])
    cps = set()
    for i in range(seg):
        for c in range(starts[i], ends[i] + 1):
            if c == 0xFFFF:
                continue
            if ranges[i] == 0:
                gid = (c + deltas[i]) & 0xFFFF
            else:
                addr = ro_o + 2 * i + ranges[i] + 2 * (c - starts[i])
                gid = struct.unpack('>H', data[addr:addr + 2])[0]
                if gid:
                    gid = (gid + deltas[i]) & 0xFFFF
            if gid:
                cps.add(c)
    return cps


def _format12(data, o):
    n = struct.unpack('>I', data[o + 12:o + 16])[0]
    cps = set()
    for i in range(n):
        s, e, g = struct.unpack('>III', data[o + 16 + 12 * i:o + 28 + 12 * i])
        for c in range(s, e + 1):
            if g + (c - s):
                cps.add(c)
    return cps


def codepoints(data):
    t = tables(data)
    if 'cmap' not in t:
        raise ValueError('no cmap table')
    base = t['cmap'][0]
    n = struct.unpack('>H', data[base + 2:base + 4])[0]
    subs = []
    for i in range(n):
        pid, eid, off = struct.unpack('>HHI', data[base + 4 + 8 * i:base + 12 + 8 * i])
        fmt = struct.unpack('>H', data[base + off:base + off + 2])[0]
        subs.append((pid, eid, fmt, base + off))
    cps = set()
    for pid, eid, fmt, o in subs:
        if fmt == 12 and (pid, eid) in ((3, 10), (0, 4), (0, 6)):
            cps |= _format12(data, o)
        elif fmt == 4 and (pid, eid) in ((3, 1), (0, 3), (0, 4), (0, 1)):
            cps |= _format4(data, o)
    return cps


def family_name(data):
    """Best-effort family name from the 'name' table (nameID 1, Windows/Unicode)."""
    t = tables(data)
    if 'name' not in t:
        return ''
    o = t['name'][0]
    count, str_off = struct.unpack('>HH', data[o + 2:o + 6])
    for i in range(count):
        pid, eid, lang, nid, length, off = struct.unpack('>6H', data[o + 6 + 12 * i:o + 18 + 12 * i])
        if nid == 1 and pid == 3:
            raw = data[o + str_off + off:o + str_off + off + length]
            return raw.decode('utf-16-be', 'replace')
    return ''


def check_font(path, role='ui', size=24):
    """role: 'ui' (body text, full ASCII + French) or 'title' (display font).
    Returns (errors, warnings, info dict)."""
    errors, warnings = [], []
    info = {}
    with open(path, 'rb') as f:
        data = f.read()
    try:
        t = tables(data)
    except ValueError as e:
        return [str(e)], warnings, info
    for tag in ('cmap', 'glyf', 'head', 'hhea', 'hmtx', 'maxp', 'name'):
        if tag not in t:
            errors.append(f'missing required table {tag}')
    info['variable'] = 'fvar' in t
    info['family'] = family_name(data)
    info['tables'] = len(t)
    try:
        cps = codepoints(data)
    except Exception as e:  # malformed cmap
        return errors + [f'cmap parse error: {e}'], warnings, info
    info['glyphs'] = len(cps)
    required = REQUIRED_UI if role == 'ui' else REQUIRED_TITLE
    missing = [c for c in required if ord(c) not in cps]
    if missing:
        errors.append('missing required glyphs: ' + ''.join(missing))
    nice = [c for c in dict.fromkeys(NICE_TO_HAVE) if ord(c) not in cps and c not in missing]
    if nice:
        warnings.append('missing optional glyphs: ' + ''.join(nice))
    try:
        from PIL import ImageFont
        font = ImageFont.truetype(path, size)
        for ch in 'éèàçê':
            box = font.getbbox(ch)
            if box[2] - box[0] <= 0 or box[3] - box[1] <= 0:
                errors.append(f'PIL renders an empty glyph for {ch!r}')
        info['pil'] = 'ok'
    except Exception as e:
        errors.append(f'PIL/FreeType cannot load the font: {e}')
    return errors, warnings, info
