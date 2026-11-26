/* ============ 峡谷之战 · 核心逻辑 ============ */
'use strict';

const $ = id => document.getElementById(id);
const canvas = $('gameCanvas');
const ctx = canvas.getContext('2d');
const miniCanvas = $('minimap');
const mctx = miniCanvas.getContext('2d');

/* ---------- 工具函数 ---------- */
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const rand = (a, b) => a + Math.random() * (b - a);
const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
const angle = (a, b) => Math.atan2(b.y - a.y, b.x - a.x);
function norm(x, y) { const l = Math.hypot(x, y) || 1; return { x: x / l, y: y / l }; }
function pathLength(path) { let L = 0; for (let i = 1; i < path.length; i++) L += dist(path[i - 1], path[i]); return L; }
function pointAt(path, t) {
  let d = clamp(t, 0, 1) * pathLength(path);
  for (let i = 1; i < path.length; i++) {
    const seg = dist(path[i - 1], path[i]);
    if (d <= seg) { const k = seg ? d / seg : 0; return { x: path[i - 1].x + (path[i].x - path[i - 1].x) * k, y: path[i - 1].y + (path[i].y - path[i - 1].y) * k }; }
    d -= seg;
  }
  return { ...path[path.length - 1] };
}
function barColor(r) { return r > 0.5 ? '#4ade80' : r > 0.25 ? '#facc15' : '#f87171'; }

/* ---------- 全局状态 ---------- */
const G = {
  phase: 'menu', paused: false, time: 0,
  units: [], projectiles: [], particles: [], texts: [], rings: [], areas: [],
  player: null, selectedHero: 'houyi',
  kills: { blue: 0, red: 0 },
  spawnTimer: 0, nextId: 1, shopCache: null
};

/* ---------- 输入 ---------- */
const keys = {};
const mouse = { x: MAP.W / 2, y: MAP.H / 2 };

/* ---------- 单位类 ---------- */
class Unit {
  constructor(o) {
    this.id = G.nextId++;
    this.kind = o.kind; this.team = o.team;
    this.x = o.x; this.y = o.y;
    this.radius = o.radius || 14;
    this.maxHp = o.hp; this.hp = o.hp;
    this.atk = o.atk || 0; this.atkRange = o.range || 0;
    this.atkSpeed = o.atkSpeed || 1; this.atkTimer = rand(0, 0.5);
    this.moveSpeed = o.speed || 0;
    this.alive = true;
    this.stunTimer = 0; this.shield = 0;
    this.buffSpeed = 1; this.buffTimer = 0;
    this.flashTimer = 0;
    this.faceAngle = o.team === 'blue' ? 0 : Math.PI;
  }
  get color() { return TEAM_COLOR[this.team]; }
  updateCommon(dt) {
    if (this.stunTimer > 0) this.stunTimer -= dt;
    if (this.buffTimer > 0) { this.buffTimer -= dt; if (this.buffTimer <= 0) this.buffSpeed = 1; }
    if (this.atkTimer > 0) this.atkTimer -= dt;
    if (this.flashTimer > 0) this.flashTimer -= dt;
  }
  applyBuff(mul, dur) { this.buffSpeed = Math.max(this.buffSpeed, mul); this.buffTimer = Math.max(this.buffTimer, dur); }
}

class Hero extends Unit {
  constructor(o) {
    super({ kind: 'hero', ...o, hp: o.def.maxHp, atk: o.def.atk, range: o.def.atkRange, atkSpeed: o.def.atkSpeed, speed: o.def.moveSpeed, radius: 16, name: o.def.name });
    this.def = o.def; this.isPlayer = !!o.isPlayer;
    this.level = 1; this.exp = 0; this.gold = 0;
    this.maxMp = 220; this.mp = 220; this.mpRegen = 11;
    this.kills = 0; this.deaths = 0;
    this.skills = o.def.skills.map(s => ({ ...s, cdTimer: 0 }));
    this.cdr = 0;
    this.lane = o.lane || 'mid';
    this.path = o.team === 'blue' ? LANES[this.lane] : [...LANES[this.lane]].reverse();
    this.wp = 1;
    this.retreating = false; this.retreatTarget = null;
    this.recalling = false; this.recallTimer = 0;
    this.deadTimer = 0;
    this.aiSkillTimer = rand(1.5, 3.5);
    this.items = {};
    this.moveTarget = null;
  }
  get expNeed() { return 60 + this.level * 45; }
  get effectiveAtk() { return this.atk; }
}

class Minion extends Unit {
  constructor(o) {
    super({ kind: 'minion', ...o, hp: o.hp, atk: o.atk, range: o.range, atkSpeed: o.atkSpeed, speed: o.speed, radius: o.ranged ? 8 : 10 });
    this.path = o.path; this.wp = 1; this.ranged = o.ranged; this.endBase = o.team === 'blue' ? MAP.RED_BASE : MAP.BLUE_BASE;
  }
}

class Tower extends Unit {
  constructor(o) {
    super({ kind: 'tower', ...o, hp: o.hp, atk: o.atk, range: o.range || 235, atkSpeed: 0.8, speed: 0, radius: 20 });
    this.tier = o.tier;
  }
}

class Nexus extends Unit {
  constructor(o) {
    super({ kind: 'nexus', ...o, hp: o.hp, atk: o.atk, range: o.range || 200, atkSpeed: 0.8, speed: 0, radius: 28 });
  }
}

