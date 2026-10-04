# Writes Mouthful's sea (Red Tide's format: data/mouthful/sea/reef/) and its art rows (data/mouthful/art/) from the
# tables below (design: Reference_For_Future_MP_Games/Mouthful — Arcade Game 8 Design Document.pdf, pp. 3-9).
#   powershell -File tools\mouthful_data.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sea = Join-Path $root 'data\mouthful\sea\reef'; $art = Join-Path $root 'data\mouthful\art'
New-Item -ItemType Directory -Force $sea, $art | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false
function Save($path, $obj) { [IO.File]::WriteAllText($path, ($obj | ConvertTo-Json -Depth 12), $utf8) }

# the web's species: name, class, size (1-6), tier, social, group, home band, speed m/s, hp, sight, aggression, fear, blood threshold, tags, body plan, length m, girth, head, tail, base, belly, accent
$S = @(
 @('Plankton','Fish',1,1,'school',60,'The Shallows',0.6,2,4,0,0.2,0,'schooler','fusiform',0.04,0.3,0.3,'forked','sea glass','marble white','sea glass'),
 @('Minnow','Fish',1,1,'school',20,'The Shallows',2.2,6,12,0,0.9,0,'schooler','fusiform',0.08,0.26,0.3,'forked','sand cream','marble white','gold leaf'),
 @('Shrimp','Crustacean',1,1,'solitary',1,'The Shallows',1.4,6,8,0,0.9,0,'','shrimp',0.06,0.25,0.3,'fan','coral pink','sand cream','blood red'),
 @('Snail','Invertebrate',1,1,'solitary',1,'The Shallows',0.3,8,4,0,0.5,0,'armored','echinoderm',0.05,0.6,0.5,'none','staghorn tan','sand cream','terracotta'),
 @('Sardine','Fish',1,1,'school',40,'The Reef',2.4,10,14,0,0.9,0,'schooler','fusiform',0.2,0.28,0.3,'forked','slate blue','marble white','gold leaf'),
 @('Crab','Crustacean',2,2,'solitary',1,'The Reef',1.0,20,10,0.1,0.6,0,'armored','crab',0.25,0.6,0.4,'none','terracotta','sand cream','blood red'),
 @('Urchin','Invertebrate',2,2,'solitary',1,'The Reef',0.1,20,4,0,0,0,'armored spined','echinoderm',0.2,0.9,0.5,'none','deep violet','deep violet','bone'),
 @('Clam','Invertebrate',2,2,'solitary',1,'The Reef',0,20,2,0,0,0,'armored','echinoderm',0.25,0.7,0.5,'none','bone','marble white','pearl'),
 @('Mackerel','Fish',2,2,'school',12,'The Blue',3.4,20,18,0.2,0.7,0,'schooler','fusiform',0.4,0.24,0.28,'forked','sea glass','marble white','slate blue'),
 @('Snapper','Fish',2,2,'solitary',1,'The Reef',2.6,25,16,0.2,0.6,0,'','compressiform',0.5,0.4,0.32,'forked','coral pink','sand cream','blood red'),
 @('Squid','Cephalopod',2,2,'school',6,'The Reef',3.0,20,18,0.2,0.7,0,'inker','cephalopod',0.4,0.3,0.4,'fins','pearl','marble white','coral pink'),
 @('Tuna','Fish',4,4,'school',8,'The Blue',6.0,80,26,0.4,0.4,0,'fighter','fusiform',1.6,0.3,0.26,'lunate','midnight blue','marble white','gold leaf'),
 @('Barracuda','Fish',4,4,'solitary',1,'The Blue',5.0,70,24,0.6,0.3,0,'fighter','fusiform',1.4,0.16,0.3,'forked','slate grey','marble white','bone'),
 @('Rattail','Fish',2,3,'solitary',1,'The Trench',1.2,20,10,0.1,0.6,0,'','anguilliform',0.6,0.14,0.3,'tapering','slate grey','bone','bone'),
 @('Anglerfish','Fish',2,3,'solitary',1,'The Trench',0.8,25,12,0.5,0.2,0,'lure','fusiform',0.5,0.6,0.55,'rounded','charcoal','charcoal','sea glass'),
 @('Hatchetfish','Fish',1,2,'school',20,'The Trench',1.6,10,10,0,0.8,0,'schooler luminous','compressiform',0.08,0.6,0.4,'forked','pearl','pearl','sea glass'),
 @('Giant Isopod','Crustacean',3,3,'solitary',1,'The Trench',0.6,40,8,0.1,0.3,0,'armored','insect',0.4,0.5,0.3,'fan','bone','sand cream','slate grey'),
 @('Vent Crab','Crustacean',2,3,'solitary',1,'The Trench',0.8,30,8,0.1,0.4,0,'armored','crab',0.3,0.6,0.4,'none','bone','bone','blood red'),
 @('Reef Shark','Fish',5,5,'solitary',1,'The Reef',4.4,300,30,0.8,0.1,6,'apex npc','shark',2.0,0.2,0.24,'heterocercal','slate grey','marble white','charcoal'),
 @('Great White','Fish',6,7,'solitary',1,'The Blue',5.0,900,40,0.9,0,4,'apex npc','shark',5.5,0.24,0.22,'lunate','slate grey','marble white','charcoal'),
 @('Tiger Shark','Fish',6,7,'solitary',1,'The Blue',4.6,850,36,0.9,0,4,'apex npc','shark',4.5,0.22,0.22,'heterocercal','staghorn tan','marble white','charcoal'),
 @('Hammerhead','Fish',6,7,'solitary',1,'The Blue',4.8,800,38,0.8,0,4,'apex npc','shark',4.2,0.2,0.22,'heterocercal','slate grey','marble white','charcoal'),
 @('Leviathan','Fish',6,9,'solitary',1,'The Trench',3.0,9000,30,1,0,2,'apex npc','leviathan',30,0.2,0.3,'lunate','charcoal','bone','blood red'),
 @('Orca','Mammal',6,7,'pack',3,'The Blue',6.0,1200,40,0.9,0,4,'npc','cetacean',7,0.26,0.24,'flukes','charcoal','marble white','marble white')
)
# the player's forms (art rows only: the game draws them, the web doesn't spawn them)
$F = @(
 @('Fry','Fish','fusiform',0.1,0.3,0.34,'forked','sand cream','marble white','gold leaf'),
 @('Moray','Fish','anguilliform',0.6,0.12,0.3,'tapering','olive green','sand cream','gold leaf'),
 @('Sea Snake','Fish','snake',1.4,0.1,0.25,'paddle','marble white','marble white','charcoal'),
 @('Conger','Fish','anguilliform',2.0,0.12,0.28,'tapering','slate grey','bone','charcoal'),
 @('Marine Crocodile','Reptile','crocodilian',5.0,0.2,0.3,'tapering','olive green','sand cream','bone'),
 @('Oarfish King','Fish','anguilliform',9.0,0.06,0.2,'tapering','pearl','marble white','blood red'),
 @('Reef Squid','Cephalopod','cephalopod',0.4,0.3,0.4,'fins','coral pink','pearl','gold leaf'),
 @('Octopus','Cephalopod','cephalopod',1.2,0.5,0.6,'arms','terracotta','sand cream','gold leaf'),
 @('Cuttlefish','Cephalopod','cephalopod',1.0,0.45,0.45,'fins','staghorn tan','pearl','sea glass'),
 @('Giant Pacific Octopus','Cephalopod','cephalopod',4.0,0.5,0.6,'arms','blood red','coral pink','bone'),
 @('Colossal Squid','Cephalopod','cephalopod',9.0,0.3,0.35,'fins','blood red','pearl','sea glass'),
 @('Dogfish','Fish','shark',0.6,0.18,0.24,'heterocercal','staghorn tan','sand cream','charcoal'),
 @('Bull Shark','Fish','shark',2.4,0.26,0.24,'heterocercal','slate grey','marble white','charcoal'),
 @('Mantis Shrimp','Crustacean','shrimp',0.3,0.25,0.3,'fan','sea glass','coral pink','gold leaf'),
 @('Spiny Lobster','Crustacean','shrimp',1.0,0.3,0.3,'fan','terracotta','sand cream','gold leaf'),
 @('Coconut Crab','Crustacean','crab',1.0,0.6,0.4,'none','deep violet','terracotta','bone'),
 @('King Crab','Crustacean','crab',3.0,0.6,0.4,'none','blood red','sand cream','bone'),
 @('Puffer','Fish','fusiform',0.3,0.8,0.5,'rounded','sand cream','marble white','charcoal'),
 @('Porcupinefish','Fish','fusiform',0.8,0.8,0.5,'rounded','staghorn tan','marble white','charcoal'),
 @('Boxfish','Fish','compressiform',0.8,0.8,0.5,'rounded','gold leaf','marble white','charcoal'),
 @('Titan Puffer','Fish','fusiform',3.5,0.85,0.5,'rounded','olive green','marble white','charcoal'),
 @('Stonefish','Fish','depressiform',2.0,0.6,0.5,'rounded','staghorn tan','staghorn tan','blood red'),
 @('Blobfish','Fish','fusiform',0.4,0.95,0.7,'rounded','coral pink','coral pink','coral pink'),
 @('Bigger Blobfish','Fish','fusiform',1.4,0.95,0.7,'rounded','coral pink','coral pink','coral pink'),
 @('Enormous Blobfish','Fish','fusiform',4.0,0.95,0.7,'rounded','coral pink','coral pink','coral pink'),
 @('The Blobfish King','Fish','fusiform',9.0,0.95,0.7,'rounded','coral pink','coral pink','gold leaf')
)
$species = @(); $artRows = @(); $i = 1
foreach ($s in $S) {
  $species += [ordered]@{ id = $i++; name = $s[0]; class = $s[1]; size = $s[2]; tier = $s[3]; tags = $s[13]; archetype = $(if ($s[13] -match 'apex') { 'Predator' } elseif ($s[4] -eq 'school') { 'Schooler' } else { 'Grazer' }); social = $s[4]; group_size = $s[5]; home_zone = $s[6]; home_flora = '';
    defend_radius_m = 0; blood_threshold = $s[12]; aggression = $s[10]; fear = $s[11]; curiosity = 0.2; sight_m = $s[9]; scent_m = [Math]::Max(6, $s[9]); hearing_m = $s[9]; electro_m = $(if ($s[0] -eq 'Hammerhead') { 15 } else { 0 });
    weak_point = 'none'; armor_front = $(if ($s[13] -match 'armored') { 0.5 } else { 0 }); hp_base = $s[8]; bounty_base = 0; blood_death = [Math]::Max(4, $s[8] / 5); blood_per_s_wounded = 2; speed_mps = $s[7]; turn_deg_s = $(if ($s[2] -ge 5) { 90 } else { 300 }); drop_pct = 0; special = '' }
  $artRows += [ordered]@{ beast = $s[0]; class = $s[1]; size = $s[2]; body_plan = $s[14]; length_m = $s[15]; girth_ratio = $s[16]; head_ratio = $s[17]; tail_or_limb = $s[18]; fins_or_legs = 'dorsal x1, pectoral, pelvic, anal'; eye_size = $(if ($s[0] -match 'Angler|Hatchet|Rattail') { 'large' } else { 'medium' }); mouth = $(if ($s[13] -match 'apex') { 'large' } else { 'small' });
    armor_plates = $(if ($s[13] -match 'armored') { 'yes' } else { 'no' }); luminous = $(if ($s[13] -match 'luminous|lure') { 'yes' } else { 'no' }); base_tone = $s[19]; belly_tone = $s[20]; accent = $s[21]; material = 'scale (specular, wet)'; chains = 'none'; idle = 'hover'; distinctive_part = ''; lod_billboard_m = 60; ink_weight_px = 1.5 }
}
foreach ($f in $F) {
  $artRows += [ordered]@{ beast = $f[0]; class = $f[1]; size = 2; body_plan = $f[2]; length_m = $f[3]; girth_ratio = $f[4]; head_ratio = $f[5]; tail_or_limb = $f[6]; fins_or_legs = $(if ($f[2] -match 'crab|shrimp') { 'legs x8' } else { 'dorsal x1, pectoral, pelvic, anal' }); eye_size = $(if ($f[0] -match 'Blob') { 'small' } else { 'large' }); mouth = 'large';
    armor_plates = $(if ($f[0] -match 'Box|Crab|Lobster|Isopod') { 'yes' } else { 'no' }); luminous = 'no'; base_tone = $f[7]; belly_tone = $f[8]; accent = $f[9]; material = 'scale (specular, wet)'; chains = 'none'; idle = 'hover'; distinctive_part = ''; lod_billboard_m = 80; ink_weight_px = 1.6 }
}
Save (Join-Path $sea 'species.json') $species
Save (Join-Path $art 'species_mouthful_reef.json') $artRows
# the diet: who eats whom in the web (the player's fish are "Divers" to it)
$foods = @('Plankton','Minnow','Shrimp','Snail','Sardine','Crab','Urchin','Clam','Mackerel','Snapper','Squid','Tuna','Barracuda','Rattail','Anglerfish','Hatchetfish','Giant Isopod','Vent Crab','Divers','Corpse','Detritus')
$rows = [ordered]@{
  Minnow = @{ Plankton = 1 }; Sardine = @{ Plankton = 1 }; Hatchetfish = @{ Plankton = 1 }; Plankton = @{ Detritus = 1 }; Shrimp = @{ Detritus = 1 }; Snail = @{ Detritus = 1 }; Clam = @{ Plankton = 1 }; Urchin = @{ Detritus = 1 }
  Crab = @{ Detritus = 0.5; Corpse = 0.5 }; 'Vent Crab' = @{ Detritus = 0.5; Corpse = 0.5 }; 'Giant Isopod' = @{ Corpse = 1 }; Rattail = @{ Corpse = 0.5; Detritus = 0.5 }
  Mackerel = @{ Sardine = 0.6; Minnow = 0.4 }; Snapper = @{ Crab = 0.3; Shrimp = 0.4; Minnow = 0.3 }; Squid = @{ Shrimp = 0.5; Minnow = 0.5 }; Anglerfish = @{ Hatchetfish = 0.7; Divers = 0.3 }
  Tuna = @{ Sardine = 0.6; Mackerel = 0.4 }; Barracuda = @{ Sardine = 0.4; Mackerel = 0.4; Divers = 0.2 }
  'Reef Shark' = @{ Mackerel = 0.3; Snapper = 0.3; Divers = 0.4 }; 'Great White' = @{ Tuna = 0.4; Divers = 0.6 }; 'Tiger Shark' = @{ Tuna = 0.3; Barracuda = 0.2; Divers = 0.5 }; Hammerhead = @{ Squid = 0.3; Tuna = 0.2; Divers = 0.5 }
  Leviathan = @{ Divers = 1 }; Orca = @{ Tuna = 0.3; Divers = 0.7 }
}
Save (Join-Path $sea 'diet.json') ([ordered]@{ foods = $foods; rows = $rows })
$atk = @(); foreach ($n in @('Reef Shark','Great White','Tiger Shark','Hammerhead','Leviathan','Orca','Barracuda','Anglerfish')) { $atk += [ordered]@{ beast = $n; attack = 'Bite'; damage = 100; windup_s = 0.5; cooldown_s = 4; range_m = $(if ($n -eq 'Leviathan') { 8 } else { 2.5 }); 'tell (what the player sees)' = 'A shape turning toward you'; effect = '' } }
Save (Join-Path $sea 'attacks.json') $atk
# the rest of Red Tide's files: copied from the Flight's tropical sea (the zones and spawns are built in code)
foreach ($f in @('blockout.json','boss.json','flora.json','tidecurve.json','spawn.json')) { Copy-Item (Join-Path $root "data\flight\sea\tropical\$f") (Join-Path $sea $f) -Force }
Save (Join-Path $sea 'extra.json') ([ordered]@{ field_max_cells = 400000 })
[IO.File]::WriteAllText((Join-Path $sea 'readme.json'), '["Mouthful: The Reef (Red Tide''s format, written by tools/mouthful_data.ps1)"]', $utf8)
"wrote $($species.Count) species, $($artRows.Count) art rows"
