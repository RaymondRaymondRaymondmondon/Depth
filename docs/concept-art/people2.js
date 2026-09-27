// The Nautilus's hands, the dealer, and every human (or once-human) foe.
function diverHelmet(D, cx, cy, o = {}) {
  D.b(C(cx, cy, 38), o.brass || '#b28b3e', { hi: 0.4 });
  D.b(C(cx + 20, cy + 6, 19), '#1c2628', { k: 3 });
  if (o.glow) { D.glow(cx + 20, cy + 6, 50, o.glow, 0.9); D.fill(C(cx + 20, cy + 6, 14), rgba(o.glow, 0.6)); }
  D.ring(cx + 20, cy + 6, 18, sh(o.brass || '#b28b3e', 0.8), 5);
  if (o.broken) { D.fill(P([[cx + 8, cy - 8], [cx + 30, cy - 10], [cx + 20, cy + 6], [cx + 34, cy + 20], [cx + 10, cy + 18]]), 'rgba(0,0,0,0.9)'); if (o.glow) D.eyes([[cx + 20, cy + 4]], 2.4, o.glow); }
}

const SALON = {
  helmsman: { name: 'Helmsman', sub: 'At the wheel', draw(D) {
    hum(D, { skin: '#c49574', coat: '#2e3a4c', pants: '#2a2a2e', boots: '#1c1718',
      pose: { fe: [224, 186], fh: [246, 178], be: [118, 204], bh: [140, 238] },
      front(D) { for (let y = 164; y < 262; y += 12) D.line([[124, y], [196, y - 2]], 1.2, 'rgba(0,0,0,0.35)'); D.b(P([[118, 148], [204, 146], [200, 164], [122, 166]], true), '#3c4a60', { k: 3 }); },
      head(D, hx, hy) { face(D, hx, hy, '#c49574', { beard: '#6a4a30' }); cap(D, hx, hy, '#1e2434', '#e8e0d0'); },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -0.4, D => { D.b(R(-8, -7, 60, 14, 3), '#7a5a2e', { k: 3 }); D.b(R(46, -9, 22, 18, 3), '#b8943e', { hi: 0.4, k: 3 }); }); } });
  } },
  radio: { name: 'Radio Operator', sub: 'Listening to the deep', draw(D) {
    hum(D, { skin: '#d2a684', coat: '#4b4a3e', sleeve: '#8a8676', pants: '#3a3830', boots: '#221c18',
      pose: { fe: [222, 214], fh: [240, 236], be: [118, 206], bh: [134, 246] },
      front(D) { D.b(P([[146, 150], [176, 150], [172, 262], [150, 262]]), '#8a8676', { k: 3 }); D.b(R(154, 150, 14, 30, 2), '#3a2a2a', { k: 2, hatch: false }); },
      head(D, hx, hy) {
        face(D, hx, hy, '#d2a684', { hair: '#2a1e18' });
        D.b(strip(bez([hx - 16, hy + 2], [hx - 20, hy - 42], [hx + 20, hy - 44], [hx + 14, hy - 6]), 3.5, 3.5), '#3a3a3a', { k: 1 });
        D.b(E(hx - 12, hy + 4, 9, 12), '#2a2a2c', { k: 3 }); D.line([[hx - 12, hy + 14], [hx - 20, hy + 70], [hx + 40, hy + 120]], 2, '#1a1a1a', true);
      },
      weapon(D, g) { D.b(R(g.fh[0] - 6, g.fh[1] - 4, 30, 38, 2), '#d8ceb4', { k: 3 }); for (let i = 0; i < 4; i++) D.line([[g.fh[0], g.fh[1] + 6 + i * 7], [g.fh[0] + 18, g.fh[1] + 6 + i * 7]], 1, '#555'); } });
  } },
  engineer: { name: 'Engineer', sub: 'Keeps the reactor fed', draw(D) {
    hum(D, { skin: '#b88866', coat: '#4a5560', pants: '#4a5560', boots: '#241e1a', glove: '#3a3430', bulk: 1.1,
      front(D) {
        D.b(P([[128, 176], [196, 176], [196, 264], [128, 264]], true), '#56626e'); D.line([[132, 176], [138, 148]], 3); D.line([[192, 176], [186, 148]], 3);
        D.fill(E(150, 220, 14, 10), 'rgba(12,10,8,0.6)'); D.fill(E(184, 250, 10, 7), 'rgba(12,10,8,0.5)');
      },
      head(D, hx, hy) { face(D, hx, hy, '#b88866', { stubble: '#1a1410' }); D.fill(E(hx + 16, hy + 12, 8, 5), 'rgba(10,8,6,0.5)'); cap(D, hx, hy, '#3a4048', null, '#2a2e34'); },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], 0.9, D => { D.b(R(-10, -5, 60, 10, 3), '#6f7275', { hi: 0.4, k: 2 }); D.b(P([[46, -14], [66, -12], [70, -4], [58, 0], [70, 4], [66, 12], [46, 14]], true), '#6f7275', { k: 3 }); }); } });
  } },
  professor: { name: 'Professor', sub: 'Naturalist of the salon', draw(D) {
    hum(D, { skin: '#dcb898', coat: '#3e3530', long: true, skirt: '#3a302c', pants: '#2c2826', boots: '#1a1614',
      pose: { fe: [214, 196], fh: [232, 184] },
      front(D) { D.b(P([[148, 150], [180, 150], [176, 244], [152, 244]]), '#6a5a40', { k: 3 }); D.b(P([[152, 150], [176, 150], [164, 176]]), '#e8e0cc', { k: 1, hatch: false }); D.line([[156, 206], [176, 214]], 1.6, '#c9a24a'); },
      head(D, hx, hy) {
        face(D, hx, hy, '#dcb898', { hair: '#b8b0a4', specs: true });
        D.b(P([[hx - 12, hy + 2], [hx + 2, hy + 6], [hx + 2, hy + 26], [hx - 10, hy + 22]], true), '#c8c0b4', { k: 2 });
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -0.2, D => { D.b(R(-10, -26, 44, 34, 3), '#5a2a22', { k: 4 }); D.b(R(-8, -24, 40, 4, 1), '#e8dcc0', { k: 1, hatch: false, lw: 1.4 }); D.line([[2, -18], [22, -18]], 1.6, '#c9a24a'); }); } });
  } },
  steward: { name: 'Steward', sub: 'Tea at any depth', draw(D) {
    hum(D, { skin: '#c69878', coat: '#cfc8b6', pants: '#1e1c20', boots: '#141214',
      pose: { fe: [226, 190], fh: [252, 176] },
      front(D) { for (let i = 0; i < 4; i++) D.b(C(172, 168 + i * 22, 3), '#c9a24a', { k: 1, lw: 1.5, hatch: false }); D.b(P([[152, 150], [176, 150], [164, 168]]), INK, { k: 1, hatch: false }); },
      head(D, hx, hy) { face(D, hx, hy, '#c69878', { hair: '#1e1612' }); },
      weapon(D, g) {
        D.b(E(g.fh[0] + 6, g.fh[1] - 4, 40, 7), '#b8b4ac', { hi: 0.5, k: 3 });
        D.b(P([[g.fh[0] - 8, g.fh[1] - 30], [g.fh[0] + 12, g.fh[1] - 30], [g.fh[0] + 8, g.fh[1] - 10], [g.fh[0] - 4, g.fh[1] - 10]], true), '#e8e4dc', { k: 3 });
        D.fill(C(g.fh[0] + 30, g.fh[1] - 40, 6), 'rgba(220,220,210,0.3)');
      } });
  } },
  orderly: { name: 'Orderly', sub: 'Sick bay attendant', draw(D) {
    hum(D, { skin: '#d4ae8e', coat: '#bdb8aa', long: true, skirt: '#b0aa9a', pants: '#3a3a3a', boots: '#262424', glove: '#8a6a4a',
      front(D) { D.fill(E(176, 230, 10, 16, 0.3), 'rgba(110,26,22,0.45)'); D.fill(E(150, 300, 8, 6), 'rgba(110,26,22,0.4)'); D.b(R(128, 170, 22, 26, 2), '#a8a294', { k: 2, hatch: false }); },
      head(D, hx, hy) { face(D, hx, hy, '#d4ae8e', { hair: '#5a4030' }); D.b(P([[hx + 4, hy + 4], [hx + 30, hy + 2], [hx + 32, hy + 24], [hx + 10, hy + 26]], true), '#dcd6c8', { k: 2 }); },
      weapon(D, g) { D.b(E(g.fh[0] + 10, g.fh[1] + 6, 14, 12), '#e8e2d2', { k: 3 }); D.ring(g.fh[0] + 10, g.fh[1] + 6, 5, '#c8c0ae', 3); } });
  } },
  cat: { name: 'Ship\'s Cat', sub: 'Owns the salon', draw(D) {
    D.shadow(170, 366, 70, 11);
    const tail = bez([214, 360], [270, 360], [284, 320], [262, 290]); D.b(strip(tail, 11, 6), '#1e1c22');
    D.b(P([[122, 366], [118, 310], [138, 262], [182, 250], [214, 290], [222, 366]], true), '#1e1c22', { k: 16 });
    D.b(P([[150, 270], [178, 268], [188, 320], [170, 360], [150, 330]], true), '#e8e2d6', { k: 6 });
    D.b(E(136, 360, 16, 9), '#e8e2d6', { k: 3 }); D.b(E(188, 360, 16, 9), '#e8e2d6', { k: 3 });
    D.b(P([[128, 232], [132, 196], [148, 214], [178, 212], [192, 190], [198, 232], [186, 262], [140, 264]], true), '#1e1c22', { k: 12 });
    D.b(P([[152, 240], [172, 240], [176, 262], [150, 262]], true), '#e8e2d6', { k: 3 });
    D.eyes([[150, 230], [178, 230]], 3, '#e8c040'); D.fill(E(150, 230, 1.2, 3.4), INK); D.fill(E(178, 230, 1.2, 3.4), INK);
    for (const s of [-1, 1]) for (let i = 0; i < 3; i++) D.line([[164 + s * 8, 246 + i * 3], [164 + s * 36, 240 + i * 6]], 1, '#cfc8b8');
  } },
};