/* ---------- 初始化 ---------- */
function initGame() {
  G.units = []; G.projectiles = []; G.particles = []; G.texts = []; G.rings = []; G.areas = [];
  G.time = 0; G.kills = { blue: 0, red: 0 }; G.spawnTimer = 0; G.paused = false; G.shopCache = null;
  $('killFeed').innerHTML = '';

  const def = HEROES.find(h => h.id === G.selectedHero) || HEROES[0];
  const p = new Hero({ team: 'blue', def, isPlayer: true, lane: 'mid', x: MAP.BLUE_BASE.x + 40, y: MAP.BLUE_BASE.y - 45 });
  G.player = p; G.units.push(p);

  const allyLanes = ['top', 'mid', 'mid', 'bot'];
  allyLanes.forEach((lane, i) => {
    const d = HEROES[(i + 1) % HEROES.length];
    G.units.push(new Hero({ team: 'blue', def: d, lane, x: MAP.BLUE_BASE.x + rand(-30, 50), y: MAP.BLUE_BASE.y + rand(-45, 30) }));
  });
  const enemyLanes = ['top', 'top', 'mid', 'mid', 'bot'];
  enemyLanes.forEach((lane, i) => {
    const d = HEROES[i % HEROES.length];
    G.units.push(new Hero({ team: 'red', def: d, lane, x: MAP.RED_BASE.x + rand(-45, 30), y: MAP.RED_BASE.y + rand(-30, 45) }));
  });

  spawnTowers();
  G.units.push(new Nexus({ team: 'blue', x: MAP.BLUE_BASE.x, y: MAP.BLUE_BASE.y, hp: 4200, atk: 72, range: 205, name: '蓝色水晶' }));
  G.units.push(new Nexus({ team: 'red', x: MAP.RED_BASE.x, y: MAP.RED_BASE.y, hp: 4200, atk: 72, range: 205, name: '红色水晶' }));

  spawnWave('blue'); spawnWave('red');
  $('shopPanel').classList.add('hidden');
  $('pauseOverlay').classList.add('hidden');
  $('recallStatus').textContent = '';
  $('portraitIcon').textContent = def.icon;
  $('portrait').style.background = `linear-gradient(160deg, ${def.color}, #16203a)`;
  ['Q', 'E', 'R'].forEach((k, i) => { $('skillIcon' + (i + 1)).textContent = k; });
  updateUI();
}

function spawnTowers() {
  for (const lane of ['top', 'mid', 'bot']) {
    const path = LANES[lane];
    [[0.19, '外塔', 1900, 74], [0.40, '高地塔', 2350, 90]].forEach(([t, nm, hp, atk]) => {
      const pos = pointAt(path, t);
      G.units.push(new Tower({ team: 'blue', x: pos.x, y: pos.y, hp, atk, tier: nm, name: '我方' + nm }));
    });
    [[0.81, '外塔', 1900, 74], [0.60, '高地塔', 2350, 90]].forEach(([t, nm, hp, atk]) => {
      const pos = pointAt(path, t);
      G.units.push(new Tower({ team: 'red', x: pos.x, y: pos.y, hp, atk, tier: nm, name: '敌方' + nm }));
    });
  }
}

function spawnWave(team) {
  const scale = 1 + Math.min(1.2, G.time / 480);
  for (const lane of ['top', 'mid', 'bot']) {
    const base = team === 'blue' ? MAP.BLUE_BASE : MAP.RED_BASE;
    const path = team === 'blue' ? LANES[lane] : [...LANES[lane]].reverse();
    for (let i = 0; i < 3; i++) {
      const ranged = i === 0;
      G.units.push(new Minion({
        team, lane, path, ranged,
        x: base.x + rand(-22, 22), y: base.y + rand(-22, 22),
        hp: (ranged ? 250 : 340) * scale,
        atk: (ranged ? 26 : 33) * scale,
        range: ranged ? 155 : 64,
        atkSpeed: ranged ? 0.9 : 1.05,
        speed: ranged ? 76 : 83
      }));
    }
  }
}

/* ---------- 目标查找与移动 ---------- */
function findEnemy(unit, range) {
  let best = null, bd = range;
  for (const u of G.units) {
    if (!u.alive || u.team === unit.team) continue;
    const d = dist(unit, u);
    if (d < bd) { bd = d; best = u; }
  }
  return best;
}
function findTowerTarget(tower) {
  let minion = null, struct = null, md = Infinity, sd = Infinity;
  for (const u of G.units) {
    if (!u.alive || u.team === tower.team) continue;
    const d = dist(tower, u);
    if (d > tower.atkRange) continue;
    if (u.kind === 'minion' && d < md) { md = d; minion = u; }
    if ((u.kind === 'hero' || u.kind === 'nexus') && d < sd) { sd = d; struct = u; }
  }
  return minion || struct;
}
function moveToward(unit, tx, ty, dt, mul = 1) {
  const dx = tx - unit.x, dy = ty - unit.y;
  const d = Math.hypot(dx, dy);
  if (d < 1) return true;
  const step = unit.moveSpeed * unit.buffSpeed * mul * dt;
  if (step >= d) { unit.x = tx; unit.y = ty; return true; }
  unit.x += dx / d * step; unit.y += dy / d * step;
  unit.faceAngle = Math.atan2(dy, dx);
  return false;
}
function moveAlongPath(unit, dir, dt, mul = 1) {
  if (dir > 0) {
    if (unit.wp >= unit.path.length) return false;
    if (moveToward(unit, unit.path[unit.wp].x, unit.path[unit.wp].y, dt, mul)) { unit.wp++; return true; }
  } else {
    const idx = unit.wp - 1;
    if (idx < 0) return false;
    if (moveToward(unit, unit.path[idx].x, unit.path[idx].y, dt, mul)) { unit.wp--; return true; }
  }
  return false;
}

/* ---------- 攻击 / 投射物 ---------- */
function tryAttack(unit, target) {
  if (unit.atkTimer > 0 || !target || !target.alive) return;
  unit.atkTimer = 1 / unit.atkSpeed;
  unit.faceAngle = angle(unit, target);
  const dmg = unit.kind === 'hero' ? unit.effectiveAtk : unit.atk;
  const projectile = unit.kind === 'tower' || unit.kind === 'nexus' || (unit.kind === 'minion' && unit.ranged) || (unit.kind === 'hero' && unit.atkRange > 130);
  if (projectile) {
    spawnProjectile({
      x: unit.x, y: unit.y, tx: target.x, ty: target.y,
      team: unit.team, source: unit, dmg,
      speed: unit.kind === 'tower' ? 640 : unit.kind === 'nexus' ? 560 : 540,
      color: unit.color ? unit.color.light : '#fff',
      radius: unit.kind === 'tower' ? 6 : 4,
      life: 1.5
    });
  } else {
    dealDamage(target, dmg, unit);
    spawnHit(target.x, target.y, unit.color ? unit.color.light : '#fff');
  }
}
function spawnProjectile(o) {
  const a = Math.atan2(o.ty - o.y, o.tx - o.x);
  G.projectiles.push({
    x: o.x, y: o.y, vx: Math.cos(a) * o.speed, vy: Math.sin(a) * o.speed,
    team: o.team, source: o.source, dmg: o.dmg, color: o.color, radius: o.radius || 4,
    speed: o.speed, life: o.life || 1.5, targetRef: o.targetRef || null,
    homing: !!o.homing, stun: o.stun || 0, pierce: !!o.pierce, hitSet: new Set()
  });
}
function updateProjectiles(dt) {
  for (const p of G.projectiles) {
    if (p.homing && p.targetRef && p.targetRef.alive) {
      const a = Math.atan2(p.targetRef.y - p.y, p.targetRef.x - p.x);
      p.vx = Math.cos(a) * p.speed; p.vy = Math.sin(a) * p.speed;
    }
    p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt;
    for (const u of G.units) {
      if (!u.alive || u.team === p.team || p.hitSet.has(u.id)) continue;
      if (dist(p, u) < u.radius + p.radius) {
        dealDamage(u, p.dmg, p.source, { stun: p.stun });
        p.hitSet.add(u.id);
        spawnHit(u.x, u.y, p.color);
        if (!p.pierce) { p.life = 0; break; }
      }
    }
  }
  G.projectiles = G.projectiles.filter(p => p.life > 0 && p.x > -60 && p.x < MAP.W + 60 && p.y > -60 && p.y < MAP.H + 60);
}

