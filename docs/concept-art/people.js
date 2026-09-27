// Humanoid figures: chunky, top-heavy, heroic. All face right with feet at (165, 368).
function hum(D, o) {
  const g = Object.assign({ bs: [128, 160], be: [110, 208], bh: [118, 254], fs: [198, 158], fe: [228, 196], fh: [250, 214] }, o.pose || {});
  const skin = o.skin || '#c8997a', coat = o.coat || '#4a5a5e', pants = o.pants || '#3a3430', boots = o.boots || '#2a2220';
  const sleeve = o.sleeve || coat, glove = o.glove || skin, hs = o.hand || 11, hunch = o.hunch || 0;
  if (!o.noShadow) D.shadow(165, 366, 78, 13);
  o.back && o.back(D, g);
  if (!o.noLegs) {
    D.b(L(148, 252, 128, 308, 21, 15), pants); D.b(L(128, 308, 120, 352, 15, 12), pants);
    if (o.fins) D.b(P([[98, 352], [128, 346], [150, 372], [120, 366], [86, 374]], true), o.fins, { k: 3 });
    else D.b(P([[96, 346], [134, 340], [142, 369], [92, 370]], true), boots, { k: 4 });
  } else o.lower && o.lower(D, g);
  // back arm
  D.b(L(g.bs[0], g.bs[1], g.be[0], g.be[1], 18, 14), sleeve); D.b(L(g.be[0], g.be[1], g.bh[0], g.bh[1], 14, 11), sleeve);
  o.backHand && o.backHand(D, g);
  D.b(C(g.bh[0], g.bh[1], hs), glove, { k: 3 });
  if (!o.noLegs) {
    D.b(L(178, 252, 196, 302, 22, 16), pants); D.b(L(196, 302, 206, 350, 16, 13), pants);
    if (o.fins) D.b(P([[190, 350], [220, 346], [252, 372], [222, 366], [188, 374]], true), o.fins, { k: 3 });
    else if (o.bigBoots) D.b(P([[184, 322], [226, 322], [230, 350], [244, 356], [244, 372], [180, 372]], true), boots, { k: 5 });
    else D.b(P([[190, 344], [226, 344], [240, 369], [188, 371]], true), boots, { k: 4 });
  }
  const w = o.bulk || 1;
  const tp = [[110, 150], [210, 148], [202, 206], [192, 264], [132, 264], [120, 206]]
    .map(([x, y]) => [160 + (x - 160) * w, y + hunch * (264 - y) / 114]);
  if (o.long) D.b(P(o.hem || [[130, 238], [196, 238], [224, 326], [172, 336], [106, 324]], true), o.skirt || coat);
  D.b(P(tp, true), coat);
  o.front && o.front(D, g);
  if (o.belt) D.b(P([[128, 242], [196, 242], [195, 258], [129, 258]]), o.belt, { k: 2, hatch: false });
  const hx = 168 + (o.hx || 0), hy = 106 + hunch;
  if (!o.noNeck) D.b(L(162, 150 + hunch * 0.5, hx - 2, hy + 18, 13, 12), skin, { k: 3 });
  o.head ? o.head(D, hx, hy) : face(D, hx, hy, skin, o.faceOpts || {});
  // front arm
  D.b(L(g.fs[0], g.fs[1], g.fe[0], g.fe[1], 19, 15), sleeve); D.b(L(g.fe[0], g.fe[1], g.fh[0], g.fh[1], 15, 12), sleeve);
  o.weapon && o.weapon(D, g);
  if (!o.noHand) D.b(C(g.fh[0], g.fh[1], hs + 1), glove, { k: 3 });
  o.over && o.over(D, g);
}