const DEALER = { name: 'The Dealer', sub: 'Across the card table', draw(D) {
  hum(D, { skin: '#cdbba4', coat: '#2a2024', long: true, skirt: '#241c20', pants: '#1e1a1c', boots: '#141012', glove: '#cdbba4', bulk: 0.92,
    hem: [[128, 238], [196, 238], [214, 364], [106, 364]],
    pose: { fe: [224, 214], fh: [246, 200], be: [112, 212], bh: [132, 240] },
    front(D) { D.b(P([[150, 150], [178, 150], [172, 250], [154, 250]]), '#4a2a2e', { k: 3 }); D.line([[160, 208], [180, 222]], 1.6, '#c9a24a'); D.b(C(180, 222, 4), '#c9a24a', { k: 1, hatch: false }); },
    head(D, hx, hy) {
      face(D, hx, hy, '#cdbba4', { deep: true, glint: '#e8d8a0' });
      D.line([[hx + 10, hy + 20], [hx + 18, hy + 22], [hx + 24, hy + 18]], 1.8);
      D.b(P([[hx - 44, hy - 14], [hx - 20, hy - 22], [hx - 16, hy - 48], [hx + 18, hy - 50], [hx + 24, hy - 22], [hx + 52, hy - 12], [hx + 10, hy - 8], [hx - 20, hy - 8]], true), '#1c1618');
      D.b(P([[hx - 16, hy - 26], [hx + 22, hy - 28], [hx + 22, hy - 20], [hx - 16, hy - 18]]), '#5a2226', { k: 2, hatch: false });
    },
    weapon(D, g) {
      for (let i = 0; i < 5; i++) D.tool(g.fh[0] + 6, g.fh[1] - 4, -1.2 + i * 0.25, D => { D.b(R(0, -9, 44, 18, 3), i === 4 ? '#e8dcc0' : '#d8ccb0', { k: 2, hatch: false, lw: 2 }); D.fill(R(30, -5, 8, 10), i % 2 ? '#8e2a24' : '#2a2a3a'); });
    } });
} };

