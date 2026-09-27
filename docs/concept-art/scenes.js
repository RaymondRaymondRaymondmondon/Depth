// Full scenes, 640 x 360. Inked painterly layers, lightmaps, and the cast placed in them.
const SW = 640, SH = 360;
function vgrad(D, stops, x = 0, y = 0, w = SW, h = SH) {
  const c = D.c, g = c.createLinearGradient(0, y, 0, y + h);
  stops.forEach((s, i) => g.addColorStop(i / (stops.length - 1), s)); c.fillStyle = g; c.fillRect(x, y, w, h);
}
// a jagged silhouette band rising from the bottom (or hanging from the top)
function ridge(D, y, amp, col, seed, o = {}) {
  const r = rng(seed), pts = [], n = o.n || 18, top = o.top;
  for (let i = 0; i <= n; i++) pts.push([i / n * (SW + 40) - 20, y + (r() - 0.5) * amp * 2 + (o.fn ? o.fn(i / n) : 0)]);
  const ed = top ? -20 : SH + 20;
  D.b(P([[-20, ed], ...pts, [SW + 20, ed]], !o.sharp), col, { k: o.k ?? 10, lw: o.lw ?? 3, hatch: o.hatch ?? true, alpha: o.alpha });
}
function haze(D, y, h, col, a) { const c = D.c, g = c.createLinearGradient(0, y, 0, y + h); g.addColorStop(0, rgba(col, 0)); g.addColorStop(0.5, rgba(col, a)); g.addColorStop(1, rgba(col, 0)); c.fillStyle = g; c.fillRect(0, y, SW, h); }
function rays(D, list, col = '#bfe8ff', a = 0.12) {
  const c = D.c; c.save(); c.globalCompositeOperation = 'lighter';
  for (const [x, w, tilt] of list) { const g = c.createLinearGradient(0, 0, 0, SH); g.addColorStop(0, rgba(col, a)); g.addColorStop(1, rgba(col, 0)); c.fillStyle = g; c.beginPath(); c.moveTo(x, -10); c.lineTo(x + w, -10); c.lineTo(x + w + tilt + w, SH); c.lineTo(x + tilt - w * 0.5, SH); c.fill(); }
  c.restore();
}
function motes(D, n, col, y0 = 0, y1 = SH) { for (let i = 0; i < n; i++) D.dot(D.R(0, SW), D.R(y0, y1), D.R(0.5, 1.7), rgba(col, D.R(0.3, 0.9))); }
function darkness(D, x, y, r, a = 0.93, cone) {
  const c = D.c, g = c.createRadialGradient(x, y, r * 0.15, x, y, r);
  g.addColorStop(0, 'rgba(2,3,6,0)'); g.addColorStop(0.6, `rgba(2,3,6,${a * 0.6})`); g.addColorStop(1, `rgba(2,3,6,${a})`);
  c.fillStyle = g; c.fillRect(0, 0, SW, SH);
  if (cone) { c.save(); c.globalCompositeOperation = 'lighter'; const g2 = c.createRadialGradient(x, y, 0, x, y, cone[2]); g2.addColorStop(0, 'rgba(255,230,170,0.35)'); g2.addColorStop(1, 'rgba(255,230,170,0)'); c.fillStyle = g2; c.beginPath(); c.moveTo(x, y); c.arc(x, y, cone[2], cone[0], cone[1]); c.fill(); c.restore(); }
}
// a 2.5D platform block: front face, a top and right side receding up-right
function block(D, x, y, w, h, col, o = {}) {
  const d = o.d ?? 9;
  D.b(P([[x, y], [x + d, y - d], [x + w + d, y - d], [x + w, y]]), mix(col, '#fff', 0.18), { k: 2, lw: 2.4, hatch: false });
  D.b(P([[x + w, y], [x + w + d, y - d], [x + w + d, y + h - d], [x + w, y + h]]), sh(col, 0.55), { k: 2, lw: 2.4, hatch: false });
  D.b(R(x, y, w, h), col, { k: Math.min(10, h * 0.25), lw: 2.6 });
  if (o.grid) { for (let gx = x + o.grid; gx < x + w; gx += o.grid) D.line([[gx, y], [gx, y + h]], 1.2, 'rgba(0,0,0,0.45)'); for (let gy = y + o.grid; gy < y + h; gy += o.grid) D.line([[x, gy], [x + w, gy]], 1.2, 'rgba(0,0,0,0.45)'); }
  if (o.rivets) for (let rx = x + 6; rx < x + w; rx += 14) D.dot(rx, y + 5, 1.4, 'rgba(0,0,0,0.6)');
}
function pipe(D, x1, y1, x2, y2, r, col) { D.b(L(x1, y1, x2, y2, r), col, { hi: 0.4, k: r * 0.5, lw: 2.6 }); const horiz = Math.abs(x2 - x1) > Math.abs(y2 - y1); for (let t = 0.2; t < 1; t += 0.3) { const x = x1 + (x2 - x1) * t, y = y1 + (y2 - y1) * t; D.b(horiz ? R(x - 3, y - r - 2, 6, r * 2 + 4, 1) : R(x - r - 2, y - 3, r * 2 + 4, 6, 1), sh(col, 0.8), { k: 1, lw: 1.8, hatch: false }); } }
// a battle line-up: four heroes facing right, four foes facing left
function party(D, heroes, foes, gy = 318, s = 0.3) {
  heroes.forEach((f, i) => { D.shadow(250 - i * 60, gy, 34, 6, 0.6); put(D, f, 250 - i * 60, gy, s, false); });
  foes.forEach((f, i) => { const k = f.s || 1; D.shadow(395 + i * 62, gy, 34 * k, 6, 0.6); put(D, f.draw || f, 395 + i * 62 + (f.dx || 0), gy, s * k, true); });
}
const H = k => HEROES[k].draw, B = k => BEASTS[k].draw, F = k => HFOES[k].draw;
function torch(D, x, y) { D.glow(x, y, 240, '#ffb060', 0.35); }

