import importlib, hashlib
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill
from openpyxl.utils import get_column_letter
F=Font(name="Arial",size=10); H=Font(name="Arial",size=10,bold=True); B=Font(name="Arial",size=10,color="0000FF"); FILL=PatternFill("solid",fgColor="DDEBF7")
def hdr(ws,row,vals):
    for i,v in enumerate(vals,1):
        c=ws.cell(row=row,column=i,value=v); c.font=H; c.fill=FILL
def fit(ws,w=16):
    for i in range(1,ws.max_column+1): ws.column_dimensions[get_column_letter(i)].width=w
wb=Workbook(); ws=wb.active; ws.title="README"
for i,t in enumerate(["Red Tide — art workbook: body plans and per-species parameters","",
"BodyPlans: the 20 procedural body plans CreatureBuilder implements (primitives, parameters, animation). Each map's Species sheet assigns every beast a plan and its parameter values, a 3-band palette, a material, secondary-motion chains, an idle, and its distinctive part.",
"Rules: length by size class (1: 0.1-0.3 m, 2: 0.3-0.9, 3: 1-2.2, 4: 2.5-4.5, 5: 5-12, 6: 18-30, 7: 120). Ink outline weight = 1 px per metre of length, min 1.5, max 6. Palettes use the map's 5 base tones and 2 accents (Palettes sheet) so a species reads as belonging to its map.",
"Silhouette test: every species must be identifiable as a black shape at 20 m against the others in its map (the --silhouette shot mode from the master reference)."],1):
    ws.cell(row=i,column=1,value=t).font=H if i==1 else F
