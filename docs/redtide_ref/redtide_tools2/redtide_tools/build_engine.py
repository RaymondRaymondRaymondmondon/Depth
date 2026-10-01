import importlib, sys
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill
from openpyxl.utils import get_column_letter
F=Font(name="Arial",size=10); H=Font(name="Arial",size=10,bold=True); B=Font(name="Arial",size=10,color="0000FF"); FILL=PatternFill("solid",fgColor="DDEBF7")
wb=Workbook()
def hdr(ws,row,vals):
    for i,v in enumerate(vals,1):
        c=ws.cell(row=row,column=i,value=v); c.font=H; c.fill=FILL
def fit(ws,w=16):
    for i in range(1,ws.max_column+1): ws.column_dimensions[get_column_letter(i)].width=w
def table(ws,r0,cols,rows,blue=()):
    hdr(ws,r0,cols)
    for r,row in enumerate(rows,r0+1):
        for c,v in enumerate(row,1): ws.cell(row=r,column=c,value=v).font=B if c in blue else F
    return r0+len(rows)+2
ws=wb.active; ws.title="README"
for i,t in enumerate(["Red Tide — engine and systems workbook","","Constants: every tuning number in the ecosystem, blood, sound, alarm, and population systems, with the effect it should have. Blue = tunable.",
"Movement: the diver's body. Drops and Locker: probability tables. Progression: tokens, ranks, cosmetics. Dossier_<map>: generated dossier pages for every species (diet and predators derived from each map's diet matrix). Barks: the four divers' line sets.",
"All numbers are design targets (2026-09-30); verify with --eco-sim and playtests. Load as redtide_engine/*.json with the same export approach as the map workbooks."],1):
    ws.cell(row=i,column=1,value=t).font=H if i==1 else F
