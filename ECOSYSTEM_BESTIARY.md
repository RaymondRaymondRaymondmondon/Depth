# Depth: the ecosystem/parkour expansion - full bestiary and mechanics brief

This is the source-of-truth brief for the parkour ecosystem expansion tracked in ECOSYSTEM_ROADMAP.md,
recorded here **verbatim** from the user (developed with Gemini) so it never has to be re-pasted or
reconstructed from memory again. ECOSYSTEM_ROADMAP.md tracks decisions, status and what's actually built;
this file is the unabridged specification it's built against.

Scope, from the user directly: "this will involve developing the five new levels (the four that come from
the main game are meant to be additions to the parkour section and not affecting the main game at all)" -
i.e. new parkour-only versions of the four Shallows locations (Island, Cave, Weeds, Atlantis) plus The Open
Abyss, alongside retrofitting the three existing platform levels (Pipes, Hull, Pirate Ship). The turn-based
expedition versions of Island/Cave/Weeds/Atlantis are never touched.

---

## 1. Aquatic Traversal Mechanics

In water-based biomes (The Hull, The Weeds, Atlantis, and sections of The Cave/Island), standard ground
movement switches to 3D fluid physics. The player receives two core momentum tools designed to navigate
aquatic hazards and interact with systemic forces:

```cpp
struct WaterTraversalState {
    bool isDashing = false;
    bool isGliding = false;
    float dashCooldownTimer = 0.0f;
    float currentStamina = 100.0f;
    sf::Vector2f velocityVector;
};
```

### Water Dash
- **Impulse Mechanics**: Triggered by pressing the dodge/dash key alongside a directional input. The
  movement controller applies a sudden vector force (`DashImpulse = Direction * BaseForce`) that overrides
  ambient fluid drag for a short burst (0.3 seconds).
- **Ecosystem Displacement**: The dash creates a radial force wave in the physics world. Small entities
  (e.g., Phytoplankton, Cleaner Shrimp, Cave Crickets) within a 2-meter radius are pushed outward by a force
  vector proportional to the dash distance. High-aggression predators in proximity register this sudden
  displacement wave as an acoustic trigger, instantly shifting their attention state to "Investigate."
- **Resource Cost**: Consumes 20% of the stamina/oxygen pool per burst, preventing infinite spamming while
  providing critical escape bursts from aggressive predators.

### Hydro-Glide
- **Streamline Trajectory**: Holding the glide modifier aligns the player character's body collision mesh
  with their velocity vector, reducing the fluid drag coefficient by 65%.
- **Current Alignment**: Hydro-gliding allows the player to lock into ambient fluid vectors, such as
  underwater thermal vents, Manta Ray slipstreams, or tidal drainage pipes. While gliding inside a
  slipstream, momentum is maintained without stamina consumption, granting high-speed traversal across
  large open gaps.
- **Stealth & Aggro Reduction**: Moving via Hydro-Glide produces near-zero acoustic noise in the fluid
  simulation, allowing the player to glide past high-aggression/low-curiosity predators without triggering
  their visual/vibrational perception fields.

## 2. Multi-Species Ecosystem Interaction Arrays

Every entity in each of the seven biomes continuously runs perception checks against a dynamic Entity
Relationship Matrix. Interactions are not limited to "Attack Player" - creatures prioritize feeding,
defending territory, seeking shelter, and responding to environmental triggers.

```
[Entity A Perception] ---> (Relationship Lookup) ---> [Applies Personality Modifiers] ---> (Action Execution)
                                                            |
                                                            v
                                            [Modifies Entity B State]
```

### The Pipes (Micro-Ecosystem)
Entities ignore the player; all hazards stem from systemic chaos and collateral physics.

```
                       +--> Rust-Mites (Consumes Metal Flakes)
                       |
Pipe Leak --> Rusting Wall --> Blind Pipe-Rats (Bite Wall)
                                   |
                                   v
Scavenger Mice <-- (Stampede) <-- Cave Crickets <-- Glow-Beetles (Flash Pulse)
```

1. Dust Moths fly toward bioluminescent pipe leaks and lights.
2. Water-Spiders build sticky silk webs across pipe bends, catching Dust Moths.
3. Scurrying Centipedes move along webs to consume trapped Moths, disturbing the webs.
4. Blind Pipe-Rats locate Centipedes via sound vibrations; high-aggression Pipe-Rats bite into rusted pipes
   to reach them.
