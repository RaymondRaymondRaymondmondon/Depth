#pragma once
// Fowl Play's art, drawn in code through Red Tide's inked renderer (rt::): the marsh at golden hour, the porch and the
// clubhouse room, the birds, the dog, Mr. Zappa and the raccoon, and the toy guns (every gun in the same two-tone grey
// moulded plastic with a red trigger and the red Zappa stamp, keeping its real silhouette).
#include "fowl.h"

namespace fpart {
void DrawMarsh(float t, bool night, float flare);
void DrawPorch(const fp::World& w, int me, float t);
void DrawRoom(const fp::World& w, float t);
void DrawBird(const fp::Bird& b, float t, bool night);
void DrawDog(const fp::World& w, float t, float tallyX = 0);
void DrawProjs(const fp::World& w, float t);
void DrawPlayerFigure(const fp::World& w, const fp::Player& p, float t);
// a toy gun: `frame` places the gun's grip (x right, y up, z along the barrel); paint 0 grey, else a Slop paint index
void DrawToyGun(int gunDef, Matrix frame, int paint, float spin = 0, float scale = 1);
Color PaintBody(int paint);
}