/* ---------- 伤害 / 死亡 ---------- */
function dealDamage(target, amount, source, opts = {}) {
  if (!target.alive) return;
  let dmg = amount;
  if (target.shield > 0) {
    const s = Math.min(target.shield, dmg);
    target.shield -= s; dmg -= s;
    if (s > 0) addText(target.x, target.y - 14, Math.round(s), '#7fe3ff');
  }
  if (dmg > 0) {
    target.hp -= dmg; target.flashTimer = 0.12;
    addText(target.x + rand(-9, 9), target.y - 12, Math.round(dmg), '#ff7a7a');
    spawnParticles(target.x, target.y, target.color ? target.color.light : '#fff', 5, 95);
  }
  if (opts.stun && target.kind !== 'tower' && target.kind !== 'nexus') target.stunTimer = Math.max(target.stunTimer, opts.stun);
  if (target.kind === 'hero' && target.recalling) { target.recalling = false; target.recallTimer = 0; }
  if (target.hp <= 0) { target.hp = 0; onDeath(target, source); }
}
function onDeath(v, source) {
  v.alive = false;
  spawnParticles(v.x, v.y, v.color ? v.color.main : '#fff', v.kind === 'hero' ? 26 : 12, 180);
  if (v.kind === 'hero') {
    v.deaths++; v.deadTimer = Math.min(15, 5 + v.level * 1.2);
    v.recalling = false; v.retreating = false; v.moveTarget = null;
    if (source && source.team !== v.team) {
      if (source.kind === 'hero') { source.kills++; source.gold += 300; gainExp(source, 160); }
      if (source.team === 'blue') G.kills.blue++; else G.kills.red++;
      if (source.kind === 'hero' && source.def) addKillFeed(source, v);
      else addKillFeedText(`${source.name || '防御塔'} 击败了 ${v.def ? v.def.name : '英雄'}`);
    }
  } else if (v.kind === 'minion') {
    if (source && source.kind === 'hero') { source.gold += 28; gainExp(source, 26); }
    else {
      for (const u of G.units) {
        if (u.kind === 'hero' && u.team !== v.team && u.alive && dist(u, v) < 280) { u.gold += 12; gainExp(u, 10); }
      }
    }
  } else if (v.kind === 'tower') {
    for (const u of G.units) {
      if (u.kind === 'hero' && u.team !== v.team && u.alive) { u.gold += 150; gainExp(u, 100); }
    }
    addKillFeedText(`${v.name} 已被摧毁！`);
  } else if (v.kind === 'nexus') {
    endGame(v.team === 'red');
  }
}
function gainExp(h, amount) {
  if (h.kind !== 'hero') return;
  h.exp += amount;
  while (h.exp >= h.expNeed) {
    h.exp -= h.expNeed; h.level++;
    h.maxHp += 85; h.hp = Math.min(h.maxHp, h.hp + h.maxHp * 0.35);
    h.atk += 7; h.maxMp += 20; h.mp = h.maxMp;
    spawnParticles(h.x, h.y, '#f5d76e', 20, 150);
    addText(h.x, h.y - 32, 'LEVEL UP!', '#f5d76e');
  }
}

/* ---------- 技能 ---------- */
function castSkill(h, idx, aim) {
  if (!h.alive || h.stunTimer > 0) return false;
  const s = h.skills[idx];
  if (!s || s.cdTimer > 0 || h.mp < s.mp) return false;
  s.cdTimer = s.cd * (1 - h.cdr);
  h.mp -= s.mp;
  h.faceAngle = Math.atan2(aim.y - h.y, aim.x - h.x);
  const col = h.color, team = h.team;

  if (s.type === 'projectile') {
    const base = angle(h, aim);
    for (let i = 0; i < s.count; i++) {
      const a = base + (i - (s.count - 1) / 2) * s.spread;
      spawnProjectile({
        x: h.x + Math.cos(a) * 18, y: h.y + Math.sin(a) * 18,
        tx: h.x + Math.cos(a) * 300, ty: h.y + Math.sin(a) * 300,
        team, source: h, dmg: s.dmg, speed: s.speed, color: col.light, radius: 6,
        life: s.range / s.speed, stun: s.stun, pierce: s.pierce
      });
    }
    spawnParticles(h.x, h.y, col.light, 8, 110);
  } else if (s.type === 'homing') {
    const targets = G.units.filter(u => u.alive && u.team !== team && dist(h, u) < s.range + 120)
      .sort((a, b) => dist(h, a) - dist(h, b)).slice(0, s.count);
    for (const t of targets) {
      spawnProjectile({ x: h.x, y: h.y, tx: t.x, ty: t.y, team, source: h, dmg: s.dmg, speed: s.speed, color: col.light, radius: 6, life: 2.2, homing: true, targetRef: t });
    }
    spawnParticles(h.x, h.y, col.light, 10, 120);
  } else if (s.type === 'aoe') {
    let tx = aim.x, ty = aim.y;
    const d = dist(h, { x: tx, y: ty });
    if (d > s.range) { const k = s.range / d; tx = h.x + (tx - h.x) * k; ty = h.y + (ty - h.y) * k; }
    G.areas.push({ x: tx, y: ty, radius: s.radius, timer: s.delay || 0, delay: s.delay || 0, dmg: s.dmg, team, source: h, color: col.main, stun: s.stun || 0 });
  } else if (s.type === 'aoe_self') {
    G.areas.push({ x: h.x, y: h.y, radius: s.radius, timer: 0, delay: 0, dmg: s.dmg, team, source: h, color: col.main, stun: 0 });
    spawnParticles(h.x, h.y, col.main, 16, 145);
  } else if (s.type === 'self') {
    h.shield = Math.max(h.shield, s.shield); h.applyBuff(s.speedBuff, s.duration);
    addText(h.x, h.y - 30, '霸体!', '#7fe3ff');
    spawnParticles(h.x, h.y, '#7fe3ff', 16, 125);
  } else if (s.type === 'heal') {
    const before = h.hp; h.hp = Math.min(h.maxHp, h.hp + s.heal);
    h.applyBuff(s.speedBuff, s.duration);
    addText(h.x, h.y - 30, '+' + Math.round(h.hp - before), '#6bff9a');
    spawnParticles(h.x, h.y, '#6bff9a', 22, 140);
  }
  return true;
}
function updateAreas(dt) {
  for (const a of G.areas) {
    a.timer -= dt;
    if (a.timer <= 0) {
      for (const u of G.units) {
        if (!u.alive || u.team === a.team) continue;
        if (dist(a, u) <= a.radius + u.radius * 0.5) dealDamage(u, a.dmg, a.source, { stun: a.stun });
      }
      spawnExplosion(a.x, a.y, a.radius, a.color);
      a.done = true;
    }
  }
  G.areas = G.areas.filter(a => !a.done);
}
function tryPlayerSkill(i) {
  const h = G.player;
  if (!h || !h.alive || G.paused) return;
  let aim = { x: mouse.x, y: mouse.y };
  if (dist(h, aim) < 25) aim = { x: h.x + Math.cos(h.faceAngle) * 200, y: h.y + Math.sin(h.faceAngle) * 200 };
  if (castSkill(h, i, aim)) { h.recalling = false; h.recallTimer = 0; }
}

