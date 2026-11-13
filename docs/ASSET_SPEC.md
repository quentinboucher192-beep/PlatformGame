# Cosmic Platformer — Asset Specification

This document is the contract between the asset generators (`tools/assetgen/`) and the
engine code. Every path below is relative to `assets/`. The engine loads exactly these
files; changing a name, frame size or frame count requires updating the matching JSON.

All art is **pixel art**, drawn at 1x (one texel = one world pixel). The world camera shows
480x270 world pixels at zoom 1 and is scaled to the window with the `PIXELART` scale mode.
Tiles are 16x16. All sprites face **right**; the engine mirrors them horizontally.

## 1. Art direction — planet Sylvanox

Sylvanox is an alien world at perpetual dusk: ancient technological ruins overgrown by
bioluminescent vegetation, floating islands and broken bridges suspended above a glowing
toxic abyss. Mood: mysterious, calm, beautiful but dangerous.

Shared palette (stick to it; small variations allowed for shading ramps):

| Role | Colours |
|---|---|
| Outline / deepest shadow | `#0b0f1e` |
| Night sky / far background | `#0b0f1e` `#121a33` `#1a2747` `#22365a` `#2e4a73` |
| Ruin stone | `#1d2b3a` `#2a3e4f` `#3b5466` `#577286` `#7d97a8` |
| Vegetation / moss | `#0f3b3a` `#135c50` `#1f8a6a` `#35b583` |
| Bioluminescent cyan (main glow) | `#1fb8b0` `#39f0d8` `#8ffff0` `#e6fffb` |
| Bioluminescent violet (secondary glow) | `#6a2bb8` `#b04cff` `#e08aff` |
| Ancient-tech amber lights | `#c8741f` `#ffb347` `#ffd98a` |
| Toxic abyss (acid) | `#2f7a1a` `#4fbf26` `#9dff3a` `#e4ffb0` |
| Danger / enemy accents | `#a3123a` `#ff4d6d` `#ff8a3d` |
| Hero suit | `#e8eef5` `#c3d0de` `#9fb3c8` `#6c819a` |
| Hero visor | `#39f0d8` `#8ffff0` |
| Hero scarf (complementary, makes Nova pop) | `#b8421e` `#ff7a45` `#ffb08a` |

Rules: 1px dark outline (`#0b0f1e` or a darker shade of the local colour) around characters,
enemies and props; light comes from the top-left; glowing parts use the bright tones and no
outline on their lit side. Enemies must read as hostile (red/pink/amber accents, eyes).

## 2. Animation JSON schema (`animations/<name>.json`)

Every animated sheet has a companion JSON:

```json
{
  "texture": "sprites/player/nova.png",
  "frameWidth": 32,
  "frameHeight": 32,
  "pivot": [16, 32],
  "clips": {
    "idle":   { "row": 0, "count": 6, "fps": 8, "loop": true },
    "attack": { "row": 9, "count": 5, "durations": [0.04, 0.05, 0.07, 0.08, 0.10],
                "loop": false, "next": "idle",
                "events": [ { "frame": 2, "name": "hit" } ] }
  }
}
```

* One clip per row, frames left to right starting at column `start` (default 0).
* `pivot` = pixel in the frame placed at the entity anchor = **bottom-centre of the
  collision box** (for every entity, flying ones included).
* `durations` (seconds per frame) overrides `fps`.
* Event names consumed by the engine: `footstep`, `hit`, `fire`, `impact`, `sfx:<sound>`.

## 3. Player — Nova (`sprites/player/nova.png`, `animations/nova.json`)

Frames 32x32, pivot `[16, 32]`. Collision box is 10x22, so the visible body is about
12-14 px wide and 22-24 px tall, centred horizontally, feet on the bottom row.
Design: small explorer in an ivory space suit, round helmet with glowing cyan visor,
orange scarf that trails and reacts to motion, small backpack with a cyan energy core.

