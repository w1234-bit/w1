/* ============ 峡谷之战 · 游戏配置 ============ */

const MAP = {
  W: 1120, H: 760,
  BLUE_BASE: { x: 115, y: 650 },
  RED_BASE:  { x: 1005, y: 110 }
};

/* 三条兵线的路点：从蓝方基地 -> 红方基地 */
const LANES = {
  top: [ { x: 115, y: 650 }, { x: 115, y: 108 }, { x: 1005, y: 108 } ],
  mid: [ { x: 115, y: 650 }, { x: 560, y: 380 }, { x: 1005, y: 110 } ],
  bot: [ { x: 115, y: 650 }, { x: 1005, y: 650 }, { x: 1005, y: 110 } ]
};

/* 可选英雄 */
const HEROES = [
  {
    id: 'houyi', name: '后羿', role: '射手', icon: '羿', color: '#e8a33d',
    desc: '远程持续输出，箭雨覆盖战场。走位放风筝，后期伤害爆炸。',
    maxHp: 740, atk: 54, atkRange: 235, atkSpeed: 1.35, moveSpeed: 155,
    skills: [
      { key: 'Q', name: '多重箭矢', cd: 5,  mp: 30,  type: 'projectile', count: 3, spread: 0.3,  dmg: 72,  speed: 560, range: 430, desc: '扇形三连箭' },
      { key: 'E', name: '落日霹雳', cd: 7,  mp: 45,  type: 'aoe',        count: 1, dmg: 115, radius: 88, range: 370, delay: 0.35, desc: '范围爆发伤害' },
      { key: 'R', name: '全日蚀',   cd: 26, mp: 100, type: 'projectile', count: 1, dmg: 300, speed: 920, range: 1000, stun: 1.2, pierce: true, desc: '全屏贯穿眩晕' }
    ]
  },
  {
    id: 'daji', name: '妲己', role: '法师', icon: '妲', color: '#c56bff',
    desc: '高爆发法师，一套技能秒杀脆皮。控制与收割兼备。',
    maxHp: 650, atk: 46, atkRange: 215, atkSpeed: 1.1, moveSpeed: 152,
    skills: [
      { key: 'Q', name: '灵魂冲击', cd: 4.5, mp: 30,  type: 'projectile', count: 1, dmg: 100, speed: 610, range: 430, desc: '直线法球' },
      { key: 'E', name: '偶像魅力', cd: 7,   mp: 50,  type: 'aoe',        count: 1, dmg: 90,  radius: 96, range: 350, stun: 1.1, desc: '范围眩晕控制' },
      { key: 'R', name: '女王崇拜', cd: 24,  mp: 100, type: 'homing',     count: 5, dmg: 74,  speed: 520, range: 500, desc: '五道追踪法球' }
    ]
  },
  {
    id: 'chengyaojin', name: '程咬金', role: '坦克', icon: '程', color: '#5fd39a',
    desc: '超强回血和护盾，冲锋陷阵的钢铁前排。越打越勇。',
    maxHp: 1150, atk: 60, atkRange: 100, atkSpeed: 1.0, moveSpeed: 148,
    skills: [
      { key: 'Q', name: '霸体',     cd: 9,  mp: 35,  type: 'self',     shield: 200, speedBuff: 1.45, duration: 3, desc: '获得护盾并加速' },
      { key: 'E', name: '巨斧猛击', cd: 6,  mp: 40,  type: 'aoe_self', count: 1, dmg: 120, radius: 125, desc: '近身范围横扫' },
      { key: 'R', name: '正义潜能', cd: 28, mp: 100, type: 'heal',     heal: 480, speedBuff: 1.3, duration: 4, desc: '巨额回血并加速' }
    ]
  }
];

/* 装备商店（可重复购买，价格递增） */
const ITEMS = [
  { id: 'sword',  name: '无尽战刃',   desc: '攻击力 +25',              price: 320, stat: 'atk',   value: 25 },
  { id: 'blade',  name: '破军',       desc: '攻击力 +45',              price: 620, stat: 'atk',   value: 45 },
  { id: 'armor',  name: '贤者的庇护', desc: '最大生命 +300',           price: 350, stat: 'hp',    value: 300 },
  { id: 'boots',  name: '冷静之靴',   desc: '移速 +18，冷却缩减 10%',  price: 300, stat: 'speed', value: 18, cdr: 0.1 },
  { id: 'fang',   name: '影刃',       desc: '攻击速度 +20%',           price: 400, stat: 'aspd',  value: 0.2 },
  { id: 'amulet', name: '圣杯',       desc: '最大法力 +150，回蓝加快', price: 280, stat: 'mp',    value: 150 }
];

/* 阵营配色 */
const TEAM_COLOR = {
  blue: { main: '#3aa0ff', dark: '#1452a8', light: '#8fd0ff', glow: 'rgba(58,160,255,0.55)' },
  red:  { main: '#ff4d4f', dark: '#a31621', light: '#ff9b9c', glow: 'rgba(255,77,79,0.55)' }
};
