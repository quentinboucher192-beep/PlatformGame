"""Shared Sylvanox palette (see docs/ASSET_SPEC.md section 1)."""


def hex_rgba(h, a=255):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


OUTLINE = hex_rgba('#0b0f1e')
SKY = [hex_rgba(c) for c in ('#0b0f1e', '#121a33', '#1a2747', '#22365a', '#2e4a73')]
STONE = [hex_rgba(c) for c in ('#1d2b3a', '#2a3e4f', '#3b5466', '#577286', '#7d97a8')]
MOSS = [hex_rgba(c) for c in ('#0f3b3a', '#135c50', '#1f8a6a', '#35b583')]
CYAN = [hex_rgba(c) for c in ('#1fb8b0', '#39f0d8', '#8ffff0', '#e6fffb')]
VIOLET = [hex_rgba(c) for c in ('#6a2bb8', '#b04cff', '#e08aff')]
AMBER = [hex_rgba(c) for c in ('#c8741f', '#ffb347', '#ffd98a')]
ACID = [hex_rgba(c) for c in ('#2f7a1a', '#4fbf26', '#9dff3a', '#e4ffb0')]
DANGER = [hex_rgba(c) for c in ('#a3123a', '#ff4d6d', '#ff8a3d')]
SUIT = [hex_rgba(c) for c in ('#e8eef5', '#c3d0de', '#9fb3c8', '#6c819a')]
VISOR = [hex_rgba(c) for c in ('#39f0d8', '#8ffff0')]
SCARF = [hex_rgba(c) for c in ('#b8421e', '#ff7a45', '#ffb08a')]
TRANSPARENT = (0, 0, 0, 0)
