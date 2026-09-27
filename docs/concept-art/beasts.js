// Creatures, all facing right with their weight on the floor at y≈368.
function tentacle(D, pts, w0, w1, col, cups) {
  const sp = bez(...pts); D.b(strip(sp, w0, w1), col);
  if (cups) for (let i = 3; i < sp.length - 3; i += 2) {
    const f = spineFrame(sp, i), t = i / sp.length, w = w0 + (w1 - w0) * t;
    D.b(C(f.x - f.nx * w * 0.6, f.y - f.ny * w * 0.6, Math.max(1.2, w * 0.32)), cups, { k: 1, lw: 1.1, hatch: false });
  }
  return sp;
}
function leg(D, pts, w, col) { for (let i = 0; i < pts.length - 1; i++) D.b(L(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], w * (1 - i * 0.25), w * (1 - (i + 1) * 0.25)), col, { k: 3 }); }
function claw(D, x, y, s, ang, col, open = 0.35) {
  D.tool(x, y, ang, D => {
    D.b(P([[0, -14 * s], [46 * s, -22 * s], [76 * s, -8 * s], [44 * s, 4 * s], [0, 12 * s]], true), col, { hi: 0.35 });
    D.tool(40 * s, 0, open, D => D.b(P([[0, -4 * s], [34 * s, 4 * s], [48 * s, 18 * s], [20 * s, 14 * s], [0, 8 * s]], true), col));
    D.tool(40 * s, -6 * s, -open * 0.4, D => D.b(P([[0, -10 * s], [44 * s, -18 * s], [70 * s, -6 * s], [36 * s, 2 * s]], true), col, { hi: 0.3 }));
  });
}
function crab(D, o) {
  const col = o.col, s = o.s || 1, cx = 165, cy = o.cy || 290;
  D.shadow(cx, 366, 120 * s, 14);
  for (let i = 0; i < 4; i++) {
    const bx = cx - 30 * s + i * 20 * s;
    leg(D, [[bx, cy + 10 * s], [bx - 50 * s - i * 8 * s, cy - 20 * s], [bx - 70 * s - i * 10 * s, 366]], 9 * s, sh(col, 0.8));
    leg(D, [[bx + 20 * s, cy + 10 * s], [bx + 70 * s + i * 4 * s, cy - 26 * s], [bx + 90 * s + i * 6 * s, 366]], 9 * s, col);
  }
  o.under && o.under(D, cx, cy, s);
  const shell = P([[cx - 80 * s, cy + 16 * s], [cx - 76 * s, cy - 24 * s], [cx - 30 * s, cy - 50 * s], [cx + 36 * s, cy - 52 * s], [cx + 84 * s, cy - 22 * s], [cx + 86 * s, cy + 14 * s], [cx + 40 * s, cy + 34 * s], [cx - 40 * s, cy + 34 * s]], true);
  D.b(shell, col, { hi: 0.3 });
  if (o.spikes) for (let i = 0; i < o.spikes; i++) { const x = cx - 70 * s + i * (150 * s / o.spikes); D.b(P([[x - 7 * s, cy - 40 * s + Math.abs(i - o.spikes / 2) * 4 * s], [x, cy - 70 * s + Math.abs(i - o.spikes / 2) * 6 * s], [x + 7 * s, cy - 40 * s + Math.abs(i - o.spikes / 2) * 4 * s]], true), sh(col, 1.1), { k: 2 }); }
  for (const [x, y, r] of o.bumps || []) D.b(C(cx + x * s, cy + y * s, r * s), mix(col, '#d8b890', 0.35), { k: 3 });
  for (const ex of [cx + 36 * s, cx + 58 * s]) { D.b(L(ex, cy - 36 * s, ex + 6 * s, cy - 64 * s, 4 * s), col, { k: 1, lw: 2 }); D.b(C(ex + 6 * s, cy - 66 * s, 6 * s), INK, { k: 1 }); D.dot(ex + 8 * s, cy - 68 * s, 1.6 * s, o.eye || '#e8d8a0'); }
  claw(D, cx + 62 * s, cy - 4 * s, (o.bigClaw || 1.2) * s, -1.05, col);
  if (!o.oneClaw) claw(D, cx - 56 * s, cy - 4 * s, (o.smallClaw || 0.9) * s, -2.2, sh(col, 0.85));
  o.over && o.over(D, cx, cy, s);
}