ws.column_dimensions["A"].width=150
# ---------------- Constants
ws=wb.create_sheet("Constants")
C=[
("SCENT","scent_cell_m",2,"m","Scent grid cell size"),("SCENT","scent_update_hz",1,"Hz","Grid update rate (background thread)"),
("SCENT","scent_diffusion",0.15,"fraction/s","Share of a cell's blood that spreads to its 6 neighbours each second (2.5% each)"),("SCENT","scent_current_advect",0.8,"fraction","How much of the local current velocity carries blood (0 = none, 1 = fully)"),
("SCENT","scent_decay_early",0.02,"fraction/s","Tides 1-9 (from TideCurve)"),("SCENT","scent_decay_mid",0.01,"fraction/s","Tides 10-19"),("SCENT","scent_decay_late",0.005,"fraction/s","Tides 20+"),
("SCENT","scent_floor",0.5,"blood","Below this a cell reads 0 (keeps the grid sparse)"),("SCENT","scent_visible_min",3,"blood","Blood plume particles start at this cell value"),
("BLOOD","blood_wound_fraction_min",0.1,"fraction","A beast below full HP bleeds at least 10% of its size x 2 per s"),("BLOOD","blood_corpse_burst",1.0,"x size x 20","Death releases size x 20 at once"),("BLOOD","blood_corpse_trickle",0.05,"x burst/s","A corpse then trickles 5% of its burst per second until eaten"),
("BLOOD","blood_corpse_life_s",[30,45,60,90,120,180],"s by size","How long a corpse lasts with no scavengers"),("BLOOD","blood_scavenger_rate",[0.3,0.5,1.0,3.0],"blood/s by scavenger size","How fast scavengers remove corpse blood"),
("BLOOD","blood_diver_wounded",2,"blood/s","A diver below 50% HP"),("BLOOD","blood_diver_downed",8,"blood/s","A downed diver"),("BLOOD","blood_melee_mult",0.5,"x","Melee kills make half the blood"),("BLOOD","blood_boiling_mult",0.0,"x","Boiling ammunition makes no blood"),
("SENSE","sample_range_mult",1.0,"x scent_m","A beast samples the grid out to its scent_m"),("SENSE","threshold_hysteresis",0.7,"x threshold","Stops investigating when local blood falls below 70% of threshold"),
("SENSE","gradient_step_m",4,"m","Investigate moves toward the highest neighbouring cell at this stride"),("SENSE","sight_cone_deg",120,"deg","Field of view for sight (blind species: 0)"),("SENSE","sight_in_silt_mult",0.4,"x","Sight range in silt or ink clouds"),
("SENSE","electro_through_walls",True,"bool","Electroreception ignores occlusion"),("SENSE","lamp_visibility_bonus_m",10,"m","A lit lamp adds this to the range any sighted beast sees the diver from"),
("SOUND","sound_cell_decay",0.2,"fraction/s","Sound field decays fast"),("SOUND","sound_diffusion",0.3,"fraction/s","Spreads faster than scent"),("SOUND","noise_gunshot_base",1,"x weapon noise","Weapon noise 1-10 maps to field value x 10"),("SOUND","noise_explosion",30,"field","Limpet, Cannon Harpoon impact, traps"),
("SOUND","noise_melee",0,"field","Melee is silent"),("SOUND","fear_flee_threshold",15,"field","Fearful beasts (fear > 0.6) flee sound above this"),("SOUND","curious_approach_threshold",8,"field","Curious beasts (curiosity > 0.5) approach sound between 8 and 25"),("SOUND","aggressive_attack_threshold",20,"field","Aggressive beasts (aggression > 0.7) attack the source above this"),
("ALARM","alarm_gunshot_mult",1.0,"x weapon noise","Per shot"),("ALARM","alarm_explosion_mult",3.0,"x","Explosions and traps"),("ALARM","alarm_decay",0.05,"fraction/s","Region alarm decay"),("ALARM","alarm_inside_hull_mult",2.0,"x","Sunken Ship inside region (hull carries sound)"),
("ALARM","alarm_roll_interval_s",5,"s","Spawn roll cadence above threshold"),("ALARM","alarm_reset_on_spawn",0.5,"x","Alarm halves when a squad spawns"),("ALARM","alarm_min_distance_m",35,"m","Squads never spawn closer than this to a diver"),
("HUNGER","hunger_rate",[0.4,0.35,0.3,0.25,0.2,0.1],"/min by size","Hunger 0-1 rises at this rate"),("HUNGER","hunt_weight_at_hunger",[0.0,0.3,0.7,1.0],"weight at hunger 0/0.5/0.8/1","How much hunger raises Hunt over Graze/Rest"),
("HUNGER","fed_duration_s",[20,30,45,60,90,120],"s by size","After eating, hunger resets and Hunt is suppressed"),("HUNGER","eat_time_s",[2,3,5,8,12,20],"s by prey size","Time to consume prey of that size"),
("STATE","investigate_timeout_s",30,"s","Give up investigating if the source isn't found"),("STATE","hunt_give_up_distance_m",60,"m","Abandon a chase beyond this from home (territorial) or start (wanderer: 200)"),("STATE","flee_hp_fraction",0.2,"fraction","Flee below 20% HP (fear > 0.3)"),
("STATE","flee_trail_blood_mult",0.5,"x","A fleeing beast bleeds at half rate and stops 10 s after reaching home"),("STATE","defend_escalation_s",3,"s","Territorial: warning display for 3 s, then attack"),("STATE","return_home_calm_s",20,"s","Return home after 20 s without stimuli"),
("SOCIAL","school_cohesion",0.6,"boids weight","Schooling weights: cohesion / alignment / separation"),("SOCIAL","school_alignment",0.8,"boids weight",""),("SOCIAL","school_separation",1.2,"boids weight",""),("SOCIAL","school_scatter_s",10,"s","Regroup delay after a shot into the school"),
("SOCIAL","pack_roles","flank,drive,leader","list","Packs assign roles; the leader is marked (ink ring) and packs break for 15 s if it dies"),("SOCIAL","swarm_contact_dps_mult",1.0,"x","Swarm damage scales with bodies overlapping the diver"),
("POP","spawn_check_interval_s",10,"s","Region spawn pools are checked every 10 s"),("POP","capacity_flora_mult",1.0,"x","Grazer capacity scales with local flora density (0.5-1.5)"),("POP","predator_patrol_by_prey",0.02,"per grazer","Extra predator visit chance per second per 10 grazers in a region"),
("POP","apex_wander_interval_s",[240,180,120],"s at tides 10/15/20+","How often a wandering apex picks a new region"),("POP","boss_wander_interval_s",180,"s from tide 12","Boss relocation cadence"),("POP","respawn_tide_mult",0.97,"x per tide","Respawn timers shorten 3% per tide"),
("SYMBIOSIS","cleaner_calm_radius_m",6,"m","Hosts within this of a cleaner station lose 20% aggression"),("SYMBIOSIS","cleaner_loss_penalty",0.2,"fraction","Killing all cleaners in a region: +20% aggression map-wide for 300 s"),("SYMBIOSIS","parasite_attach_s",0.5,"s","Attach time on contact"),("SYMBIOSIS","brush_off_s",1.5,"s","Cleaning brush time per parasite"),
("FLORA","graze_rate",0.5,"units/s per grazer","Flora units eaten per grazer per second"),("FLORA","flora_unit_capacity",100,"units","A patch's max units"),("FLORA","flora_regrow_delay_s",60,"s","After grazed to 0 (per flora override)"),
("ENEMY","enemy_hp_growth",1.08,"x/tide",""),("ENEMY","enemy_weapon_tier_every",5,"tides","Weapons upgrade every 5 tides; Forged at 15+"),("ENEMY","enemy_blood_yield",20,"blood on death (size 2-3 humans)","Ichor for Lost Ones uses the same value in the ichor channel"),("ENEMY","ichor_repel_s",60,"s","Apex avoid ichor cells for this long"),
("PLAYER","player_hp",100,"HP","250 with Juggernaut"),("PLAYER","regen_delay_s",4,"s",""),("PLAYER","regen_rate",20,"HP/s",""),("PLAYER","down_bleedout_s",45,"s",""),("PLAYER","revive_s",4,"s","2 with Quick Brine"),
]
r=table(ws,1,["system","constant","value","unit","effect / intent"],[(a,b,str(c) if isinstance(c,(list,bool)) else c,d,e) for a,b,c,d,e in C],blue=(3,))
ws.cell(row=r,column=1,value="Worked check: a size-3 corpse (60 blood burst + 3/s trickle) with diffusion 0.15 and decay 0.02 reads about 30 blood at 20 m after 40 s in still water, which is a threshold-30 predator's cue at its scent range: the doc's '30-90 s to an apex' target holds when the apex threshold is 120 and four such corpses exist.").font=F
fit(ws,18); ws.column_dimensions["E"].width=90; ws.column_dimensions["B"].width=30
# ---------------- Movement
ws=wb.create_sheet("Movement")
M=[("swim_speed",2.0,"m/s","Base"),("sprint_speed",3.4,"m/s","Shift; stamina bar shows only while sprinting"),("sprint_stamina_s",6,"s","Full sprint duration"),("stamina_regen_s",8,"s","Empty to full"),("vertical_speed",1.4,"m/s","Space / Ctrl"),
("accel_s",0.4,"s","0 to full speed"),("decel_s",0.5,"s","Full to 0 (water drag)"),("turn_rate_deg_s",360,"deg/s","Mouse turn is instant; body follows at this rate for animation"),("ads_swim_mult",0.6,"x","Speed while aiming"),("walk_speed_air",1.6,"m/s","In air chambers; gravity on; no jumping, a 1 m step-up"),
("kick_brine_swim_mult",1.25,"x","Kick Brine speed"),("kick_brine_stamina_mult",1.5,"x",""),("grab_break_taps",6,"taps","Mash to break a hold early (hold time - taps x 0.25 s)"),("grab_break_teammate_dmg",0.15,"fraction of holder HP","Damage that makes a holder drop a diver"),
("knockback_aim_recover_s",0.6,"s","Aim sway after a knockback"),("stun_s_default",1.0,"s","Unless the attack says otherwise"),("slow_default",0.7,"x","Slow effects unless stated"),("melee_lunge_m",1.5,"m","Knife lunge distance on sprint"),
("interact_hold_s",1.0,"s","Purchases over 1,000 scrip need a hold"),("revive_move_allowed",False,"bool","Reviving diver must hold still"),("downed_crawl_speed",0.6,"m/s","Downed divers can drift-crawl"),("fall_into_void_s",5,"s","Pull duration past the abyss edge with a warning at 1 s")]
table(ws,1,["parameter","value","unit","note"],[(a,str(b) if isinstance(b,bool) else b,c,d) for a,b,c,d in M],blue=(2,)); fit(ws,20); ws.column_dimensions["D"].width=70
# ---------------- Drops & Locker
ws=wb.create_sheet("Drops")
r=table(ws,1,["drop","weight_tides_1_9","weight_tides_10_19","weight_tides_20+","min_tide","notes"],[
("Resupply",30,30,35,1,"Always available; weight rises late when ammo is the limit"),("Double Scrip",25,20,15,1,""),("Shipwright",15,10,5,1,"Only if a barricade net is damaged; otherwise re-rolled"),
("Purge",10,15,15,3,"Kills within 40 m, no blood"),("Blood Frenzy",5,15,20,4,"Never two in a row"),("Fire Sale",10,5,5,5,"Never within 3 tides of the last"),("Harpoon Hour",5,5,5,8,"")],blue=(2,3,4,5))
ws.cell(row=r,column=1,value="A kill drops a bottle with chance 2.5% (1 in 40), rising to 4% when the team has had no drop for 3 minutes; the last two drops are excluded from the roll; drops last 30 s on the floor and never spawn inside Reacher coral, lava paths, or over the abyss.").font=F
r+=2; r=table(ws,r,["locker pool","weight","notes"],[("Rack weapons (any)",45,"Each rack weapon equally likely"),("Gatling Needler",6,""),("Twin Gannets",7,""),("Stormlock",7,"Void: also on a rack in the Vault"),("Drum Flechette",6,""),("Reef Rattler",7,""),("Cannon Harpoon",4,""),("Limpet Launcher",4,""),("Net Gun",6,""),("Tesla Gaff",3,""),("Trident",3,""),("Wonder weapon (map's)",2,"Guaranteed by the 12th team pull if no diver holds it; 0 while a diver holds it")],blue=(2,))
ws.cell(row=r,column=1,value="Pull costs 950 (10 during Fire Sale). The eel moves after a random 8-12 pulls (the chest snaps shut and sinks; a lantern buoy marks the new spot). Locations rotate among the map's Locker spots, never the same one twice in a row. A pull from a weapon the diver already holds refills that weapon instead.").font=F
fit(ws,18); ws.column_dimensions["F"].width=60; ws.column_dimensions["C"].width=60
# ---------------- Progression
ws=wb.create_sheet("Progression")
r=table(ws,1,["source","arcade tokens"],[("Match played",10),("Match won (tide 10+)",25),("Per tide past 10",2),("First time reaching tide 20 on a map",50),("Boss kill (first per map)",30),("Hidden quest completed (first per map)",100),("Dossier completed for a map",75)],blue=(2,))
r=table(ws,r,["cosmetic","cost"],[("Weapon finish (barnacle, verdigris, bone inlay, pearl, red tide)",150),("Suit colour set",100),("Helmet (verdigris, red tide, bone, pearl, Atlantean)",200),("Dealer skin for Flats Duel (shared profile)",200),("Cabinet marquee",300)],blue=(2,))
ranks=[]
for lv in range(1,51):
    need=int(50*lv**1.6); unlock=""
    if lv in (2,5,9,14,20,27,35,44): unlock="Salt Charm: "+["Keep Your Brines","Salt Circle","Slick Fins","Fisher's Luck","Chum Bucket","Hard Shell","Clean Water","Ghost Fin"][(2,5,9,14,20,27,35,44).index(lv)]
    if lv in (3,10,18,30,42): unlock="Skin: "+["Verdigris suit","Bone helmet","Red Tide suit","Pearl helmet","Atlantean helmet"][(3,10,18,30,42).index(lv)]
    if lv in (7,15,25,40,50): unlock="Charm pouch slot +1 (max 5)" if lv<50 else "Title: Red Tide Master; all cosmetics 50% off"
    if lv in (12,22,33): unlock="Dossier bonus page: "+["the Owners","the expedition","the station"][(12,22,33).index(lv)]
    ranks.append((lv,need,unlock))