ws.column_dimensions["A"].width=150
PLANS=[
("fusiform","Torpedo fish","Lathed spindle body; dorsal, anal, paired pectoral and pelvic fins as tapered quads; caudal fin by tail_type","length, girth_ratio (0.15-0.35), head_ratio (0.2-0.35), tail_type (forked/rounded/lunate/pointed), dorsal_count (1-2), fin_scale, eye_size, mouth (small/under/gape)","Traveling sine along the spine, amplitude rising to the tail; pectorals sweep on turns; gill flare on attack","Sardine, barracuda, tuna, grouper, wrasse"),
("compressiform","Deep-bodied fish","Lathed body squashed sideways (girth_z 0.3 x girth_y); tall dorsal and anal fins; small caudal","length, height_ratio (0.5-0.9), fin_height, eye_size, stripe_count","Body rocks side to side rather than sweeping; fins flutter","Angelfish, butterflyfish, triggerfish, surgeonfish, cardinalfish"),
("anguilliform","Eels, worms, ribbons","A tube swept along a spline of 12-24 segments; continuous fin ribbon; no pectorals (eels) or none at all (worms)","length, segments, girth_ratio (0.04-0.12), fin_ribbon (bool), head (eel/worm/lure), jaw_hinge","Full-body undulation with 1.5-2.5 wavelengths; a strike straightens the front half in 0.2 s","Moray, conger, cave eel, oarfish, bobbit worm, ghost worm, the Wyrm"),
("depressiform","Rays, skates, flounders","A flattened disc (lathed ellipse, girth_y 0.1) with wing edges; a whip or paddle tail","span, disc_shape (round/diamond/kite), tail_type (whip/paddle/none), eye_top (bool), barb (bool)","Wing-edge wave front to back; rests by settling into silt with a particle puff","Stingray, manta, whip ray, electric ray, deep skate, flounder"),
("shark","Sharks","Fusiform with a heterocercal tail, 5-7 gill slits as ink lines, pointed snout, triangular dorsal","length, girth_ratio, snout (pointed/blunt/hammer/goblin/frilled), dorsal_size, gill_slits, tail_upper_ratio","Stiff-body sine (low amplitude), tail does the work; rolls the eye on bite; pectorals drop as a threat display","Bull, tiger, hammerhead, sixgill, goblin, blacktip, nurse"),
("turtle","Turtles","A domed shell (lathed hemisphere with plate ink lines) over a flattened body; four flippers or legs; retractable head","length, dome_height, plate_pattern (scute/leather), limb (flipper/leg), neck_length, beak (bool)","Alternating flipper strokes; head retracts on hit; shell is the armored front","Green, hawksbill, loggerhead, leatherback, hadal, snapping"),
("crocodilian","Crocodiles, caimans, monitors","Long jaw (two hinged wedges), armored back (rows of scute quads), four splayed legs, a tapering tail","length, jaw_ratio (0.2-0.3), scute_rows, leg_length, tail_ratio, eye_ridge","Lateral tail sculling in water; sprawling gait on land; death roll is a full-body spin about the spine","Saltwater crocodile, Nile crocodile, cave caiman, pale caiman, water monitor"),
("snake","Sea snakes, kraits","Anguilliform with a paddle tail, distinct head, and banding ink","length, band_count, head_ratio, paddle_tail (bool)","Undulation; coils before a strike (S-curve), then straightens","Banded, olive, cave, temple, abyssal snakes"),
("crab","Crabs, lobsters, isopods","A carapace (lathed dome or box), 8 legs of 2-3 segments on IK, 2 claws (lobster/crab) or none (isopod); antennae chains","width, carapace_shape (round/box/segmented), claw_size, leg_length, antenna_length, spines (bool), decoration (bool)","Sideways walk on IK legs; claws raise as a tell; isopods roll into a ball","Fiddler, hermit, spider crab, lobsters, isopods, the Lobster"),
("shrimp","Shrimps, mantis shrimps, amphipods","Segmented arched body of 6-8 lathed rings; fan tail; long antennae chains; raptorial arms (mantis)","length, segments, arch, antenna_length, raptorial (bool), transparency","Tail flicks propel backward; mantis punch is a 2-frame snap","Cleaner shrimp, glass shrimp, mantis shrimp, krill, amphipods"),
("cephalopod","Octopus, squid, cuttlefish","A mantle (lathed egg or cone), a head with two large eyes, 8 arm chains (plus 2 tentacle chains for squid), fins (squid/cuttlefish)","mantle_length, mantle_shape (egg/cone/round), arm_length, arm_count, fin (bool), eye_size, chromatophore (bool)","Arms are Chains driven by the body; jet pulse compresses the mantle; chromatophore flash is a palette pulse","Octopus, squid, cuttlefish, blue-ring, dumbo, colossal squid"),
("jelly","Jellies, siphonophores, anemones","A bell (lathed hemisphere, translucent material) and tentacle chains; siphonophores are a chain of bells","bell_diameter, tentacle_count, tentacle_length, translucency, bell_count (siphonophore)","Bell pulse (squash then stretch); tentacles trail as Chains","Box jelly, moon jelly, man o' war, deep jelly, siphonophore, anemones"),
("amphibian","Frogs, toads, newts, salamanders, olms","A broad head with a hinged jaw, four legs (frogs: long hind), smooth skin material; tail (newts, salamanders) or none (frogs)","length, head_width, leg_type (frog/salamander), tail (bool), gills_external (bool), skin (smooth/warty)","Frog: sit, then leap; salamander: lateral gait; tongue is a fast Chain extension","Toad, frogs, newts, olm, giant salamander, lab salamander, axolotl"),
("pinniped","Seals","Fusiform mammal with a blunt head, whisker ink, two front flippers and joined hind flippers","length, girth_ratio (0.3-0.4), whiskers (bool), bull_nose (bool)","Rolling swim with front-flipper strokes; hauls out with a belly hop","Monk seal, elephant seal"),
("cetacean","Whales, dolphins, orcas","Fusiform mammal with a horizontal fluke, a blowhole, dorsal fin, pectoral paddles","length, girth_ratio, dorsal (tall/curved/none), fluke_span, melon (bool), head (beak/blunt/block), patch_pattern (orca)","Vertical (up-down) sine unlike fish; breaches; clicks are a head-shake particle burst","Dolphin, humpback, sperm whale, the Matriarch"),
("insect","Insects, spiders","A segmented body of 2-3 lathed segments, 6 (insect) or 8 (spider) IK legs, optional wings (quads), antennae","length, segments, legs (6/8), wings (bool), leg_span, abdomen_ratio","Leg-cycle walk on rock; water-surface skating with a ripple; nymph jet pulse","Crickets, water bugs, beetles, nymphs, fishing spiders, sea spiders"),
("echinoderm","Urchins, stars, cucumbers, sea pigs","Radial: a lathed disc or ball with spines (urchin), five arms (star), a soft tube (cucumber), stubby legs (sea pig)","radius, arms (0/5), spine_length, tube (bool), legs (bool)","Near-static; urchin spines twitch; sea pig plods; brittle stars writhe","Urchins, crown-of-thorns, brittle stars, sand dollars, sea pigs, feather stars"),
("burrower","Garden eels, sand eels, lampreys, tube-dwellers","A short anguilliform that lives half-hidden: only the front third is built when emerged","length, emerged_ratio, mouth (sucker/point)","Rises from the sand, retracts in 0.15 s; lamprey attaches with a sucker ring","Garden eel, sand eel, lamprey, silt lamprey, hagfish"),
("bird","Cormorants, swiftlets","A fusiform body with two wing quads (folded or spread), a beak, thin legs","length, wingspan, beak_length, tail_length","Flapping in air; folded-wing torpedo dive underwater with a bubble trail","Cormorant, cave swiftlet"),
("colony","Colonies, swarms, sessile masses","Many small instanced bodies (krill, crickets, glowworms) or a mass (snapping shrimp colony, bone worms, glowworm threads)","body_count, body_plan_of_parts, spread_radius, light (bool)","Instanced boids or a static mass with a shimmer","Krill, crickets, glowworms, snapping shrimp, bone worms, sand hoppers"),
("leviathan","Bosses","The species' base plan scaled up with extra detail: plate rows, barnacles, scars, multiple eyes or lures; separate rig parts for jaw, tail, fins, and lure","base_plan, scale, plate_rows, barnacle_density, scar_count, extra_parts (lure/claws/fluke)","Slow, heavy: every motion has anticipation and overshoot; the jaw and tail are separate Chains","The Goliath, the Lobster, the Matriarch, the Wyrm, the Leviathan, the Sand Worm"),
]
ws=wb.create_sheet("BodyPlans"); hdr(ws,1,["plan","for","primitives","parameters","animation","examples"])
for r,p in enumerate(PLANS,2):
    for c,v in enumerate(p,1): ws.cell(row=r,column=c,value=v).font=F