| row | clip | frames | notes |
|---|---|---|---|
| 0 | idle | 6 | breathing, scarf sway, loop |
| 1 | walk | 8 | loop, `footstep` on 2 contact frames |
| 2 | run | 8 | leaning forward, scarf streaming, loop, `footstep` on contacts |
| 3 | jump | 3 | takeoff → rising, hold last |
| 4 | double_jump | 6 | forward flip / spin, then hold |
| 5 | fall | 3 | loop, scarf up |
| 6 | land | 3 | squash, `next: idle` |
| 7 | slide | 3 | low ground slide (body ~12 px tall), hold last |
| 8 | wall_slide | 2 | back against wall, hand on wall (wall is on the RIGHT side of the frame), loop |
| 9 | climb | 4 | seen from behind climbing vines, loop |
| 10 | attack | 5 | energy-blade slash forward, `hit` on frame 2 |
| 11 | air_attack | 4 | airborne slash, `hit` on frame 1 |
| 12 | cast | 4 | arm thrust forward for powers, `fire` on frame 1 |
| 13 | dash | 4 | stretched horizontal pose, loop |
| 14 | hurt | 3 | recoil |
| 15 | death | 10 | collapse then dissolve into cyan particles (last frames mostly empty) |
| 16 | respawn | 10 | materialise from cyan light (reverse of dissolve) |
| 17 | victory | 10 | jump/fist pump, hold last |
| 18 | interact | 4 | reach forward (press a switch) |

## 4. Enemies (`sprites/enemies/<id>.png`, `animations/<id>.json`)

All face right. Pivot = bottom-centre of the listed collision box.

| id | frame | collision box | description | clips (frames) |
|---|---|---|---|---|
| `glowmite` | 24x16 | 14x10 | small bioluminescent beetle, glowing cyan back with red eyes | idle 4, walk 6, telegraph 3, attack 4 (bite, `hit` f2), hurt 2, death 6 |
| `sentinel` | 32x32 | 16x14 | hovering ancient drone, stone+metal shell, single red-amber eye, small thrusters | idle 6, move 6, telegraph 6 (eye charging, brighter each frame), attack 3 (`fire` f0), hurt 2, death 8 |
| `sporeflower` | 32x32 | 16x20 | carnivorous plant turret rooted in ground, violet bulb that inflates | idle 6, telegraph 5 (inflate), attack 4 (spit, `fire` f1), hurt 2, death 7 |
| `stalker` | 32x24 | 18x12 | fast lean four-legged predator, dark with cyan stripes and magenta eyes | idle 4, run 6, telegraph 4 (crouch), attack 4 (lunge), recover 3, hurt 2, death 7 |
| `golem` | 48x48 | 26x34 | heavy ruin golem: stone body with amber core, big arms, can raise a stone shield arm | idle 4, walk 6, defend 3 (hold last), telegraph 5 (raise arms), attack 5 (slam, `impact` f3), hurt 2, death 9 |
| `wisp` | 24x24 | 12x12 | arcane will-o'-wisp: violet flame with a mask-like face | idle 6, move 6, telegraph 5, attack 4 (`fire` f2), teleport_out 5, teleport_in 5, summon 6, hurt 2, death 7 |
| `thornwarden` | 96x96 | 44x60 | MINI-BOSS "Thornwarden, ancient guardian": giant ruin-knight overgrown with thorny vines, glowing cyan core in chest that turns red when enraged, huge arm | idle 6, walk 8, telegraph_slam 6, slam 6 (`impact` f3), telegraph_charge 4, charge 6 (loop), telegraph_barrage 6, barrage 4 (`fire` f1), summon 6, roar 8, telegraph_laser 6, laser 4 (loop), hurt 2, death 14 |

## 5. Projectiles (`sprites/projectiles/projectiles.png`, `animations/projectiles.json`)

Frames 16x16, pivot `[8, 8]` (projectiles are centred). Each row is a looping clip:
`player_wave` (crescent of cyan energy, 4), `drone_bolt` (red-amber bolt, 4),
`spore` (violet spore ball, 4), `wisp_orb` (violet orb, 4), `boss_thorn` (thorn spike, 4),
`boss_orb` (big red-cyan orb, 4).

## 6. Objects (`sprites/objects/<name>.png` + `animations/<name>.json`)

Pivot = bottom-centre unless noted.