// ================= SALON =================
function salon(D) {
  const c = D.c;
  vgrad(D, ['#2a1c16', '#1c1410']);
  // ceiling, walls, floor in one-point perspective
  D.b(P([[0, 0], [640, 0], [492, 62], [148, 62]]), '#2e2018', { k: 8, lw: 3 });
  for (let i = 1; i < 8; i++) D.line([[i * 80, 0], [148 + i * 43, 62]], 2, 'rgba(0,0,0,0.5)');
  D.b(P([[0, 0], [148, 62], [148, 232], [0, 360]]), '#4a3226', { k: 10 });
  D.b(P([[640, 0], [492, 62], [492, 232], [640, 360]]), '#3e2a20', { k: 10 });
  D.b(R(148, 62, 344, 170), '#553a2a', { k: 12 });
  D.b(P([[0, 360], [148, 232], [492, 232], [640, 360]]), '#4a2e22', { k: 6 });
  for (let i = 0; i <= 12; i++) D.line([[148 + i * 344 / 12, 232], [i * 640 / 12 - 0, 360]], 1.4, 'rgba(0,0,0,0.45)');
  D.b(P([[120, 330], [196, 262], [444, 262], [520, 330]]), '#6a2a26', { k: 8 });
  D.b(P([[146, 318], [206, 270], [434, 270], [494, 318]]), '#7a3a2e', { k: 4, hatch: false });
  D.line([[160, 308], [480, 308]], 2, '#c9a24a'); D.line([[196, 276], [444, 276]], 2, '#c9a24a');
  // wainscot panels
  for (let i = 0; i < 6; i++) D.b(R(158 + i * 55, 176, 46, 48, 2), '#4a3022', { k: 4, lw: 2, hatch: false });
  // the great viewport
  D.clipDo(C(320, 124, 64), () => {
    vgrad(D, ['#2a6a86', '#0e2a3e', '#061420'], 250, 60, 140, 130);
    rays(D, [[290, 10, 20], [330, 8, 10]], '#bfefff', 0.25);
    for (let i = 0; i < 5; i++) { const x = 270 + i * 22, y = 110 + (i % 3) * 18; D.fill(P([[x, y], [x + 12, y - 4], [x + 16, y], [x + 12, y + 4], [x + 18, y + 6], [x + 18, y - 6]]), 'rgba(10,30,40,0.8)'); }
    D.fill(P([[250, 190], [290, 160], [340, 172], [390, 150], [390, 190]]), '#08161e');
  });
  D.ring(320, 124, 64, '#b28b3e', 10); for (let i = 0; i < 16; i++) { const a = i / 16 * TAU; D.dot(320 + Math.cos(a) * 69, 124 + Math.sin(a) * 69, 2, '#5a3a14'); }
  D.line([[256, 124], [384, 124]], 3, '#8d6c2c'); D.line([[320, 60], [320, 188]], 3, '#8d6c2c');
  // bookcases on the back wall
  for (const bx of [166, 402]) {
    D.b(R(bx, 76, 72, 150, 2), '#3a2418', { k: 8 });
    for (let s = 0; s < 4; s++) { D.line([[bx + 4, 108 + s * 30], [bx + 68, 108 + s * 30]], 3, '#241810'); for (let b = 0; b < 8; b++) D.b(R(bx + 6 + b * 8, 84 + s * 30, 6, 23 - (b * 7 % 5), 1), ['#6a2a22', '#2a4a3a', '#6a5a2a', '#3a2a4a'][(b + s) % 4], { k: 1, lw: 1.2, hatch: false }); }
  }
  // left wall: helm and radio (painted flat, receding)
  D.b(P([[30, 120], [110, 94], [110, 186], [30, 236]]), '#3a261c', { k: 6 });
  D.ring(70, 160, 30, '#6a4a2a', 6); for (let i = 0; i < 8; i++) { const a = i / 8 * TAU; D.b(L(70 + Math.cos(a) * 20, 160 + Math.sin(a) * 26, 70 + Math.cos(a) * 44, 160 + Math.sin(a) * 50, 3), '#6a4a2a', { k: 1, lw: 1.6 }); }
  D.b(C(70, 160, 7), '#b28b3e', { k: 2 });
  D.b(P([[118, 120], [142, 112], [142, 170], [118, 180]]), '#2a2a26', { k: 4 }); D.glow(130, 140, 26, '#7af0a0', 0.5); D.dot(126, 134, 2, '#afffc0'); D.dot(134, 146, 2, '#ffcf70');
  // right wall: workshop bench, periscope column
  D.b(P([[510, 110], [600, 70], [600, 170], [510, 200]]), '#3a2a20', { k: 6 });
  for (let i = 0; i < 5; i++) D.b(R(518 + i * 16, 110 - i * 6, 6, 30, 1), '#6f7275', { k: 1, lw: 1.4, hatch: false });
  D.b(P([[500, 230], [620, 190], [640, 250], [520, 300]]), '#5a3a22', { k: 6 });
  D.b(R(444, 40, 16, 196, 3), '#8d6c2c', { hi: 0.4, k: 4 }); D.b(R(432, 190, 40, 30, 4), '#8d6c2c', { hi: 0.3 }); D.b(R(428, 196, 16, 12, 2), '#1a1a1a', { k: 1 });
  // card table (front right) and sick bay cot (front left)
  D.b(E(520, 322, 70, 20), '#5a2e1e', { k: 6 }); D.b(E(520, 316, 62, 16), '#2e5a3a', { k: 4, hatch: false }); D.b(R(514, 324, 12, 36), '#3a2014', { k: 2 });
  D.b(P([[40, 300], [150, 280], [170, 320], [60, 344]]), '#d8d0c0', { k: 5 }); D.b(P([[40, 300], [60, 344], [60, 360], [40, 330]]), '#6a4a2a', { k: 2 });
  // chandelier
  D.line([[320, 0], [320, 26]], 3, '#b28b3e');
  D.b(E(320, 34, 44, 8), '#b28b3e', { hi: 0.4, k: 3 });
  for (let i = 0; i < 8; i++) { const x = 282 + i * 11; D.b(R(x - 2, 20, 4, 12, 1), '#e8dcc0', { k: 1, lw: 1.2, hatch: false }); D.glow(x, 18, 30, '#ffc070', 0.5); D.dot(x, 17, 2, '#fff0c0'); }
  for (let i = 0; i < 9; i++) D.fill(E(288 + i * 8, 46 + (i % 2) * 4, 2, 5), 'rgba(220,240,255,0.7)');
  // the lamplight
  D.glow(320, 60, 380, '#ffb060', 0.28); D.glow(520, 300, 120, '#ffc070', 0.25); D.glow(80, 180, 120, '#ffc070', 0.2);
  // two hands at work and the cat
  put(D, SALON.helmsman.draw, 106, 296, 0.3, true);
  put(D, SALON.professor.draw, 238, 262, 0.26, false);
  put(D, DEALER.draw, 560, 316, 0.27, true);
  put(D, SALON.cat.draw, 336, 300, 0.2, false);
  motes(D, 40, '#ffe0b0', 20, 200);
}