5. Rust-Mites swarm out of broken pipe sections to eat loose metal flakes.
6. Pillbugs/Woodlice curl into hard rolling spheres when sprayed by steam leaks or touched by Rust-Mites,
   rolling down sloped pipes as heavy physics projectiles.
7. Scavenger Mice hunt loose Pillbugs, but flee when hit by rolling shells.
8. Territorial Cockroaches fight Scavenger Mice over fallen scraps, forming dense squabbling piles that
   block narrow pipes.
9. Glow-Beetles emit a bright flash pulse when stepped on by fighting Cockroaches.
10. Cave Crickets panic from the bright light pulses, jumping wildly into other entities and triggering a
    chain-reaction stampede through the pipe network.

**Status: not yet built.** The Pipes currently have a single simpler ambient-life pass (`PlatCritter` in
platformer.cpp) predating this list - harmless vermin with Flee/Investigate/Idle states reacting to the
diver, not yet this full ten-species, player-ignoring, systemic-chaos chain. Revisit to bring it in line.

### The Hull (Barnacle-Encrusted Exterior)
1. Cleaner Shrimp establish cleaning stations on barnacle clusters, attracting larger marine life.
2. Moray Eels hide in hull breaches, waiting for Cleaner Shrimp to groom their skin.
3. Camouflage Octopuses blend into hull textures to ambush Cleaner Shrimp.
4. Barnacle Crabs pinch Octopuses that land too close to their barnacle beds.
5. Octopuses spray an opaque ink cloud when pinched by Crabs or bumped by the player.
6. Pufferfish enter the ink cloud, panic from blind contact, and puff up into spiky hazards.
7. Hull-Leeches detach from the ship hull when bumped by inflated Pufferfish and float downstream.
8. Stinging Anemones capture floating Hull-Leeches, expanding their tentacles when fed.
9. Hermit Crabs crawl along the hull, using discarded pipe debris or barnacle shells; they feed on anemone
   scraps.
10. Brittle-Stars form dense carpet mats over slippery hull sections. Walking or dashing over Brittle-Stars
    destroys the mat, dropping the player or walking crabs onto underlying hazards.

**Status: in progress.** The Hull's existing crab ('c') and eel ('e') map onto **Barnacle Crab** and
**Moray Eel** here - both already retrofitted with PersonalityProfile (see ECOSYSTEM_ROADMAP.md). The
other 7 species (Cleaner Shrimp, Camouflage Octopus, Pufferfish, Hull-Leech, Stinging Anemone, Hermit Crab,
Brittle-Star) and their real inter-species chain are what's being built now.

### The Pirate Ship (Deck & Rigging)
1. Ship Rats scurry across deck planks toward food barrels or dropped items.
2. Stray Cats stalk Ship Rats along the lower cannons and floorboards.
3. Parakeets perched in the lower rigging screech when Cats, Rats, or Players rush past them.
4. Short-Range Pirates respond to screeching Parakeets by swinging cutlasses at nearby motion.
5. Long-Range Pirates fire flintlock pistols toward commotion created by Short-Range Pirates.
6. Gunpowder Monkeys pick up dropped flintlock embers or gunpowder sacks; if scared, they drop powder lines
   across the deck.
7. Flea Swarms inhabit stray dog coats; gunpowder explosions scatter Flea Swarms into nearby entities,
   causing uncontrolled itching animations.
8. Guard Dogs infected by Flea Swarms go berserk, attacking Pirates, Cats, and Players indiscriminately.
9. Barn Owls nest in the crow's nest, diving at high speed to snatch fleeing Rats or Monkeys off the
   rigging.
10. Albatrosses circle the top sails, stealing dropped items from Owls or Monkeys and dropping heavy bones
    onto lower platforms.

**Status: not yet built.** The Pirate Ship's existing 'P' (ambusher), 'G' (gunner) and 'p' (parakeet) map
loosely onto Short-Range Pirates, Long-Range Pirates and Parakeets, but none of the rest of this chain
exists yet, and the existing three don't yet read personality. Also note: the same dead-code bug that
blocked the Hull's eels (see ECOSYSTEM_ROADMAP.md) very likely blocks parakeets here too - check first.

### The Island (Vertical Tribal Terrain)
1. Tribal Warriors patrol wooden platforms and fire blowdarts/spears at large beasts.
2. Feral Boars charge anything in their line of sight when struck by stray blowdarts.
3. Canopy Snakes drop down from tree branches onto charging Boars or Warriors.
4. Monitor Lizards scavenge fallen prey on the jungle floor, defending carcasses fiercely.
5. Fruit Bats roost in high palm fronds; structural vibrations from charging Boars send them swarming into
   the air.