// A craggy face in three-quarter profile, eyes lost in the shadow of the brow.
function face(D, hx, hy, skin, o = {}) {
  const head = P([[hx - 21, hy - 22], [hx + 14, hy - 26], [hx + 25, hy - 6], [hx + 27, hy + 6], [hx + 22, hy + 20], [hx + 8, hy + 31], [hx - 10, hy + 28], [hx - 22, hy + 8]], true);
  if (o.hairBack) D.b(P([[hx - 26, hy - 18], [hx + 6, hy - 30], [hx - 4, hy + 10], [hx - 20, hy + 60], [hx - 38, hy + 50], [hx - 32, hy + 10]], true), o.hairBack);
  D.b(head, skin);
  D.b(E(hx - 7, hy + 4, 5, 7), skin, { k: 2, lw: 2.4 });
  D.b(P([[hx + 21, hy - 3], [hx + 32, hy + 10], [hx + 22, hy + 13]]), skin, { k: 2, lw: 2.4 });
  if (o.hair) D.b(P([[hx - 23, hy - 6], [hx - 18, hy - 26], [hx + 10, hy - 30], [hx + 24, hy - 16], [hx + 8, hy - 18], [hx - 10, hy - 12]], true), o.hair, { k: 4 });
  if (o.stubble) D.clipDo(head, () => D.fill(E(hx + 10, hy + 20, 20, 12), rgba(o.stubble, 0.5)));
  if (o.beard) D.b(P([[hx - 14, hy + 8], [hx + 4, hy + 15], [hx + 26, hy + 14], [hx + 25, hy + 30], [hx + 10, hy + 50], [hx - 6, hy + 40], [hx - 18, hy + 18]], true), o.beard);
  if (o.mask) D.b(P([[hx - 6, hy - 18], [hx + 24, hy - 16], [hx + 30, hy + 6], [hx + 22, hy + 24], [hx + 2, hy + 22]], true), o.mask, { k: 3 });
  D.clipDo(head, () => {
    const c = D.c, g = c.createLinearGradient(0, hy - 30, 0, hy + (o.deep ? 14 : 7));
    g.addColorStop(0, 'rgba(8,6,9,0.55)'); g.addColorStop(0.55, 'rgba(8,6,9,0.95)'); g.addColorStop(0.8, 'rgba(8,6,9,0.9)'); g.addColorStop(1, 'rgba(8,6,9,0)');
    c.fillStyle = g; c.fillRect(hx - 4, hy - 30, 44, 50);
  });
  if (o.mask) { D.dot(hx + 10, hy - 2, 4, INK); D.dot(hx + 22, hy - 2, 3.4, INK); }
  if (o.glint) { D.dot(hx + 16, hy - 3, 1.6, o.glint); D.dot(hx + 25, hy - 3, 1.3, o.glint); }
  if (o.specs) { D.line([[hx + 6, hy - 3], [hx + 28, hy - 3]], 1.6, '#c9b27a'); D.ring(hx + 13, hy - 3, 4.5, '#c9b27a', 1.4); D.ring(hx + 24, hy - 3, 3.8, '#c9b27a', 1.4); }
  if (!o.beard && !o.mask) D.line([[hx + 12, hy + 19], [hx + 22, hy + 17]], 2);
}
function cap(D, hx, hy, col, band, brim) {
  D.b(P([[hx - 24, hy - 14], [hx - 20, hy - 34], [hx + 18, hy - 38], [hx + 30, hy - 22], [hx + 26, hy - 12]], true), col);
  if (band) D.b(P([[hx - 24, hy - 18], [hx + 28, hy - 20], [hx + 27, hy - 12], [hx - 24, hy - 10]]), band, { k: 2, hatch: false });
  D.b(P([[hx + 6, hy - 14], [hx + 30, hy - 16], [hx + 42, hy - 10], [hx + 26, hy - 8]], true), brim || INK, { k: 2 });
}
function blade(D, len, w, col) { D.b(P([[0, -w], [len * 0.8, -w * 1.1], [len, 0], [len * 0.8, w * 0.6], [0, w * 0.7]], true), col, { hi: 0.45, k: 3 }); }

