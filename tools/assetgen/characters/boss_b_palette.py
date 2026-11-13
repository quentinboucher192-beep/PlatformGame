"""Pyralis / Glacior colours and materials for the BOSSES-B sheets
(magmaroth, glaciarch, ignivore, cryowyrm).  Colours follow the v2 content
contract (section 1); a few extra in-between tones are added for shading ramps."""
from common.palette import hex_rgba as H
from raster import Mat

# ----------------------------------------------------------------- Pyralis
P_OUT = H('#14090b')                     # warm near-black outline
BASALT = [H(c) for c in ('#14090b', '#241215', '#3a1d1c', '#5a2b22')]
BASALT_LIT = H('#86533f')                # bevel highlight on basalt edges
LAVA = [H(c) for c in ('#b3201a', '#ff4d1e', '#ff8a3d', '#ffc56b', '#fff1c2')]
OBSID = [H(c) for c in ('#5b2a6e', '#9b4dca')]
RUST = [H(c) for c in ('#6e3b2a', '#a0583a', '#d08a5a')]
ASH = [H(c) for c in ('#4a3a3a', '#6e5a56', '#9a8580')]

# ----------------------------------------------------------------- Glacior
G_OUT = H('#0a1428')
NAVY = [H(c) for c in ('#0a1428', '#13264a', '#1f3a66')]
ICE = [H(c) for c in ('#2f6fb3', '#5fb0ff', '#a8e4ff', '#eefaff')]
SNOW = [H(c) for c in ('#c9d8ea', '#dfe9f5', '#ffffff')]
FROST = H('#7ff6ff')
AURORA = [H(c) for c in ('#5dffb0', '#b48cff')]
STEEL = [H(c) for c in ('#3d4a5e', '#5c6b80', '#8a9ab0')]
STEEL_LIT = H('#b4c2d4')

WHITE = H('#ffffff')


# ------------------------------------------------------------ Pyralis mats
# Magmaroth basalt: slightly cooler / greyer than the Pyralis rock so the golem separates from the
# red-brown forge backgrounds while the lava stays the only hot accent.
MAG = [H(c) for c in ('#8c6c60', '#5f4641', '#3d2b2b', '#24181a', '#150d0f')]
BAS = Mat([MAG[0], MAG[1], MAG[2], MAG[3]], outline=P_OUT, name="bb_basalt", thresholds=[0.8, 0.58, 0.34])
BAS_DK = Mat([MAG[1], MAG[2], MAG[3], MAG[4]], outline=P_OUT, name="bb_basalt_dk", thresholds=[0.8, 0.56, 0.32])
BAS_BK = Mat([MAG[2], MAG[3], MAG[4], MAG[4]], outline=P_OUT, name="bb_basalt_bk", thresholds=[0.78, 0.5, 0.3])
RUST_M = Mat([RUST[2], RUST[1], RUST[0], BASALT[2]], outline=P_OUT, name="bb_rust", thresholds=[0.8, 0.56, 0.32])
RUST_DK = Mat([RUST[1], RUST[0], BASALT[2], BASALT[1]], outline=P_OUT, name="bb_rust_dk",
              thresholds=[0.8, 0.56, 0.32])
OBS_M = Mat([H('#c58af0'), OBSID[1], OBSID[0], H('#2a1430')], outline=P_OUT, name="bb_obsidian",
            thresholds=[0.9, 0.66, 0.4])
OBS_DK = Mat([OBSID[1], OBSID[0], H('#2a1430'), BASALT[0]], outline=P_OUT, name="bb_obsidian_dk",
             thresholds=[0.86, 0.6, 0.36])
LAVA_M = Mat([LAVA[4], LAVA[3], LAVA[2], LAVA[1], LAVA[0]], outline=P_OUT, emissive=True, name="bb_lava",
             thresholds=[0.9, 0.74, 0.54, 0.32])
LAVA_NO = Mat([LAVA[4], LAVA[3], LAVA[2], LAVA[1], LAVA[0]], outline=None, emissive=True, name="bb_lava_no",
              thresholds=[0.9, 0.74, 0.54, 0.32])
SEAM = Mat([LAVA[3], LAVA[2], LAVA[1], LAVA[0]], outline=P_OUT, emissive=True, name="bb_seam",
           thresholds=[0.8, 0.55, 0.3])
SEAM_DIM = Mat([LAVA[1], LAVA[0], H('#6a1410')], outline=P_OUT, emissive=True, name="bb_seam_dim",
               thresholds=[0.6, 0.3])
SEAM_DEAD = Mat([BASALT[1], BASALT[0]], outline=P_OUT, name="bb_seam_dead", thresholds=[0.5])
EYE_FIRE = Mat([LAVA[4], LAVA[3], LAVA[2]], outline=P_OUT, emissive=True, name="bb_eye_fire",
               thresholds=[0.7, 0.4])
SLOT_P = Mat([BASALT[1], BASALT[0]], outline=P_OUT, name="bb_slot_p", thresholds=[0.5])
FLAME = Mat([LAVA[4], LAVA[3], LAVA[2], LAVA[1]], outline=None, emissive=True, name="bb_flame",
            thresholds=[0.82, 0.6, 0.34])
SMOKE = Mat([ASH[2], ASH[1], ASH[0]], outline=None, name="bb_smoke", thresholds=[0.7, 0.4])