// ================= CARD TABLE =================
function cardTable(D) {
  const c = D.c;
  vgrad(D, ['#07060a', '#0b090c']);
  // barely-there room: the dark is the point
  for (let i = 0; i < 5; i++) D.fill(R(40 + i * 130, 40, 60, 200), 'rgba(40,30,26,0.12)');
  D.line([[320, 0], [320, 44]], 2, '#3a2a1a');
  D.b(P([[296, 44], [344, 44], [356, 66], [284, 66]]), '#6a4a24', { k: 3 });
  D.glow(320, 70, 320, '#ffb060', 0.35);
  put(D, DEALER.draw, 320, 380, 0.78, true);
  c.save(); c.globalCompositeOperation = 'lighter'; const g = c.createRadialGradient(320, 150, 20, 320, 180, 220); g.addColorStop(0, 'rgba(255,190,110,0.18)'); g.addColorStop(1, 'rgba(255,190,110,0)'); c.fillStyle = g; c.fillRect(0, 0, SW, SH); c.restore();
  // the table
  D.b(P([[40, 360], [120, 214], [520, 214], [600, 360]]), '#4a2a1a', { k: 8 });
  D.b(P([[70, 360], [140, 226], [500, 226], [570, 360]]), '#2e4a36', { k: 5 });
  // three glowing flats per side
  for (const [row, a] of [[0, 0.35], [1, 0.55]]) for (let i = 0; i < 3; i++) {
    const y0 = row ? 292 : 236, y1 = row ? 340 : 280, t0 = (y0 - 226) / 134, t1 = (y1 - 226) / 134;
    const l0 = 140 - 70 * t0, r0 = 500 + 70 * t0, l1 = 140 - 70 * t1, r1 = 500 + 70 * t1;
    const w0 = (r0 - l0) / 3, w1 = (r1 - l1) / 3;
    const q = P([[l0 + w0 * i + 10, y0], [l0 + w0 * (i + 1) - 10, y0], [l1 + w1 * (i + 1) - 10, y1], [l1 + w1 * i + 10, y1]]);
    D.b(q, '#3e6a52', { k: 2, lw: 2, hatch: false }); D.glow((l0 + w0 * (i + 0.5) + l1 + w1 * (i + 0.5)) / 2, (y0 + y1) / 2, 60, '#8affc0', a * 0.5);
  }
  // a card played on the dealer's middle flat and one of ours
  D.b(P([[296, 240], [340, 240], [344, 276], [292, 276]]), '#d8ccb0', { k: 3, lw: 2 }); D.fill(E(318, 256, 10, 8), '#6a2a24');
  D.b(P([[300, 298], [346, 298], [350, 338], [296, 338]]), '#d8ccb0', { k: 3, lw: 2 }); D.fill(C(323, 316, 8), '#2a4a6a');
  // the bell and a candle
  D.b(E(552, 300, 26, 7), '#6a4a24', { k: 2 }); D.b(P([[532, 298], [538, 272], [552, 264], [566, 272], [572, 298]], true), '#c9a24a', { hi: 0.5 }); D.b(C(552, 262, 5), '#c9a24a', { k: 1 });
  D.b(R(90, 262, 14, 36, 2), '#e8dcc0', { k: 3 }); D.glow(97, 254, 60, '#ffc070', 0.7); D.fill(E(97, 254, 3, 7), '#fff0c0');
  // our hand, fanned at the bottom
  for (let i = 0; i < 5; i++) D.tool(320, 420, -0.5 + i * 0.25 - Math.PI / 2, D => { D.b(R(40, -24, 90, 48, 5), '#e0d4b8', { k: 4, lw: 2.4 }); D.fill(C(100, 0, 10), ['#6a2a24', '#2a4a6a', '#3a5a2a', '#6a5a24', '#4a2a5a'][i]); });
}

