Painted art goes here. Everything is optional: where a file is missing the game keeps its built-in procedural drawing.
Paths in the text files below are relative to this folder. See src/sprite_renderer.h and src/skeletal_node.h for the details.

characters/<class>/skeleton.txt   (class = nurse, diver, captain, mechanic, whaler, stowaway, merman, queen, robot, ...)
    bone <name> <parent|-> <x> <y> <rotation> <scaleX> <scaleY> <length>      bones are listed parents first
    slot <name> <bone>                                                           slots draw in order, back to front
    attach <slot> <name> <path.png> <pivotX> <pivotY> <scale>                    pivot 0..1 inside the image: the joint
    anim <name> <duration>   then   key <bone> <rot|x|y|sx|sy> <time> <value>    animations "idle" and "walk" are used
enemies/<name>/layers.txt   (name lower-case, spaces as underscores: ghost_worm, crustacean_queen, cthulhu)
    layer <path.png> <offsetX> <offsetY> <scale> <pivotX> <pivotY> <alpha|add|mul> <wobble> <wobbleRate>
    hull <x> <y> <w> <h>       optional collision box (art pixels from the feet); otherwise derived from the layers
backgrounds/<region>/layers.txt   (region = cave, island, weeds, atlantis)
    layer <path.png> <factorX> <factorY> <scale> <yAnchor> <repeat 0|1> <distant|mid|fore>
    factor: 0 fixed to the screen, 1 moves with the world, above 1 sweeps past faster (foreground)
Images are loaded with mipmaps and premultiplied alpha, up to 8192 x 8192.