6. Web-Spinning Spiders build giant vertical webs between canopy trees that catch flying Fruit Bats.
7. Coconut Crabs climb trees to cut down coconuts or spider egg sacks, dropping them onto entities below.
8. Poison Dart Frogs leap across mud paths; stepped-on Frogs apply a poison overlay to the footwear/feet of
   whatever stepped on them, causing damage over time to that entity.
9. Territorial Seagulls dive-bomb Coconut Crabs to steal cracked coconut meat.
10. Hunting Dogs follow the scent trails left behind by poisoned or bleeding entities, leading their pack to
    injured targets.

**Status: not yet built.** A new parkour-only platform biome (the turn-based Island expedition location is
untouched) - doesn't exist yet at all.

### The Cave (Claustrophobic Tunnels)
1. Stalactite Spiders hang from high ceilings, waiting to drop on passing prey.
2. Cave Bats roost in ceiling clusters; Spiders dropping through their roosts trigger loud ultrasonic
   shrieks.
3. Stalactites loosen from ultrasonic shrieks and fall, smashing onto the tunnel floor.
4. Rock-Boring Urchins feed on mineral-rich fallen stalactites.
5. Giant Tube Worms line the tunnel walls, acting as platform footholds; heavy stalactite impacts cause them
   to instantly retract into the walls.
6. Mutated Crustaceans emerge from deep wall cracks to feed on broken Urchin remains.
7. Bioluminescent Jellies float in flooded cave pockets, flashing when physical forces (like water dashes)
   pass through them.
8. Pale Salamanders hunt Jellies, using the light flashes to locate prey in pitch darkness.
9. Fungal Beetles feed on decaying organic matter; if killed by Salamanders or Crustaceans, they burst into
   a spore cloud.
10. Cave Leeches drop from the ceiling when spore clouds or swimming entities touch their sensory tendrils.

**Status: not yet built.** A new parkour-only platform biome (the turn-based Cave expedition location, and
the existing dungeon Cave, are both untouched) - doesn't exist yet at all.

### The Weeds (Dense Vertical Kelp Forests)
1. Swarming Phytoplankton form dense floating clouds that obscure visibility.
2. Kelp-Seahorses anchor themselves to kelp stalks and filter-feed on Phytoplankton clouds.
3. Barracudas patrol in small packs, hunting Seahorses inside the kelp canopy.
4. Manatees swim slowly through the biome, grazing on kelp stalks; their massive bodies act as moving
   terrain platforms.
5. Tiger Sharks hunt Barracuda packs, using open water lanes between kelp stalks to build momentum.
6. Electric Rays rest on the muddy floor; if stepped on or bumped by a Manatee, they release a localized
   shockwave.
7. Mermen use coral spears to hunt Tiger Sharks stunned by Electric Ray shockwaves.
8. Fungal Parasites drift through the water; they attach to wounded Mermen or Sharks, turning them
   host-aggressive and overriding their normal flee states.
9. Snapping Turtles attack Fungal Parasites floating in the water column.
10. Sea Kraits (Snakes) weave through kelp stalks to target Snapping Turtle eggs and young turtles.

**Status: not yet built.** A new parkour-only platform biome (the turn-based Weeds expedition location is
untouched) - doesn't exist yet at all.

### Atlantis (Crumbling Roman-Style Ruins)

```
Clockwork Drones (Patrol) --> Fires Lasers --> Abyssal Sharks (Thrash)
                                                    |
                                                    v
Living Marble Gargoyles <-- Manta Slipstream <-- Arch Collapse (Stone-Crabs)
         |
         v
"Lost Ones" (Panic) --> Lionfish Traps --> Nautiluses (Shield Barrage)
```

1. Clockwork Defense Drones follow fixed sub-aquatic patrol routes, firing energy pulses at biological
   entities.
2. Abyssal Sharks attack passing Drones, crushing them in their jaws and triggering electrical discharges.
3. Stone-Crabs disguised as stone carvings on ancient arches detach when hit by electrical discharges,
   causing archways to collapse.
4. Deep-Sea Anglerfish use their bioluminescent lures to illuminate newly opened collapse paths.
5. Ghostly Manta Rays flee illuminated zones, swimming rapidly into dark corridors and leaving high-speed
   Hydro-Glide slipstreams in their wake.
