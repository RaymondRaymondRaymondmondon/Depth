// Ink toolkit: every shape is filled, cut by a hard block shadow (light from the upper left),
// crosshatched in the shadow, then ringed in heavy black ink.
const INK = '#0c0a0d';
const TAU = Math.PI * 2;

function rgbOf(col) {
  if (col[0] === '#') {
    let h = col.slice(1);
    if (h.length === 3) h = h.split('').map(x => x + x).join('');
    return [0, 2, 4].map(i => parseInt(h.slice(i, i + 2), 16));
  }
  return col.match(/[\d.]+/g).slice(0, 3).map(Number);
}
const clamp255 = v => Math.max(0, Math.min(255, Math.round(v)));
function sh(col, f) { return 'rgb(' + rgbOf(col).map(v => clamp255(v * f)).join(',') + ')'; }
function mix(a, b, t) { const A = rgbOf(a), B = rgbOf(b); return 'rgb(' + A.map((v, i) => clamp255(v + (B[i] - v) * t)).join(',') + ')'; }
function rgba(col, a) { return 'rgba(' + rgbOf(col).join(',') + ',' + a + ')'; }
function rng(seed) {
  return function () {
    seed |= 0; seed = seed + 0x6D2B79F5 | 0;
    let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

// ---- path makers (each returns fn(ctx) with a .bb bounding box) ----
function withBB(fn, x0, y0, x1, y1) { fn.bb = [x0, y0, x1, y1]; return fn; }
function E(x, y, rx, ry, r = 0) {
  const m = Math.max(rx, ry);
  return withBB(c => { c.moveTo(x + rx * Math.cos(r), y + rx * Math.sin(r)); c.ellipse(x, y, rx, ry, r, 0, TAU); }, x - m, y - m, x + m, y + m);
}
const C = (x, y, r) => E(x, y, r, r);
function P(pts, smooth) {
  const xs = pts.map(p => p[0]), ys = pts.map(p => p[1]);
  return withBB(c => {
    const n = pts.length;
    if (!smooth) { c.moveTo(pts[0][0], pts[0][1]); for (let i = 1; i < n; i++) c.lineTo(pts[i][0], pts[i][1]); c.closePath(); return; }
    const mid = (a, b) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    const m0 = mid(pts[n - 1], pts[0]);
    c.moveTo(m0[0], m0[1]);
    for (let i = 0; i < n; i++) { const p = pts[i], q = pts[(i + 1) % n], m = mid(p, q); c.quadraticCurveTo(p[0], p[1], m[0], m[1]); }
    c.closePath();
  }, Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys));
}
function L(x1, y1, x2, y2, w1, w2) {
  if (w2 === undefined) w2 = w1;
  const m = Math.max(w1, w2);
  return withBB(c => {
    const a = Math.atan2(y2 - y1, x2 - x1), n = a + Math.PI / 2;
    c.moveTo(x1 + Math.cos(n) * w1, y1 + Math.sin(n) * w1);
    c.lineTo(x2 + Math.cos(n) * w2, y2 + Math.sin(n) * w2);
    c.arc(x2, y2, w2, n, n - Math.PI, true);
    c.lineTo(x1 - Math.cos(n) * w1, y1 - Math.sin(n) * w1);
    c.arc(x1, y1, w1, n - Math.PI, n - TAU, true);
    c.closePath();
  }, Math.min(x1, x2) - m, Math.min(y1, y2) - m, Math.max(x1, x2) + m, Math.max(y1, y2) + m);
}
function R(x, y, w, h, r = 0) {
  return withBB(c => { if (r && c.roundRect) c.roundRect(x, y, w, h, r); else c.rect(x, y, w, h); }, x, y, x + w, y + h);
}
function bez(p0, p1, p2, p3, n = 28) {
  const o = [];
  for (let i = 0; i <= n; i++) {
    const t = i / n, u = 1 - t;
    o.push([u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
            u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]]);
  }
  return o;
}
function spineFrame(sp, i) {
  const a = sp[Math.max(0, i - 1)], b = sp[Math.min(sp.length - 1, i + 1)];
  const dx = b[0] - a[0], dy = b[1] - a[1], l = Math.hypot(dx, dy) || 1;
  return { x: sp[i][0], y: sp[i][1], nx: -dy / l, ny: dx / l, tx: dx / l, ty: dy / l };
}
// a body built around a spine: worms, tentacles, tails, eels
function strip(sp, w0, w1) {
  const W = typeof w0 === 'function' ? w0 : t => w0 + (w1 - w0) * t;
  const A = [], B = [];
  for (let i = 0; i < sp.length; i++) {
    const f = spineFrame(sp, i), w = W(i / (sp.length - 1));
    A.push([f.x + f.nx * w, f.y + f.ny * w]); B.push([f.x - f.nx * w, f.y - f.ny * w]);
  }
  return P(A.concat(B.reverse()), true);
}