fit(ws,22); ws.column_dimensions["C"].width=70; ws.column_dimensions["D"].width=70; ws.column_dimensions["E"].width=60
PAL={"ship":("rust brown, wet iron grey, kelp green, sand ochre, deep teal","brass gold, blood red"),"cave":("bone white, wet slate, glow-mould green, silt brown, ink black","phosphor teal, snottite yellow"),
"reef":("coral pink, lagoon turquoise, sand cream, staghorn tan, deep cobalt","sun gold, blood red"),"atlantis":("marble white, verdigris green, ichor black, sea-glass blue, terracotta","gold leaf, glow-moss violet"),"void":("abyss black, pressure grey, vent rust, bone pale, brine blue","emergency red, lure white")}
ws=wb.create_sheet("Palettes"); hdr(ws,1,["map","five base tones","two accents","rule"])
for r,(k,(b,a)) in enumerate(PAL.items(),2):
    for c,v in enumerate([k,b,a,"Base and belly from the base tones; accent only on eyes, lures, venom, and wounds; luminous species may use the accent as an emissive band"],1): ws.cell(row=r,column=c,value=v).font=F
fit(ws,40)
def plan_for(name,cls,tags):
    n=name.lower()
    if "worm" in n and "sand" in n or name in ("The Goliath","The Lobster","The Matriarch","The Cistern Wyrm","The Lantern Leviathan","The Sand Worm"): return "leviathan"
    if any(k in n for k in ("krill","cricket","glowworm","snapping shrimp","bone worm","sand hopper","amphipod","hadal amphipod")): return "colony"
    if cls=="Bird": return "bird"
    if cls=="Mammal": return "pinniped" if "seal" in n else "cetacean"
    if cls=="Amphibian": return "amphibian"
    if cls=="Cephalopod": return "cephalopod"
    if cls=="Cnidarian" or "anemone" in n: return "jelly"
    if cls=="Echinoderm": return "echinoderm"
    if cls.startswith("Insect") or "sea spider" in n: return "insect"
    if cls=="Crustacean" or cls=="Crustacean-like": return "shrimp" if any(k in n for k in ("shrimp","krill")) else "crab"
    if cls=="Reptile": return "turtle" if "turtle" in n or "loggerhead" in n or "leatherback" in n or "hawksbill" in n else ("snake" if "snake" in n or "krait" in n else "crocodilian")
    if cls=="Worm": return "anguilliform"
    if cls=="Mollusc": return "echinoderm" if "snail" in n or "whelk" in n else "cephalopod"
    if any(k in n for k in ("garden eel","sand eel","lamprey","hagfish")): return "burrower"
    if any(k in n for k in ("eel","conger","oarfish","wyrm","gulper","frilled","pelican")): return "anguilliform"
    if any(k in n for k in ("ray","skate","flounder")): return "depressiform"
    if "shark" in n or "chimaera" in n: return "shark"
    if any(k in n for k in ("angelfish","butterfly","trigger","surgeon","cardinal","damsel","sergeant","rabbit","bream","salema","dentex","anthias","clownfish","razorfish","hatchet","lionfish","squirrel","bass","grouper","parrot","wrasse")): return "compressiform" if any(k in n for k in ("angelfish","butterfly","trigger","surgeon","razorfish","hatchet","bream")) else "fusiform"
    return "fusiform"