# ------------------------------------------------------------ Glacior mats
STL = Mat([STEEL_LIT, STEEL[2], STEEL[1], STEEL[0]], outline=G_OUT, name="bb_steel", thresholds=[0.82, 0.6, 0.36])
STL_DK = Mat([STEEL[2], STEEL[1], STEEL[0], NAVY[2]], outline=G_OUT, name="bb_steel_dk",
             thresholds=[0.8, 0.56, 0.32])
STL_BK = Mat([STEEL[1], STEEL[0], NAVY[2], NAVY[1]], outline=G_OUT, name="bb_steel_bk",
             thresholds=[0.78, 0.52, 0.3])
ICE_M = Mat([ICE[3], ICE[2], ICE[1], ICE[0]], outline=G_OUT, name="bb_ice", thresholds=[0.8, 0.6, 0.38])
ICE_DK = Mat([ICE[2], ICE[1], ICE[0], NAVY[2]], outline=G_OUT, name="bb_ice_dk", thresholds=[0.8, 0.56, 0.32])
CRYST = Mat([ICE[3], FROST, ICE[2], ICE[1], ICE[0]], outline=G_OUT, emissive=True, name="bb_crystal",
            thresholds=[0.86, 0.7, 0.5, 0.3])
CRYST_NO = Mat([ICE[3], FROST, ICE[2], ICE[1]], outline=None, emissive=True, name="bb_crystal_no",
               thresholds=[0.82, 0.62, 0.36])
FROST_GLOW = Mat([WHITE, ICE[3], FROST, ICE[1]], outline=G_OUT, emissive=True, name="bb_frost_glow",
                 thresholds=[0.88, 0.66, 0.4])
CAPE = Mat([ICE[0], NAVY[2], NAVY[1], NAVY[0]], outline=G_OUT, name="bb_cape", thresholds=[0.8, 0.56, 0.3])
SNOW_M = Mat([SNOW[2], SNOW[1], SNOW[0], ICE[2]], outline=G_OUT, name="bb_snow", thresholds=[0.8, 0.56, 0.32])
EYE_ICE = Mat([WHITE, FROST, ICE[1]], outline=G_OUT, emissive=True, name="bb_eye_ice", thresholds=[0.78, 0.4])
GEM_AUR = Mat([H('#f0e2ff'), AURORA[1], H('#7a52c8')], outline=G_OUT, emissive=True, name="bb_gem_aurora",
              thresholds=[0.75, 0.42])
SLOT_G = Mat([NAVY[1], NAVY[0]], outline=G_OUT, name="bb_slot_g", thresholds=[0.5])
ICE_DEAD = Mat([STEEL[1], STEEL[0], NAVY[1]], outline=G_OUT, name="bb_ice_dead", thresholds=[0.6, 0.3])
MIST = Mat([WHITE, SNOW[1], ICE[2]], outline=None, name="bb_mist", thresholds=[0.7, 0.4])

# colour lists used by particles / effects (bright -> dark)
EMBERS = [LAVA[4], LAVA[3], LAVA[2], LAVA[1], LAVA[0]]
ASHES = [ASH[2], ASH[1], ASH[0]]
FROSTS = [WHITE, ICE[3], FROST, ICE[2], ICE[1]]
SNOWS = [SNOW[2], SNOW[1], SNOW[0], ICE[2]]
DUST_P = [H('#7a5a4a'), H('#5a3e34'), BASALT[2]]
DUST_G = [SNOW[1], SNOW[0], ICE[2]]

# ------------------------------------------------------------ wyrm scales
SCALE_F = Mat([H('#9a4a32'), H('#6a2a20'), H('#3e1618'), H('#220c10')], outline=P_OUT, name="bb_scale_fire",
              thresholds=[0.8, 0.57, 0.34])
SCALE_F_DK = Mat([H('#6a2a20'), H('#3e1618'), H('#220c10'), BASALT[0]], outline=P_OUT, name="bb_scale_fire_dk",
                 thresholds=[0.8, 0.55, 0.32])
BELLY_F = Mat([LAVA[4], LAVA[3], LAVA[2], LAVA[1], LAVA[0]], outline=P_OUT, emissive=True, name="bb_belly_fire",
              thresholds=[0.9, 0.72, 0.5, 0.28])
SCALE_I = Mat([ICE[1], ICE[0], NAVY[2], NAVY[1]], outline=G_OUT, name="bb_scale_ice", thresholds=[0.8, 0.57, 0.34])
SCALE_I_DK = Mat([ICE[0], NAVY[2], NAVY[1], NAVY[0]], outline=G_OUT, name="bb_scale_ice_dk",
                 thresholds=[0.8, 0.55, 0.32])
BELLY_I = Mat([WHITE, SNOW[1], SNOW[0], ICE[2], ICE[1]], outline=G_OUT, emissive=True, name="bb_belly_ice",
              thresholds=[0.9, 0.72, 0.5, 0.28])
TEETH = Mat([H('#fff6e0'), H('#e0cfae'), H('#a8916e')], outline=P_OUT, name="bb_teeth", thresholds=[0.7, 0.4])
DEAD_F = Mat([BASALT[2], BASALT[1], BASALT[0]], outline=P_OUT, name="bb_dead_fire", thresholds=[0.6, 0.3])
DEAD_I = Mat([STEEL[1], STEEL[0], NAVY[1]], outline=G_OUT, name="bb_dead_ice", thresholds=[0.6, 0.3])
MIST_M = Mat([SNOW[1], SNOW[0], ICE[2], ICE[1]], outline=G_OUT, name="bb_mist_strand", thresholds=[0.8, 0.56, 0.32])