// ================= COMBAT LOCATIONS =================
function caveBg(D, deep) {
  vgrad(D, deep ? ['#050a12', '#0a1620', '#060a0e'] : ['#0a1822', '#12303a', '#0a141a']);
  ridge(D, 150, 40, '#132a32', 11, { alpha: 0.9, hatch: false, lw: 2 });
  for (let i = 0; i < 14; i++) { const x = i * 48 + 10, l = 30 + (i * 37 % 60); D.b(P([[x - 18, -5], [x + 18, -5], [x + 2, l]], true), '#1a3440', { k: 6, lw: 2.6 }); }
  haze(D, 170, 90, '#4aa0a0', 0.18);
  ridge(D, 240, 30, '#1e3a3e', 23, { k: 12 });
  const cols = ['#e05a7a', '#f0a050', '#6ad0c0'];
  for (let i = 0; i < 9; i++) { const x = 20 + i * 76, y = 250 + (i % 3) * 8; for (let j = 0; j < 4; j++) D.b(L(x + j * 7, y, x + j * 9 - 8, y - 22 - j * 6, 4, 2), cols[i % 3], { k: 2, lw: 1.8, hatch: false }); D.glow(x + 10, y - 20, 34, cols[i % 3], 0.3); }
  for (let i = 0; i < 26; i++) { const x = D.R(0, SW), y = D.R(40, 280); D.glow(x, y, D.R(8, 20), '#7affd0', 0.35); }
  ridge(D, 312, 8, '#1a2a2c', 37, { k: 6, n: 30 });
  torch(D, 170, 250);
}
function islandBg(D) {
  vgrad(D, ['#3a5a3a', '#223a2a', '#101c16']);
  rays(D, [[80, 30, 60], [240, 20, 40], [420, 40, 50]], '#f0f0b0', 0.14);
  for (let i = 0; i < 6; i++) { const x = 40 + i * 110; D.b(strip(bez([x, 300], [x + 10, 200], [x + 40, 120], [x + 70, 60]), 8, 5), '#2a3a24', { k: 4, alpha: 0.8 }); for (let f = 0; f < 5; f++) D.b(strip(bez([x + 70, 60], [x + 70 + (f - 2) * 20, 30], [x + 70 + (f - 2) * 40, 40], [x + 70 + (f - 2) * 50, 80]), 6, 1), '#2e4a2a', { k: 3, alpha: 0.8 }); }
  haze(D, 150, 120, '#8ab080', 0.2);
  for (let i = 0; i < 8; i++) { const x = 20 + i * 84, h = 60 + (i * 53 % 70); D.b(P([[x, 300], [x, 300 - h], [x + 14, 294 - h], [x + 28, 300 - h], [x + 28, 300]]), '#2a2a2e', { k: 8 }); D.line([[x + 14, 294 - h], [x + 14, 300]], 1.4, 'rgba(0,0,0,0.5)'); }
  for (const x of [120, 530]) { D.b(R(x - 5, 180, 10, 130), '#d8ccb0', { k: 3 }); for (let j = 0; j < 3; j++) { D.b(C(x, 190 + j * 34, 13), '#e3d8c0', { k: 4 }); D.dot(x - 4, 188 + j * 34, 3, INK); D.dot(x + 5, 188 + j * 34, 3, INK); } }
  ridge(D, 312, 10, '#3a3424', 51, { k: 6, n: 26 });
  torch(D, 170, 250);
}
function weedsBg(D) {
  vgrad(D, ['#1a4a4a', '#0e2e30', '#08181a']);
  rays(D, [[60, 20, 40], [200, 34, 20], [380, 26, 30], [520, 20, 20]], '#cfffe8', 0.12);
  for (let layer = 0; layer < 3; layer++) {
    const col = ['#1a3a2e', '#224a30', '#2e5a2e'][layer], a = [0.6, 0.8, 1][layer];
    for (let i = 0; i < 9; i++) { const x = (i * 83 + layer * 37) % 680 - 20; const sp = bez([x, 330], [x + 30, 240], [x - 30, 120], [x + 12, -10]); D.b(strip(sp, 7 + layer * 2, 4), col, { k: 4, alpha: a, lw: 2 + layer * 0.6 }); for (let j = 4; j < sp.length - 2; j += 5) { const f = spineFrame(sp, j); D.b(E(f.x + 14, f.y, 14, 5, -0.5), col, { k: 2, alpha: a, lw: 1.6 }); } }
    haze(D, 100 + layer * 60, 120, '#5ab0a0', 0.08);
  }
  for (let i = 0; i < 12; i++) { const x = D.R(0, SW), y = D.R(40, 200); D.fill(P([[x, y], [x + 10, y - 3], [x + 14, y], [x + 10, y + 3], [x + 16, y + 5], [x + 16, y - 5]]), 'rgba(6,20,20,0.6)'); }
  ridge(D, 312, 8, '#1e2e22', 61, { k: 6, n: 26 });
  torch(D, 170, 250);
}
function atlantisBg(D) {
  vgrad(D, ['#0a1430', '#101e3a', '#060a18']);
  ridge(D, 160, 20, '#121c36', 71, { sharp: true, n: 10, hatch: false, lw: 2, alpha: 0.9 });
  D.b(P([[240, 150], [320, 110], [400, 150]]), '#1c2a48', { k: 6 });
  for (let i = 0; i < 5; i++) D.b(R(250 + i * 34, 150, 14, 90), '#1c2a48', { k: 4 });
  haze(D, 170, 80, '#4a6ab0', 0.2);
  for (const [x, h, br] of [[60, 190, 0], [150, 150, 1], [480, 170, 0], [580, 120, 1]]) { D.b(R(x - 16, 310 - h, 32, h), '#3a4a5a', { k: 8 }); for (let j = 1; j < 4; j++) D.line([[x - 10 + j * 5, 312 - h], [x - 10 + j * 5, 310]], 1.2, 'rgba(0,0,0,0.4)'); D.b(R(x - 22, 300 - h, 44, 12), '#4a5a6a', { k: 3 }); if (br) D.b(P([[x - 22, 300 - h], [x + 22, 300 - h], [x + 14, 290 - h], [x - 30, 306 - h]]), '#4a5a6a', { k: 3 }); }
  for (const x of [220, 420]) { D.b(P([[x - 22, 310], [x - 14, 150], [x + 14, 140], [x + 22, 310]]), '#101418', { k: 10 }); for (let j = 0; j < 5; j++) { D.glow(x, 180 + j * 24, 18, '#6ae0b0', 0.6); D.line([[x - 6, 176 + j * 24], [x + 6, 184 + j * 24]], 2, '#8affd0'); } }
  ridge(D, 312, 6, '#1e2436', 81, { k: 6, n: 26 });
  for (let i = 0; i < 8; i++) D.line([[i * 90, 318], [i * 90 + 40, 360]], 1.2, 'rgba(0,0,0,0.4)');
  torch(D, 170, 250);
}
const CREW = [H('diver'), H('captain'), H('nurse'), H('mechanic')];