| name | frame | clips |
|---|---|---|
| `checkpoint` | 24x48 | inactive 1, activate 8 (`next: active`), active 6 loop — ancient beacon pylon whose crystal lights cyan |
| `exit_portal` | 64x64 | idle 8 loop, active 8 loop (brighter, swirling) — ancient ring gate |
| `shard` | 12x12 | spin 8 loop — cyan energy shard collectible (pivot centre `[6,6]`) |
| `relic` | 16x16 | idle 8 loop — golden ancient relic (secret collectible) (pivot centre) |
| `health_orb` | 12x12 | idle 6 loop — pink-red heart orb (pivot centre) |
| `energy_orb` | 12x12 | idle 6 loop — cyan energy orb (pivot centre) |
| `power_unlock` | 32x32 | rows `dash`, `wave`, `shield`, `impulse`, `teleport`, each 8 frames loop: floating ancient tablet/orb with the power symbol (pivot centre `[16,16]`) |
| `door` | 16x64 | closed 1, opening 6 (`next: open`), open 1, closing 6 (`next: closed`) — vertical energy barrier between two stone posts |
| `lever` | 16x16 | off 1, switching 4 (`next: on`), on 1 |
| `receptor` | 16x32 | off 1, on 6 loop — energy receptor activated by the energy wave |
| `bounce_pad` | 32x16 | idle 1, bounce 5 (`next: idle`) — mushroom/organic spring pad |
| `breakable_wall` | 32x64 | intact 1, cracked 1 — crystal wall broken by dash (pivot bottom-centre) |
| `phase_wall` | 16x64 | idle 6 loop — shimmering translucent energy wall passable by teleport |
| `sign` | 24x24 | idle 1 — ancient stone tablet with glowing glyph |
| `laser_emitter` | 16x16 | off 1, charging 4, on 4 loop |

Static/3-slice sprites (no JSON needed):

* `sprites/objects/moving_platform.png` 48x32: row 0 = left cap / middle / right cap (16x16 each) normal, row 1 = same with lights on. Floating ancient tech platform with amber lights and a glow under it.
* `sprites/objects/crumble_platform.png` 48x48: three rows (intact, cracked, very cracked), 3-slice each (16 px cap, 16 px repeatable middle, 16 px cap). Mossy fragile stone.
* `sprites/objects/spikes.png` 32x16: two 16x16 variants of upward-pointing crystal thorns/spikes (tileable horizontally).
* `sprites/objects/laser_beam.png` 64x8: 4 frames of 16x8 beam segment, horizontally tileable, red-pink core.
* Liquids, each a 128x32 image: row 0 = 8 frames of a 16x16 animated surface (wave crest
  at the top, horizontally tileable), row 1 = 8 frames of a 16x16 body tile (tileable in
  both directions).
  * `sprites/objects/liquid_acid.png` (toxic lime abyss liquid, glowing)
  * `sprites/objects/liquid_water.png` (deep teal water, slightly transparent)
  * `sprites/objects/liquid_lava.png` (orange-red lava)

## 7. Terrain tilesets (`textures/tiles/<material>.png` + `.json`)

Atlas 256x64 of 16x16 cells:

* Row 0: 16 autotile cells indexed by a 4-bit neighbour mask — bit0 UP, bit1 RIGHT,
  bit2 DOWN, bit3 LEFT (bit set = same material on that side). Edges with no neighbour
  show the border (top edges get the surface/moss lip, sides/bottom a darker rim).
* Row 1: 16 interior variants (mask 15), chosen by position hash — mostly plain with a few
  details (glyph carving, crack, glowing vein, small fossil).
* Row 2: cells 0-3 = inner-corner overlays (TL, TR, BL, BR) drawn over a fully surrounded
  tile whose diagonal neighbour is empty; rest unused (transparent).
* Row 3: 8 top-decoration overlays (grass, glowing moss tufts, small mushrooms, hanging
  roots) drawn above exposed top edges, offset `topDecorOffsetY` = -12 (they overlap the top
  4 px of the tile and stick out 12 px above). Transparent background.

JSON:

```json
{ "texture": "textures/tiles/ruin_stone.png", "tileSize": 16,
  "maskRow": 0, "variantRow": 1, "variantCount": 16,
  "innerCornerRow": 2, "topDecorRow": 3, "topDecorCount": 8, "topDecorOffsetY": -12,
  "mapColor": "#3b5466" }
```