/* ---------- 特效 ---------- */
function spawnParticles(x, y, color, count, speed) {
  for (let i = 0; i < count; i++) {
    const a = Math.random() * Math.PI * 2, s = rand(speed * 0.3, speed);
    G.particles.push({ x, y, vx: Math.cos(a) * s, vy: Math.sin(a) * s, life: rand(0.25, 0.6), max: 0.6, color, size: rand(2, 4.5) });
  }
}
function spawnHit(x, y, color) { spawnParticles(x, y, color, 6, 110); }
function spawnExplosion(x, y, r, color) {
  spawnParticles(x, y, color, 26, r * 2.4);
  G.rings.push({ x, y, r: 8, maxR: r, life: 0.35 });
  G.rings[G.rings.length - 1].color = color;
}
function addText(x, y, text, color) { G.texts.push({ x, y, text, color, life: 0.9, vy: -44 }); }
function addKillFeed(killer, victim) {
  const kb = killer.team === 'blue' ? 'b' : 'r', vb = victim.team === 'blue' ? 'b' : 'r';
  const div = document.createElement('div'); div.className = 'kill-msg';
  div.innerHTML = `<span class="${kb}">${killer.def.name}</span> 击败 <span class="${vb}">${victim.def.name}</span>`;
  $('killFeed').appendChild(div);
  setTimeout(() => div.remove(), 3500);
}
function addKillFeedText(text) {
  const div = document.createElement('div'); div.className = 'kill-msg'; div.textContent = text;
  $('killFeed').appendChild(div);
  setTimeout(() => div.remove(), 3500);
}
function updateEffects(dt) {
  for (const p of G.particles) { p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= 0.94; p.vy *= 0.94; p.life -= dt; }
  G.particles = G.particles.filter(p => p.life > 0);
  for (const t of G.texts) { t.y += t.vy * dt; t.vy *= 0.96; t.life -= dt; }
  G.texts = G.texts.filter(t => t.life > 0);
  for (const r of G.rings) { r.life -= dt; r.r = r.maxR * (1 - r.life / 0.35); }
  G.rings = G.rings.filter(r => r.life > 0);
}