// ---- humanoid foes ----
const HFOES = {
  lostDiver: { name: 'The Lost Diver', sub: 'Cave mini-boss', draw(D) {
    hum(D, { skin: '#6a6450', coat: '#5e5a48', pants: '#56523e', boots: '#2e2e30', glove: '#3a3228', bulk: 1.18, hunch: 14, bigBoots: true, hand: 14, noNeck: true,
      back(D) { D.b(strip(bez([140, 110], [80, 100], [70, 190], [100, 250]), 7, 7), '#3a3428'); },
      front(D) {
        D.b(P([[112, 154], [210, 154], [220, 176], [160, 192], [102, 176]], true), '#6a6a4a');
        D.fill(P([[150, 210], [176, 206], [170, 244], [146, 240]]), 'rgba(0,0,0,0.7)'); D.glow(160, 226, 30, '#7ad0a0', 0.5);
        for (const [x, y, c] of [[118, 170, '#c46a5a'], [126, 186, '#d88a6a'], [200, 170, '#c46a5a'], [190, 250, '#6ea890']])
          for (let i = 0; i < 4; i++) D.b(L(x, y, x - 6 + i * 5, y - 12 - i * 3, 3, 2), c, { k: 1, lw: 1.6, hatch: false });
      },
      head(D, hx, hy) {
        diverHelmet(D, hx - 2, hy + 4, { brass: '#6f7a5a', glow: '#7ad0a0', broken: true });
        for (let i = 0; i < 6; i++) D.b(L(hx - 20 + i * 6, hy - 26, hx - 30 + i * 9, hy - 50 - (i % 3) * 8, 4, 2), i % 2 ? '#c46a5a' : '#d8a080', { k: 1, lw: 1.6, hatch: false });
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], 0.5, D => { D.b(R(-8, -6, 20, 12, 3), '#3a2a1e', { k: 2 }); blade(D, 60, 8, '#7a6a52'); }); } });
  } },
  spearman: { name: 'Tribal Spearman', sub: 'Island · front rank', draw(D) {
    const sk = '#6a4430';
    hum(D, { skin: sk, coat: sk, pants: sk, boots: sk, glove: sk, bulk: 1.05,
      pose: { be: [120, 200], bh: [150, 214], fe: [226, 190], fh: [250, 196] },
      front(D) { D.b(P([[128, 240], [196, 240], [206, 300], [176, 290], [160, 306], [144, 290], [120, 300]], true), '#6a5a2a'); D.line([[130, 170], [190, 176]], 2, '#e8dcc0'); D.line([[128, 186], [192, 192]], 2, '#e8dcc0'); },
      backHand(D, g) { D.b(E(g.bh[0] + 4, g.bh[1] + 10, 34, 52), '#7a5a30'); D.ring(g.bh[0] + 4, g.bh[1] + 10, 18, '#e8dcc0', 3); D.dot(g.bh[0] + 4, g.bh[1] + 10, 5, '#8e2a24'); },
      head(D, hx, hy) {
        face(D, hx, hy, sk, { mask: '#e3d8c0' });
        for (let i = 0; i < 5; i++) D.b(L(hx - 10 + i * 4, hy - 20, hx - 24 + i * 6, hy - 58 + (i % 2) * 8, 3, 1), ['#8e2a24', '#e3d8c0', '#2e5a52'][i % 3], { k: 1, lw: 1.6, hatch: false });
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -0.1, D => { D.b(R(-130, -4, 214, 8, 3), '#5a3c26', { k: 2 }); D.b(P([[80, -10], [118, 0], [80, 10]], true), '#3a3634', { hi: 0.5, k: 3 }); }); } });
  } },
  shaman: { name: 'Island Shaman', sub: 'Island · back rank', draw(D) {
    const sk = '#5e3c2a';
    hum(D, { skin: sk, coat: '#4a5a2e', long: true, skirt: '#3e4e26', pants: sk, boots: sk, glove: sk, sleeve: sk, hunch: 10,
      hem: [[124, 236], [200, 236], [224, 360], [196, 348], [176, 364], [150, 348], [100, 360]],
      pose: { fe: [220, 170], fh: [238, 150] },
      front(D) { for (let i = 0; i < 9; i++) D.line([[124 + i * 9, 240], [118 + i * 12, 350]], 1.6, 'rgba(10,20,6,0.6)'); },
      head(D, hx, hy) {
        for (let i = 0; i < 9; i++) { const a = -2.6 + i * 0.26; D.b(E(hx + Math.cos(a) * 44, hy - 6 + Math.sin(a) * 44, 7, 22, a + Math.PI / 2), ['#8e2a24', '#c9a24a', '#2e6a5a'][i % 3], { k: 3, hatch: false }); }
        face(D, hx, hy, sk, { mask: '#c8b890' }); D.line([[hx + 8, hy + 8], [hx + 26, hy + 8]], 2, '#8e2a24');
      },
      weapon(D, g) {
        D.b(R(g.fh[0] - 5, g.fh[1] - 60, 10, 280, 3), '#4a3020', { k: 2 });
        D.b(C(g.fh[0], g.fh[1] - 70, 14), '#2e6a5a', { k: 4 }); D.glow(g.fh[0], g.fh[1] - 70, 60, '#7af0c0', 0.7);
        for (let i = 0; i < 3; i++) D.fill(C(g.fh[0] - 10 + i * 12, g.fh[1] - 100 - i * 18, 10 + i * 4), 'rgba(160,220,190,0.18)');
      } });
  } },
  demigod: { name: 'Tribal Demigod', sub: 'Island mini-boss', draw(D) {
    const sk = '#6e6a66';
    hum(D, { skin: sk, coat: sk, pants: sk, boots: '#4a4644', glove: sk, bulk: 1.34, hand: 16,
      pose: { fe: [222, 150], fh: [214, 100] },
      front(D) {
        D.line([[140, 170], [160, 200], [150, 240]], 3, '#ffa040'); D.line([[180, 180], [170, 220]], 3, '#ffa040'); D.glow(160, 210, 60, '#ff8a30', 0.5);
        D.b(P([[118, 240], [206, 240], [214, 300], [110, 300]], true), '#8a6a3a');
      },
      head(D, hx, hy) {
        D.b(P([[hx - 40, hy - 20], [hx - 30, hy - 70], [hx - 10, hy - 40], [hx, hy - 84], [hx + 12, hy - 40], [hx + 30, hy - 72], [hx + 36, hy - 18]], true), '#c9a24a', { hi: 0.4 });
        face(D, hx, hy, sk, { deep: true, glint: '#ffc060' }); D.line([[hx + 2, hy - 20], [hx + 8, hy + 6]], 2.4, '#ffa040');
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -1.6, D => { D.b(R(-30, -7, 80, 14, 4), '#4a3020', { k: 3 }); D.b(P([[46, -24], [118, -30], [126, 0], [118, 30], [46, 24]], true), '#3a3634', { hi: 0.3 }); for (let i = 0; i < 4; i++) D.b(P([[60 + i * 16, -24], [66 + i * 16, -38], [72 + i * 16, -26]], true), '#e3d8c0', { k: 1, lw: 1.5, hatch: false }); }); } });
  } },
  coconutQueen: { name: 'Coconut Queen', sub: 'Island mini-boss', draw(D) {
    const sk = '#8a5a3a';
    hum(D, { skin: sk, coat: '#6a4a2a', long: true, skirt: '#4a6a2e', pants: sk, boots: sk, glove: sk, sleeve: sk, bulk: 1.45,
      hem: [[112, 236], [212, 236], [250, 364], [210, 350], [190, 366], [160, 350], [130, 366], [80, 362]],
      front(D) {
        D.b(E(142, 200, 26, 30), '#5a3a1e', { hi: 0.3 }); D.b(E(186, 200, 26, 30), '#5a3a1e', { hi: 0.3 });
        for (const x of [136, 180]) { D.dot(x, 192, 3, INK); D.dot(x + 12, 192, 3, INK); D.dot(x + 6, 204, 3, INK); }
        for (let i = 0; i < 10; i++) D.line([[116 + i * 10, 244], [104 + i * 14, 360]], 1.6, 'rgba(10,20,6,0.6)');
      },
      head(D, hx, hy) {
        for (let i = 0; i < 7; i++) { const a = -2.8 + i * 0.38; D.b(strip(bez([hx, hy - 20], [hx + Math.cos(a) * 30, hy - 20 + Math.sin(a) * 40], [hx + Math.cos(a) * 60, hy - 20 + Math.sin(a) * 50], [hx + Math.cos(a) * 80, hy + Math.sin(a) * 40]), 7, 1), '#4a7a32', { k: 3 }); }
        face(D, hx, hy, sk, { deep: true, glint: '#e8d080' });
        D.b(P([[hx - 26, hy - 18], [hx - 18, hy - 40], [hx - 4, hy - 28], [hx + 6, hy - 44], [hx + 14, hy - 28], [hx + 26, hy - 40], [hx + 28, hy - 16]]), '#c9a24a', { hi: 0.5, k: 3 });
      },
      weapon(D, g) { D.b(C(g.fh[0] + 14, g.fh[1] - 10, 18), '#5a3a1e', { hi: 0.3 }); D.line([[g.fh[0] + 20, g.fh[1] - 28], [g.fh[0] + 30, g.fh[1] - 44]], 2.4); D.glow(g.fh[0] + 30, g.fh[1] - 46, 20, '#ffb040', 0.9); } });
  } },
  sunGod: { name: 'The Sun God', sub: 'Island boss', draw(D) {
    const gold = '#c9a24a';
    hum(D, { skin: '#a8743a', coat: gold, long: true, skirt: '#8a6a2a', pants: '#6a4a24', boots: '#4a3418', glove: '#a8743a', sleeve: '#a8743a', bulk: 1.3, hand: 15,
      pose: { fe: [230, 170], fh: [256, 140], be: [96, 180], bh: [80, 140] },
      back(D) {
        D.glow(168, 100, 180, '#ffc860', 0.6);
        for (let i = 0; i < 16; i++) { const a = i / 16 * TAU; D.b(P([[168 + Math.cos(a - 0.08) * 50, 100 + Math.sin(a - 0.08) * 50], [168 + Math.cos(a) * 118, 100 + Math.sin(a) * 118], [168 + Math.cos(a + 0.08) * 50, 100 + Math.sin(a + 0.08) * 50]]), i % 2 ? '#e8c060' : '#c9903a', { k: 5, hatch: false }); }
      },
      front(D) { D.b(C(160, 200, 24), '#e8c060', { hi: 0.5 }); D.ring(160, 200, 24, '#8a5a1a', 4); for (let i = 0; i < 5; i++) D.line([[124, 250 + i * 20], [200, 250 + i * 20]], 2, '#6a4a14'); },
      head(D, hx, hy) {
        D.b(P([[hx - 26, hy - 30], [hx + 30, hy - 30], [hx + 34, hy + 10], [hx + 16, hy + 36], [hx - 20, hy + 36], [hx - 30, hy + 4]], true), '#e0b050', { hi: 0.6 });
        D.fill(R(hx - 4, hy - 8, 34, 7), INK); D.eyes([[hx + 8, hy - 5], [hx + 22, hy - 5]], 2, '#fff0b0');
        D.fill(R(hx + 4, hy + 16, 22, 4), INK); D.line([[hx + 2, hy - 20], [hx + 30, hy - 20]], 2, '#8a5a1a');
      },
      weapon(D, g) { D.glow(g.fh[0], g.fh[1] - 20, 70, '#ffd070', 0.9); D.dot(g.fh[0], g.fh[1] - 20, 14, '#fff2c0'); },
      backHand(D, g) { D.glow(g.bh[0], g.bh[1] - 16, 50, '#ffd070', 0.7); } });
  } },
  feralMerman: { name: 'Feral Merman', sub: 'Weeds · skirmisher', draw(D) {
    const sk = '#2e5a4e';
    hum(D, { skin: sk, coat: sk, pants: '#28504a', glove: sk, fins: '#4e8a7a', hunch: 22, hx: 12, bulk: 1.08, hand: 12,
      pose: { fe: [230, 220], fh: [256, 252], be: [106, 222], bh: [96, 262] },
      front(D) { D.scales(P([[110, 170], [210, 168], [192, 264], [132, 264]], true), 110, 168, 212, 266, 13, 'rgba(8,24,20,0.6)'); D.line([[140, 200], [160, 230]], 2, '#8e2a24'); D.line([[150, 196], [170, 226]], 2, '#8e2a24'); },
      head(D, hx, hy) {
        D.b(P([[hx - 30, hy - 2], [hx - 26, hy - 40], [hx - 8, hy - 22], [hx - 2, hy - 44], [hx + 10, hy - 22]], true), rgba('#6ea8a0', 0.7), { k: 2, hatch: false, lw: 2 });
        face(D, hx, hy, sk, { deep: true, glint: '#c8ffe0' });
        for (let i = 0; i < 4; i++) D.b(P([[hx + 12 + i * 4, hy + 16], [hx + 14 + i * 4, hy + 24], [hx + 16 + i * 4, hy + 16]]), '#e8e0cc', { k: 1, lw: 1, hatch: false });
      },
      over(D, g) { for (const h of [g.fh, g.bh]) for (let i = 0; i < 3; i++) D.b(P([[h[0] + 4, h[1] - 6 + i * 6], [h[0] + 22, h[1] + 4 + i * 8], [h[0] + 6, h[1] + i * 6]], true), '#d8d0bc', { k: 1, lw: 1.4, hatch: false }); } });
  } },
  enemySiren: { name: 'Weeds Siren', sub: 'Weeds · back rank', draw(D) { sirenFig(D, { skin: '#6e8a88', tail: '#3a4e5e', fin: '#8ac0d8', hair: '#101a1e', eye: '#c8f0ff' }); } },
  neptune: { name: 'Neptune', sub: 'Weeds boss', draw(D) {
    const sk = '#5e7a6a';
    hum(D, { skin: sk, coat: sk, long: true, skirt: '#2e5a52', pants: sk, boots: sk, glove: sk, sleeve: sk, bulk: 1.36, hand: 16,
      pose: { fe: [230, 196], fh: [252, 196] },
      front(D) { D.scales(P([[108, 150], [212, 148], [200, 240], [120, 240]], true), 100, 150, 214, 240, 16, 'rgba(10,26,22,0.35)'); D.b(P([[110, 170], [214, 232], [210, 246], [104, 186]]), '#b89a4a', { hi: 0.4, k: 3 }); },
      head(D, hx, hy) {
        face(D, hx, hy, sk, { deep: true, glint: '#d8fff0', hairBack: '#3a5a4a' });
        D.b(P([[hx - 22, hy + 4], [hx + 4, hy + 14], [hx + 30, hy + 12], [hx + 34, hy + 40], [hx + 20, hy + 80], [hx + 6, hy + 62], [hx - 6, hy + 90], [hx - 16, hy + 56], [hx - 26, hy + 30]], true), '#4a6a4a');
        for (let i = 0; i < 5; i++) D.line([[hx - 12 + i * 8, hy + 20], [hx - 16 + i * 9, hy + 70]], 1.4, 'rgba(10,20,12,0.6)');
        D.b(P([[hx - 26, hy - 18], [hx - 24, hy - 50], [hx - 12, hy - 30], [hx - 2, hy - 62], [hx + 8, hy - 30], [hx + 20, hy - 54], [hx + 30, hy - 18]]), '#c9a24a', { hi: 0.5 });
      },
      weapon(D, g) {
        D.b(R(g.fh[0] - 6, 40, 12, 326, 3), '#b89a4a', { hi: 0.4, k: 3 });
        D.b(P([[g.fh[0] - 34, 64], [g.fh[0] - 30, 20], [g.fh[0] - 24, 50], [g.fh[0] - 6, 50], [g.fh[0] - 6, 4], [g.fh[0], -12], [g.fh[0] + 6, 4], [g.fh[0] + 6, 50], [g.fh[0] + 24, 50], [g.fh[0] + 30, 20], [g.fh[0] + 34, 64]], true), '#c9a24a', { hi: 0.5 });
        D.glow(g.fh[0], 30, 60, '#8af0ff', 0.4);
      } });
  } },
  lostOne: { name: 'Lost One', sub: 'Atlantis · infantry', draw(D) {
    const br = '#5f7f6a';
    hum(D, { skin: '#2a2a2a', coat: br, pants: '#3a3a36', boots: '#4a5a4e', glove: '#4a5a4e', sleeve: '#3a3a36', hunch: 8,
      pose: { be: [118, 196], bh: [148, 208] },
      front(D) { for (let i = 0; i < 4; i++) D.line([[122, 180 + i * 20], [198, 178 + i * 20]], 2, 'rgba(10,20,14,0.6)'); D.fill(E(150, 200, 10, 14), 'rgba(140,200,170,0.35)'); for (let i = 0; i < 8; i++) D.b(R(126 + i * 9, 250, 8, 30, 1), '#6a5a3a', { k: 1, lw: 1.5, hatch: false }); },
      backHand(D, g) { D.b(C(g.bh[0], g.bh[1] + 6, 46), br); D.ring(g.bh[0], g.bh[1] + 6, 30, '#8aa890', 4); D.fill(C(g.bh[0], g.bh[1] + 6, 12), 'rgba(0,0,0,0.5)'); },
      head(D, hx, hy) {
        D.b(P([[hx - 26, hy + 20], [hx - 26, hy - 18], [hx - 8, hy - 34], [hx + 22, hy - 30], [hx + 30, hy - 8], [hx + 30, hy + 24], [hx + 14, hy + 26], [hx + 14, hy]], true), br, { hi: 0.3 });
        D.fill(P([[hx + 6, hy - 8], [hx + 30, hy - 8], [hx + 30, hy + 22], [hx + 14, hy + 22]]), '#050506');
        D.eyes([[hx + 20, hy - 1]], 1.8, '#8affd0');
        D.b(P([[hx - 30, hy - 26], [hx - 10, hy - 60], [hx + 20, hy - 56], [hx + 10, hy - 32]], true), '#8e2a24');
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -0.2, D => { D.b(R(-110, -4, 190, 8, 3), '#4a4030', { k: 2 }); D.b(P([[74, -9], [112, 0], [74, 9]], true), '#5f7f6a', { k: 3 }); }); } });
  } },
  cultist: { name: 'Cultist', sub: 'Atlantis · back rank', draw(D) {
    hum(D, { skin: '#9a9a8a', coat: '#2e2a3e', long: true, skirt: '#26223a', pants: '#1e1a2a', boots: '#141220', glove: '#9a9a8a', hunch: 12,
      hem: [[126, 236], [198, 236], [224, 366], [104, 366]],
      pose: { fe: [224, 206], fh: [246, 196], be: [110, 210], bh: [120, 250] },
      front(D) { D.b(C(160, 190, 14), '#1a3a34', { k: 3 }); for (let i = 0; i < 5; i++) { const a = i / 5 * TAU; D.line([[160, 190], [160 + Math.cos(a) * 22, 190 + Math.sin(a) * 22], [160 + Math.cos(a + 0.6) * 28, 190 + Math.sin(a + 0.6) * 28]], 2, '#6ae0b0', true); } },
      head(D, hx, hy) {
        D.b(P([[hx - 36, hy + 40], [hx - 32, hy - 18], [hx - 4, hy - 42], [hx + 26, hy - 32], [hx + 40, hy + 4], [hx + 34, hy + 36]], true), '#2e2a3e');
        D.fill(P([[hx - 4, hy - 20], [hx + 30, hy - 14], [hx + 34, hy + 30], [hx - 2, hy + 30]], true), '#050508');
        D.eyes([[hx + 12, hy], [hx + 24, hy]], 1.6, '#6ae0b0');
      },
      weapon(D, g) { D.b(R(g.fh[0] - 10, g.fh[1] + 4, 20, 30, 3), '#3a4a3e', { k: 3 }); D.glow(g.fh[0], g.fh[1] + 20, 70, '#6ae0b0', 0.8); D.fill(R(g.fh[0] - 6, g.fh[1] + 8, 12, 22), '#b0ffe0'); },
      backHand(D, g) { D.tool(g.bh[0], g.bh[1], 1.3, D => blade(D, 40, 6, '#6a7a70')); } });
  } },
  giantLostOne: { name: 'Legionary Colossus', sub: 'Atlantis mini-boss', draw(D) {
    const br = '#6a7f5e';
    hum(D, { skin: '#222', coat: br, pants: '#3a3a36', boots: '#4a5a4e', glove: '#4a5a4e', sleeve: '#4a5a4e', bulk: 1.42, hand: 15,
      pose: { be: [112, 200], bh: [138, 214] },
      front(D) { for (let i = 0; i < 5; i++) D.b(P([[108, 176 + i * 17], [214, 174 + i * 17], [212, 188 + i * 17], [110, 190 + i * 17]]), i % 2 ? br : '#5a6e50', { k: 3, hatch: false }); for (let i = 0; i < 9; i++) D.b(R(118 + i * 10, 258, 9, 36, 1), '#6a5a3a', { k: 1, lw: 1.5, hatch: false }); },
      backHand(D, g) { D.b(R(g.bh[0] - 44, g.bh[1] - 80, 88, 170, 10), '#7a2a24'); D.b(C(g.bh[0], g.bh[1] + 5, 16), '#b89a4a', { hi: 0.4 }); D.line([[g.bh[0] - 40, g.bh[1] - 60], [g.bh[0] + 40, g.bh[1] - 60]], 3, '#b89a4a'); D.line([[g.bh[0] - 40, g.bh[1] + 70], [g.bh[0] + 40, g.bh[1] + 70]], 3, '#b89a4a'); },
      head(D, hx, hy) {
        D.b(P([[hx - 28, hy + 26], [hx - 28, hy - 18], [hx - 8, hy - 36], [hx + 24, hy - 32], [hx + 32, hy - 8], [hx + 32, hy + 28], [hx + 14, hy + 30], [hx + 14, hy]], true), br, { hi: 0.3 });
        D.fill(P([[hx + 6, hy - 8], [hx + 32, hy - 8], [hx + 32, hy + 26], [hx + 14, hy + 26]]), '#050506'); D.eyes([[hx + 20, hy - 1], [hx + 28, hy]], 1.8, '#8affd0');
        D.b(P([[hx - 40, hy - 18], [hx - 30, hy - 60], [hx - 4, hy - 76], [hx + 30, hy - 60], [hx + 20, hy - 34]], true), '#8e2a24');
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -0.5, D => { D.b(R(-12, -12, 14, 24, 2), '#b89a4a', { k: 2 }); blade(D, 90, 11, '#7a8a7a'); }); } });
  } },
};