// ================= PLATFORM LEVELS =================
function pipesLevel(D) {
  vgrad(D, ['#1a1410', '#120e0c']);
  for (let y = 0; y < 360; y += 32) for (let x = (y / 32 % 2) * 16; x < 640; x += 32) D.fill(R(x + 1, y + 1, 30, 30), 'rgba(60,40,30,0.25)');
  pipe(D, -10, 60, 650, 60, 12, '#5a4a3a'); pipe(D, 90, 0, 90, 360, 10, '#4a4a44'); pipe(D, 520, 0, 520, 360, 14, '#6a3a2a');
  block(D, 0, 300, 640, 60, '#4a3a30', { grid: 32, rivets: true });
  block(D, 0, 0, 640, 24, '#4a3a30', { grid: 32 });
  block(D, 150, 230, 96, 20, '#5a4436', { grid: 32, rivets: true });
  pipe(D, 290, 190, 420, 190, 11, '#8a6a3a');
  block(D, 452, 150, 50, 150, '#4a3a30', { grid: 25 });
  block(D, 560, 110, 80, 20, '#5a4436', { rivets: true });
  pipe(D, 600, 24, 600, 90, 9, '#5a5a54');
  for (let i = 0; i < 3; i++) D.fill(C(360 + i * 10, 150 - i * 20, 8 + i * 5), 'rgba(200,200,190,0.18)');
  put(D, PLATFOLK.diver.draw, 330, 190 - 11, 0.2, false);
  darkness(D, 344, 146, 250, 0.95, [-0.5, 0.5, 260]);
  motes(D, 25, '#ffe8c0', 100, 260);
}
function hullLevel(D) {
  vgrad(D, ['#12344a', '#0a2030', '#040c14']);
  rays(D, [[140, 30, 40], [380, 24, 30]], '#bfefff', 0.12);
  D.b(P([[-20, 40], [300, 70], [360, 160], [300, 250], [-20, 280]], true), '#1a2a34', { k: 16, alpha: 0.7, lw: 2 });
  for (let i = 0; i < 6; i++) D.line([[0, 70 + i * 34], [320, 90 + i * 28]], 1.4, 'rgba(0,0,0,0.4)');
  D.b(P([[440, 360], [460, 250], [520, 240], [540, 360]]), '#0a141c', { k: 8, alpha: 0.9 }); D.fill(R(520, 230, 120, 130), 'rgba(0,0,0,0.6)');
  block(D, 0, 290, 200, 70, '#4a5058', { grid: 30, rivets: true });
  block(D, 250, 240, 90, 18, '#5a6068', { rivets: true });
  block(D, 380, 190, 70, 18, '#5a6068', { rivets: true });
  block(D, 470, 290, 60, 70, '#4a5058', { grid: 30 });
  pipe(D, 560, 360, 560, 150, 13, '#4a4c4e'); D.b(E(560, 150, 16, 8), '#0a0a0c', { k: 2 });
  put(D, BEASTS.hullCrab.draw, 290, 240 - 9, 0.16, true);
  put(D, BEASTS.moray.draw, 590, 190, 0.2, true);
  put(D, PLATFOLK.diver.draw, 150, 290 - 9, 0.2, false);
  for (let i = 0; i < 20; i++) { const x = D.R(0, SW), y = D.R(0, SH); D.b(C(x, y, D.R(1.5, 4)), 'rgba(190,230,255,0.25)', { flat: true, lw: 1, ink: 'rgba(190,230,255,0.5)' }); }
  motes(D, 50, '#d8f0ff');
}
function krakenArena(D) {
  vgrad(D, ['#06121e', '#040a14', '#020408']);
  put(D, BEASTS.kraken.draw, 470, 440, 1.3, true);
  D.fill(R(0, 0, SW, SH), 'rgba(2,6,14,0.55)');
  D.eyes([[470 - 37 * 1.3, 440 - 168 * 1.3]], 6, '#ffe060');
  tentacle(D, [[80, 380], [40, 240], [150, 120], [200, 170]], 26, 6, '#6a2e3a', '#e8b0b8');
  tentacle(D, [[560, 380], [620, 200], [460, 90], [430, 250]], 28, 8, '#5a2632', '#e8b0b8');
  block(D, 0, 310, 180, 50, '#3a3a3e', { grid: 30 }); block(D, 250, 270, 120, 18, '#3a3a3e', { rivets: true }); block(D, 460, 310, 180, 50, '#3a3a3e', { grid: 30 });
  D.fill(E(430, 262, 60, 12), 'rgba(10,6,8,0.6)');
  put(D, PLATFOLK.diver.draw, 300, 270 - 9, 0.2, false);
  motes(D, 60, '#c8e0ff');
}
function pirateDeck(D) {
  vgrad(D, ['#141a30', '#1e2440', '#2a2230']);
  D.glow(520, 70, 120, '#e8e0c0', 0.5); D.b(C(520, 70, 26), '#e8e2c8', { k: 6, lw: 2 });
  for (let i = 0; i < 40; i++) D.dot(D.R(0, SW), D.R(0, 160), D.R(0.5, 1.4), '#e8e8ff');
  for (const x of [160, 440]) {
    D.b(R(x - 7, 20, 14, 280), '#4a3020', { k: 4 }); D.line([[x - 80, 60], [x + 80, 60]], 5, '#3a2618');
    D.b(P([[x - 76, 64], [x + 76, 64], [x + 86, 150], [x + 50, 144], [x + 10, 160], [x - 30, 146], [x - 86, 152]], true), '#b8ac8c', { k: 12 });
    D.fill(C(x + 30, 110, 10), 'rgba(0,0,0,0.6)'); D.line([[x, 20], [x - 200, 290]], 1.2, 'rgba(10,8,6,0.7)'); D.line([[x, 20], [x + 200, 290]], 1.2, 'rgba(10,8,6,0.7)');
  }
  D.b(R(0, 290, 640, 70), '#5a3a24', { k: 8 }); for (let x = 0; x < 640; x += 40) D.line([[x, 290], [x, 360]], 1.2, 'rgba(0,0,0,0.5)');
  block(D, 0, 290, 640, 12, '#6a4a2c', { d: 6 });
  block(D, 260, 210, 100, 16, '#6a4a2c', { d: 6 });
  D.b(R(560, 200, 60, 90, 3), '#3a2416', { k: 6 }); D.fill(R(566, 206, 48, 84), '#0a0604');
  put(D, PLATFOLK.ambusher.draw, 580, 290, 0.24, true);
  put(D, PLATFOLK.gunner.draw, 440, 290, 0.24, true);
  put(D, BEASTS.parakeet.draw, 360, 150, 0.18, true);
  put(D, PLATFOLK.diver.draw, 300, 210 - 6, 0.2, false);
  for (const [x, y] of [[100, 250], [500, 240]]) { D.b(R(x - 7, y - 10, 14, 20, 2), '#6a4a20', { k: 2 }); D.glow(x, y, 80, '#ffb060', 0.55); D.fill(R(x - 4, y - 7, 8, 14), '#ffe0a0'); }
}
function pirateHold(D) {
  vgrad(D, ['#1a120c', '#120c08']);
  for (let x = -40; x < 680; x += 90) D.b(strip(bez([x, 0], [x + 30, 120], [x + 30, 240], [x, 360]), 9, 9), '#3a2616', { k: 4, lw: 2.4 });
  for (let y = 20; y < 360; y += 36) D.line([[0, y], [640, y]], 1.2, 'rgba(0,0,0,0.4)');
  block(D, 0, 300, 640, 60, '#4a3220', { grid: 40 });
  for (const [x, y] of [[40, 256], [84, 256], [62, 212]]) { D.b(E(x + 20, y + 22, 22, 24), '#6a4a2c', { k: 8 }); D.line([[x, y + 12], [x + 40, y + 12]], 2.6, '#3a3634'); D.line([[x, y + 34], [x + 40, y + 34]], 2.6, '#3a3634'); }
  block(D, 200, 230, 80, 16, '#5a3e26', { d: 6 }); block(D, 330, 180, 70, 16, '#5a3e26', { d: 6 }); block(D, 450, 240, 70, 16, '#5a3e26', { d: 6 });
  D.line([[240, 0], [240, 130]], 2, '#6a5a4a'); D.b(R(228, 130, 24, 30, 3), '#6a4a20', { k: 3 }); D.glow(240, 146, 200, '#ffb060', 0.45); D.fill(R(232, 136, 16, 20), '#ffe0a0');
  D.b(R(540, 250, 70, 50, 4), '#5a3a1a', { k: 6 }); D.b(R(540, 250, 70, 14, 3), '#6a4a24', { k: 2 }); D.fill(R(570, 262, 10, 12), '#c9a24a'); D.glow(575, 250, 60, '#ffd070', 0.4);
  put(D, PLATFOLK.diver.draw, 360, 180 - 6, 0.2, false);
  put(D, PLATFOLK.ambusher.draw, 480, 240 - 6, 0.2, true);
  motes(D, 30, '#ffe0b0', 100, 300);
}
function cabinArena(D) {
  vgrad(D, ['#1e140e', '#140c08']);
  for (let i = 0; i < 4; i++) { const x = 170 + i * 80; D.b(R(x, 50, 64, 110, 2), '#2a1a10', { k: 4 }); D.fill(R(x + 4, 54, 56, 102), '#1a2a4a'); D.glow(x + 32, 100, 60, '#9ab0e0', 0.3); D.line([[x + 32, 54], [x + 32, 156]], 3, '#2a1a10'); D.line([[x + 4, 105], [x + 60, 105]], 3, '#2a1a10'); }
  D.b(P([[0, 180], [640, 180], [640, 200], [0, 200]]), '#3a2414', { k: 3 });
  block(D, 0, 300, 640, 60, '#4a3020', { grid: 40 });
  D.b(R(40, 250, 80, 50, 4), '#5a3a1a', { k: 6 }); for (let i = 0; i < 8; i++) D.b(C(52 + i * 9, 248 - (i % 3) * 4, 6), '#e0b040', { k: 1, lw: 1.4, hatch: false }); D.glow(80, 250, 80, '#ffd070', 0.4);
  D.b(R(560, 200, 40, 100, 3), '#3a2416', { k: 5 });
  for (const x of [130, 510]) { D.line([[x, 0], [x, 40]], 2, '#6a5a4a'); D.b(R(x - 10, 40, 20, 26, 3), '#6a4a20', { k: 3 }); D.glow(x, 54, 160, '#ffb060', 0.45); D.fill(R(x - 6, 44, 12, 18), '#ffe0a0'); }
  put(D, PLATFOLK.blackbeard.draw, 430, 300, 0.42, true);
  for (let i = 0; i < 5; i++) D.line([[480 + i * 8, 250 + i * 6], [540 + i * 14, 250 + i * 6]], 2, 'rgba(255,220,180,0.25)');
  put(D, PLATFOLK.diver.draw, 220, 300, 0.2, false);
}