LEN={1:(0.1,0.3),2:(0.3,0.9),3:(1.0,2.2),4:(2.5,4.5),5:(5,12),6:(18,30),7:(120,120)}
def h(s): return int(hashlib.md5(s.encode()).hexdigest(),16)
PART={"Bilge Sprat":"silver flank flash","Silverside":"mirror stripe","Great Barracuda":"underbite of long teeth; silver bars","Grouper":"vast mouth, mottled","Green Moray":"gaping jaw, green mucus sheen","Common Octopus":"a stolen item in one arm","Saltwater Crocodile":"barnacled scutes","Tiger Shark":"faded stripes, blunt snout","The Goliath":"boiler-sized, algae-lit gills behind plates",
"Olm":"pink, eyeless, external gills","Giant Cave Toad":"warty, throat sac","Cave Anglerfish":"a lure shaped like a drop bottle","Ghost Worm":"pale, translucent, glowing wall-cracks","Sleeper Shark":"parasite-clouded eyes, silt on the back","The Lobster":"crystal shards in the shell","Current Runner":"sail fin, streamlined",
"Blue-ringed Octopus":"pulsing blue rings","Bobbit Worm":"iridescent, antennae tips above sand","Titan Triggerfish":"yellow-edged fins, buck teeth","Bottlenose Dolphin":"grey, a scarred fin per individual","Great Hammerhead":"the hammer","The Matriarch":"orca patches, a notched dorsal, a scar across the eye patch","Reef Axolotl":"pink external gills, a permanent smile","Napoleon Wrasse":"the hump and lips",
"Octopus":"amphora fragments on its den","Conger Eel":"steel grey, drain-scarred","Water Monitor":"forked tongue, yellow bands","Nile Crocodile of the Canals":"a sluice-scarred snout","Giant Squid":"one eye the size of a helmet","The Cistern Wyrm":"a street-long conger with grate scars","Swordfish":"the bill","Bluefin Tuna":"a silver flash, yellow finlets","Leatherback":"ridged leathery shell",
"Pale Caiman":"eyeless, pale, tank-tag on the leg","The Relict":"mosasaur: four flippers, a crocodile's mouth on a whale's body, hadal pale","The Lantern Leviathan":"a lure that is an emergency light","The Sand Worm":"a horizon of sand and a mouth of rings","Colossal Squid":"hooks on the tentacles","Sperm Whale":"a block head, scars from squid","Coelacanth":"lobed fins, blue-steel scales, tank-tag","Lab Salamander":"lab-bred, pale, numbered","Elephant Seal":"the proboscis","Oarfish":"a red crest ribbon","Dragonfish":"invisible red light, needle teeth","Goblin Shark":"the slingshot jaw, pink skin"}
IDLE={"fusiform":"hover with slow tail sweeps, pectorals fanning","compressiform":"rock and fin-flutter","anguilliform":"slow S-wave, head questing","depressiform":"wing-edge ripple, settles into silt","shark":"endless slow cruise, never still","turtle":"slow flipper strokes, head scans","crocodilian":"float motionless, eyes above","snake":"gentle coil and uncoil","crab":"claw cleaning, sidestep","shrimp":"antenna sweep, fan tail twitch","cephalopod":"arm curl, chromatophore drift","jelly":"bell pulse","amphibian":"throat pulse, blink","pinniped":"roll, whisker twitch","cetacean":"slow rise and fall, fluke idle","insect":"leg twitch, antenna sweep","echinoderm":"spine twitch","burrower":"half-emerged sway","bird":"wing shrug, head bob","colony":"shimmer","leviathan":"breathing heave, barnacle drips"}
CHAIN={"fusiform":"none (fins are quads)","compressiform":"none","anguilliform":"the whole body is a Chain","depressiform":"tail Chain","shark":"none; tail on a bone","turtle":"none","crocodilian":"tail Chain","snake":"body Chain","crab":"2 antenna Chains","shrimp":"2 antenna Chains","cephalopod":"8-10 arm Chains","jelly":"tentacle Chains","amphibian":"tongue Chain (toad); tail Chain (salamander)","pinniped":"whisker Chains","cetacean":"none","insect":"antenna Chains","echinoderm":"arm Chains (brittle star) or none","burrower":"body Chain","bird":"none","colony":"none","leviathan":"jaw, tail, fins, lure as separate Chains"}
MAT={"Fish":"scale (specular, wet)","Reptile":"scute (matte, hard)","Crustacean":"shell (hard, glossy)","Crustacean-like":"shell","Amphibian":"skin (soft, wet sheen)","Mammal":"skin (matte, wet)","Cephalopod":"skin (chromatophore, soft)","Cnidarian":"translucent","Echinoderm":"shell (matte)","Insect":"chitin (glossy)","Insect (arachnid)":"chitin","Worm":"skin (translucent)","Mollusc":"shell / skin","Bird":"feather (matte)"}
for mod in ["species_ship","species_cave","species_reef","species_atlantis","species_void"]:
    m=importlib.import_module(mod); ws=wb.create_sheet("Species_"+m.SLUG)
    hdr(ws,1,["beast","class","size","body_plan","length_m","girth_ratio","head_ratio","tail_or_limb","fins_or_legs","eye_size","mouth","armor_plates","luminous","base_tone","belly_tone","accent","material","chains","idle","distinctive_part","lod_billboard_m","ink_weight_px"])
    base,acc=PAL[m.SLUG]; bases=[b.strip() for b in base.split(",")]; accs=[a.strip() for a in acc.split(",")]
    for r,s in enumerate(m.S,2):
        name,cls,size,tier,tags=s[0],s[1],s[2],s[3],s[4]; plan=plan_for(name,cls,tags); lo,hi=LEN[size]; hh=h(name)
        L=round(lo+(hi-lo)*((hh%100)/100),2)
        girth={"anguilliform":0.06,"burrower":0.07,"snake":0.05,"depressiform":0.9,"jelly":1.0,"echinoderm":1.0,"crab":0.8,"colony":0,"leviathan":0.3}.get(plan,0.22+((hh>>8)%12)/100)
        head={"cephalopod":0.4,"crocodilian":0.28,"amphibian":0.35,"leviathan":0.35}.get(plan,0.22+((hh>>16)%10)/100)
        tail={"fusiform":["forked","rounded","lunate","pointed"][hh%4],"shark":"heterocercal","cetacean":"horizontal fluke","depressiform":"whip" if "ray" in name.lower() else "paddle","turtle":"flippers","crocodilian":"tapered tail + 4 legs","snake":"paddle tail","crab":"8 legs + claws","shrimp":"fan tail","cephalopod":f"{8 if 'octopus' in name.lower() else 10} arms","jelly":"tentacles","amphibian":"tail" if "frog" not in name.lower() and "toad" not in name.lower() else "hind legs","pinniped":"hind flippers","insect":"6 legs" if "spider" not in name.lower() else "8 legs","bird":"tail feathers","burrower":"buried","echinoderm":"radial","colony":"n/a","anguilliform":"continuous fin","compressiform":"small caudal","leviathan":"see base plan"}.get(plan,"")
        fins={"fusiform":f"dorsal x{1+(hh>>4)%2}, pectoral, pelvic, anal","compressiform":"tall dorsal + anal","shark":"triangular dorsal, pectorals","cetacean":"dorsal + pectoral paddles","depressiform":"wing edges","crab":"legs 8, claws 2","shrimp":"swimmerets","insect":"legs","bird":"wings","cephalopod":"mantle fins" if "squid" in name.lower() or "cuttle" in name.lower() else "none"}.get(plan,"")
        eye={"shark":"small","cephalopod":"huge","depressiform":"top-mounted","crocodilian":"ridged","leviathan":"tiny, many"}.get(plan,"medium")
        if "blind" in tags or "Blind" in name or name=="Olm": eye="none"
        if "eye" in (PART.get(name,"")): eye="huge"
        mouth={"shark":"underslung gape","anguilliform":"hinged gape","crocodilian":"long jaw","cephalopod":"beak","jelly":"none","echinoderm":"none","colony":"none"}.get(plan,"small" if size<=2 else "gape")
        armor="yes" if "armored" in tags or plan in ("turtle","crab","crocodilian") else "no"
        lum="yes" if "luminous" in tags or "glow" in name.lower() else "no"
        bt=bases[hh%5]; bl=bases[(hh>>3)%5]; ac=accs[(hh>>5)%2]
        if "venom" in tags or "toxic" in tags: ac="warning "+accs[0]
        ws.cell(row=r,column=1,value=name).font=H
        for c,v in enumerate([cls,size,plan,L,girth,head,tail,fins,eye,mouth,armor,lum,bt,bl,ac,MAT.get(cls,"skin"),CHAIN[plan],IDLE[plan],PART.get(name,"as the plan; distinguish by tone and scale"),80 if size>=3 else 40,round(min(6,max(1.5,L)),1)],2):
            ws.cell(row=r,column=c,value=v).font=B if c in (5,6,7) else F
    fit(ws,14); ws.column_dimensions["A"].width=26; ws.column_dimensions["T"].width=50; ws.column_dimensions["S"].width=34; ws.freeze_panes="B2"
wb.save("/mnt/user-data/outputs/RedTide_Art_BodyPlans.xlsx"); print("saved")