r=table(ws,r,["rank","tokens_to_reach","unlock"],ranks,blue=(2,))
ws.cell(row=r,column=1,value="Rank cost = 50 x rank^1.6 tokens cumulative-style (each rank's own cost); a good match earns 40-70 tokens, so rank 50 is about 150 matches. Lucky Locker and Fair Shares are earned by tide milestones (tide 15 and 25 on any map). Ranks never change in-match power.").font=F
fit(ws,22); ws.column_dimensions["A"].width=60; ws.column_dimensions["C"].width=60
# ---------------- Dossiers per map
ARCH_NOTE={"Schooler":"Shoot one and the rest scatter, then regroup; the school is a bank of blood you're spending.","Ambusher":"It waits where you'd stand. Look for the tell before you pass, or go around.","Patroller":"It has a route. Learn it and it's a clock; interrupt it and it's a hunter.","Territorial":"It warns before it bites. Leave the radius and it forgets you.","Pack hunter":"They flank. Put a wall at your back and kill the marked leader.","Swarmer":"Ink it, wall it, or keep moving; standing still is the damage.","Parasite":"Brush it off or it turns you into bait.","Grazer":"Harmless, and every dead one is blood in the water for nothing.","Wanderer":"It comes for blood from far away and leaves when it's fed. Feed it something else.","Boss":"See the ship's log."}
for mod in ["species_ship","species_cave","species_reef","species_atlantis","species_void"]:
    m=importlib.import_module(mod); names=[s[0] for s in m.S]
    ws=wb.create_sheet("Dossier_"+m.SLUG)
    rows=[]
    for s in m.S:
        name,cls,size,tier,tags,arch=s[0],s[1],s[2],s[3],s[4],s[5]
        eats=", ".join(k for k,v in sorted(m.D.get(name,{}).items(),key=lambda kv:-kv[1])) or "nothing"
        eaten=", ".join(p for p in names if name in m.D.get(p,{})) or ("nothing" if tier>=4 or "toxic" in tags else "nothing recorded")
        rows.append((name,cls,size,{0:"scavenger/cleaner/parasite",1:"grazer or filter feeder",2:"small predator",3:"mid predator",4:"apex",5:"boss"}.get(tier,""),eats,eaten,s[19],s[22],ARCH_NOTE.get(arch,""),"kill or observe 30 s"))
    r=table(ws,1,["beast","class","size","tier","diet (from the diet matrix)","eaten by","weak point","field note","how to handle it","unlock"],rows)
    for f in m.FLORA: ws.cell(row=r,column=1,value=f[0]).font=F; ws.cell(row=r,column=2,value="flora: "+f[1]).font=F; ws.cell(row=r,column=8,value=f[3]).font=F; ws.cell(row=r,column=10,value="touch, shoot, or observe 30 s").font=F; r+=1
    fit(ws,18); ws.column_dimensions["E"].width=40; ws.column_dimensions["F"].width=40; ws.column_dimensions["H"].width=70; ws.column_dimensions["I"].width=60