// ---- the platform levels' cast ----
const PLATFOLK = {
  diver: { name: 'The Diver', sub: 'Platform hero', draw(D) {
    hum(D, { skin: '#c8997a', coat: '#3e4a56', pants: '#36424e', boots: '#2e3034', glove: '#5a4a38', bulk: 1.08, bigBoots: true, hand: 12, noNeck: true,
      pose: { fe: [228, 180], fh: [252, 164], be: [104, 214], bh: [110, 256] },
      back(D) { D.b(R(90, 142, 26, 96, 12), '#8a8e90', { hi: 0.4 }); D.b(R(108, 136, 26, 100, 12), '#9a9ea0', { hi: 0.4 }); D.b(R(104, 128, 10, 14, 2), '#b28b3e', { k: 2 }); },
      front(D) { D.b(P([[112, 144], [210, 144], [218, 166], [160, 180], [104, 166]], true), '#b28b3e', { hi: 0.35 }); D.b(R(122, 236, 80, 14, 2), '#4a3a2a', { k: 2, hatch: false }); },
      head(D, hx, hy) {
        const cx = hx - 2, cy = hy + 2;
        D.b(C(cx, cy, 38), '#c29a44', { hi: 0.55 });
        D.b(P([[cx - 4, cy - 12], [cx + 36, cy - 14], [cx + 38, cy + 12], [cx - 2, cy + 14]], true), '#123a44', { k: 3 });
        D.glow(cx + 18, cy, 50, '#5af0ff', 0.7); D.fill(P([[cx + 2, cy - 8], [cx + 32, cy - 10], [cx + 33, cy + 8], [cx + 3, cy + 10]], true), 'rgba(110,240,255,0.55)');
        D.line([[cx + 8, cy - 6], [cx + 20, cy - 6]], 2, '#e8ffff');
        for (const a of [-2.2, -1.6, -1, 1.8, 2.4]) D.dot(cx + Math.cos(a) * 32, cy + Math.sin(a) * 32, 2.6, '#6a4a1a');
      },
      weapon(D, g) { D.glow(g.fh[0] + 24, g.fh[1] - 4, 70, '#fff0c0', 0.5); D.tool(g.fh[0], g.fh[1], -0.3, D => { D.b(R(-6, -8, 30, 16, 4), '#4a4c4e', { k: 3 }); D.b(E(26, 0, 6, 10), '#fff0c0', { k: 2, hatch: false }); }); } });
  } },
  ambusher: { name: 'Pirate Ambusher', sub: 'Pirate Ship · bursts from doors (P)', draw(D) {
    hum(D, { skin: '#b8835e', coat: '#8a8472', pants: '#3a3a4a', boots: '#241c18', hunch: 12, hx: 6,
      pose: { fe: [236, 206], fh: [264, 206] },
      front(D) { for (let y = 160; y < 262; y += 14) D.line([[118, y], [202, y - 2]], 5, 'rgba(120,26,24,0.8)'); D.b(P([[128, 240], [196, 240], [196, 254], [128, 254]]), '#5a3a1e', { k: 2, hatch: false }); },
      head(D, hx, hy) {
        face(D, hx, hy, '#b8835e', { stubble: '#1e140e', deep: true, glint: '#e8d0a0' });
        D.b(P([[hx - 26, hy - 4], [hx - 18, hy - 30], [hx + 12, hy - 32], [hx + 28, hy - 14], [hx - 4, hy - 12], [hx - 30, hy + 16], [hx - 38, hy + 4]], true), '#8e2a24');
        D.ring(hx - 4, hy + 16, 3, '#c9a24a', 1.6);
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], 0.05, D => { D.b(E(-4, 0, 12, 10), '#b8943e', { k: 2, hatch: false }); D.b(c => { c.moveTo(4, -5); c.quadraticCurveTo(50, -18, 92, -2); c.quadraticCurveTo(50, 0, 4, 5); c.closePath(); }, '#a7aaa3', { hi: 0.5, k: 3 }); }); } });
  } },
  gunner: { name: 'Musket Gunner', sub: 'Pirate Ship · behind a barrel (G)', draw(D) {
    hum(D, { skin: '#c49574', coat: '#3a2a26', pants: '#2e2a2a', boots: '#1e1816',
      pose: { be: [150, 196], bh: [196, 206], fe: [220, 206], fh: [232, 200] },
      head(D, hx, hy) {
        face(D, hx, hy, '#c49574', { beard: '#2a1a12', deep: true });
        D.b(P([[hx - 36, hy - 14], [hx - 10, hy - 22], [hx - 4, hy - 44], [hx + 20, hy - 42], [hx + 26, hy - 22], [hx + 46, hy - 16], [hx + 20, hy - 6], [hx - 10, hy - 10]], true), '#1c1618');
      },
      weapon(D, g) { D.tool(g.fh[0] - 40, g.fh[1] + 4, -0.08, D => { D.b(P([[-40, -4], [20, -8], [30, 6], [-30, 16], [-48, 12]], true), '#5e3e26'); D.b(R(20, -8, 130, 8, 2), '#3a3c3e', { hi: 0.4 }); D.glow(152, -4, 30, '#ffb060', 0.5); }); },
      over(D) {
        D.b(P([[100, 262], [234, 262], [240, 312], [234, 368], [100, 368], [94, 312]], true), '#6a4a2c');
        for (const y of [276, 350]) D.b(R(94, y, 146, 9, 2), '#3a3634', { k: 2, hatch: false });
        for (let x = 116; x < 234; x += 22) D.line([[x, 264], [x - 2, 366]], 1.6, 'rgba(10,6,4,0.55)');
      } });
  } },
  blackbeard: { name: 'Blackbeard', sub: 'Pirate Ship boss', draw(D) {
    const c = D.c; c.save(); c.translate(165, 368); c.scale(1, 1.08); c.translate(-165, -368);
    hum(D, { skin: '#b07a58', coat: '#1a1418', long: true, skirt: '#16121a', pants: '#2a2020', boots: '#0e0a0a', glove: '#2a1e1a', bulk: 1.2, hand: 13,
      hem: [[124, 236], [200, 236], [234, 360], [214, 344], [196, 362], [170, 346], [146, 362], [96, 358]],
      pose: { fe: [230, 176], fh: [254, 150], be: [106, 206], bh: [104, 250] },
      front(D) { D.b(P([[106, 150], [210, 240], [206, 256], [102, 166]]), '#3a2a20', { k: 3 }); for (let i = 0; i < 3; i++) D.b(R(126 + i * 26, 172 + i * 24, 26, 10, 3), '#4a4a4c', { k: 2 }); D.b(R(144, 240, 36, 20, 3), '#c9a24a', { hi: 0.4, k: 3 }); },
      head(D, hx, hy) {
        face(D, hx, hy, '#b07a58', { deep: true, glint: '#ffcf80', hairBack: '#0e0a0c' });
        D.b(P([[hx - 20, hy + 2], [hx + 8, hy + 12], [hx + 30, hy + 8], [hx + 36, hy + 50], [hx + 20, hy + 96], [hx + 4, hy + 70], [hx - 10, hy + 100], [hx - 16, hy + 60], [hx - 26, hy + 24]], true), '#121014');
        for (const [x, y] of [[hx + 26, hy + 60], [hx - 8, hy + 80], [hx + 34, hy + 20]]) { D.line([[x, y], [x + 8, y - 12]], 2, '#8a7a5a'); D.glow(x + 8, y - 14, 22, '#ffa040', 0.9); D.dot(x + 8, y - 14, 2.4, '#fff0b0'); D.fill(C(x + 10, y - 30, 8), 'rgba(160,160,150,0.25)'); }
        D.b(P([[hx - 50, hy - 16], [hx - 24, hy - 26], [hx - 14, hy - 56], [hx + 22, hy - 56], [hx + 30, hy - 26], [hx + 58, hy - 14], [hx + 26, hy - 8], [hx - 20, hy - 8]], true), '#141014');
        D.b(P([[hx - 2, hy - 44], [hx + 12, hy - 44], [hx + 14, hy - 32], [hx - 4, hy - 32]], true), '#e8dcc0', { k: 1, lw: 1.6 });
      },
      weapon(D, g) { D.tool(g.fh[0], g.fh[1], -1.4, D => { D.b(E(-4, 0, 16, 12), '#b8943e', { k: 2, hatch: false }); D.b(c => { c.moveTo(6, -7); c.quadraticCurveTo(80, -26, 150, -4); c.quadraticCurveTo(80, -2, 6, 7); c.closePath(); }, '#a7aaa3', { hi: 0.55, k: 3 }); }); } });
    c.restore();
  } },
};