const BEASTS = {
  crustacean: { name: 'Dysformed Crustacean', sub: 'Cave · front rank', draw(D) {
    crab(D, { col: '#8a4a3a', bumps: [[-40, -30, 14], [-10, -44, 9], [20, -10, 11], [52, -34, 7]], bigClaw: 1.5, smallClaw: 0.6, eye: '#ffcf70' });
  } },
  pistolShrimp: { name: 'Pistol Shrimp', sub: 'Cave · stunner', draw(D) {
    const col = '#b86a4a'; D.c.save(); D.c.translate(-26, 20); D.shadow(165, 346, 110, 12);
    for (let i = 0; i < 5; i++) leg(D, [[110 + i * 18, 300], [100 + i * 18, 340], [96 + i * 18, 366]], 5, sh(col, 0.8));
    const sp = bez([60, 250], [100, 330], [200, 320], [230, 250]);
    const body = strip(sp, t => 18 + Math.sin(t * Math.PI) * 30, 0); D.b(body, col, { hi: 0.3 });
    for (let i = 4; i < sp.length - 3; i += 4) { const f = spineFrame(sp, i); D.line([[f.x + f.nx * 44, f.y + f.ny * 44], [f.x - f.nx * 44, f.y - f.ny * 44]], 2, 'rgba(10,6,4,0.5)'); }
    D.b(P([[40, 236], [62, 222], [76, 262], [52, 280], [30, 270]], true), sh(col, 1.1));
    D.line([[228, 240], [300, 160], [318, 170]], 2, INK, true); D.line([[232, 246], [310, 210]], 2);
    D.b(C(226, 242, 7), INK, { k: 1 }); D.dot(228, 240, 2, '#ffe0a0');
    claw(D, 214, 262, 1.5, -0.75, '#d08058', 0.7);
    D.glow(290, 176, 60, '#bfeaff', 0.9); for (let i = 0; i < 8; i++) { const a = i / 8 * TAU; D.line([[290 + Math.cos(a) * 14, 176 + Math.sin(a) * 14], [290 + Math.cos(a) * 30, 176 + Math.sin(a) * 30]], 2, '#e8f8ff'); }
    D.c.restore();
  } },
  seaWorm: { name: 'Deep Sea Worm', sub: 'Cave · lurker', draw(D) {
    const col = '#8a6a70'; D.shadow(165, 366, 90, 12);
    D.b(E(150, 360, 70, 12), '#2a2224', { k: 3 });
    const sp = bez([140, 366], [100, 260], [260, 220], [190, 110]);
    D.b(strip(sp, 30, 26), col);
    for (let i = 2; i < sp.length; i += 2) { const f = spineFrame(sp, i); D.line([[f.x + f.nx * 30, f.y + f.ny * 30], [f.x - f.nx * 30, f.y - f.ny * 30]], 1.8, 'rgba(10,4,6,0.55)'); }
    D.b(E(186, 104, 36, 18, -0.4), '#2a0e12', { k: 3 });
    for (let i = 0; i < 12; i++) { const a = i / 12 * TAU; D.b(P([[186 + Math.cos(a) * 30, 104 + Math.sin(a) * 14], [186 + Math.cos(a) * 16, 104 + Math.sin(a) * 6], [186 + Math.cos(a + 0.2) * 30, 104 + Math.sin(a + 0.2) * 14]], true), '#e8dcc0', { k: 1, lw: 1.2, hatch: false }); }
    D.glow(186, 104, 26, '#ff6a5a', 0.4);
  } },
  lobster: { name: 'The Lobster', sub: 'Cave mini-boss', draw(D) {
    const col = '#9a3024'; D.shadow(165, 366, 120, 14);
    for (let i = 0; i < 4; i++) { leg(D, [[140 + i * 12, 280], [110 + i * 10, 330], [100 + i * 14, 366]], 7, sh(col, 0.8)); leg(D, [[150 + i * 12, 280], [190 + i * 10, 330], [200 + i * 12, 366]], 7, col); }
    const tail = bez([150, 290], [110, 330], [60, 340], [50, 300]);
    for (let i = tail.length - 1; i >= 0; i -= 4) { const f = spineFrame(tail, i); D.b(E(f.x, f.y, 26 - i * 0.4, 18, Math.atan2(f.ty, f.tx)), col, { k: 5 }); }
    D.b(P([[40, 300], [20, 270], [44, 280], [56, 262], [62, 296]], true), sh(col, 1.1));
    D.b(P([[120, 300], [110, 220], [140, 150], [190, 130], [216, 170], [206, 260], [180, 300]], true), col, { hi: 0.35 });
    for (let y = 180; y < 290; y += 22) D.line([[118, y], [206, y - 6]], 2, 'rgba(10,4,4,0.5)');
    D.b(P([[176, 132], [220, 110], [238, 130], [210, 150]], true), col);
    D.line([[226, 118], [300, 40], [312, 60]], 2.4, INK, true); D.line([[222, 124], [310, 100], [318, 130]], 2.4, INK, true);
    D.b(C(214, 120, 6), INK, { k: 1 }); D.dot(216, 118, 2, '#ffcf80');
    claw(D, 200, 190, 2.0, -0.9, col, 0.5); claw(D, 130, 200, 1.6, -2.3, sh(col, 0.85), 0.2);
  } },
  ghostWorm: { name: 'The Ghost Worm', sub: 'Cave mini-boss', draw(D) {
    const c = D.c; D.glow(180, 200, 180, '#9ad8e0', 0.3);
    const sp = bez([40, 370], [60, 180], [300, 300], [230, 70]);
    const body = strip(sp, 44, 30);
    D.b(body, rgba('#c8e0e0', 0.55), { k: 18, dark: 0.55 });
    D.clipDo(body, () => { for (let i = 0; i < sp.length; i += 2) { const f = spineFrame(sp, i); D.glow(f.x, f.y, 26, '#bfffff', 0.25); } });
    for (let i = 1; i < sp.length; i += 2) { const f = spineFrame(sp, i); D.line([[f.x + f.nx * 40, f.y + f.ny * 40], [f.x - f.nx * 40, f.y - f.ny * 40]], 1.4, 'rgba(20,40,44,0.5)'); }
    for (let i = 3; i < sp.length - 3; i += 3) { const f = spineFrame(sp, i); D.dot(f.x, f.y, 5, 'rgba(40,20,30,0.5)'); }
    D.b(E(236, 64, 40, 22, -0.3), '#10181a', { k: 3 });
    for (let i = 0; i < 16; i++) { const a = i / 16 * TAU; D.line([[236 + Math.cos(a) * 36, 64 + Math.sin(a) * 18], [236 + Math.cos(a) * 20, 64 + Math.sin(a) * 9]], 2.4, '#e8f8f8'); }
  } },
  crabQueen: { name: 'The Crustacean Queen', sub: 'Cave boss', draw(D) {
    crab(D, { col: '#6a3a4a', s: 1.0, cy: 290, spikes: 7, bigClaw: 1.3, smallClaw: 1.1, eye: '#ff9aa0',
      under(D, cx, cy, s) { for (let i = 0; i < 9; i++) { const x = cx - 70 + i * 18, y = cy + 44 + (i % 2) * 10; D.b(C(x, y, 11), '#e8a060', { k: 3 }); D.glow(x, y, 20, '#ffb060', 0.5); } },
      over(D, cx, cy, s) {
        D.b(P([[cx - 30, cy - 50], [cx - 24, cy - 110], [cx - 8, cy - 78], [cx + 6, cy - 128], [cx + 20, cy - 80], [cx + 36, cy - 112], [cx + 42, cy - 52]], true), '#c9a24a', { hi: 0.45 });
        for (const x of [-24, 6, 36]) { D.b(C(cx + x, cy - 108 - (x === 6 ? 18 : 0), 6), '#8a2a4a', { k: 2 }); D.glow(cx + x, cy - 108 - (x === 6 ? 18 : 0), 20, '#ff6090', 0.6); }
      } });
  } },
  warDog: { name: 'War Dog', sub: 'Island · fast biter', draw(D) {
    const col = '#5a4030'; D.shadow(165, 366, 110, 12);
    leg(D, [[110, 280], [96, 320], [104, 366]], 13, sh(col, 0.8)); leg(D, [[210, 270], [214, 320], [206, 366]], 12, sh(col, 0.8));
    D.b(strip(bez([80, 250], [40, 230], [30, 190], [50, 170]), 9, 3), col);
    D.b(P([[76, 262], [100, 226], [170, 220], [222, 212], [240, 250], [216, 290], [150, 292], [96, 298]], true), col, { hi: 0.25 });
    for (let i = 0; i < 5; i++) D.line([[120 + i * 16, 240], [124 + i * 16, 276]], 2, 'rgba(8,4,2,0.45)');
    D.b(P([[140, 216], [210, 206], [220, 236], [150, 244]], true), '#e3d8c0', { k: 5 });
    leg(D, [[126, 284], [132, 326], [120, 366]], 13, col); leg(D, [[222, 272], [242, 316], [236, 366]], 12, col);
    D.b(P([[216, 214], [250, 180], [290, 190], [312, 214], [300, 236], [264, 240], [234, 250]], true), col, { hi: 0.3 });
    D.b(P([[236, 196], [240, 164], [256, 190]], true), sh(col, 0.9));
    D.b(P([[250, 186], [296, 190], [304, 210], [262, 210]], true), '#e3d8c0', { k: 3 });
    D.dot(272, 200, 4, INK); D.eyes([[272, 200]], 1.4, '#ff8a4a');
    D.b(P([[270, 222], [312, 214], [306, 236], [276, 238]], true), '#3a1010', { k: 2 });
    for (let i = 0; i < 4; i++) D.b(P([[278 + i * 8, 222], [282 + i * 8, 232], [286 + i * 8, 221]]), '#f0e8d8', { k: 1, lw: 1, hatch: false });
  } },
  giantOctopus: { name: 'Giant Octopus', sub: 'Weeds · grappler', draw(D) {
    const col = '#6a3a58'; D.shadow(165, 366, 130, 14);
    const arms = [[[140, 250], [80, 290], [30, 330], [20, 366]], [[150, 260], [110, 330], [70, 360], [60, 370]], [[180, 260], [220, 330], [260, 350], [300, 366]], [[190, 250], [250, 270], [300, 240], [296, 190]], [[160, 262], [150, 320], [180, 360], [200, 370]], [[140, 240], [60, 220], [30, 160], [70, 120]]];
    for (const a of arms) tentacle(D, a, 20, 3, col, '#e0b0c8');
    D.b(P([[110, 250], [100, 170], [130, 90], [196, 76], [240, 130], [236, 220], [210, 262]], true), col, { k: 20 });
    for (const [x, y, r] of [[140, 130, 8], [200, 110, 6], [170, 170, 9], [215, 180, 7]]) D.fill(C(x, y, r), rgba('#3a1a30', 0.7));
    D.fill(E(172, 236, 64, 16), 'rgba(8,2,6,0.7)');
    D.b(E(212, 226, 14, 10), '#d8b040', { k: 2 }); D.fill(R(204, 224, 16, 4), INK);
  } },
  eel: { name: 'Electric Eel', sub: 'Weeds mini-boss', draw(D) {
    const col = '#3a4a3a';
    const sp = bez([30, 330], [120, 400], [120, 120], [260, 150]);
    for (let i = 0; i < sp.length - 1; i += 3) { const f = spineFrame(sp, i); D.glow(f.x, f.y, 40, '#9ae0ff', 0.25); }
    D.b(strip(sp, t => 10 + Math.sin(Math.min(1, t * 1.4) * Math.PI) * 18 + (t > 0.9 ? 10 : 0), 0), col);
    for (let i = 2; i < sp.length - 2; i += 2) { const f = spineFrame(sp, i); D.dot(f.x - f.nx * 6, f.y - f.ny * 6, 3, '#d8e8a0'); }
    const hx = 264, hy = 150;
    D.b(E(hx, hy, 34, 20, 0.2), col, { hi: 0.3 }); D.b(P([[hx + 10, hy + 6], [hx + 40, hy + 8], [hx + 30, hy + 18]]), '#1a0a0a', { k: 2 });
    D.eyes([[hx + 12, hy - 6]], 1.8, '#c8ffe0');
    const c = D.c; c.save(); c.globalCompositeOperation = 'lighter';
    for (let k = 0; k < 6; k++) { let x = 60 + k * 40, y = 250 + (k % 2) * 30; const pts = [[x, y]]; for (let j = 0; j < 5; j++) { x += D.R(-20, 20); y -= D.R(10, 26); pts.push([x, y]); } D.line(pts, 2.4, 'rgba(170,230,255,0.9)'); }
    c.restore();
  } },
  shark: { name: 'Great White', sub: 'Weeds mini-boss', draw(D) {
    const col = '#5a6670';
    D.b(P([[100, 196], [90, 150], [128, 186]], true), col);
    D.b(P([[20, 200], [60, 160], [180, 140], [270, 170], [306, 210], [270, 250], [170, 270], [60, 244], [10, 290], [0, 230], [8, 160]], true), col, { k: 20 });
    D.b(P([[60, 240], [170, 262], [270, 246], [296, 222], [250, 226], [160, 240]], true), '#d8d4c8', { k: 6 });
    D.b(P([[150, 150], [190, 70], [210, 146]], true), col, { k: 8 }); D.b(P([[160, 250], [200, 320], [216, 256]], true), sh(col, 0.9), { k: 8 });
    for (let i = 0; i < 5; i++) D.line([[218 + i * 7, 190], [214 + i * 7, 214]], 1.6, 'rgba(10,10,12,0.6)');
    D.b(P([[250, 220], [302, 212], [290, 238], [252, 236]], true), '#3a1418', { k: 2 });
    for (let i = 0; i < 6; i++) { D.b(P([[254 + i * 8, 220], [258 + i * 8, 230], [262 + i * 8, 219]]), '#f0ece0', { k: 1, lw: 1, hatch: false }); D.b(P([[254 + i * 7, 236], [258 + i * 7, 228], [262 + i * 7, 236]]), '#f0ece0', { k: 1, lw: 1, hatch: false }); }
    D.dot(262, 186, 5, INK); D.dot(263, 185, 1.4, '#c8c8c0');
    D.line([[120, 170], [150, 200]], 1.6, '#c8c0b0'); D.line([[130, 168], [160, 196]], 1.6, '#c8c0b0'); D.line([[200, 170], [214, 186]], 1.6, '#c8c0b0');
  } },
  alienHorror: { name: 'The Alien Horror', sub: 'Atlantis mini-boss', draw(D) {
    const col = '#4a3a5a'; D.shadow(165, 366, 130, 14);
    for (let i = 0; i < 7; i++) tentacle(D, [[165, 260], [100 + i * 22, 300], [40 + i * 40, 330], [20 + i * 48, 368]], 14, 2, i % 2 ? col : sh(col, 0.8), '#b8a0d0');
    D.b(P([[80, 270], [70, 170], [120, 80], [210, 70], [262, 150], [256, 262]], true), col, { k: 22 });
    for (const [x, y, r] of [[130, 130, 16], [196, 120, 20], [160, 190, 12], [226, 190, 10], [110, 210, 9], [176, 90, 8]]) {
      D.b(C(x, y, r), '#d8d0b0', { k: 3 }); D.dot(x + r * 0.2, y, r * 0.45, INK); D.glow(x, y, r * 2, '#c8ff80', 0.3);
    }
    D.b(P([[120, 250], [165, 230], [210, 250], [200, 280], [165, 268], [130, 280]], true), '#1a0a14', { k: 3 });
    for (let i = 0; i < 6; i++) D.b(L(128 + i * 15, 250, 124 + i * 16, 292, 3, 1), '#e8e0cc', { k: 1, lw: 1.2, hatch: false });
  } },
  cthulhu: { name: 'Cthulhu', sub: 'Atlantis boss', draw(D) {
    const col = '#3e5a4a';
    D.glow(165, 200, 200, '#6ae0b0', 0.25);
    for (const s of [-1, 1]) D.b(P([[165 + s * 50, 170], [165 + s * 150, 60], [165 + s * 160, 120], [165 + s * 140, 110], [165 + s * 150, 180], [165 + s * 126, 170], [165 + s * 130, 240], [165 + s * 60, 230]], true), s < 0 ? sh(col, 0.7) : sh(col, 0.85), { k: 12 });
    D.b(P([[90, 368], [96, 240], [120, 190], [210, 190], [236, 240], [240, 368]], true), col, { k: 22 });
    D.b(P([[112, 190], [104, 110], [132, 60], [198, 56], [228, 110], [220, 190]], true), col, { k: 20, hi: 0.2 });
    for (let i = 0; i < 7; i++) tentacle(D, [[120 + i * 15, 176], [110 + i * 17, 230], [96 + i * 22, 270], [110 + i * 20, 320]], 10, 2, i % 2 ? col : sh(col, 0.85), '#a8d8b8');
    D.fill(E(166, 130, 50, 14), 'rgba(4,10,8,0.85)');
    D.eyes([[146, 130], [188, 130]], 3.4, '#ffe060');
    for (const [x, y] of [[120, 280], [200, 300], [150, 330]]) D.fill(C(x, y, 14), 'rgba(10,20,16,0.5)');
  } },
  // platform creatures
  hullCrab: { name: 'Hull Crab', sub: 'Hull · scuttles the plating', draw(D) { crab(D, { col: '#a0584a', s: 0.9, cy: 310, eye: '#ffe0a0' }); } },
  moray: { name: 'Moray Eel', sub: 'Hull · strikes from pipes', draw(D) {
    const col = '#5a6a3a';
    D.b(R(20, 200, 80, 90, 8), '#4a4c4e', { hi: 0.3 }); D.b(E(92, 245, 16, 40), '#0a0a0c', { k: 2 });
    const sp = bez([92, 250], [160, 280], [200, 180], [260, 190]);
    D.b(strip(sp, 24, 20), col);
    for (let i = 0; i < 40; i++) { const f = spineFrame(sp, Math.floor(D.rand() * sp.length)); D.dot(f.x + D.R(-14, 14), f.y + D.R(-14, 14), 2.4, 'rgba(210,200,120,0.6)'); }
    D.b(P([[240, 170], [290, 176], [310, 196], [292, 210], [250, 214]], true), col, { hi: 0.3 });
    D.b(P([[262, 196], [310, 196], [300, 214], [266, 212]], true), '#2a0a0a', { k: 2 });
    for (let i = 0; i < 5; i++) D.b(P([[268 + i * 8, 196], [271 + i * 8, 205], [274 + i * 8, 196]]), '#f0e8d8', { k: 1, lw: 1, hatch: false });
    D.eyes([[270, 182]], 1.8, '#e8ff80');
  } },
  parakeet: { name: 'Parakeet', sub: 'Pirate Ship · dive-bomber', draw(D) {
    D.b(P([[120, 200], [60, 170], [30, 190], [70, 214], [40, 240], [110, 230]], true), '#2e6a3a', { k: 8 });
    D.b(P([[130, 250], [90, 320], [110, 330], [150, 262]], true), '#2a4a8a');
    D.b(E(170, 220, 56, 40, -0.3), '#3a8a3a', { k: 14, hi: 0.3 });
    D.b(P([[140, 190], [230, 120], [250, 150], [180, 230]], true), '#2e6a3a', { k: 10 });
    for (let i = 0; i < 4; i++) D.line([[160 + i * 16, 200 - i * 14], [200 + i * 12, 160 - i * 12]], 1.6, 'rgba(10,20,10,0.6)');
    D.b(C(222, 188, 26), '#c0382a', { k: 8 }); D.b(P([[242, 184], [270, 192], [260, 214], [240, 204]], true), '#e8d8b0', { k: 3 });
    D.b(C(226, 182, 6), '#f0e8d0', { k: 1 }); D.dot(228, 182, 3, INK);
    leg(D, [[160, 254], [160, 290]], 4, '#6a5a4a'); leg(D, [[180, 254], [184, 290]], 4, '#6a5a4a');
  } },
  kraken: { name: 'The Kraken', sub: 'Hull boss · colossal', draw(D) {
    const col = '#6a2e3a'; D.glow(165, 200, 200, '#3a6aa0', 0.3);
    for (let i = 0; i < 6; i++) { const x = 20 + i * 56; tentacle(D, [[165, 250], [x, 290], [x + (i % 2 ? 40 : -40), 200 - (i % 3) * 30], [x + 10, 60 + (i % 3) * 40]], 20, 3, i % 2 ? col : sh(col, 0.8), '#e8b0b8'); }
    D.b(P([[80, 290], [60, 160], [110, 40], [220, 30], [274, 150], [256, 290]], true), col, { k: 26 });
    for (const [x, y, r] of [[120, 90, 12], [210, 70, 9], [150, 160, 14], [236, 200, 10], [96, 220, 8]]) D.fill(C(x, y, r), 'rgba(40,10,20,0.6)');
    D.b(E(202, 200, 28, 20), '#e8c860', { k: 4 }); D.fill(E(202, 200, 5, 16), INK); D.glow(202, 200, 40, '#ffe080', 0.4);
    D.fill(E(202, 176, 34, 10), 'rgba(10,2,6,0.7)');
    D.b(P([[160, 270], [196, 250], [214, 280], [190, 310], [170, 300]], true), '#2a2020', { k: 3 }); D.line([[176, 282], [206, 280]], 2, '#e8dcc0');
  } },
};