// ================================ THE CREW ================================
const HEROES = {
  nurse: { name: 'Nurse', sub: 'Healer · bone-saw', draw(D) {
    hum(D, { skin: '#dcc0a2', coat: '#43363d', long: true, skirt: '#3a2e34', pants: '#2c2427', boots: '#221b1d', glove: '#8f877a',
      hem: [[128, 238], [196, 238], [230, 334], [200, 330], [176, 344], [150, 332], [102, 338]],
      pose: { fe: [232, 176], fh: [246, 132] },
      front(D) {
        D.b(P([[138, 156], [188, 154], [198, 250], [206, 334], [152, 340], [118, 326], [130, 250]], true), '#b8ae9c');
        D.fill(E(176, 290, 12, 18, 0.4), 'rgba(110,26,22,0.55)'); D.fill(E(150, 312, 7, 10), 'rgba(110,26,22,0.45)');
        D.b(P([[156, 172], [166, 172], [166, 182], [176, 182], [176, 192], [166, 192], [166, 202], [156, 202], [156, 192], [146, 192], [146, 182], [156, 182]]), '#8e2a24', { k: 2, hatch: false, lw: 2 });
      },
      head(D, hx, hy) {
        D.b(P([[hx - 44, hy - 14], [hx - 12, hy - 42], [hx + 24, hy - 36], [hx + 36, hy - 10], [hx + 30, hy + 22], [hx + 10, hy + 44], [hx - 34, hy + 64], [hx - 52, hy + 30]], true), '#9f988b');
        face(D, hx + 3, hy + 4, '#dcc0a2', { deep: true });
        D.b(P([[hx - 8, hy - 30], [hx + 28, hy - 30], [hx + 38, hy - 6], [hx + 24, hy - 14], [hx - 4, hy - 13]], true), '#aea796', { k: 3 });
        D.b(P([[hx + 6, hy - 38], [hx + 12, hy - 38], [hx + 12, hy - 33], [hx + 17, hy - 33], [hx + 17, hy - 28], [hx + 12, hy - 28], [hx + 12, hy - 23], [hx + 6, hy - 23], [hx + 6, hy - 28], [hx + 1, hy - 28], [hx + 1, hy - 33], [hx + 6, hy - 33]]), '#8e2a24', { k: 1, hatch: false, lw: 1.6 });
      },
      weapon(D, g) {
        D.tool(g.fh[0], g.fh[1], -1.9, D => {
          D.b(R(-14, -6, 34, 12, 4), '#5a3c28', { k: 2 });
          const t = [[20, -14], [104, -18], [112, -6]];
          for (let x = 108; x > 20; x -= 9) t.push([x, 8], [x - 4.5, 14]);
          t.push([20, 10]); D.b(P(t), '#a3a49c', { hi: 0.5, k: 3 });
          D.dot(30, -2, 3, INK); D.fill(E(80, 4, 14, 5), 'rgba(110,26,22,0.6)');
        });
      } });
  } },
  diver: { name: 'Diver', sub: 'Tank · harpoon lunge', draw(D) {
    hum(D, { skin: '#c8997a', coat: '#77725c', pants: '#6d6852', boots: '#3a3a3c', glove: '#5a4a38', bulk: 1.14, bigBoots: true, hand: 13, noNeck: true,
      back(D) { D.b(strip(bez([140, 96], [90, 90], [80, 170], [110, 230]), 7, 7), '#4a4438', { k: 3 }); },
      front(D) {
        D.b(P([[112, 142], [210, 142], [220, 166], [160, 184], [102, 166]], true), '#a8843a', { hi: 0.35 });
        for (let i = 0; i < 6; i++) D.dot(116 + i * 18, 160 + Math.abs(i - 2.5) * 3, 2.4, '#e0c070');
        D.b(P([[118, 234], [202, 234], [200, 250], [120, 250]]), '#4a3a2a', { k: 2, hatch: false });
      },
      head(D, hx, hy) {
        const cx = hx - 2, cy = hy + 4;
        D.b(C(cx, cy, 38), '#b28b3e', { hi: 0.5, k: 9 });
        D.b(C(cx + 20, cy + 6, 19), '#1c2628', { k: 3 });
        D.fill(C(cx + 20, cy + 6, 16), 'rgba(90,150,160,0.35)');
        D.line([[cx + 12, cy - 6], [cx + 20, cy + 4], [cx + 16, cy + 14]], 1.6, '#cfe6e6'); D.line([[cx + 20, cy + 4], [cx + 32, cy + 2]], 1.4, '#cfe6e6');
        D.ring(cx + 20, cy + 6, 18, '#8d6c2c', 5);
        D.b(C(cx - 6, cy - 30, 9), '#8d6c2c', { k: 2 }); D.b(C(cx - 22, cy + 8, 9), '#1c2628', { k: 2 }); D.ring(cx - 22, cy + 8, 8, '#8d6c2c', 3);
        for (const [x, y, r] of [[-30, -16, 4], [-26, -24, 3], [-34, -6, 3.5], [8, -34, 3], [14, -30, 4], [-14, 30, 3.5]])
          D.b(P([[cx + x - r, cy + y + r], [cx + x, cy + y - r * 1.4], [cx + x + r, cy + y + r]], true), '#cfc4ab', { k: 1, lw: 1.5, hatch: false });
      },
      weapon(D, g) {
        D.tool(g.fh[0], g.fh[1], -0.18, D => {
          D.b(R(-130, -5, 210, 10, 4), '#6a4a2e', { k: 2 });
          D.b(P([[76, -9], [120, 0], [76, 9], [84, 0]]), '#8f928e', { hi: 0.5, k: 3 });
          D.b(P([[88, -4], [98, -18], [96, -2]]), '#8f928e', { k: 1, lw: 2 });
          D.b(P([[88, 4], [98, 18], [96, 2]]), '#8f928e', { k: 1, lw: 2 });
        });
      } });
  } },
  captain: { name: 'Captain', sub: 'Leader · overhead cut', draw(D) {
    hum(D, { skin: '#c49276', coat: '#27304a', long: true, skirt: '#222a40', pants: '#2c2a2c', boots: '#1c1718', glove: '#3a2c24',
      hem: [[128, 236], [196, 236], [230, 330], [214, 318], [200, 338], [180, 326], [162, 342], [140, 326], [104, 334]],
      pose: { fe: [214, 150], fh: [236, 104] },
      front(D) {
        D.b(P([[158, 150], [190, 150], [184, 262], [164, 262]]), '#7a6a52', { k: 3 });
        for (let i = 0; i < 4; i++) D.b(C(150, 170 + i * 22, 3.5), '#c9a24a', { k: 1, lw: 1.5, hatch: false });
        D.b(E(122, 150, 18, 9, -0.2), '#c9a24a', { k: 2, hi: 0.4 });
        D.line([[106, 156], [108, 170]], 2, '#c9a24a'); D.line([[114, 158], [115, 172]], 2, '#c9a24a'); D.line([[122, 158], [122, 172]], 2, '#c9a24a');
      },
      head(D, hx, hy) {
        face(D, hx, hy, '#c49276', { beard: '#8f8a80', hair: '#8f8a80' });
        D.b(P([[hx - 28, hy - 16], [hx - 26, hy - 34], [hx - 12, hy - 44], [hx + 26, hy - 42], [hx + 36, hy - 26], [hx + 28, hy - 14]], true), '#1d2232');
        D.b(P([[hx - 28, hy - 20], [hx + 30, hy - 22], [hx + 29, hy - 13], [hx - 28, hy - 11]]), '#b8943e', { k: 2, hatch: false });
        D.b(P([[hx + 8, hy - 14], [hx + 32, hy - 16], [hx + 46, hy - 8], [hx + 26, hy - 6]], true), INK, { k: 2 });
        D.b(E(hx + 6, hy - 32, 6, 5), '#c9a24a', { k: 1, lw: 1.6 });
      },
      weapon(D, g) {
        D.tool(g.fh[0], g.fh[1], -2.1, D => {
          D.b(E(-4, 0, 16, 12), '#b8943e', { k: 2, hatch: false });
          D.b(c => { c.moveTo(6, -6); c.quadraticCurveTo(70, -22, 128, -4); c.quadraticCurveTo(76, -4, 6, 6); c.closePath(); }, '#a7aaa3', { hi: 0.55, k: 3 });
        });
      },
      over(D) { D.b(E(206, 146, 20, 10, 0.3), '#c9a24a', { k: 2, hi: 0.4 }); } });
  } },
  mechanic: { name: 'Mechanic', sub: 'Bruiser · heavy swing', draw(D) {
    hum(D, { skin: '#c28e6c', coat: '#56504a', pants: '#3e3a34', boots: '#2a2420', glove: '#5a5e62', sleeve: '#6a6258', bulk: 1.24, hand: 16,
      pose: { be: [104, 214], bh: [110, 262], fe: [222, 150], fh: [212, 96] },
      front(D) {
        D.b(P([[124, 172], [198, 172], [206, 264], [214, 330], [116, 330], [118, 264]], true), '#6e4c30');
        D.line([[124, 172], [140, 150]], 3.2); D.line([[198, 172], [184, 150]], 3.2);
        D.b(R(146, 206, 30, 22, 3), '#5a3e28', { k: 2 });
        D.fill(E(170, 300, 16, 9), 'rgba(20,16,14,0.55)'); D.fill(E(136, 280, 9, 6), 'rgba(20,16,14,0.45)');
      },
      head(D, hx, hy) {
        face(D, hx, hy, '#c28e6c', { stubble: '#2a2018', hair: '#3a2c22' });
        D.b(R(hx - 22, hy - 26, 52, 8, 3), '#3a2c22', { k: 2, hatch: false });
        D.ring(hx + 2, hy - 26, 8, '#9c7c3c', 4); D.ring(hx + 22, hy - 26, 7, '#9c7c3c', 4);
        D.fill(C(hx + 2, hy - 26, 8), 'rgba(160,200,190,0.5)'); D.fill(C(hx + 22, hy - 26, 7), 'rgba(160,200,190,0.5)');
      },
      weapon(D, g) {
        D.tool(g.fh[0], g.fh[1], -1.35, D => {
          D.b(R(-60, -8, 150, 16, 5), '#5c5f60', { hi: 0.3 });
          D.b(R(-40, -12, 50, 24, 4), '#8a5a2e', { k: 3 });
          D.b(P([[80, -30], [124, -34], [134, -14], [108, -10], [110, 10], [134, 14], [124, 34], [80, 30]], true), '#6f7275', { hi: 0.4 });
          D.b(R(20, 10, 60, 10, 4), '#9c7c3c', { k: 2, hatch: false });
        });
      } });
  } },
  whaler: { name: 'Whaler', sub: 'Striker · harpoon gun', draw(D) {
    hum(D, { skin: '#c49574', coat: '#8d7a38', long: true, skirt: '#7e6c30', pants: '#3a342a', boots: '#1f1a17', glove: '#4a3a2a',
      pose: { be: [140, 214], bh: [196, 232], fe: [224, 212], fh: [236, 226] },
      back(D) { D.b(E(104, 214, 22, 30), '#8a7a56'); D.line([[88, 200], [120, 200]], 2); D.line([[86, 214], [122, 214]], 2); D.line([[88, 228], [120, 228]], 2); },
      front(D) { D.line([[160, 150], [160, 262]], 2.4); for (let y = 164; y < 262; y += 22) D.b(R(154, y, 12, 6, 2), '#3a3020', { k: 1, lw: 1.6, hatch: false }); },
      head(D, hx, hy) {
        face(D, hx, hy, '#c49574', { beard: '#3a2a1e' });
        D.b(P([[hx - 40, hy - 2], [hx - 22, hy - 38], [hx + 20, hy - 40], [hx + 36, hy - 14], [hx + 50, hy - 6], [hx + 24, hy - 10], [hx - 10, hy - 12], [hx - 30, hy + 18]], true), '#a08c3e');
      },
      weapon(D, g) {
        D.tool(g.fh[0] - 30, g.fh[1] + 4, -0.06, D => {
          D.b(P([[-40, -6], [10, -10], [20, 6], [-30, 20], [-46, 14]], true), '#5e3e26');
          D.b(R(10, -12, 110, 14, 3), '#4a4c4e', { hi: 0.4 });
          D.b(R(116, -9, 34, 8, 2), '#6a4a2e', { k: 2 });
          D.b(P([[150, -16], [182, -5], [150, 6], [156, -5]]), '#9a9d98', { hi: 0.5, k: 2 });
          D.line([[10, 2], [-4, 40], [20, 80]], 2, '#b8a878', true);
        });
      } });
  } },
  stowaway: { name: 'Stowaway', sub: 'Rogue · dirty tricks', draw(D) {
    hum(D, { skin: '#c79a78', coat: '#4a4434', sleeve: '#8a7c62', pants: '#4a4034', boots: '#2e2620', hunch: 18, hx: 10, bulk: 0.96,
      pose: { bs: [130, 172], be: [118, 214], bh: [134, 246], fs: [196, 170], fe: [226, 214], fh: [250, 236] },
      front(D) { D.b(P([[120, 180], [150, 176], [148, 262], [130, 262]]), '#8a7c62', { k: 3 }); D.line([[132, 200], [146, 214]], 1.6); D.line([[182, 190], [192, 204]], 1.6); },
      backHand(D, g) {
        D.tool(g.bh[0], g.bh[1], -1.1, D => {
          D.b(P([[-10, -9], [30, -10], [40, -5], [56, -4], [56, 4], [40, 5], [30, 10], [-10, 9]], true), '#3c5a36', { hi: 0.55, k: 3 });
          D.b(R(56, -4, 8, 8, 1), '#6a4a2e', { k: 1, lw: 2, hatch: false });
        });
      },
      head(D, hx, hy) {
        face(D, hx, hy, '#c79a78', { stubble: '#3a2c20' });
        D.b(P([[hx - 25, hy - 6], [hx - 20, hy - 32], [hx + 2, hy - 42], [hx + 24, hy - 30], [hx + 28, hy - 14], [hx - 4, hy - 14]], true), '#6e2e26');
        D.b(C(hx - 14, hy - 40, 6), '#6e2e26', { k: 2 });
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], 0.25, D => { D.b(R(-8, -5, 18, 10, 3), '#3a2a1e', { k: 2 }); blade(D, 46, 6, '#9a9a92'); }); } });
  } },
  merman: { name: 'Merman', sub: 'Skirmisher · lure & spear', draw(D) {
    const skin = '#3d6c64';
    hum(D, { skin, coat: skin, pants: '#35605a', glove: skin, fins: '#6ea8a0', bulk: 1.05,
      back(D) { D.b(P([[112, 170], [70, 150], [80, 200], [108, 214]], true), rgba('#8ad0c4', 0.6), { k: 3, hatch: false, lw: 2 }); },
      front(D) {
        D.scales(P([[110, 150], [210, 148], [202, 206], [192, 264], [132, 264], [120, 206]], true), 110, 150, 214, 266, 14, 'rgba(10,30,28,0.55)');
        D.b(P([[118, 160], [200, 234], [196, 246], [112, 172]]), '#4a3a28', { k: 2, hatch: false });
        D.b(P([[128, 240], [196, 240], [198, 256], [126, 256]]), '#4a3a28', { k: 2, hatch: false });
      },
      head(D, hx, hy) {
        D.b(P([[hx - 30, hy - 4], [hx - 20, hy - 46], [hx - 6, hy - 30], [hx + 4, hy - 50], [hx + 12, hy - 26]], true), rgba('#8ad0c4', 0.75), { k: 3, hatch: false, lw: 2 });
        face(D, hx, hy, skin, { deep: true });
        D.scales(E(hx, hy, 26, 30), hx - 26, hy - 26, hx + 30, hy + 30, 9, 'rgba(10,30,28,0.5)');
        D.b(P([[hx - 16, hy + 10], [hx - 30, hy + 2], [hx - 26, hy + 20]], true), '#6ea8a0', { k: 1, lw: 2 });
        D.line([[hx + 6, hy - 22], [hx + 20, hy - 58], [hx + 50, hy - 50], [hx + 58, hy - 22]], 3, INK, true);
        D.glow(hx + 58, hy - 18, 44, '#9ff0d8', 0.8); D.b(C(hx + 58, hy - 18, 6), '#dffff0', { k: 1, lw: 2, hatch: false });
      },
      over(D, g) { D.b(P([[g.fe[0] - 6, g.fe[1] - 8], [g.fe[0] - 30, g.fe[1] - 34], [g.fe[0] + 8, g.fe[1] - 12]], true), rgba('#8ad0c4', 0.7), { k: 2, hatch: false, lw: 2 }); },
      weapon(D, g) {
        D.tool(g.fh[0], g.fh[1], -0.3, D => {
          D.b(R(-120, -4, 196, 8, 3), '#d8b8a0', { k: 2 });
          D.b(P([[70, -12], [84, -6], [120, -2], [84, 4], [70, 12], [78, 0]], true), '#c46a5a', { k: 3 });
        });
      } });
  } },
  queen: { name: 'Dethroned Queen', sub: 'Support · island hexes', draw(D) {
    const skin = '#7a5238';
    hum(D, { skin, coat: '#8a6a54', long: true, skirt: '#7c5a6a', pants: '#6a4a5a', boots: '#4a3040', glove: skin, sleeve: skin,
      hem: [[126, 236], [198, 236], [236, 360], [200, 350], [180, 366], [150, 352], [96, 364]],
      pose: { fe: [224, 200], fh: [238, 236], be: [114, 206], bh: [132, 244] },
      back(D) { D.b(P([[120, 110], [150, 96], [140, 200], [120, 260], [96, 240], [106, 160]], true), '#241a18'); },
      front(D) {
        D.b(P([[122, 170], [200, 166], [196, 214], [126, 216]], true), '#a38a6a');
        for (let i = 0; i < 7; i++) D.b(P([[128 + i * 10, 150 + (i % 2) * 2], [133 + i * 10, 166], [138 + i * 10, 150]], true), '#e8dcc0', { k: 1, lw: 1.6, hatch: false });
        D.line([[128, 226], [196, 226]], 2.4, '#c9a24a');
      },
      head(D, hx, hy) {
        face(D, hx, hy, skin, { hairBack: '#241a18', deep: true });
        const cr = [[hx - 22, hy - 18], [hx - 20, hy - 44], [hx - 10, hy - 28], [hx - 2, hy - 54], [hx + 6, hy - 30], [hx + 12, hy - 40], [hx + 14, hy - 34], [hx + 20, hy - 30], [hx + 26, hy - 20]];
        D.b(P(cr), '#e3cfb0', { hi: 0.4, k: 3 });
        D.line([[hx - 2, hy - 42], [hx + 4, hy - 32], [hx - 2, hy - 26]], 1.8);
        D.line([[hx + 20, hy - 8], [hx + 22, hy + 18]], 2, '#e8dcc0');
      },
      weapon(D, g) {
        D.b(R(g.fh[0] - 5, 70, 10, 296, 3), '#5a3c26', { k: 2 });
        D.b(P([[g.fh[0] - 16, 74], [g.fh[0] - 12, 50], [g.fh[0] + 12, 48], [g.fh[0] + 18, 70], [g.fh[0] + 8, 88], [g.fh[0] - 8, 88]], true), '#e3d8c0', { k: 3 });
        D.dot(g.fh[0] - 3, 66, 4, INK); D.dot(g.fh[0] + 9, 66, 3.5, INK);
        for (let i = 0; i < 3; i++) D.b(P([[g.fh[0] - 6, 92], [g.fh[0] - 24 + i * 6, 136 + i * 6], [g.fh[0] - 12 + i * 6, 134 + i * 6]], true), ['#8e2a24', '#c9a24a', '#2e5a52'][i], { k: 1, lw: 1.6, hatch: false });
      } });
  } },
  robot: { name: 'Automaton', sub: 'Guardian · furnace heart', draw(D) {
    const iron = '#5c6064';
    hum(D, { skin: iron, coat: iron, pants: '#4c5054', boots: '#3a3d40', glove: '#7a6040', sleeve: '#6a6e72', bulk: 1.2, hand: 14, noNeck: true,
      back(D) { D.b(R(96, 120, 22, 70, 4), '#9a6038', { k: 3 }); D.b(C(107, 116, 10), '#9a6038', { k: 2 }); },
      front(D) {
        D.b(R(136, 176, 50, 46, 6), '#2a2220');
        D.glow(161, 199, 70, '#ff8a3a', 0.9);
        D.fill(R(140, 180, 42, 38, 4), '#ffb050');
        for (let x = 142; x < 184; x += 9) D.b(R(x, 178, 4, 42, 1), '#3a3d40', { k: 1, lw: 1.4, hatch: false });
        D.b(strip(bez([112, 160], [140, 140], [190, 140], [206, 170]), 6, 6), '#b8703e', { k: 3, hi: 0.4 });
        for (let i = 0; i < 6; i++) { D.dot(124 + i * 16, 238, 2.4, '#1e2022'); D.dot(126, 160 + i * 16, 2.4, '#1e2022'); }
      },
      head(D, hx, hy) {
        D.b(R(143, 138, 38, 20, 3), '#4c5054', { k: 2 });
        D.b(P([[hx - 26, hy - 26], [hx + 24, hy - 30], [hx + 30, hy + 20], [hx - 24, hy + 26]], true), '#6a6e72', { hi: 0.3 });
        D.b(R(hx - 4, hy - 8, 32, 8, 2), '#1a1414', { k: 1 }); D.glow(hx + 14, hy - 4, 40, '#ffaa44', 0.9); D.fill(R(hx, hy - 6, 26, 4, 2), '#ffd080');
        for (const [x, y] of [[-20, -20], [18, -24], [-18, 18], [22, 14]]) D.dot(hx + x, hy + y, 2.5, '#1e2022');
        D.b(R(hx - 30, hy - 40, 10, 16, 2), '#b8703e', { k: 2 }); D.fill(C(hx - 25, hy - 46, 6), 'rgba(200,200,190,0.4)'); D.fill(C(hx - 30, hy - 58, 9), 'rgba(200,200,190,0.25)');
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], 0.1, D => { D.b(R(0, -12, 40, 24, 4), '#b8703e', { k: 3 }); D.b(R(36, -20, 30, 40, 4), '#4c5054', { hi: 0.3 }); }); } });
  } },
  octopus: { name: 'Octopus', sub: 'Controller · ink & grapple', draw(D) {
    const sk = '#8a3e4c';
    D.shadow(165, 366, 90, 14);
    const arms = [[[150, 250], [120, 300], [70, 320], [60, 364]], [[160, 256], [150, 320], [110, 350], [96, 368]], [[176, 256], [196, 320], [230, 340], [252, 366]], [[184, 250], [230, 290], [270, 320], [280, 364]], [[168, 258], [166, 320], [170, 350], [184, 370]]];
    for (const a of arms) {
      const sp = bez(...a); D.b(strip(sp, 16, 3), sk);
      for (let i = 4; i < sp.length - 2; i += 3) { const f = spineFrame(sp, i); D.b(C(f.x - f.nx * 9 * (1 - i / sp.length), f.y - f.ny * 9 * (1 - i / sp.length), 3.5 * (1 - i / sp.length) + 1), '#e0a8a8', { k: 1, lw: 1.2, hatch: false }); }
    }
    D.b(P([[118, 180], [210, 176], [214, 250], [116, 254]], true), '#6a5a3a');
    D.b(P([[112, 176], [214, 172], [220, 190], [106, 194]], true), '#a8843a', { hi: 0.4 });
    const sp1 = bez([200, 200], [250, 190], [260, 140], [240, 100]); D.b(strip(sp1, 14, 4), sk);
    D.tool(240, 108, -1.2, D => { D.b(R(-6, -5, 16, 10, 2), '#3a2a1e', { k: 2 }); blade(D, 44, 6, '#9a9a92'); });
    const sp2 = bez([122, 200], [80, 210], [70, 250], [90, 280]); D.b(strip(sp2, 14, 4), sk);
    D.b(R(76, 280, 26, 34, 4), '#6a5020', { k: 3 }); D.glow(89, 297, 50, '#ffcc66', 0.7); D.fill(R(80, 286, 18, 22, 2), '#ffe0a0');
    D.b(P([[112, 120], [118, 60], [150, 22], [196, 26], [226, 70], [224, 130], [206, 180], [124, 180]], true), sk, { k: 12 });
    D.b(E(178, 70, 30, 34), rgba('#c8e6f0', 0.14), { flat: true, lw: 0 });
    D.fill(E(172, 146, 48, 12), 'rgba(10,4,6,0.85)');
    D.b(E(142, 148, 11, 8), '#d8c060', { k: 2 }); D.b(E(204, 146, 11, 8), '#d8c060', { k: 2 });
    D.fill(R(138, 145, 9, 3), INK); D.fill(R(200, 143, 9, 3), INK);
    D.fill(E(172, 162, 50, 10), 'rgba(8,4,6,0.6)');
    for (const [x, y, r] of [[140, 60, 6], [200, 50, 5], [160, 90, 4], [210, 100, 6]]) D.fill(C(x, y, r), rgba('#5a2230', 0.7));
    D.b(c => { c.moveTo(178 + 64, 90); c.arc(178, 90, 64, 0, TAU); c.moveTo(178 + 60, 90); c.arc(178, 90, 60, 0, TAU, true); }, rgba('#cfe8ee', 0.5), { k: 1, hatch: false, lw: 1.6 });
    D.line([[140, 52], [150, 70], [144, 84]], 1.4, '#e8f6f6');
  } },
  siren: { name: 'Siren', sub: 'Debuffer · drowning song', draw(D) { sirenFig(D, { skin: '#9ab0a0', tail: '#4e7a6c', fin: '#b0e0d8', hair: '#1e2a24' }); } },
  wisp: { name: 'Wisp', sub: 'Mystic · cold light', draw(D) {
    const c = D.c;
    D.glow(165, 220, 170, '#5a9ad8', 0.45);
    const body = P([[164, 40], [210, 70], [226, 130], [240, 230], [262, 330], [236, 318], [222, 350], [196, 334], [176, 364], [156, 336], [130, 356], [118, 322], [86, 336], [100, 240], [110, 130], [124, 70]], true);
    D.b(body, rgba('#274868', 0.72), { k: 16, dark: 0.3 });
    D.clipDo(body, () => {
      const g = c.createLinearGradient(0, 40, 0, 360); g.addColorStop(0, 'rgba(160,220,255,0.3)'); g.addColorStop(1, 'rgba(20,40,70,0)');
      c.fillStyle = g; c.fillRect(0, 0, 330, 400);
    });
    D.b(P([[134, 90], [168, 70], [200, 90], [204, 140], [168, 158], [132, 140]], true), '#0a1018', { k: 3 });
    D.eyes([[156, 114], [184, 112]], 2.4, '#bfe8ff');
    for (let i = 0; i < 160; i++) {
      const t = D.rand(), x = 165 + (D.rand() - 0.5) * (80 + t * 170), y = 60 + t * 300;
      D.dot(x, y, D.R(0.6, 2.2), rgba('#cfefff', D.R(0.3, 0.95)));
      if (i % 9 === 0) D.glow(x, y, 12, '#9fdcff', 0.5);
    }
    D.b(L(120, 190, 70, 240, 14, 8), rgba('#274868', 0.8), { k: 5 });
    D.b(L(212, 190, 262, 170, 14, 8), rgba('#274868', 0.8), { k: 5 });
    D.glow(270, 166, 60, '#9fdcff', 0.9); D.dot(270, 166, 5, '#e8faff');
  } },
};

