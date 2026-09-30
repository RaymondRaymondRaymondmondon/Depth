#!/usr/bin/env python3
"""Exports the Red Tide reference workbooks (Red_Tide_Reference/*.xlsx) to the JSON the game loads.

  python3 tools/export_redtide.py

Writes data/redtide/engine/*.json (from RedTide_Engine_and_Systems.xlsx), data/redtide/art/*.json (from
RedTide_Art_BodyPlans.xlsx) and data/redtide/maps/<map>/*.json (one folder per map workbook). The workbooks are
the source of truth (the design doc's "Map data workbooks" and "Engine and systems workbook" sections): edit a
workbook, re-run this, rebuild. Only data/redtide/maps/<map>/extra.json is hand-written (things the workbooks
state only in prose, like current direction and alarm regions).
"""
import json, os, re, sys
import openpyxl

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "Red_Tide_Reference")
OUT = os.path.join(ROOT, "data", "redtide")
MAPS = {"SunkenShip": "ship", "UnderwaterCave": "cave", "CoralReef": "reef", "Atlantis": "atlantis",
        "ApproachingTheVoid": "void"}


def cell(v):
    if v is None:
        return ""
    if isinstance(v, float) and v.is_integer():
        return int(v)
    if isinstance(v, str):
        s = v.strip()
        # list-valued constants are written as "[0.4, 0.35, ...]"
        if s.startswith("[") and s.endswith("]"):
            try:
                return json.loads(s)
            except ValueError:
                return s
        if s in ("True", "False"):
            return s == "True"
        return s
    return v


def rows_of(ws):
    out = []
    for r in ws.iter_rows(values_only=True):
        r = [cell(c) for c in r]
        while r and r[-1] == "":
            r.pop()
        if any(c != "" for c in r):
            out.append(r)
    return out


def table(rows, header_index=0, stop=None):
    """Rows after a header row become dicts keyed by the header. Stops at a blank-keyed or footnote row."""
    head = [str(h) for h in rows[header_index]]
    recs = []
    for r in rows[header_index + 1:]:
        if stop and stop(r):
            break
        if len(r) == 1 and isinstance(r[0], str) and len(head) > 2:
            continue  # a one-cell footnote under a table
        rec = {}
        for i, h in enumerate(head):
            if h:
                rec[h] = r[i] if i < len(r) else ""
        recs.append(rec)
    return recs


def split_sections(rows, titles):
    """Blockout-style sheets hold several tables, each introduced by a one-cell title row."""
    secs, cur = {}, None
    for r in rows:
        if len(r) == 1 and isinstance(r[0], str) and any(r[0].startswith(t) for t in titles):
            cur = next(t for t in titles if r[0].startswith(t))
            secs[cur] = []
            continue
        if cur:
            secs[cur].append(r)
    return secs


def dump(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1, ensure_ascii=False)


def export_map(xlsx, key):
    wb = openpyxl.load_workbook(xlsx, data_only=True)
    d = os.path.join(OUT, "maps", key)
    sheets = {ws.title: rows_of(ws) for ws in wb.worksheets}
    footnote = lambda r: len(r) >= 1 and isinstance(r[0], str) and not str(r[0]).isdigit() and len(r) <= 1
    dump(os.path.join(d, "readme.json"), [r[0] for r in sheets["README"]])
    sp = table(sheets["Species"], stop=lambda r: not isinstance(r[0], int))
    dump(os.path.join(d, "species.json"), sp)
    dump(os.path.join(d, "attacks.json"), table(sheets["Attacks"], stop=footnote))
    # Diet: predator rows x food columns
    diet = sheets["Diet"]
    foods = [str(x) for x in diet[0][1:]]
    rows = {}
    for r in diet[1:]:
        if not r or not isinstance(r[0], str) or len(r) < 2:
            continue
        w = {}
        for i, f in enumerate(foods):
            v = r[i + 1] if i + 1 < len(r) else ""
            if isinstance(v, (int, float)) and v > 0 and f and f.lower() not in ("sum", "row_sum"):
                w[f] = v
        if w:
            rows[r[0]] = w
    dump(os.path.join(d, "diet.json"), {"foods": [f for f in foods if f and f.lower() not in ("sum", "row_sum")], "rows": rows})
    dump(os.path.join(d, "flora.json"), table(sheets["Flora"], stop=footnote))
    dump(os.path.join(d, "spawn.json"), table(sheets["Spawn"], stop=footnote))
    # TideCurve: a Tunables block of name/value pairs, then the per-tide table
    tc = sheets["TideCurve"]
    tun, i = {}, 0
    while i < len(tc) and tc[i][0] != "tide":
        if len(tc[i]) >= 2 and isinstance(tc[i][0], str) and tc[i][0] != "Tunables":
            tun[tc[i][0]] = tc[i][1]
        i += 1
    tides = table(tc, i, stop=lambda r: not isinstance(r[0], int))
    dump(os.path.join(d, "tidecurve.json"), {"tunables": tun, "tides": tides})
    # Blockout: Zones / Links / Points of interest / Design notes
    secs = split_sections(sheets["Blockout"], ["Zones", "Links", "Points of interest", "Design notes"])
    bo = {}
    for name, key2 in (("Zones", "zones"), ("Links", "links"), ("Points of interest", "pois")):
        if name in secs and secs[name]:
            bo[key2] = table(secs[name], stop=lambda r: len(r) == 1)
    bo["notes"] = [" ".join(str(c) for c in r) for r in secs.get("Design notes", [])]
    dump(os.path.join(d, "blockout.json"), bo)
    dump(os.path.join(d, "boss.json"), [r[0] for r in sheets["Boss"]])
    if "Faction" in sheets:
        dump(os.path.join(d, "faction.json"), [[c for c in r] for r in sheets["Faction"]])
    print(f"  {key}: {len(sp)} species, {len(rows)} diet rows, {len(bo.get('zones', []))} zones")