Materials: `ruin_stone` (main: dark teal carved stone, glowing cyan moss on top),
`ancient_metal` (tech ruin plating, amber light strips, rivets), `crystal` (violet crystal rock,
glowing), `root` (giant organic roots/bark with cyan veins).

One-way platforms `textures/tiles/platforms.png` 48x48: row 0 `ruin_bridge` (old stone/metal
bridge with vines), row 1 `tech_ledge` (thin metal ledge with amber light), row 2
`root_branch` (organic branch). Columns: left cap, middle (tileable), right cap. Walkable top
on the first 2 px rows. JSON `textures/tiles/platforms.json`:
`{ "texture": "textures/tiles/platforms.png", "tileSize": 16, "styles": { "ruin_bridge": 0, "tech_ledge": 1, "root_branch": 2 } }`

## 8. Backgrounds (`backgrounds/sylvanox/`)

Each 640x360, horizontally seamless; all but `sky` have transparency.

| file | parallax | content |
|---|---|---|
| `sky.png` | 0.0 | dusk gradient (navy→teal→violet near horizon), stars, nebula, a huge ringed gas giant and two small moons |
| `far.png` | 0.12 | very distant floating ruins and megastructure silhouettes, low contrast, mist |
| `mid.png` | 0.3 | giant bioluminescent mushrooms / alien trees silhouettes with glowing spots |
| `near.png` | 0.55 | closer ruins and foliage, hanging vines with glowing pods, more contrast |
| `abyss.png` | 0.5 | glowing toxic mist rising from below (bright lime/cyan at the bottom, transparent at top) |
| `fg_vines.png` | 1.2 | foreground: dark hanging vines/leaves along the top, mostly transparent |

## 9. Decor atlas (`sprites/decor/sylvanox_decor.png` + `sprites/decor/sylvanox_decor.json`)

```json
{ "texture": "sprites/decor/sylvanox_decor.png",
  "sprites": {
    "mushroom_tall": { "rect": [0, 0, 32, 48], "pivot": [16, 48], "frames": 1, "fps": 0,
                       "glow": "#39f0d8", "glowRadius": 40, "glowOffset": [0, -36] }
  } }
```

Animated decor: `frames` frames laid horizontally from `rect` (each `rect.w` wide).
Required sprites: `mushroom_small`, `mushroom_tall`, `mushroom_cluster`, `glow_fern`,
`glow_flowers` (animated 4), `crystal_cluster`, `crystal_big`, `ruin_pillar`,
`ruin_pillar_broken`, `ruin_arch`, `statue_head`, `tech_panel` (animated 4), `lantern_pod`
(animated 6, hanging from top: pivot at top-centre), `hanging_vine_short`,
`hanging_vine_long` (pivot top-centre), `roots`, `rock_small`, `rock_big`, `floating_rock`,
`holo_glyph` (animated 6), `pipe_broken`, `banner_torn`, `grass_tuft`, `bones_ancient`.

## 10. VFX textures (`textures/fx/`)

White or near-white so they can be tinted at runtime unless noted.

| file | size | content |
|---|---|---|
| `glow.png` | 64x64 | soft radial gradient white→transparent (smooth, not pixelated) |
| `particle.png` | 8x8 | soft round dot |
| `spark.png` | 8x8 | elongated spark (horizontal) |
| `star.png` | 8x8 | 4-point twinkle |
| `ring.png` | 64x64 | thin shockwave ring |
| `smoke.png` | 64x16 | 4 frames 16x16 soft smoke puffs |
| `dust.png` | 80x16 | 5 frames 16x16 dust puff |
| `slash.png` | 192x32 | 4 frames 48x32 slash arc (white/cyan) |
| `hit.png` | 120x24 | 5 frames 24x24 impact star |
| `explosion.png` | 384x48 | 8 frames 48x48 (coloured: amber/white core) |
| `shield.png` | 288x48 | 6 frames 48x48 bubble (cyan, translucent) |
| `teleport.png` | 256x32 | 8 frames 32x32 vertical light burst (cyan/violet) |
| `warning.png` | 16x16 | red "!" alert icon for enemy telegraphs (coloured) |

## 11. UI (`ui/`)