function sirenFig(D, p) {
  hum(D, { skin: p.skin, coat: p.skin, glove: p.skin, noLegs: true, noShadow: true, bulk: 0.9,
    pose: { be: [104, 200], bh: [96, 240], fe: [232, 182], fh: [258, 170] },
    lower(D) {
      D.shadow(170, 366, 96, 12);
      const sp = bez([160, 250], [150, 330], [220, 350], [280, 330]);
      const tail = strip(sp, t => 34 - 26 * t, 0); D.b(tail, p.tail);
      D.scales(tail, 120, 240, 290, 370, 12, 'rgba(10,20,18,0.5)');
      const fin = P([[276, 330], [316, 290], [308, 320], [322, 352], [300, 344], [290, 362]], true);
      D.b(fin, rgba(p.fin, 0.55), { k: 3, hatch: false, lw: 2 });
      for (const [x, y] of [[300, 316], [306, 340]]) D.fill(C(x, y, 4), 'rgba(0,0,0,0.6)');
      D.b(P([[196, 346], [214, 372], [232, 348]], true), rgba(p.fin, 0.5), { k: 2, hatch: false, lw: 2 });
    },
    back(D) { D.b(P([[140, 110], [180, 90], [170, 200], [150, 270], [110, 260], [120, 170]], true), p.hair); },
    front(D) {
      D.line([[130, 168], [160, 196], [196, 168]], 2, '#e8dcc0', true);
      for (let i = 0; i < 7; i++) D.b(P([[134 + i * 9, 170 + Math.sin(i / 6 * Math.PI) * 24], [138 + i * 9, 190 + Math.sin(i / 6 * Math.PI) * 24], [142 + i * 9, 170 + Math.sin(i / 6 * Math.PI) * 24]], true), '#e8dcc0', { k: 1, lw: 1.4, hatch: false });
      D.b(P([[110, 170], [80, 150], [72, 196], [104, 210]], true), rgba(p.fin, 0.5), { k: 2, hatch: false, lw: 2 });
    },
    head(D, hx, hy) {
      face(D, hx, hy, p.skin, { hairBack: p.hair, deep: true, glint: p.eye });
      D.b(P([[hx - 26, hy - 6], [hx - 20, hy - 28], [hx + 8, hy - 32], [hx + 26, hy - 18], [hx + 20, hy - 10], [hx - 6, hy - 18], [hx - 10, hy + 20]], true), p.hair, { k: 4 });
      D.b(P([[hx - 14, hy + 2], [hx - 36, hy - 18], [hx - 30, hy + 14]], true), rgba(p.fin, 0.6), { k: 2, hatch: false, lw: 2 });
    },
    over(D, g) { for (let i = 0; i < 4; i++) D.b(P([[g.fh[0] + 4, g.fh[1] - 8 + i * 5], [g.fh[0] + 24, g.fh[1] - 16 + i * 8], [g.fh[0] + 8, g.fh[1] - 2 + i * 5]], true), '#e8e0cc', { k: 1, lw: 1.4, hatch: false }); } });
}