/* ---------- 单位更新 ---------- */
function updateHero(h, dt) {
  h.updateCommon(dt);
  h.mp = Math.min(h.maxMp, h.mp + h.mpRegen * dt);
  for (const s of h.skills) if (s.cdTimer > 0) s.cdTimer = Math.max(0, s.cdTimer - dt);
  if (!h.alive) { h.deadTimer -= dt; if (h.deadTimer <= 0) revive(h); return; }
  if (h.recalling) {
    // 玩家按方向键可打断回城
    const moving = h.isPlayer && (keys['w'] || keys['a'] || keys['s'] || keys['d'] || keys['arrowup'] || keys['arrowdown'] || keys['arrowleft'] || keys['arrowright']);
    if (moving) {
      h.recalling = false; h.recallTimer = 0;
    } else {
      h.recallTimer -= dt;
      if (h.recallTimer <= 0) {
        const base = h.team === 'blue' ? MAP.BLUE_BASE : MAP.RED_BASE;
        h.x = base.x + rand(-24, 24); h.y = base.y + rand(-24, 24);
        h.hp = h.maxHp; h.mp = h.maxMp; h.recalling = false; h.retreating = false; h.wp = 1;
        spawnParticles(h.x, h.y, '#f5d76e', 24, 180);
      }
      return;
    }
  }
  if (h.isPlayer) updatePlayer(h, dt); else updateAI(h, dt);
}
function revive(h) {
  h.alive = true; h.hp = h.maxHp; h.mp = h.maxMp;
  const base = h.team === 'blue' ? MAP.BLUE_BASE : MAP.RED_BASE;
  h.x = base.x + rand(-24, 24); h.y = base.y + rand(-24, 24);
  h.wp = 1; h.retreating = false; h.stunTimer = 0; h.shield = 0; h.moveTarget = null;
  spawnParticles(h.x, h.y, '#ffffff', 20, 160);
}
function updatePlayer(h, dt) {
  if (h.stunTimer > 0) return;
  let dx = 0, dy = 0;
  if (keys['w'] || keys['arrowup']) dy -= 1;
  if (keys['s'] || keys['arrowdown']) dy += 1;
  if (keys['a'] || keys['arrowleft']) dx -= 1;
  if (keys['d'] || keys['arrowright']) dx += 1;
  if (dx || dy) {
    const n = norm(dx, dy);
    h.x = clamp(h.x + n.x * h.moveSpeed * h.buffSpeed * dt, 22, MAP.W - 22);
    h.y = clamp(h.y + n.y * h.moveSpeed * h.buffSpeed * dt, 22, MAP.H - 22);
    h.faceAngle = Math.atan2(n.y, n.x);
    h.moveTarget = null; h.recalling = false; h.recallTimer = 0;
  } else if (h.moveTarget) {
    if (moveToward(h, h.moveTarget.x, h.moveTarget.y, dt)) h.moveTarget = null;
  }
  if (keys['space'] || keys['j']) {
    const t = findEnemy(h, h.atkRange + 28);
    if (t) { h.recalling = false; h.recallTimer = 0; tryAttack(h, t); }
  }
}
function updateAI(h, dt) {
  if (h.stunTimer > 0) return;
  if (!h.retreating && h.hp < h.maxHp * 0.26) {
    h.retreating = true;
    h.retreatTarget = h.team === 'blue' ? MAP.BLUE_BASE : MAP.RED_BASE;
    h.moveTarget = null;
  }
  if (h.retreating) {
    const base = h.retreatTarget;
    if (dist(h, base) < 52) {
      h.hp = Math.min(h.maxHp, h.hp + h.maxHp * 0.6); h.mp = h.maxMp;
      h.retreating = false; h.wp = 1;
      spawnParticles(h.x, h.y, '#7fe3ff', 12, 120);
    } else moveToward(h, base.x, base.y, dt);
    return;
  }
  const t = findEnemy(h, h.atkRange + 48);
  if (t) {
    const d = dist(h, t);
    if (d > h.atkRange * 0.92) moveToward(h, t.x, t.y, dt);
    else tryAttack(h, t);
    h.aiSkillTimer -= dt;
    if (h.aiSkillTimer <= 0) {
      h.aiSkillTimer = rand(2.2, 4.2);
      const usable = h.skills.map((s, i) => ({ s, i })).filter(o => o.s.cdTimer <= 0 && h.mp >= o.s.mp);
      if (usable.length && d < h.atkRange + 220) {
        const pick = usable[Math.floor(Math.random() * usable.length)];
        castSkill(h, pick.i, { x: t.x, y: t.y });
      }
    }
  } else {
    moveAlongPath(h, 1, dt);
  }
}
function updateMinion(m, dt) {
  m.updateCommon(dt);
  const t = findEnemy(m, m.atkRange + 22);
  if (t) {
    const d = dist(m, t);
    if (d > m.atkRange) moveToward(m, t.x, t.y, dt, 0.9);
    else tryAttack(m, t);
  } else {
    if (m.wp >= m.path.length) {
      if (dist(m, m.endBase) > 70) moveToward(m, m.endBase.x + rand(-16, 16), m.endBase.y + rand(-16, 16), dt, 0.9);
    } else moveAlongPath(m, 1, dt, 0.9);
  }
}
function updateStructure(u, dt) {
  u.updateCommon(dt);
  const t = findTowerTarget(u);
  if (t) tryAttack(u, t);
}