def export_engine(xlsx):
    wb = openpyxl.load_workbook(xlsx, data_only=True)
    d = os.path.join(OUT, "engine")
    sh = {ws.title: rows_of(ws) for ws in wb.worksheets}
    consts = {}
    for r in table(sh["Constants"]):
        if r.get("constant"):
            consts[r["constant"]] = {"system": r.get("system", ""), "value": r.get("value"), "unit": r.get("unit", ""),
                                     "note": r.get("effect / intent", "")}
    dump(os.path.join(d, "constants.json"), consts)
    mv = {}
    for r in table(sh["Movement"]):
        if r.get("parameter"):
            mv[r["parameter"]] = r.get("value")
    dump(os.path.join(d, "movement.json"), mv)
    drops = sh["Drops"]
    i = next(k for k, r in enumerate(drops) if r[0] == "locker pool")
    dump(os.path.join(d, "drops.json"), {
        "drops": table(drops[:i], stop=lambda r: len(r) == 1),
        "drop_rule": next((r[0] for r in drops[:i] if len(r) == 1 and isinstance(r[0], str) and "chance" in r[0]), ""),
        "locker": table(drops, i, stop=lambda r: len(r) == 1),
        "locker_rule": next((r[0] for r in drops[i:] if len(r) == 1), "")})
    dump(os.path.join(d, "progression.json"), [[c for c in r] for r in sh["Progression"]])
    for m in MAPS.values():
        s = "Dossier_" + m
        if s in sh:
            dump(os.path.join(d, f"dossier_{m}.json"), table(sh[s], stop=lambda r: len(r) == 1))
    dump(os.path.join(d, "barks.json"), table(sh["Barks"], stop=lambda r: len(r) == 1))
    print(f"  engine: {len(consts)} constants, {len(mv)} movement values")


def export_art(xlsx):
    wb = openpyxl.load_workbook(xlsx, data_only=True)
    d = os.path.join(OUT, "art")
    sh = {ws.title: rows_of(ws) for ws in wb.worksheets}
    dump(os.path.join(d, "bodyplans.json"), table(sh["BodyPlans"]))
    dump(os.path.join(d, "palettes.json"), table(sh["Palettes"]))
    for m in MAPS.values():
        s = "Species_" + m
        if s in sh:
            dump(os.path.join(d, f"species_{m}.json"), table(sh[s], stop=lambda r: len(r) == 1))
    print("  art: body plans, palettes, per-map species art")


def main():
    if not os.path.isdir(SRC):
        sys.exit("Red_Tide_Reference/ not found")
    for f in sorted(os.listdir(SRC)):
        m = re.match(r"RedTide_(\w+)_data\.xlsx$", f)
        if m and m.group(1) in MAPS:
            export_map(os.path.join(SRC, f), MAPS[m.group(1)])
    export_engine(os.path.join(SRC, "RedTide_Engine_and_Systems.xlsx"))
    export_art(os.path.join(SRC, "RedTide_Art_BodyPlans.xlsx"))


if __name__ == "__main__":
    main()