6. Living Marble Gargoyles remain dormant until a Manta slipstream or dynamic water current passes over
   their pedestals, awakening them to guard nearby passageways.
7. The "Lost Ones" (waterlogged citizens) flee Gargoyles, running/swimming through narrow marble halls.
8. Venomous Lionfish expand their spines in narrow doorways to trap fleeing Lost Ones.
9. Armored Nautiluses drift through halls, using their heavy shells to block Lionfish spines and project
   localized force shields.
10. Ruins-Dwelling Octopuses steal mechanical parts from destroyed Drones, using them as makeshift armored
    doors or weapons against Nautiluses.

**Status: not yet built.** A new parkour-only platform biome (the turn-based Atlantis expedition location
is untouched) - doesn't exist yet at all.

## 3. Personality System

To guarantee that individual creatures act distinctly during every run, each spawned entity generates a
personality struct derived from the level's procedural seed:

```cpp
struct PersonalityProfile {
    float aggression; // [0.0 = Pacifist/Flee, 1.0 = Relentless Hunter]
    float bravery;    // [0.0 = Flees larger entities, 1.0 = Attacks larger predators]
    float energy;     // [0.0 = Lethargic/Slow force, 1.0 = Hyperactive/Fast impulse]
    float curiosity;  // [0.0 = Ignores environment, 1.0 = Investigates sounds/disturbances]
};
```

### Personality Variable Interactions

```cpp
void EntityBrain::EvaluateBehavior(Entity& self, Environment& env) {
    Entity* target = env.GetNearestEntity(self.position);
    float targetDistance = GetDistance(self.position, target->position);

    // Dynamic Detection Radius modified by Curiosity and Target Movement
    float effectivePerceptionRadius = self.basePerceptionRadius * (1.0f + self.personality.curiosity);

    if (targetDistance <= effectivePerceptionRadius) {
        // Evaluate Threat level based on Target Size and Self Bravery
        bool isTargetThreat = (target->sizeCategory > self.sizeCategory) &&
                               (self.personality.bravery < 0.5f);

        if (isTargetThreat) {
            self.SetState(State::Flee);
            self.ApplyMovementForce(self.fleeVector * self.personality.energy);
        }
        else if (self.personality.aggression > 0.4f) {
            self.SetState(State::Hunt);
            self.ApplyMovementForce(self.huntVector * (1.0f + self.personality.energy * 0.5f));
        }
        else if (self.personality.curiosity > 0.6f) {
            self.SetState(State::Investigate);
            self.MoveTo(target->position);
        }
    }
}
```

### Behavioral Variations in Action
- **Aggressive vs. Cowardly Sharks**: A Tiger Shark generated with `Aggression: 0.9` and `Bravery: 0.8`
  actively hunts the player and attacks Manatees. A shark in the same level generated with
  `Aggression: 0.2` and `Bravery: 0.1` avoids the player, feeding strictly on small Seahorses or fleeing if
  a Merman enters its territory.
- **Curious vs. Lethargic Monkeys**: A Gunpowder Monkey with `Curiosity: 0.95` will run toward dropped
  flintlock embers, grab them, and run around the deck. A monkey with `Curiosity: 0.05` will sit quietly on
  a yardarm, completely ignoring falling items or nearby rat fights unless directly hit.

---

## Notes on adapting this to Depth's actual engine

This brief was written in general/engine-agnostic pseudocode (`sf::Vector2f`, a generic `Entity`/
`EntityBrain`/`Environment` framework). Depth has no such generic entity-component system - each biome's
creatures are plain structs with hand-written per-kind behaviour in that biome's own file (`AbyssCreature`
in abyss.cpp, `PlatEnemy`/`PlatCritter` in platformer.cpp). Rather than building a generic ECS matrix
(a much larger, riskier undertaking than the game's existing architecture), each biome gets its own
struct(s) and its own hand-written interaction logic implementing the *specific* chain above for that
biome, sharing only `PersonalityProfile` and the Flee/Hunt/Investigate/Feed/Defend vocabulary across all
of them - the same pattern already used for the Abyss and the Pipes. Simplifications made for a given
biome's engine constraints (e.g. the 2D platformer having nothing analogous to a "dash" the way the Abyss
does) are called out in that biome's own section of ECOSYSTEM_ROADMAP.md as they're built, not here.