const SCENES = {
  salon: { name: 'The Grand Salon', sub: 'Hub · one screen, true 3D room', draw: salon },
  cards: { name: 'The Card Table', sub: 'Flats · dealer lit, room in shadow', draw: cardTable },
  cave: { name: 'The Cave', sub: 'Shallows · mold-lit grotto', draw(D) { caveBg(D); party(D, CREW, [B('crustacean'), { draw: B('pistolShrimp'), s: 0.9 }, F('lostDiver'), { draw: B('seaWorm'), s: 1.1 }]); motes(D, 50, '#bfffe0', 60, 300); } },
  caveBoss: { name: 'The Queen\'s Brood Chamber', sub: 'Cave boss room', draw(D) { caveBg(D, true); party(D, CREW, []); D.shadow(480, 318, 140, 14); put(D, B('crabQueen'), 490, 322, 0.78, true); put(D, B('lobster'), 610, 318, 0.36, true); motes(D, 50, '#ffc0d0', 60, 300); } },
  island: { name: 'The Sunken Island', sub: 'Shallows · drowned jungle', draw(D) { islandBg(D); party(D, CREW, [F('spearman'), B('warDog'), F('spearman'), F('shaman')]); motes(D, 40, '#fff8c0', 60, 300); } },
  islandBoss: { name: 'Altar of the Sun God', sub: 'Island boss room', draw(D) { islandBg(D); party(D, CREW, []); put(D, F('sunGod'), 480, 322, 0.72, true); put(D, F('coconutQueen'), 610, 318, 0.34, true); } },
  weeds: { name: 'The Weeds', sub: 'Shallows · kelp forest', draw(D) { weedsBg(D); party(D, CREW, [F('feralMerman'), { draw: B('giantOctopus'), s: 1.1 }, F('feralMerman'), F('enemySiren')]); motes(D, 50, '#d0fff0', 40, 300); } },
  weedsBoss: { name: 'Neptune\'s Court', sub: 'Weeds boss room', draw(D) { weedsBg(D); party(D, CREW, []); put(D, B('shark'), 560, 150, 0.4, true); put(D, F('neptune'), 470, 322, 0.74, true); put(D, B('eel'), 610, 330, 0.34, true); } },
  atlantis: { name: 'Atlantis', sub: 'Shallows · the drowned city', draw(D) { atlantisBg(D); party(D, CREW, [F('lostOne'), F('lostOne'), { draw: F('giantLostOne'), s: 1.1 }, F('cultist')]); motes(D, 40, '#c0e0ff', 40, 300); } },
  atlantisBoss: { name: 'The Sleeper\'s Temple', sub: 'Atlantis boss room', draw(D) { atlantisBg(D); party(D, CREW, []); put(D, B('cthulhu'), 480, 330, 0.9, true); put(D, B('alienHorror'), 620, 318, 0.3, true); } },
  pipes: { name: 'The Pipes', sub: 'Platform 1 · lamp-lit ducts, no enemies', draw: pipesLevel },
  hull: { name: 'The Hull', sub: 'Platform 2 · plating, open water, trench', draw: hullLevel },
  kraken: { name: 'The Trench Mouth', sub: 'Platform 2 boss · the Kraken', draw: krakenArena },
  pirate: { name: 'The Pirate Ship: Deck', sub: 'Platform 3 · masts, doors, barrels', draw: pirateDeck },
  hold: { name: 'The Pirate Ship: Hold', sub: 'Platform 3 · lantern-lit hold', draw: pirateHold },
  cabin: { name: 'Blackbeard\'s Cabin', sub: 'Platform 3 boss', draw: cabinArena },
};