| file | size | content |
|---|---|---|
| `icons_powers.png` | 160x32 | 5 icons 32x32: dash, wave, shield, impulse, teleport |
| `icons_medals.png` | 160x32 | 5 icons 32x32: none (empty outline), bronze, silver, gold, platinum |
| `icons_hud.png` | 176x16 | 11 icons 16x16: heart_full, heart_empty, energy, shard, relic, skull, clock, enemy, star, lock, check |
| `glyphs/xbox.png`, `glyphs/playstation.png`, `glyphs/generic.png` | 608x32 | 19 icons 32x32 in this order: South(A/Cross), East(B/Circle), West(X/Square), North(Y/Triangle), LB/L1, RB/R1, LT/L2, RT/R2, Back/View/Share, Start/Menu/Options, LS click, RS click, DPad up, down, left, right, left stick, right stick, guide |
| `planets/sylvanox.png` | 256x256 | the planet: teal/green with bioluminescent cyan patterns, thin atmosphere halo, transparent bg |
| `planets/pyralis.png` | 256x256 | volcanic teaser planet (red/orange, cracks) |
| `planets/glacior.png` | 256x256 | ice teaser planet (pale blue) |
| `planets/sun.png` | 256x256 | star with corona |
| `planets/sylvanox_banner.png` | 480x270 | pixel-art vista of Sylvanox for the planet info panel |
| `map_background.png` | 960x540 | deep space backdrop with nebula for the planet map |
| `app_icon.png` | 256x256 | game icon: Nova's helmet with cyan visor on dark round badge |

Also `installer/app.ico` (16, 32, 48, 256) built from `app_icon.png`.

## 12. Audio

Sound effects `sounds/<name>.wav` (16-bit PCM, 44.1 kHz, mono, peak ≤ -1 dBFS):
`jump`, `double_jump`, `land`, `footstep_a`, `footstep_b`, `wall_jump`, `slide`, `dash`,
`wave_cast`, `wave_hit`, `shield_up`, `shield_block`, `shield_down`, `impulse`,
`teleport_out`, `teleport_in`, `slash`, `slash_hit`, `enemy_hurt`, `enemy_death`,
`player_hurt`, `player_death`, `respawn`, `checkpoint`, `collect_shard`, `collect_relic`,
`collect_orb`, `power_unlock`, `door_open`, `lever`, `crumble`, `bounce`, `laser_charge`,
`laser_fire`, `enemy_shoot`, `enemy_alert`, `enemy_telegraph`, `golem_slam`, `explosion`,
`boss_roar`, `boss_phase`, `summon`, `wisp_teleport`, `spore_spit`, `ui_move`, `ui_confirm`,
`ui_back`, `ui_error`, `timer_tick`, `timer_warning`, `medal`, `score_tick`, `portal`,
`energy_low`.

Music `music/<name>.ogg` (Vorbis, 44.1 kHz stereo, seamless loops unless noted):
`menu_theme`, `planet_map`, `sylvanox_level`, `sylvanox_boss`, `victory` (non-looping
jingle, ~6 s), `sylvanox_ambience` (ambient night-forest loop).

## 13. Fonts (`fonts/`)

SIL Open Font License fonts, licence text copied next to them:
`Orbitron.ttf` (titles), `ChakraPetch-Regular.ttf`, `ChakraPetch-SemiBold.ttf` (UI), plus
`OFL-Orbitron.txt`, `OFL-ChakraPetch.txt`.

## 14. v2 content (Pyralis, Glacior, new enemies, bosses and abilities)

Generators: `characters/enemy_<species>.py` + `characters/variants.py` (enemies),
`characters/bosses_a.py` / `bosses_b.py` (bosses, called by `boss.py`), `characters/player_*.py`
(player rows, projectiles, objects), `environment/*` (planets), `audio/*` (music, sounds).
Art direction of the new planets: **Pyralis** — basalt/charcoal, lava oranges, obsidian
glints, rusted metal; **Glacior** — deep navy, ice blues, snow, frost cyan, aurora green/violet.

### 14.1 New enemy species (Sylvanox palette)