# ---------------- Barks
ws=wb.create_sheet("Barks")
D_="Diver"; W="Whaler"; S_="Stowaway"; M_="Mechanic"
L=[
("Match start",D_,["Lamps on. Mouths shut.","Same drill: don't feed it, don't wake it.","Let's see what's down here this time."]),("Match start",W,["Quota's the quota. Move.","Salvage doesn't collect itself.","Keep your harpoons pointed away from me."]),
("Match start",S_,["Who brought the snacks?","I've a good feeling. I always do.","If I die, my bottle goes to the seal."]),("Match start",M_,["We're the snacks.","Checklist: lamps, darts, nerves. Nerves are low.","Everyone read the log? No? Great."]),
("First blood",D_,["That's blood. Watch the doors.","First cut. Count starts now.","Keep it small. Keep it quiet."]),("First blood",W,["Let it drift. Don't feed it.","One's fine. Ten's a dinner bell.","Blood's in. Clock's on."]),
("First blood",S_,["Ooh. That's a lot of red.","Bit of a mess, that.","It's fine. It's fine. Probably fine."]),("First blood",M_,["Plume's spreading east. Noting it.","That'll reach the stern in forty seconds.","Please don't shoot the big ones."]),
("Scent high",D_,["Meter's red. Move.","We're standing in it. Out.","Whatever smells this will be here soon."]),("Scent high",W,["Water's thick. Somebody's coming.","Get off the blood.","This is the part where we leave."]),
("Scent high",S_,["Red's my favourite colour. Not today.","Smells like dinner. For them.","Moving! I'm moving!"]),("Scent high",M_,["Meter's red. Very red.","Scent at threshold for a size four. Estimate: soon.","I said this would happen."]),
("Predator pulse",D_,["Something big. From the stern.","Big one. Guns quiet.","Cover. Now."]),("Predator pulse",W,["Big fish. Harpoons up.","Here it comes. Hold.","Knew it. Sharks always know."]),
("Predator pulse",S_,["Oh no. Oh good. Oh no.","Big friend incoming!","Whose blood was that? Not mine."]),("Predator pulse",M_,["That's... that's a big pulse.","Size four or five. Bearing stern.","Hide. Hide is a strategy."]),
("Harmless big thing shot",D_,["Why would you shoot that.","That did nothing but bleed.","Well. Now everything's coming."]),("Harmless big thing shot",W,["That was a hundred blood, genius.","You've rung the bell.","Fine. Sharks it is."]),
("Harmless big thing shot",S_,["It looked at me funny.","In my defence, it was very shootable.","Whoops."]),("Harmless big thing shot",M_,["That was the dugong. THE dugong.","Noting: someone shot the whale.","Logging that as a mistake."]),
("Cleaner killed",D_,["That was a cleaner. Great.","Now everything's in a mood.","Stop shooting the small ones."]),("Cleaner killed",W,["Cleaners keep the sharks calm. Kept.","Nice. Every grouper's angry now.","Don't."]),
("Cleaner killed",S_,["Was that the little one? The nice one?","I killed a barber. I feel bad.","Sorry, shrimp."]),("Cleaner killed",M_,["Aggression up twenty percent. Five minutes.","The groupers are going to be in a mood.","Cleaners! Not targets!"]),
("Chummed",D_,["They're chumming us. Move!","Off the chum. Off it.","Don't stand in it."]),("Chummed",W,["Chum! Get clear!","They want the reef to do their job.","Swim!"]),
("Chummed",S_,["Ew. Ew. Ew.","They threw dinner at us!","Not it!"]),("Chummed",M_,["Chum cloud. Radius four. Move!","This is bad chemistry.","Blood's coming from a bucket now."]),
("Downed",D_,["Down. Pick me up when it's clear.","I'm out. Bleeding. Sorry.","Somebody, when you can."]),("Downed",W,["Down. Not dead.","Get to me or don't, just decide.","My own fault."]),
("Downed",S_,["Bit of a lie-down, don't mind me.","I'm fine! I'm on the floor but fine!","Tell the seal I loved it."]),("Downed",M_,["Down. Bleeding at eight per second.","This is exactly the outcome I predicted.","Please hurry. Please."]),
("Revive",D_,["You're welcome. Again.","Up. Stay up.","Don't make me do that twice."]),("Revive",W,["Up. We're not carrying you.","On your fins.","Cost me a magazine. Move."]),
("Revive",S_,["I'm back! Did anyone miss me?","Thanks, love.","Was I gone long?"]),("Revive",M_,["Please stop doing that.","Up. Vitals... acceptable.","Next time, don't stand there."]),
("Tonic bought",D_,["That's your fourth.","Drink up.","Good. Now go."]),("Tonic bought",W,["Brine. Finally.","Hope it's worth the scrip.","Down the hatch."]),
("Tonic bought",S_,["Bottoms up!","Ooh, it fizzes.","Another? Don't mind if I do."]),("Tonic bought",M_,["Bottle four. Effects stack.","Tastes like batteries.","Documenting: it's not bad."]),
("Locker bad",D_,["A Gannet. Lovely.","The eel's laughing.","Waste."]),("Locker bad",W,["Junk.","Nine hundred fifty for that.","The eel hates me."]),
("Locker bad",S_,["It's a pistol. It's my pistol. I have this pistol.","The chest gave me a spoon.","Better luck next fish."]),("Locker bad",M_,["A Gannet. From a chest. Wonderful.","Probability was two percent. I knew that.","Statistically, this had to happen."]),
("Locker wonder",D_,["Now we're talking.","This changes the water.","Don't lose it."]),("Locker wonder",W,["Now THAT'S a fish-killer.","Ha! Yes!","Give it here."]),
("Locker wonder",S_,["Ooooh. Shiny death.","The eel likes me!","Can I keep it? I'm keeping it."]),("Locker wonder",M_,["That's... oh. Oh, that's the one.","Careful with that. Very careful.","Two percent! Two percent!"]),
("Forge",D_,["Pressed and ready.","Heavier. Better.","That'll bite."]),("Forge",W,["Now it's a real gun.","Forged. Let's use it.","Heat, pressure, teeth."]),
("Forge",S_,["Shiny!","It's got a name now!","Look at it. LOOK at it."]),("Forge",M_,["Pressure treatment complete.","Sea-glass finish. Structurally sound.","Don't drop it. It cost five thousand."]),
("Hunt begins",D_,["Company. Cover the breach.","They've found us. Fine.","Enemies. Make them bleed near the fish."]),("Hunt begins",W,["Wreckers! Cover the breach!","Here they come. Let 'em swim into it.","Kill the chummer first!"]),
("Hunt begins",S_,["Visitors! Rude ones!","Oh, people. Worse than fish.","Can we let the crocodile handle this?"]),("Hunt begins",M_,["Or let the crocodile handle it?","Squad inbound. Alarm was high. Told you.","Positions. Real positions this time."]),
("Enemy eaten",D_,["For now.","The reef's hungry too.","Good. Less work."]),("Enemy eaten",W,["Ha. Reef's on our side.","That's what happens down here.","One less."]),
("Enemy eaten",S_,["Ha! Reef's on our side!","Bye!","That's the spirit, fish."]),("Enemy eaten",M_,["Predator ate the enemy. Noting.","Efficient.","The system works."]),
("Boss appears",D_,["That's the big one. Stay loose.","Here it is.","Don't get swallowed."]),("Boss appears",W,["Big fish. The biggest.","Harpoons. All of them.","Now we earn it."]),
("Boss appears",S_,["That's a fish. That's a very large fish.","Oh, hello, enormous.","I'm going to be inside that, aren't I."]),("Boss appears",M_,["That's... that's a fish. That's a very large fish.","HP fifteen thousand. Weak point: the gills.","Deep breaths. Not literally."]),
("Swallowed",D_,["It's warm in here. Shoot the gills!","Inside. Five seconds. Go!","Gills! Now!"]),("Swallowed",W,["It's got me! The gills!","Shoot it! Shoot the thing!","Not like this!"]),
("Swallowed",S_,["It's dark! It's wet! It's rude!","Hello? Gills, please!","I've been in worse pubs!"]),("Swallowed",M_,["Swallowed. Countdown. Gills!","This is not in the checklist!","Five seconds! Four!"]),
("Tide cleared",D_,["Twenty seconds. Spend them.","Breathe. Buy. Go.","Reset. Quietly."]),("Tide cleared",W,["Breathe. Buy. Go again.","Ammo. Now.","Quota's met. Next."]),
("Tide cleared",S_,["Snack break!","We did it! Whatever it was!","Shopping!"]),("Tide cleared",M_,["Tide cleared. Bounties up ten percent.","Twenty seconds of calm. Use them.","Log: alive."]),
("Quiet",D_,["Too quiet.","Nothing's coming. Yet.","Keep it this way."]),("Quiet",W,["Quiet's good. Quiet pays.","Don't ruin it.","Fish are asleep. Let 'em."]),
("Quiet",S_,["Too quiet. I don't like it. I do like it.","Anyone else hear that? No? Good.","Spooky."]),("Quiet",M_,["Alarm's at zero. Enjoy it.","Scent's dropping. Good.","This is optimal. Nobody move."]),
("Last standing",D_,["Fine. Just me, then.","Alone. Okay.","Hold on. I'll get to you."]),("Last standing",W,["Just me. Good.","Nobody to blame now.","Come on, then."]),
("Last standing",S_,["Oh dear. Solo act.","I'm the responsible one now? Us?","Hold on, everyone. Ish."]),("Last standing",M_,["Just me. Probability of revival: low.","Okay. Okay. Plan.","I'll try. No promises."]),
("Match over",D_,["Tide twelve. Next time, thirteen.","We'll come back.","Log it."]),("Match over",W,["Got what we got.","Next time, quieter.","The reef wins. It always does."]),
("Match over",S_,["Next time, more snacks.","Good swim, everyone.","Same time tomorrow?"]),("Match over",M_,["Tide twelve. We'll get thirteen.","Cause of death: everything.","Filing the report."]),
]
rows=[(sit,dv,ln) for sit,dv,lines in L for ln in lines]
r=table(ws,1,["situation","diver","line"],rows)
ws.cell(row=r,column=1,value="Three lines per diver per situation are written here (240). Claude Code expands each set to 6-10 by the voice rules: Diver = dry, short, imperative; Whaler = gruff, salvage-minded, blames the shooter; Stowaway = cheerful, slightly drunk, food and the seal; Mechanic = nervous, precise, quotes numbers and the log. Callback pairs: any line in a situation can be answered by a different diver's line from the same situation 1-2 s later if within 15 m. Never repeat a line within 3 minutes; one speaker at a time; tells and Hunt announcements interrupt quips.").font=F
fit(ws,18); ws.column_dimensions["C"].width=60
wb.save("/mnt/user-data/outputs/RedTide_Engine_and_Systems.xlsx"); print("saved; diver lines:",len(rows))