/* ---------- 渲染 ---------- */
let decor = [];
function initDecor() {
  decor = [];
  for (let i = 0; i < 170; i++) decor.push({ x: rand(0, MAP.W), y: rand(0, MAP.H), r: rand(2, 7), a: rand(0.04, 0.14), c: Math.random() > 0.5 ? '#2f6b3a' : '#122f1e' });
  for (let i = 0; i < 30; i++) decor.push({ x: rand(50, MAP.W - 50), y: rand(50, MAP.H - 50), r: rand(16, 32), a: 0.2, c: '#0d3a1c', bush: true });
}
function drawBasePlatform(pos, col) {
  const g = ctx.createRadialGradient(pos.x, pos.y, 8, pos.x, pos.y, 95);
  g.addColorStop(0, col.glow); g.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = g; ctx.beginPath(); ctx.arc(pos.x, pos.y, 95, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = col.main; ctx.globalAlpha = 0.45; ctx.lineWidth = 3;
  ctx.beginPath(); ctx.arc(pos.x, pos.y, 58, 0, Math.PI * 2); ctx.stroke(); ctx.globalAlpha = 1;
}
function drawMap() {
  ctx.fillStyle = '#1d3a24'; ctx.fillRect(0, 0, MAP.W, MAP.H);
  for (const d of decor) {
    ctx.fillStyle = d.c; ctx.globalAlpha = d.a;
    ctx.beginPath(); ctx.arc(d.x, d.y, d.r, 0, Math.PI * 2); ctx.fill();
  }
  ctx.globalAlpha = 1;
  ctx.save();
  ctx.strokeStyle = 'rgba(46,124,184,0.34)'; ctx.lineWidth = 74; ctx.lineCap = 'round';
  ctx.beginPath(); ctx.moveTo(-40, 110); ctx.bezierCurveTo(300, 250, 720, 480, MAP.W + 40, MAP.H - 110); ctx.stroke();
  ctx.strokeStyle = 'rgba(96,182,232,0.13)'; ctx.lineWidth = 44; ctx.stroke();
  ctx.restore();
  for (const lane of ['top', 'mid', 'bot']) {
    const path = LANES[lane];
    ctx.save(); ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    ctx.strokeStyle = '#433f31'; ctx.lineWidth = 78;
    ctx.beginPath(); ctx.moveTo(path[0].x, path[0].y); for (let i = 1; i < path.length; i++) ctx.lineTo(path[i].x, path[i].y); ctx.stroke();
    ctx.strokeStyle = '#5b5540'; ctx.lineWidth = 60; ctx.stroke();
    ctx.strokeStyle = 'rgba(200,185,140,0.10)'; ctx.lineWidth = 2; ctx.setLineDash([12, 18]); ctx.stroke();
    ctx.restore();
  }
  drawBasePlatform(MAP.BLUE_BASE, TEAM_COLOR.blue);
  drawBasePlatform(MAP.RED_BASE, TEAM_COLOR.red);
}
function drawBar(x, y, w, h, ratio, color, border) {
  ctx.fillStyle = 'rgba(6,10,16,0.9)'; ctx.fillRect(x - 1, y - 1, w + 2, h + 2);
  ctx.fillStyle = color; ctx.fillRect(x, y, w * clamp(ratio, 0, 1), h);
  if (border) { ctx.strokeStyle = border; ctx.lineWidth = 1; ctx.strokeRect(x - 0.5, y - 0.5, w + 1, h + 1); }
}
function drawHero(u) {
  const col = TEAM_COLOR[u.team];
  ctx.fillStyle = 'rgba(0,0,0,0.32)'; ctx.beginPath(); ctx.ellipse(u.x, u.y + 11, 15, 6, 0, 0, Math.PI * 2); ctx.fill();
  if (u.isPlayer) {
    ctx.save(); ctx.strokeStyle = '#f5d76e'; ctx.lineWidth = 2.5;
    ctx.globalAlpha = 0.65 + Math.sin(G.time * 4) * 0.25;
    ctx.beginPath(); ctx.arc(u.x, u.y, u.radius + 7, 0, Math.PI * 2); ctx.stroke();
    ctx.restore();
  }
  const g = ctx.createRadialGradient(u.x - 5, u.y - 6, 2, u.x, u.y, 19);
  g.addColorStop(0, u.flashTimer > 0 ? '#ffffff' : col.light);
  g.addColorStop(1, u.flashTimer > 0 ? '#ffffff' : col.dark);
  ctx.fillStyle = g; ctx.beginPath(); ctx.arc(u.x, u.y, u.radius, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = u.def.color; ctx.lineWidth = 2.5; ctx.stroke();
  ctx.fillStyle = '#fff'; ctx.font = 'bold 15px Microsoft YaHei'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText(u.def.icon, u.x, u.y + 1);
  ctx.textBaseline = 'alphabetic'; ctx.textAlign = 'left';
  drawBar(u.x - 17, u.y - 24, 34, 5, u.hp / u.maxHp, barColor(u.hp / u.maxHp), col.main);
  ctx.fillStyle = '#f5d76e'; ctx.font = 'bold 10px Arial'; ctx.fillText('Lv' + u.level, u.x + 19, u.y - 15);
  if (!u.isPlayer) { ctx.fillStyle = col.light; ctx.font = '10px Microsoft YaHei'; ctx.textAlign = 'center'; ctx.fillText(u.def.name, u.x, u.y - 30); ctx.textAlign = 'left'; }
  if (u.shield > 0) { ctx.strokeStyle = 'rgba(127,227,255,0.85)'; ctx.lineWidth = 2; ctx.beginPath(); ctx.arc(u.x, u.y, u.radius + 3, 0, Math.PI * 2); ctx.stroke(); }
  if (u.stunTimer > 0) { ctx.fillStyle = '#facc15'; ctx.font = 'bold 13px Arial'; ctx.textAlign = 'center'; ctx.fillText('✦', u.x, u.y - 34); ctx.textAlign = 'left'; }
  if (u.recalling) {
    ctx.save(); ctx.strokeStyle = '#7fe3ff'; ctx.lineWidth = 2;
    const a0 = G.time * 4; ctx.beginPath(); ctx.arc(u.x, u.y, u.radius + 11, a0, a0 + Math.PI * 1.5); ctx.stroke(); ctx.restore();
  }
}
function drawMinion(u) {
  const col = TEAM_COLOR[u.team];
  ctx.fillStyle = 'rgba(0,0,0,0.25)'; ctx.beginPath(); ctx.ellipse(u.x, u.y + 5, u.radius, 4, 0, 0, Math.PI * 2); ctx.fill();
  ctx.fillStyle = u.flashTimer > 0 ? '#ffffff' : (u.ranged ? col.light : col.main);
  ctx.beginPath(); ctx.arc(u.x, u.y, u.radius, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = col.dark; ctx.lineWidth = 1.5; ctx.stroke();
  drawBar(u.x - 10, u.y - 15, 20, 3, u.hp / u.maxHp, barColor(u.hp / u.maxHp));
}
function drawTower(u) {
  const col = TEAM_COLOR[u.team];
  ctx.fillStyle = 'rgba(0,0,0,0.35)'; ctx.beginPath(); ctx.ellipse(u.x, u.y + 12, 22, 9, 0, 0, Math.PI * 2); ctx.fill();
  ctx.fillStyle = col.dark; ctx.beginPath(); ctx.arc(u.x, u.y, 18, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = col.main; ctx.lineWidth = 3; ctx.stroke();
  ctx.fillStyle = u.flashTimer > 0 ? '#ffffff' : col.light;
  ctx.beginPath(); ctx.moveTo(u.x, u.y - 14); ctx.lineTo(u.x + 11, u.y); ctx.lineTo(u.x, u.y + 14); ctx.lineTo(u.x - 11, u.y); ctx.closePath(); ctx.fill();
  ctx.strokeStyle = 'rgba(255,255,255,0.55)'; ctx.lineWidth = 1.5; ctx.stroke();
  drawBar(u.x - 22, u.y - 30, 44, 5, u.hp / u.maxHp, barColor(u.hp / u.maxHp), col.main);
}
function drawNexus(u) {
  const col = TEAM_COLOR[u.team];
  const pulse = 1 + Math.sin(G.time * 2.5) * 0.06;
  const g = ctx.createRadialGradient(u.x, u.y, 5, u.x, u.y, 50 * pulse);
  g.addColorStop(0, col.glow); g.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = g; ctx.beginPath(); ctx.arc(u.x, u.y, 50 * pulse, 0, Math.PI * 2); ctx.fill();
  ctx.save(); ctx.translate(u.x, u.y); ctx.rotate(G.time * 0.35);
  ctx.fillStyle = u.flashTimer > 0 ? '#ffffff' : col.main;
  ctx.beginPath(); ctx.moveTo(0, -26); ctx.lineTo(19, 0); ctx.lineTo(0, 26); ctx.lineTo(-19, 0); ctx.closePath(); ctx.fill();
  ctx.strokeStyle = col.light; ctx.lineWidth = 2.5; ctx.stroke(); ctx.restore();
  drawBar(u.x - 34, u.y - 48, 68, 7, u.hp / u.maxHp, barColor(u.hp / u.maxHp), col.main);
  ctx.fillStyle = col.light; ctx.font = '11px Microsoft YaHei'; ctx.textAlign = 'center'; ctx.fillText('水晶', u.x, u.y + 42); ctx.textAlign = 'left';
}
function drawEffects() {
  for (const a of G.areas) {
    ctx.save(); ctx.strokeStyle = a.color; ctx.lineWidth = 2;
    ctx.globalAlpha = 0.55 + 0.35 * Math.sin(G.time * 12);
    ctx.setLineDash([6, 6]);
    ctx.beginPath(); ctx.arc(a.x, a.y, a.radius, 0, Math.PI * 2); ctx.stroke();
    ctx.globalAlpha = 0.12; ctx.fillStyle = a.color; ctx.fill(); ctx.restore();
  }
  for (const p of G.projectiles) {
    ctx.save(); ctx.shadowColor = p.color; ctx.shadowBlur = 10;
    ctx.fillStyle = p.color; ctx.beginPath(); ctx.arc(p.x, p.y, p.radius, 0, Math.PI * 2); ctx.fill(); ctx.restore();
  }
  for (const pt of G.particles) {
    ctx.globalAlpha = clamp(pt.life / pt.max, 0, 1); ctx.fillStyle = pt.color;
    ctx.beginPath(); ctx.arc(pt.x, pt.y, pt.size, 0, Math.PI * 2); ctx.fill();
  }
  ctx.globalAlpha = 1;
  for (const r of G.rings) {
    ctx.globalAlpha = clamp(r.life / 0.35, 0, 1); ctx.strokeStyle = r.color || '#fff'; ctx.lineWidth = 4;
    ctx.beginPath(); ctx.arc(r.x, r.y, r.r, 0, Math.PI * 2); ctx.stroke();
  }
  ctx.globalAlpha = 1;
  for (const t of G.texts) {
    ctx.globalAlpha = clamp(t.life / 0.9, 0, 1);
    ctx.font = 'bold 15px Microsoft YaHei'; ctx.textAlign = 'center';
    ctx.strokeStyle = 'rgba(0,0,0,0.85)'; ctx.lineWidth = 3; ctx.strokeText(t.text, t.x, t.y);
    ctx.fillStyle = t.color; ctx.fillText(t.text, t.x, t.y);
  }
  ctx.globalAlpha = 1; ctx.textAlign = 'left';
}
function drawMinimap() {
  const sx = miniCanvas.width / MAP.W, sy = miniCanvas.height / MAP.H;
  mctx.fillStyle = '#1a3320'; mctx.fillRect(0, 0, miniCanvas.width, miniCanvas.height);
  mctx.strokeStyle = 'rgba(46,124,184,0.4)'; mctx.lineWidth = 10;
  mctx.beginPath(); mctx.moveTo(0, 18); mctx.bezierCurveTo(48, 38, 132, 78, 180, 102); mctx.stroke();
  mctx.strokeStyle = 'rgba(170,158,112,0.5)'; mctx.lineWidth = 3;
  for (const lane of ['top', 'mid', 'bot']) {
    const p = LANES[lane]; mctx.beginPath(); mctx.moveTo(p[0].x * sx, p[0].y * sy);
    for (let i = 1; i < p.length; i++) mctx.lineTo(p[i].x * sx, p[i].y * sy); mctx.stroke();
  }
  for (const u of G.units) {
    if (!u.alive) continue;
    const x = u.x * sx, y = u.y * sy;
    mctx.fillStyle = TEAM_COLOR[u.team].main;
    if (u.kind === 'hero') {
      mctx.beginPath(); mctx.arc(x, y, u.isPlayer ? 4 : 2.8, 0, Math.PI * 2); mctx.fill();
      if (u.isPlayer) { mctx.strokeStyle = '#fff'; mctx.lineWidth = 1.4; mctx.stroke(); }
    } else if (u.kind === 'tower') mctx.fillRect(x - 2, y - 2, 4, 4);
    else if (u.kind === 'nexus') { mctx.save(); mctx.translate(x, y); mctx.rotate(Math.PI / 4); mctx.fillRect(-4, -4, 8, 8); mctx.restore(); }
    else { mctx.globalAlpha = 0.6; mctx.fillRect(x - 1, y - 1, 2, 2); mctx.globalAlpha = 1; }
  }
}
function render() {
  if (G.phase === 'menu') return;
  drawMap();
  const list = G.units.filter(u => u.alive).sort((a, b) => a.y - b.y);
  for (const u of list) {
    if (u.kind === 'tower') drawTower(u);
    else if (u.kind === 'nexus') drawNexus(u);
    else if (u.kind === 'minion') drawMinion(u);
    else drawHero(u);
  }
  drawEffects();
  drawMinimap();
}

/* ---------- HUD / 商店 ---------- */
function updateUI() {
  const p = G.player; if (!p) return;
  $('hpBarFill').style.width = (p.hp / p.maxHp * 100) + '%';
  $('hpText').textContent = Math.ceil(p.hp) + ' / ' + p.maxHp;
  $('mpBarFill').style.width = (p.mp / p.maxMp * 100) + '%';
  $('mpText').textContent = Math.floor(p.mp) + ' / ' + p.maxMp;
  $('expBarFill').style.width = (p.exp / p.expNeed * 100) + '%';
  $('levelBadge').textContent = p.level;
  $('goldText').textContent = Math.floor(p.gold);
  $('blueKills').textContent = G.kills.blue;
  $('redKills').textContent = G.kills.red;
  const m = Math.floor(G.time / 60), s = Math.floor(G.time % 60);
  $('gameTime').textContent = String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
  p.skills.forEach((s, i) => {
    const slot = $('skillSlot' + (i + 1));
    const total = s.cd * (1 - p.cdr);
    slot.querySelector('.cooldown-mask').style.transform = `translateY(${clamp(s.cdTimer / total, 0, 1) * 100}%)`;
    slot.querySelector('.cd-text').textContent = s.cdTimer > 0.05 ? Math.ceil(s.cdTimer) : '';
    slot.style.opacity = p.mp >= s.mp ? '1' : '0.55';
  });
  if (p.recalling) $('recallStatus').textContent = '回城中 ' + p.recallTimer.toFixed(1) + 's';
  else if (!p.alive) $('recallStatus').textContent = '复活中 ' + Math.ceil(p.deadTimer) + 's';
  else $('recallStatus').textContent = '';
  renderShop();
}
function renderShop() {
  const p = G.player; if (!p) return;
  const key = Math.floor(p.gold) + '|' + JSON.stringify(p.items);
  if (G.shopCache === key) return;
  G.shopCache = key;
  $('shopItems').innerHTML = ITEMS.map(it => {
    const count = p.items[it.id] || 0;
    const price = Math.round(it.price * Math.pow(1.5, count) / 10) * 10;
    const ok = p.gold >= price;
    return `<div class="shop-item ${ok ? '' : 'disabled'}" data-id="${it.id}">
      <span class="item-lv">已购 ${count}</span>
      <div class="item-name">${it.name}</div>
      <div class="item-desc">${it.desc}</div>
      <div class="item-price">● ${price}</div>
    </div>`;
  }).join('');
  $('shopItems').querySelectorAll('.shop-item').forEach(el => el.addEventListener('click', () => buyItem(el.dataset.id)));
}
function buyItem(id) {
  const p = G.player; if (!p || !p.alive) return;
  const it = ITEMS.find(x => x.id === id); if (!it) return;
  const count = p.items[id] || 0;
  const price = Math.round(it.price * Math.pow(1.5, count) / 10) * 10;
  if (p.gold < price) return;
  p.gold -= price; p.items[id] = count + 1;
  if (it.stat === 'atk') p.atk += it.value;
  else if (it.stat === 'hp') { p.maxHp += it.value; p.hp += it.value; }
  else if (it.stat === 'speed') { p.moveSpeed += it.value; p.cdr = Math.min(0.5, p.cdr + (it.cdr || 0)); }
  else if (it.stat === 'aspd') p.atkSpeed *= 1 + it.value;
  else if (it.stat === 'mp') { p.maxMp += it.value; p.mp += it.value; }
  spawnParticles(p.x, p.y, '#f5d76e', 14, 130);
  G.shopCache = null; updateUI();
}
function endGame(win) {
  if (G.phase === 'over') return;
  G.phase = 'over';
  const p = G.player;
  $('resultTitle').textContent = win ? '胜 利' : '失 败';
  $('resultTitle').className = win ? 'win' : 'lose';
  $('resultStats').innerHTML = `本局时长 <b>${$('gameTime').textContent}</b><br>击杀 <b>${p.kills}</b> / 死亡 <b>${p.deaths}</b>　等级 <b>${p.level}</b><br>团队击杀 <b>${G.kills.blue}</b> : <b>${G.kills.red}</b>`;
  $('resultOverlay').classList.remove('hidden');
}
function togglePause() {
  if (G.phase !== 'playing') return;
  G.paused = !G.paused;
  $('pauseOverlay').classList.toggle('hidden', !G.paused);
}

/* ---------- 主循环 ---------- */
let lastTs = 0;
function loop(ts) {
  requestAnimationFrame(loop);
  const dt = Math.min(0.05, (ts - lastTs) / 1000 || 0);
  lastTs = ts;
  if (G.phase === 'playing' && !G.paused) {
    G.time += dt;
    G.spawnTimer += dt;
    if (G.spawnTimer >= 22) { G.spawnTimer = 0; spawnWave('blue'); spawnWave('red'); }
    for (const u of G.units) {
      if (u.kind === 'hero') updateHero(u, dt);
      else if (u.kind === 'minion') updateMinion(u, dt);
      else updateStructure(u, dt);
    }
    updateProjectiles(dt); updateAreas(dt); updateEffects(dt);
    G.units = G.units.filter(u => u.alive || u.kind === 'hero');
    updateUI();
  }
  render();
}

/* ---------- 事件绑定 ---------- */
window.addEventListener('keydown', e => {
  let k = e.key.toLowerCase();
  if (k === ' ') k = 'space';
  keys[k] = true;
  if (['space', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(k)) e.preventDefault();
  if (G.phase !== 'playing' || e.repeat) return;
  if (k === 'q') tryPlayerSkill(0);
  else if (k === 'e') tryPlayerSkill(1);
  else if (k === 'r') tryPlayerSkill(2);
  else if (k === 'b') { if (G.player && G.player.alive) { G.player.recalling = true; G.player.recallTimer = 3; G.player.moveTarget = null; } }
  else if (k === 'p') togglePause();
});
window.addEventListener('keyup', e => { let k = e.key.toLowerCase(); if (k === ' ') k = 'space'; keys[k] = false; });
window.addEventListener('blur', () => { for (const k in keys) keys[k] = false; });
canvas.addEventListener('mousemove', e => {
  const r = canvas.getBoundingClientRect();
  mouse.x = (e.clientX - r.left) / r.width * MAP.W;
  mouse.y = (e.clientY - r.top) / r.height * MAP.H;
});
canvas.addEventListener('mousedown', e => {
  if (e.button === 0 && G.phase === 'playing' && G.player && G.player.alive) {
    G.player.moveTarget = { x: mouse.x, y: mouse.y };
    G.player.recalling = false; G.player.recallTimer = 0;
  }
});
canvas.addEventListener('contextmenu', e => e.preventDefault());

function fitScreen() {
  const s = Math.min(window.innerWidth / MAP.W, window.innerHeight / MAP.H, 1.3);
  document.querySelector('.battle-wrap').style.transform = `scale(${s})`;
}
window.addEventListener('resize', fitScreen);

function buildHeroSelect() {
  const box = $('heroSelect');
  box.innerHTML = HEROES.map((h, i) => `
    <div class="hero-card ${i === 0 ? 'selected' : ''}" data-id="${h.id}">
      <div class="hero-avatar" style="background:linear-gradient(160deg, ${h.color}, #16203a)">${h.icon}</div>
      <div class="hero-name">${h.name}</div>
      <div class="hero-role">${h.role}</div>
      <div class="hero-desc">${h.desc}</div>
      <div class="hero-skills">${h.skills.map(s => `<div><b>${s.key}</b> ${s.name} · ${s.desc}</div>`).join('')}</div>
    </div>`).join('');
  box.querySelectorAll('.hero-card').forEach(c => c.addEventListener('click', () => {
    box.querySelectorAll('.hero-card').forEach(x => x.classList.remove('selected'));
    c.classList.add('selected');
    G.selectedHero = c.dataset.id;
  }));
}

$('startBtn').addEventListener('click', () => {
  $('startScreen').classList.add('hidden');
  $('gameScreen').classList.remove('hidden');
  fitScreen(); initDecor(); initGame();
  $('controlHint').classList.remove('hidden');
  G.phase = 'playing';
});
$('closeHint').addEventListener('click', () => $('controlHint').classList.add('hidden'));
$('restartBtn').addEventListener('click', () => {
  $('resultOverlay').classList.add('hidden');
  initGame(); G.phase = 'playing';
});
$('shopBtn').addEventListener('click', () => $('shopPanel').classList.toggle('hidden'));
$('shopClose').addEventListener('click', () => $('shopPanel').classList.add('hidden'));

buildHeroSelect();
initDecor();
requestAnimationFrame(loop);