| id | frame | pivot | collision | clips (frames) |
|---|---|---|---|---|
| `nightwing` | 32x24 | [16,20] | 14x10 | idle 6, move 6, telegraph 3, attack 4 (`hit` f1), hurt 2, death 6 |
| `gloop` | 24x20 | [12,20] | 14x10 | idle 4, walk 6, telegraph 3, attack 4 (`fire` f1), hurt 2, death 6 |
| `weaver` | 32x24 | [16,24] | 16x12 | idle 4, walk 6, telegraph 3, attack 4 (`fire` f1), hurt 2, death 6 |
| `bulwark` | 32x32 | [16,32] | 16x24 | idle 4, walk 6, defend 3, telegraph 4, attack 5 (`hit` f2), hurt 2, death 8 |
| `bomber` | 32x24 | [16,20] | 18x12 | idle 6, move 6, telegraph 4, attack 4 (`fire` f1), hurt 2, death 8 |
| `burrower` | 32x32 | [16,32] | 18x16 | idle 4, move 4, teleport_out 6, teleport_in 6, telegraph 3, attack 4 (`fire` f1), hurt 2, death 7 |

`gloop_small` (and the themed small gloops) reuse the gloop sheets at scale 0.65.

### 14.2 Themed variants

Real palette swaps of the 12 regular species (same frame size, pivot, clips and events),
with ember specks (Pyralis) or frost rims (Glacior):

| base | Pyralis | Glacior |
|---|---|---|
| glowmite | ember_mite | frost_mite |
| sentinel | forge_drone | cryo_drone |
| sporeflower | cinder_bloom | frost_bloom |
| stalker | lava_stalker | snow_stalker |
| golem | magma_golem | ice_golem |
| wisp | fire_wisp | frost_wisp |
| nightwing | ash_wing | frost_wing |
| gloop | magma_gloop | ice_gloop |
| weaver | ember_weaver | frost_weaver |
| bulwark | forge_bulwark | frost_bulwark |
| bomber | forge_bomber | cryo_bomber |
| burrower | lava_burrower | ice_burrower |

### 14.3 Bosses

Clip sets (exact names, used by `data/enemies/<id>.json`):

* **Knight**: idle 6, walk 8, telegraph_slam 6, slam 6 (`impact` f3), telegraph_charge 4, charge 6,
  telegraph_barrage 6, barrage 4 (`fire` f1), summon 6, roar 8, telegraph_laser 6, laser 4, hurt 2, death 14.
* **Hydra** (rooted): idle 6, roar 8, hurt 2, death 14, telegraph_bite 5, bite 5 (`hit` f2),
  telegraph_spores 6, spores 4 (`fire` f1), telegraph_vines 5, vines 4 (`impact` f1), summon 6.
* **Core** (floating): idle 6, move 6, roar 8, hurt 2, death 14, telegraph_beam 6, beam 4,
  telegraph_volley 5, volley 4 (`fire` f1), telegraph_slam 5, slam 5 (`impact` f2), summon 6.
* **Wyrm** (flying serpent): idle 6, move 6, roar 8, hurt 2, death 14, telegraph_breath 6, breath 4,
  telegraph_dive 5, dive 4, telegraph_meteor 6, meteor 4 (`fire` f1), summon 6.

| id | name | planet | set | frame | collision |
|---|---|---|---|---|---|
| `spore_queen` | Reine Sporale | Sylvanox | hydra | 128x112 | 56x64 |
| `prime_sentinel` | Sentinelle Prime | Sylvanox | core | 112x112 | 48x48 |
| `magmaroth` | Magmaroth | Pyralis | knight | 96x96 (pivot [48,96]) | 44x60 |
| `ignivore` | Ignivore | Pyralis | wyrm | 128x96 (pivot [64,84]) | 56x40 |
| `forge_core` | Forgeron Primordial | Pyralis | core | 112x112 | 48x48 |
| `cryowyrm` | Cryowyrm | Glacior | wyrm | 128x96 (pivot [64,84]) | 56x40 |
| `frost_hydra` | Hydre de Givre | Glacior | hydra | 128x112 | 56x64 |
| `glaciarch` | Glaciarque | Glacior | knight | 96x96 (pivot [48,96]) | 44x60 |

Attack origins (mouths, weapons) are set per attack in the enemy definitions (`origin`, sprite
pixels from the feet, x mirrored with the facing).