// ---- the inking context ----
class Ink {
  constructor(c, seed) { this.c = c; this.rand = rng(seed || 7); this.ls = 1; }
  R(a, b) { return a + (b - a) * this.rand(); }
  lightOffset(k) {
    const m = this.c.getTransform(), det = m.a * m.d - m.b * m.c, s = Math.sqrt(Math.abs(det)) * k;
    const sx = -0.78, sy = -0.62;
    return [(m.d * sx - m.c * sy) / det * s, (-m.b * sx + m.a * sy) / det * s];
  }
  // shaded, inked shape
  b(path, col, o = {}) {
    const c = this.c, bb = path.bb, k = o.k ?? (bb ? Math.max(4, Math.min(28, Math.min(bb[2] - bb[0], bb[3] - bb[1]) * 0.42)) : 8), lw = (o.lw ?? 4.2) * this.ls;
    c.save();
    if (o.alpha !== undefined) c.globalAlpha = o.alpha;
    c.beginPath(); path(c);
    if (o.flat) { c.fillStyle = col; c.fill(); }
    else {
      c.save(); c.clip();
      c.fillStyle = sh(col, o.dark ?? 0.34); c.fill();
      const [ox, oy] = this.lightOffset(k);
      c.translate(ox, oy);
      c.beginPath(); path(c); c.fillStyle = col; c.fill();
      if (o.hi) { // a hard specular stripe on the lit side
        c.save(); c.translate(ox * 0.8, oy * 0.8); c.beginPath(); c.rect(-9999, -9999, 19998, 19998); path(c); c.clip('evenodd');
        c.beginPath(); c.translate(-ox * 0.8, -oy * 0.8); path(c); c.fillStyle = rgba(mix(col, '#fff4dc', 0.55), o.hi); c.fill(); c.restore();
      }
      if (o.hatch !== false && path.bb) {
        c.beginPath(); c.rect(-9999, -9999, 19998, 19998); path(c); c.clip('evenodd');
        c.translate(-ox, -oy);
        const [x0, y0, x1, y1] = path.bb, h = y1 - y0, st = (o.hs ?? 5) * this.ls;
        c.beginPath();
        for (let d = x0 - h; d < x1; d += st) { c.moveTo(d, y0); c.lineTo(d + h, y1); }
        c.strokeStyle = 'rgba(8,6,10,0.62)'; c.lineWidth = 1.1 * this.ls; c.stroke();
      }
      c.restore();
    }
    if (lw > 0) { c.beginPath(); path(c); c.lineJoin = 'round'; c.lineCap = 'round'; c.lineWidth = lw; c.strokeStyle = o.ink || INK; c.stroke(); }
    c.restore();
  }
  fill(path, col) { const c = this.c; c.beginPath(); path(c); c.fillStyle = col; c.fill(); }
  clipDo(path, fn) { const c = this.c; c.save(); c.beginPath(); path(c); c.clip(); fn(); c.restore(); }
  line(pts, lw = 2.4, col = INK, smooth = false) {
    const c = this.c; c.beginPath(); c.moveTo(pts[0][0], pts[0][1]);
    if (smooth && pts.length > 2) {
      for (let i = 1; i < pts.length - 1; i++) { const m = [(pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2]; c.quadraticCurveTo(pts[i][0], pts[i][1], m[0], m[1]); }
      c.lineTo(pts[pts.length - 1][0], pts[pts.length - 1][1]);
    } else for (let i = 1; i < pts.length; i++) c.lineTo(pts[i][0], pts[i][1]);
    c.lineWidth = lw * this.ls; c.strokeStyle = col; c.lineCap = 'round'; c.lineJoin = 'round'; c.stroke();
  }
  dot(x, y, r, col) { this.fill(C(x, y, r), col); }
  ring(x, y, r, col, w) { this.b(c => { c.moveTo(x + r + w, y); c.arc(x, y, r + w, 0, TAU); c.moveTo(x + r, y); c.arc(x, y, r, 0, TAU, true); }, col, { k: 2, hatch: false }); }
  glow(x, y, r, col, a = 0.6) {
    const c = this.c; c.save(); c.globalCompositeOperation = 'lighter';
    const g = c.createRadialGradient(x, y, 0, x, y, r);
    g.addColorStop(0, rgba(col, a)); g.addColorStop(0.4, rgba(col, a * 0.35)); g.addColorStop(1, rgba(col, 0));
    c.fillStyle = g; c.fillRect(x - r, y - r, r * 2, r * 2); c.restore();
  }
  shadow(x, y, rx, ry, a = 0.55) {
    const c = this.c, g = c.createRadialGradient(x, y, 0, x, y, rx);
    g.addColorStop(0, `rgba(0,0,0,${a})`); g.addColorStop(1, 'rgba(0,0,0,0)');
    c.save(); c.translate(x, y); c.scale(1, ry / rx); c.translate(-x, -y); c.fillStyle = g; c.fillRect(x - rx, y - rx, rx * 2, rx * 2); c.restore();
  }
  // glowing eyes (only monsters get to show their eyes)
  eyes(pts, r, col) { for (const [x, y] of pts) { this.glow(x, y, r * 5, col, 0.7); this.dot(x, y, r, mix(col, '#ffffff', 0.5)); } }
  // rows of scales / plates across a shape
  scales(path, x0, y0, x1, y1, s, col) {
    this.clipDo(path, () => {
      const c = this.c; c.beginPath();
      for (let y = y0, row = 0; y < y1; y += s * 0.7, row++)
        for (let x = x0 + (row % 2) * s / 2; x < x1; x += s) { c.moveTo(x - s / 2, y); c.quadraticCurveTo(x, y + s * 0.7, x + s / 2, y); }
      c.strokeStyle = col; c.lineWidth = 1.2 * this.ls; c.stroke();
    });
  }
  // draw in a rotated local frame (tools and weapons point along +x)
  tool(x, y, ang, fn) { const c = this.c; c.save(); c.translate(x, y); c.rotate(ang); fn(this); c.restore(); }
}

// place a figure (authored with feet at 165,368) into another picture
function put(D, fn, x, y, s, flip) {
  const c = D.c, ls = D.ls; c.save(); c.translate(x, y); c.scale(flip ? -s : s, s); c.translate(-165, -368);
  D.ls = Math.min(2.2, Math.max(1, 0.55 / s)); fn(D); D.ls = ls; c.restore();
}

let GRAIN = null;
function grain() {
  if (!GRAIN) {
    GRAIN = document.createElement('canvas'); GRAIN.width = GRAIN.height = 180;
    const x = GRAIN.getContext('2d'), id = x.createImageData(180, 180);
    for (let i = 0; i < id.data.length; i += 4) { const v = Math.random() * 255; id.data[i] = id.data[i + 1] = id.data[i + 2] = v; id.data[i + 3] = 255; }
    x.putImageData(id, 0, 0);
  }
  return GRAIN;
}
// vignette + canvas grain, the last pass on every picture
function post(c, w, h, vig = 0.8) {
  c.save();
  const g = c.createRadialGradient(w / 2, h * 0.48, Math.min(w, h) * 0.28, w / 2, h / 2, Math.max(w, h) * 0.72);
  g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, `rgba(0,0,0,${vig})`);
  c.fillStyle = g; c.fillRect(0, 0, w, h);
  const d = c.createLinearGradient(0, 0, w, h);
  d.addColorStop(0, 'rgba(255,220,170,0.05)'); d.addColorStop(0.5, 'rgba(0,0,0,0)'); d.addColorStop(1, 'rgba(0,0,0,0.35)');
  c.fillStyle = d; c.fillRect(0, 0, w, h);
  c.globalCompositeOperation = 'overlay'; c.globalAlpha = 0.08;
  c.fillStyle = c.createPattern(grain(), 'repeat'); c.fillRect(0, 0, w, h);
  c.restore();
}
// backdrop behind a single figure: a dark wash, a pool of light, a floor
function cardBg(D, w, h, top, bot, light) {
  const c = D.c, g = c.createLinearGradient(0, 0, 0, h);
  g.addColorStop(0, top); g.addColorStop(1, bot); c.fillStyle = g; c.fillRect(0, 0, w, h);
  D.glow(w * 0.5, h * 0.55, w * 0.62, light || '#c9a36a', 0.22);
  c.fillStyle = 'rgba(0,0,0,0.35)'; c.fillRect(0, h * 0.9, w, h * 0.1);
  D.line([[0, h * 0.9], [w, h * 0.9]], 2, 'rgba(0,0,0,0.6)');
}
