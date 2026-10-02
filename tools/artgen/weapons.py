# Every weapon in the Trawl's catalogue (data/trawl/weapons.tsv), part by part on real period designs (the Visual
# Overhaul Spec, section 6), built with the gun kit (gunkit.py) and baked one per file:
#     blender -b --factory-startup -P tools/artgen/weapons.py -- --out assets/shared/weapons [--only revolver,derringer]
# Recipes live in weapons_side.py (sidearms), weapons_long.py (long guns and specials) and weapons_melee.py (melee and
# thrown); each is a function build_<id>(W) on a fresh gunkit.Weapon.

import os
import sys
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import gunkit as K
import weapons_side
import weapons_long
import weapons_melee

RECIPES = {}
for mod in (weapons_side, weapons_long, weapons_melee):
    for name in dir(mod):
        if name.startswith("build_"):
            RECIPES[name[6:]] = getattr(mod, name)

a = C.args()
only = a[a.index("--only") + 1].split(",") if "--only" in a else None
out = C.out_dir()
for wid, fn in RECIPES.items():
    if only and wid not in only:
        continue
    W = K.Weapon(wid)
    fn(W)
    W.finish(out, size=getattr(fn, "texture", 1024))