### 14.4 Player, projectiles, objects

* `nova` rows 19-25: `glide` 4 (loop), `pound_dive` 2 (loop), `pound_land` 4 (`impact` f0, next idle),
  `charge` 4 (loop), `shoot` 3 (`fire` f0, next idle), `grapple` 4 (`fire` f1), `grapple_pull` 2 (loop).
* `projectiles` rows 6-18 (16x16, drawn travelling right): `charge_shot`, `fireball`, `magma_glob`,
  `ember`, `ice_shard`, `frost_orb`, `bomb`, `acid_glob`, `web`, `spore_big`, `energy_disc`, `rock`,
  `grapple_hook`.
* Objects: `grapple_point` 24x24 pivot [12,12] (idle 6, active 4), `cracked_floor` 32x16 pivot [16,16]
  (intact, cracked), `updraft_vent` 32x16 pivot [16,16] (idle 6), `heart_container` 16x16 pivot [8,16]
  (idle 8).
* `power_unlock` clips added: `glide`, `ground_pound`, `charge_shot`, `grapple`, `triple_jump`.
* `ui/icons_powers.png` is 320x32: 0 dash, 1 wave, 2 shield, 3 impulse, 4 teleport, 5 glide,
  6 ground_pound, 7 charge_shot, 8 grapple, 9 triple_jump.

### 14.5 Audio

Music: `pyralis_level`, `pyralis_boss`, `pyralis_ambience`, `glacior_level` (3/4),
`glacior_boss`, `glacior_ambience`, `sylvanox_depths` (all seamless loops; ambiences ≈ 60 s at
-24 LUFS, level tracks -14 LUFS, boss tracks -13.5 LUFS).
Sounds: `glide_open`, `glide_loop` (loop), `pound_dive`, `pound_impact`, `charge_loop` (loop),
`charge_ready`, `charge_shot`, `grapple_fire`, `grapple_hit`, `triple_jump`, `heart_pickup`,
`fireball`, `ice_shatter`, `freeze`, `bomb_drop`, `bomb_explode`, `slime_split`, `bat_screech`,
`burrow`, `web_hit`, `lava_bubble`, `boss_roar_fire`, `boss_roar_ice`, `mech_alarm`, `wyrm_roar`,
`updraft_loop` (loop). Loops are played as sustained effects (`AudioSystem::sustainLoop`).

### 14.6 Planet environments

* Tilesets (§7 format): `basalt`, `obsidian`, `forge_metal` (Pyralis); `glacier_ice` (JSON
  `"friction": 0.25`, slippery), `snow_rock`, `frost_metal` (Glacior). Optional JSON key
  `friction` (1 = normal grip).
* `textures/tiles/platforms.png` is 48x112: rows 3 `basalt_ledge`, 4 `chain_grate`, 5 `ice_ledge`,
  6 `snow_bridge`.
* Backgrounds `backgrounds/pyralis/{sky,far,mid,near,abyss,fg_embers}.png` and
  `backgrounds/glacior/{sky,far,mid,near,abyss,fg_icicles}.png`, same roles and parallax as
  Sylvanox (§8); abyss fill colour below the layer: Pyralis `#ff8a3d`, Glacior `#c2dbef`.
* Decor atlases (§9 format, names unique across atlases):
  * `pyralis_decor`: lava_vent (anim 6), basalt_column, basalt_column_broken, obsidian_spire,
    forge_anvil, forge_brazier (anim 6), hanging_chain, hanging_chain_long (pivot top), lava_fall
    (anim 6, pivot top), fire_plant (anim 4), slag_pile, gear_wreck, skull_rock, ember_crystal,
    steam_pipe, burnt_banner, ash_rock_small, ash_rock_big.
  * `glacior_decor`: ice_spire, icicles, icicles_long (pivot top), snow_drift, frozen_tree,
    snow_pine, ice_crystal, ice_crystal_big, frozen_statue, aurora_lamp (anim 6), frost_flower
    (anim 4), ice_arch, frozen_machine, frost_banner, frozen_bones, snow_rock_small, snow_rock_big,
    ice_floating (pivot centre).
* Banners `ui/planets/pyralis_banner.png`, `ui/planets/glacior_banner.png` (480x270).
