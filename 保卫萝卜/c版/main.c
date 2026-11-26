/* ============================================================================
   保卫萝卜 · C 版（Win32 / GDI 单文件实现，零第三方依赖）
   ----------------------------------------------------------------------------
   玩法：怪物沿路径走向萝卜城堡，在 12 个炮塔位建造武器拦截。
         4 张地图 / 12 种武器 / 6 种怪物 / 精英词缀 / 魔王技能 / 15 波 + 无尽。
   编译：build.bat（MSVC）  或  gcc main.c -o carrot.exe -lgdi32 -lmsimg32 -lwinmm
   操作：左键选武器→点空地建造 / 点已有塔查看；U 升级  S 出售  T 切换目标
         空格 暂停  ESC 暂停菜单  1/2/3 倍速
   ========================================================================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI  3.14159265358979f
#define TAU 6.28318530717959f
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CLIENT_W 1196
#define CLIENT_H 680
#define PLAY_W   960
#define PLAY_H   600
#define PLAY_X   236
#define PLAY_Y   56
#define TOP_H    56
#define PANEL_W  236

#define MAX_ENEMIES 320
#define MAX_BULLETS 700
#define MAX_FX      500
#define MAX_FLOATS  80
#define MAX_ITEMS   40
#define MAX_WAVE_GROUPS 6
#define MAX_SLOTS   12

#define C(r,g,b) RGB(r,g,b)

/* ---------------------------------------------------------------- 地图数据 */
typedef struct { int x, y; } IPt;

typedef struct {
    const wchar_t *name;
    int theme;                  /* 0 草原 1 火山 2 冰雪 3 沙漠 */
    IPt path[12]; int npath;
    IPt slots[12]; int nslots;
} MapDef;

static const MapDef MAPS[4] = {
    { L"草原王国", 0,
      { {-30,140},{200,140},{200,320},{420,320},{420,120},{640,120},
        {640,440},{840,440},{840,300},{930,300} }, 10,
      { {120,220},{300,220},{300,440},{120,440},{320,60},{520,220},
        {520,440},{760,220},{760,60},{760,540},{500,540},{900,140} }, 12 },
    { L"熔岩火山", 1,
      { {-30,500},{150,500},{150,300},{350,300},{350,120},{560,120},
        {560,330},{730,330},{730,480},{930,480},{930,300} }, 11,
      { {60,380},{260,420},{60,220},{250,200},{450,220},{450,400},
        {640,220},{640,420},{820,220},{820,400},{650,540},{480,540} }, 12 },
    { L"冰雪王国", 2,
      { {-30,320},{160,320},{160,120},{380,120},{380,420},{600,420},
        {600,180},{820,180},{820,460},{930,460} }, 10,
      { {80,240},{240,240},{300,200},{240,40},{460,200},{460,340},
        {300,500},{520,260},{680,100},{740,260},{900,380},{900,540} }, 12 },
    { L"沙漠遗迹", 3,
      { {-30,180},{180,180},{180,420},{400,420},{400,180},{620,180},
        {620,480},{820,480},{820,260},{900,260} }, 10,
      { {90,300},{270,300},{270,520},{500,300},{500,90},{320,90},
        {550,330},{720,330},{720,140},{700,555},{890,160},{890,380} }, 12 },
};

/* ---------------------------------------------------------------- 武器数据 */
enum { TK_BULLET, TK_SNOW, TK_SPLASH, TK_BURN, TK_PIERCE, TK_CHAIN,
       TK_PUSH, TK_BEAM, TK_GATLING, TK_FREEZE, TK_POISON, TK_MISSILE };

typedef struct {
    const wchar_t *name;
    int cost, dmg, range;
    float cd;               /* 攻击间隔（秒） */
    COLORREF color;
    int kind;
    const wchar_t *desc;
} TowerDef;

static const TowerDef TOWERS[12] = {
    { L"瓶子炮",   100, 12, 135, 0.45f, C( 58,160,255), TK_BULLET,  L"单体速射" },
    { L"雪花塔",   120,  5, 115, 1.00f, C(191,233,255), TK_SNOW,    L"范围减速" },
    { L"星星炮",   160, 22, 150, 1.10f, C(255,216, 94), TK_SPLASH,  L"溅射伤害" },
    { L"火焰炮",   100, 10, 115, 0.60f, C(255,106, 61), TK_BURN,    L"灼烧持续伤害" },
    { L"巨龙弩",   200, 50, 200, 1.80f, C(138, 92,255), TK_PIERCE,  L"巨箭贯穿直线" },
    { L"雷电磁塔", 180, 18, 150, 1.20f, C( 94,200,255), TK_CHAIN,   L"连锁闪电 3 目标" },
    { L"飓风塔",   140,  8, 130, 2.20f, C(168,230,160), TK_PUSH,    L"飓风击退怪物" },
    { L"光棱塔",   250, 26, 170, 0.10f, C(255,157,226), TK_BEAM,    L"持续激光越照越痛" },
    { L"加特林",   120,  6, 125, 0.16f, C(200,207,216), TK_GATLING, L"超高射速倾泻弹雨" },
    { L"冰晶炮",   150, 14, 140, 1.00f, C(159,224,255), TK_FREEZE,  L"冰弹冻结目标" },
    { L"毒液塔",   130,  8, 120, 0.90f, C(143,209, 79), TK_POISON,  L"毒素侵蚀持续掉血" },
    { L"火箭巢",   300, 45, 185, 2.00f, C(255,138, 92), TK_MISSILE, L"追踪导弹范围爆炸" },
};
#define TOWER_COUNT 12

static float tower_dmg(int type, int level) {
    const TowerDef *d = &TOWERS[type];
    float m = (float)(level - 1);
    return d->dmg * (type == 1 ? (1 + m * 0.3f) : (1 + m * 0.4f));
}
static int tower_range(int type, int level) {
    return (int)(TOWERS[type].range * (1 + (level - 1) * 0.15f) + 0.5f);
}
static int upgrade_cost(int type, int level) {
    return (int)(TOWERS[type].cost * 0.8f * level + 0.5f);
}
/* 出售返还：与 do_sell / 塔面板显示共用同一算法，避免数值不一致 */
static int sell_value(int type, int level) {
    int spent = TOWERS[type].cost;
    for (int l = 1; l < level; l++) spent += upgrade_cost(type, l);
    return (int)(spent * 0.6f);
}
#define MAX_LEVEL 3

/* ---------------------------------------------------------------- 怪物数据 */
enum { EN_SMALL, EN_FAST, EN_BIG, EN_BOSS, EN_ARMOR, EN_SLIME };

typedef struct {
    const wchar_t *name;
    float hp, speed;
    int r, reward, dmg;
    COLORREF body;
} EnemyDef;

static const EnemyDef ENEMIES[6] = {
    { L"小怪",    60.0f,  60.0f, 16, 10, 1, C(126,217, 87) },
    { L"飞毛腿",  42.0f, 105.0f, 14, 12, 1, C(255,210, 63) },
    { L"大块头", 230.0f,  40.0f, 24, 26, 2, C(176,132,245) },
    { L"魔王",  1400.0f,  30.0f, 34,120, 5, C(255, 90, 90) },
    { L"铁甲虫", 170.0f,  48.0f, 21, 24, 2, C(154,168,184) },
    { L"史莱姆",  95.0f,  55.0f, 18, 14, 1, C(139,224,160) },
};

/* 精英词缀 */
enum { AF_SHIELD, AF_SWIFT, AF_REGEN, AF_SPLIT, AF_TOUGH, AF_COUNT };
static const wchar_t *AFFIX_NAME[AF_COUNT] = { L"护盾", L"迅捷", L"再生", L"分裂", L"坚硬" };
static const COLORREF AFFIX_COLOR[AF_COUNT] = {
    C(159,224,255), C(255,233, 94), C(140,232,154), C(255,179, 71), C(200,207,216)
};

/* ============================ 战前契约（开局三选一） ====================== */
static void banner_set(const wchar_t *s, float t);   /* 前向声明（定义在主流程区） */

/* 成就枚举与 unlock 前置（完整定义见下方成就系统区） */
enum {
    ACH_FIRSTWAVE, ACH_CLEAR, ACH_PERFECT, ACH_ENDLESS10, ACH_COMBO10,
    ACH_RICH, ACH_SELL5, ACH_COLLECTOR, ACH_BOSSKILL, ACH_CONTRACT,
    ACH_HARDCORE, ACH_ELITE10, ACH_WAVE10, ACH_KILLS200, ACH_COUNT
};
static void unlock(int id);

typedef struct {
    const wchar_t *name, *icon;
    int stars;
    const wchar_t *good[2];
    const wchar_t *bad[2];
    float hp, spd, gold, dmg;       /* 各项倍率 */
    int life, startGold;            /* 初始增减 */
} ContractDef;

static const ContractDef CONTRACTS[7] = {
    { L"贪婪之契", L"金", 1, { L"金币收益 +45%", 0 }, { L"怪物生命 +20%", 0 }, 1.0f,1.0f,1.45f,1.0f,  0,   0 },
    { L"疾风之契", L"风", 1, { L"炮塔伤害 +15%", 0 }, { L"怪物移速 +18%", 0 }, 1.0f,1.18f,1.0f,1.15f, 0,   0 },
    { L"铁壁之契", L"盾", 1, { L"初始生命 +6",    0 }, { L"怪物生命 +15%", 0 }, 1.15f,1.0f,1.0f,1.0f,  6,   0 },
    { L"穷途之契", L"币", 2, { L"初始金币 +260",  0 }, { L"击杀金币 -30%", 0 }, 1.0f,1.0f,0.70f,1.0f,  0, 260 },
    { L"狂战之契", L"火", 2, { L"炮塔伤害 +30%", 0 }, { L"初始生命 -3",    0 }, 1.0f,1.0f,1.0f,1.30f, -3,   0 },
    { L"速攻之契", L"速", 3, { L"击杀金币 +50%", 0 }, { L"怪物移速 +30%", 0 }, 1.0f,1.30f,1.50f,1.0f, 0,   0 },
    { L"苦行之契", L"险", 3, { L"炮塔伤害 +45%", 0 }, { L"怪物生命 +45%", L"初始生命 -4" }, 1.45f,1.0f,1.0f,1.45f, -4, 0 },
};

static int   g_contract = -1;              /* 当前契约下标，-1 未签 */
static int   g_contractRoll[3] = { -1, -1, -1 };
static float g_modHp = 1, g_modSpd = 1, g_modGold = 1, g_modDmg = 1;
static int   g_modLife = 0, g_modGold0 = 0;

static void roll_contracts(void) {
    int pools[3][7], pn[3] = { 0, 0, 0 };
    for (int i = 0; i < 7; i++) {
        int s = CONTRACTS[i].stars - 1;
        pools[s][pn[s]++] = i;
    }
    for (int s = 0; s < 3; s++)
        g_contractRoll[s] = pn[s] ? pools[s][rand() % pn[s]] : -1;
}

static void clear_contract(void) {
    g_contract = -1;
    g_modHp = g_modSpd = g_modGold = g_modDmg = 1.0f;
    g_modLife = g_modGold0 = 0;
}

static void apply_contract(int idx) {
    clear_contract();
    if (idx < 0 || idx >= 3) return;
    int id = g_contractRoll[idx];
    if (id < 0) return;
    const ContractDef *c = &CONTRACTS[id];
    g_contract = id;
    g_modHp = c->hp; g_modSpd = c->spd; g_modGold = c->gold; g_modDmg = c->dmg;
    g_modLife = c->life; g_modGold0 = c->startGold;
    unlock(ACH_CONTRACT);
}

/* ============================ 成就系统 ================================== */
static const struct { const wchar_t *name, *desc; } ACHIEVEMENTS[ACH_COUNT] = {
    { L"初出茅庐",   L"完成第 1 波" },
    { L"萝卜守护者", L"通关经典模式" },
    { L"完美防守",   L"满生命通关经典模式" },
    { L"无尽先锋",   L"无尽模式撑到第 20 波" },
    { L"连击大师",   L"单局连击达到 10" },
    { L"富甲一方",   L"金币达到 500" },
    { L"断舍离",     L"累计出售 5 座炮塔" },
    { L"收藏家",     L"同时拥有 6 种不同武器" },
    { L"屠魔者",     L"击败一只魔王" },
    { L"签约者",     L"第一次签下战前契约" },
    { L"苦行僧",     L"用 ★★★ 契约通关经典模式" },
    { L"精英猎手",   L"累计击败 10 只精英怪" },
    { L"百战老兵",   L"单局坚持到第 10 波" },
    { L"杀怪如麻",   L"累计击杀 200 只怪物" },
};

static unsigned long long g_ach;           /* 已解锁位掩码（持久化） */
static unsigned long long g_achNew;        /* 本局新解锁 */
static int g_sellTotal, g_killTotal;       /* 跨局统计 */
static int g_metaBest[4];                  /* 每张地图最佳波次 */
static wchar_t g_metaPath[MAX_PATH];

static int g_selMap, g_selMode;     /* 前置声明（正式定义与初始化在全局状态区） */

static void save_meta(void) {
    FILE *f = _wfopen(g_metaPath, L"wb");
    if (!f) return;
    /* v2：追加记住上次选择的地图与模式 */
    fprintf(f, "CARROT_META 2\n%llu %d %d %d %d %d %d\n%d %d\n",
            g_ach, g_sellTotal, g_killTotal,
            g_metaBest[0], g_metaBest[1], g_metaBest[2], g_metaBest[3],
            g_selMap, g_selMode);
    fclose(f);
}

static void load_meta(void) {
    FILE *f = _wfopen(g_metaPath, L"rb");
    if (!f) return;
    char head[32] = { 0 };
    if (fscanf(f, "%31s", head) == 1 && strncmp(head, "CARROT_META", 11) == 0) {
        fscanf(f, "%llu %d %d %d %d %d %d", &g_ach, &g_sellTotal, &g_killTotal,
               &g_metaBest[0], &g_metaBest[1], &g_metaBest[2], &g_metaBest[3]);
        /* v1 旧档没有这两项，读不到就用默认值 */
        int sm = 0, sd = 0;
        if (fscanf(f, "%d %d", &sm, &sd) == 2) {
            if (sm >= 0 && sm <= 3) g_selMap = sm;
            if (sd == 0 || sd == 1) g_selMode = sd;
        }
    }
    fclose(f);
}

static void unlock(int id) {
    if (id < 0 || id >= ACH_COUNT) return;
    unsigned long long bit = 1ULL << id;
    if (g_ach & bit) return;
    g_ach |= bit;
    g_achNew |= bit;
    wchar_t buf[96];
    swprintf_s(buf, 96, L"成就解锁：%s", ACHIEVEMENTS[id].name);
    banner_set(buf, 2.6f);
    save_meta();
}

/* ---------------------------------------------------------------- 运行时类型 */
typedef struct Tower {
    int used, type, level, mode;
    float cd, ang, beamHeat;
    int beamIdx;
    float dmgDone;
    int flash;
} Tower;

typedef struct Enemy {
    int alive, dead, type, elite, affixN, affix[2];
    float hp, maxHp, dist, px, py, speedMul, wob;
    float slowT, burnT, burnDps, poisonT, poisonDps, freezeT, boostT;
    float shield, shieldMax, shieldCd, roarCd, rage;
    float lagHp, flash, regenAcc;
    int burnSlot, poisonSlot;
    int noCount;
} Enemy;

typedef struct Bullet {
    int alive, kind, splash, targetIdx, towerSlot;
    float x, y, tx, ty, speed, dmg, t;
    COLORREF color;
    int freeze, poison, burn;
    float ang;
} Bullet;

typedef struct Fx {
    int alive, kind, color;
    float x, y, r, life, maxlife, x2, y2, vx, vy, w;
} Fx;

typedef struct FloatText {
    int alive, color, size;
    float x, y, vy, life, maxlife;
    wchar_t text[48];
} FloatText;

typedef struct Item {
    int alive, type;
    float x, y, t;
} Item;

typedef struct WaveGroup { int type, n; float gap, after; } WaveGroup;

typedef struct SpawnEntry { int type, elite; float time; } SpawnEntry;

/* ---------------------------------------------------------------- 全局状态 */
enum { SC_MENU, SC_CONTRACT, SC_GAME, SC_PAUSE, SC_DEX, SC_OVER };

static int   g_screen = SC_MENU;
static int   g_map = 0, g_endless = 0;
static int   g_selMap = 0, g_selMode = 0;

static float g_time;
static float g_gold, g_goldEarned;
static int   g_lives, g_maxLives;
static int   g_wave;                 /* 0 基索引 */
static int   g_waveActive, g_bossWave, g_eliteWave;
static float g_betweenTimer;
static float g_speed = 1.0f;
static int   g_paused, g_over, g_won;
static float g_shake;
static float g_threat;
static float g_carrotFlash;
/* 顶栏动态：金币/生命变化时的脉冲高亮 */
static int   g_goldShown = -1, g_livesShown = -1;
static float g_goldPulse, g_livesPulse;
static int   g_kills, g_maxCombo, g_combo, g_eliteKills;
static float g_comboT;
static float g_frenzyT;

static Enemy g_enemies[MAX_ENEMIES];
static Bullet g_bullets[MAX_BULLETS];
static Fx g_fx[MAX_FX];
static FloatText g_floats[MAX_FLOATS];
static Item g_items[MAX_ITEMS];
static SpawnEntry g_spawn[MAX_WAVE_GROUPS * 40];
static int g_spawnCount;
static Tower g_slots[MAX_SLOTS];
static int g_slotX[MAX_SLOTS], g_slotY[MAX_SLOTS], g_slotCount;

static int g_selSlot = -1;          /* 选中/悬停的炮塔位 */
static int g_hoverSlot = -1;
static int g_buildType = -1;        /* 待建造的武器类型 */
static int g_hoverBtn = -1;

static int   g_pathX[12], g_pathY[12], g_pathN;
static float g_segLen[12];
static float g_totalLen;

static int   g_waveTotal, g_waveKilled;
static int   g_pendingEliteIdx;

static wchar_t g_banner[80];
static float   g_bannerT;

static int   g_mouseX, g_mouseY, g_mouseInside;
static int   g_winW = CLIENT_W, g_winH = CLIENT_H;
static int   g_running = 1;
static int   g_shotMode = 0;

/* 屏幕震动后的展示倍速（结算用） */
static int g_bestWave = 0;

/* ---------------------------------------------------------------- 小工具 */
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float rndf(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }
static int   rndi(int a, int b) { return a + rand() % (b - a + 1); }
static float lenf(float x, float y) { return sqrtf(x * x + y * y); }

static void banner_set(const wchar_t *s, float t) {
    wcsncpy_s(g_banner, 80, s, _TRUNCATE);
    g_bannerT = t;
}

/* ---------------------------------------------------------------- 路径 */
static void path_init(int mapIdx) {
    const MapDef *m = &MAPS[mapIdx];
    g_pathN = m->npath;
    g_totalLen = 0;
    for (int i = 0; i < m->npath; i++) {
        g_pathX[i] = m->path[i].x;
        g_pathY[i] = m->path[i].y;
    }
    for (int i = 0; i < m->npath - 1; i++) {
        float dx = (float)(g_pathX[i + 1] - g_pathX[i]);
        float dy = (float)(g_pathY[i + 1] - g_pathY[i]);
        g_segLen[i] = sqrtf(dx * dx + dy * dy);
        g_totalLen += g_segLen[i];
    }
    g_segLen[m->npath - 1] = 0;

    g_slotCount = m->nslots;
    for (int i = 0; i < m->nslots; i++) {
        g_slotX[i] = m->slots[i].x;
        g_slotY[i] = m->slots[i].y;
    }
}

static void path_point(float d, float *ox, float *oy, float *oang) {
    if (d < 0) d = 0;
    if (d > g_totalLen) d = g_totalLen;
    float acc = 0;
    for (int i = 0; i < g_pathN - 1; i++) {
        if (d <= acc + g_segLen[i]) {
            float t = g_segLen[i] > 0.001f ? (d - acc) / g_segLen[i] : 0;
            float dx = (float)(g_pathX[i + 1] - g_pathX[i]);
            float dy = (float)(g_pathY[i + 1] - g_pathY[i]);
            *ox = g_pathX[i] + dx * t;
            *oy = g_pathY[i] + dy * t;
            if (oang) *oang = atan2f(dy, dx);
            return;
        }
        acc += g_segLen[i];
    }
    *ox = (float)g_pathX[g_pathN - 1];
    *oy = (float)g_pathY[g_pathN - 1];
    if (oang) *oang = 0;
}

/* ---------------------------------------------------------------- 特效/飘字 */
static Fx *fx_new(int kind, float x, float y, float r, float life, COLORREF col) {
    for (int i = 0; i < MAX_FX; i++) {
        if (!g_fx[i].alive) {
            Fx *f = &g_fx[i];
            memset(f, 0, sizeof(*f));
            f->alive = 1; f->kind = kind; f->x = x; f->y = y; f->r = r;
            f->life = life; f->maxlife = life; f->color = (int)col;
            return f;
        }
    }
    return NULL;
}

static void float_new(float x, float y, const wchar_t *txt, COLORREF col, float life, int size) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!g_floats[i].alive) {
            FloatText *f = &g_floats[i];
            f->alive = 1; f->x = x; f->y = y; f->vy = -26; f->life = life;
            f->maxlife = life; f->color = (int)col; f->size = size;
            wcsncpy_s(f->text, 48, txt, _TRUNCATE);
            return;
        }
    }
}

static void spark_burst(float x, float y, int n, COLORREF col, float spd) {
    for (int i = 0; i < n; i++) {
        Fx *f = fx_new(2, x, y, rndf(1.2f, 2.8f), rndf(0.25f, 0.6f), col);
        if (!f) return;
        float a = rndf(0, TAU), s = rndf(spd * 0.4f, spd);
        f->vx = cosf(a) * s;
        f->vy = sinf(a) * s;
    }
}
/* ============================ 逻辑层 ==================================== */

/* 渲染层需要调用的前置声明 */
static void render(void);
static void present(HDC dst);
static void draw_world(HDC dc);
static void build_background(int mapIdx);

/* ---------------------------------------------------------------- 波次表 */
static const WaveGroup WAVES[15][MAX_WAVE_GROUPS] = {
    { {EN_SMALL, 8, 1.00f, 0} },
    { {EN_SMALL,12, 0.85f, 0} },
    { {EN_SMALL, 8, 0.90f, 0}, {EN_FAST, 4, 0.80f, 3} },
    { {EN_FAST, 10, 0.70f, 0} },
    { {EN_SMALL,10, 0.80f, 0}, {EN_BIG,  2, 2.00f, 4} },
    { {EN_FAST, 12, 0.60f, 0}, {EN_SMALL, 6, 0.90f, 2} },
    { {EN_BIG,   4, 2.00f, 0}, {EN_SMALL,10, 0.70f, 2} },
    { {EN_FAST, 14, 0.55f, 0}, {EN_BIG,  3, 2.00f, 3} },
    { {EN_SMALL,14, 0.50f, 0}, {EN_SLIME, 4, 1.40f, 3} },
    { {EN_BIG,   4, 1.80f, 0}, {EN_ARMOR,3, 1.60f, 4}, {EN_FAST, 8, 0.60f, 2} },
    { {EN_FAST, 16, 0.50f, 0}, {EN_SLIME, 5, 1.20f, 4} },
    { {EN_ARMOR, 5, 1.50f, 0}, {EN_BIG,  6, 1.50f, 3}, {EN_SMALL,14,0.60f, 2} },
    { {EN_FAST, 18, 0.45f, 0}, {EN_ARMOR,4, 1.60f, 4} },
    { {EN_BIG,   8, 1.20f, 0}, {EN_SLIME, 6, 1.30f, 3}, {EN_FAST, 12, 0.50f, 2} },
    { {EN_BOSS,  1, 1.00f, 0}, {EN_ARMOR,6, 1.40f, 4}, {EN_SLIME, 6, 1.20f, 3},
      {EN_FAST, 16, 0.50f, 2} },
};

/* 取波次内容：经典 15 波后进入程序化无尽 */
static int get_wave(int idx, WaveGroup *out) {
    if (idx < 15) {
        int n = 0;
        for (int i = 0; i < MAX_WAVE_GROUPS; i++) {
            if (WAVES[idx][i].n <= 0) break;
            out[n++] = WAVES[idx][i];
        }
        return n;
    }
    /* 无尽：根据超出波数逐步加压 */
    int k = idx - 15;
    int n = 0;
    int boss = (k % 5 == 4);
    int arm  = 2 + k / 5;
    int small = 14 + k * 2;
    int big  = 4 + k;
    out[n].type = EN_SMALL; out[n].n = small; out[n].gap = 0.42f; out[n].after = 0; n++;
    out[n].type = EN_ARMOR; out[n].n = arm;   out[n].gap = 1.20f; out[n].after = 3; n++;
    out[n].type = EN_BIG;   out[n].n = big;   out[n].gap = 1.10f; out[n].after = 2; n++;
    if (boss) { out[n].type = EN_BOSS; out[n].n = 1 + k / 15; out[n].gap = 2.0f; out[n].after = 5; n++; }
    for (int i = n; i < MAX_WAVE_GROUPS; i++) { out[i].n = 0; out[i].type = 0; out[i].gap = 0; out[i].after = 0; }
    return n;
}

/* ---------------------------------------------------------------- 目标选择 */
static int find_target(int sx, int sy, int range, int mode, float *outD) {
    int best = -1;
    float bestVal = 0;
    float rr = (float)range * range;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (!e->alive) continue;
        float dx = e->px - sx, dy = e->py - sy;
        float dd = dx * dx + dy * dy;
        if (dd > rr) continue;
        float hp = e->hp + e->shield;
        float val;
        switch (mode) {
        case 1: val = hp; break;             /* 最强 */
        case 2: val = -dd; break;            /* 最近 */
        case 3: val = -hp; break;            /* 最弱 */
        default: val = e->dist; break;       /* 最前 */
        }
        if (best < 0 || val > bestVal) { best = i; bestVal = val; if (outD) *outD = dd; }
    }
    return best;
}

/* ---------------------------------------------------------------- 生成怪物 */
static void make_elite(Enemy *e) {
    e->elite = 1;
    e->maxHp *= 1.9f;
    e->hp = e->maxHp;
    e->affixN = (g_wave >= 7) ? 2 : 1;
    if (e->affixN > 2) e->affixN = 2;
    for (int i = 0; i < e->affixN; i++) {
        int a;
        do { a = rndi(0, AF_COUNT - 1); } while (i == 1 && a == e->affix[0]);
        e->affix[i] = a;
        if (a == AF_SHIELD) { e->shieldMax = e->maxHp * 0.30f; e->shield = e->shieldMax; }
        if (a == AF_TOUGH)  { /* 减伤在 damage 里处理 */ }
    }
}

static int spawn_enemy_ex(int type, float dist, float hpMul, int elite, int noCount) {
    int slot = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!g_enemies[i].alive) { slot = i; break; }
    if (slot < 0) return -1;
    Enemy *e = &g_enemies[slot];
    memset(e, 0, sizeof(*e));
    const EnemyDef *d = &ENEMIES[type];

    float scale = (1.0f + g_wave * 0.12f) * hpMul;
    e->alive = 1;
    e->type = type;
    e->maxHp = d->hp * scale * g_modHp;                     /* 契约：怪物生命倍率 */
    e->hp = e->maxHp;
    e->dist = dist;
    e->speedMul = (0.7f + g_wave * 0.055f) * g_modSpd;      /* 契约：怪物移速倍率 */
    if (e->speedMul > 2.2f) e->speedMul = 2.2f;
    e->wob = rndf(0, TAU);
    e->lagHp = e->maxHp;
    path_point(dist, &e->px, &e->py, NULL);
    e->burnSlot = -1; e->poisonSlot = -1;
    e->noCount = noCount;

    if (type == EN_BOSS) {
        e->shield = 0;
        e->shieldMax = e->maxHp * 0.16f;
        e->shieldCd = 8.0f;
        e->roarCd = 5.0f;
    }
    if (elite) make_elite(e);
    return slot;
}
static int spawn_enemy(int type, float dist, float hpMul, int elite) {
    return spawn_enemy_ex(type, dist, hpMul, elite, 0);
}
/* 分裂体：不计入本波进度 */
static void spawn_split(int type, float dist, float hpMul) {
    spawn_enemy_ex(type, dist, hpMul, 0, 1);
}

/* ---------------------------------------------------------------- 状态重置 */
static void new_game(int mapIdx, int endless) {
    memset(g_enemies, 0, sizeof(g_enemies));
    memset(g_bullets, 0, sizeof(g_bullets));
    memset(g_fx, 0, sizeof(g_fx));
    memset(g_floats, 0, sizeof(g_floats));
    memset(g_items, 0, sizeof(g_items));
    memset(g_slots, 0, sizeof(g_slots));
    g_spawnCount = 0;

    g_map = mapIdx;
    g_endless = endless ? 1 : 0;
    g_gold = 220 + g_modGold0;                              /* 与原版一致的初始金币 + 契约加成 */
    g_goldEarned = 0;
    g_maxLives = 10 + g_modLife;                            /* 契约：初始生命增减 */
    if (g_maxLives < 3) g_maxLives = 3;
    g_lives = g_maxLives;
    g_wave = -1;
    g_waveActive = 0;
    g_bossWave = 0;
    g_eliteWave = 0;
    g_betweenTimer = 4.0f;
    g_time = 0;
    g_speed = 1.0f;
    g_paused = 0;
    g_over = 0;
    g_won = 0;
    g_shake = 0;
    g_threat = 0;
    g_carrotFlash = 0;
    g_kills = 0;
    g_maxCombo = 0;
    g_combo = 0;
    g_comboT = 0;
    g_eliteKills = 0;
    g_waveTotal = 0;
    g_waveKilled = 0;
    g_selSlot = -1;
    g_buildType = -1;
    g_pendingEliteIdx = -1;
    g_frenzyT = 0;
    g_achNew = 0;
    g_bannerT = 0;
    g_banner[0] = 0;

    path_init(mapIdx);
    for (int i = 0; i < g_slotCount; i++) g_slots[i].beamIdx = -1;
    build_background(mapIdx);
    g_screen = SC_GAME;
}

/* ---------------------------------------------------------------- 波次启动 */
static void start_wave(void) {
    g_wave++;
    WaveGroup grp[MAX_WAVE_GROUPS];
    int n = get_wave(g_wave, grp);

    g_spawnCount = 0;
    g_waveTotal = 0;
    int boss = 0;
    for (int i = 0; i < n; i++) {
        if (grp[i].type == EN_BOSS) boss = 1;
        for (int j = 0; j < grp[i].n; j++) {
            if (g_spawnCount >= (int)(sizeof(g_spawn) / sizeof(g_spawn[0]))) break;
            SpawnEntry *s = &g_spawn[g_spawnCount++];
            s->type = grp[i].type;
            s->time = g_time + grp[i].after + j * grp[i].gap;
            s->elite = 0;
            g_waveTotal++;
        }
    }
    g_bossWave = boss;
    g_waveActive = 1;
    g_waveKilled = 0;

    /* 精英怪安排：第 3 波起概率出现，5/9/13 波必出，魔王波除外 */
    g_eliteWave = 0;
    g_pendingEliteIdx = -1;
    if (!boss && g_wave >= 2) {
        int forced = (g_wave == 4 || g_wave == 8 || g_wave == 12);
        float ch = 0.16f + g_wave * 0.045f;
        if (ch > 0.7f) ch = 0.7f;
        if (forced || rndf(0, 1) < ch) {
            for (int tries = 0; tries < 20; tries++) {
                int idx = rndi(0, g_spawnCount - 1);
                if (g_spawn[idx].type != EN_BOSS) {
                    g_spawn[idx].elite = 1;
                    g_pendingEliteIdx = idx;
                    g_eliteWave = 1;
                    break;
                }
            }
        }
    }

    wchar_t buf[80];
    if (boss)
        swprintf_s(buf, 80, L"第 %d 波 · 魔王来袭！", g_wave + 1);
    else if (g_eliteWave)
        swprintf_s(buf, 80, L"第 %d 波 · 精英怪出没！", g_wave + 1);
    else
        swprintf_s(buf, 80, L"第 %d 波 来袭！", g_wave + 1);
    banner_set(buf, 2.4f);
}

/* ---------------------------------------------------------------- 伤害/击杀 */
static void add_tower_damage(int slot, float d) {
    if (slot >= 0 && slot < MAX_SLOTS) g_slots[slot].dmgDone += d;
}

static void kill_enemy(Enemy *e, float fx, float fy, int slot);

static void damage(Enemy *e, float dmg, float fx, float fy, int slot) {
    if (!e || !e->alive || e->dead) return;
    dmg *= g_modDmg;                                       /* 契约：炮塔伤害倍率 */
    if (e->type == EN_ARMOR) dmg *= 0.6f;                 /* 护甲减伤 40% */
    if (e->elite) for (int i = 0; i < e->affixN; i++) if (e->affix[i] == AF_TOUGH) { dmg *= 0.75f; break; }
    if (dmg < 1) dmg = 1;

    float real = dmg;
    if (e->shield > 0) {
        float absorb = e->shield >= dmg ? dmg : e->shield;
        e->shield -= absorb;
        dmg -= absorb;
        real = absorb;
        if (e->shield <= 0.01f) {
            e->shield = 0;
            fx_new(1, e->px, e->py, 56, 0.45f, AFFIX_COLOR[AF_SHIELD]);
            for (int i = 0; i < 10; i++) {
                Fx *f = fx_new(2, e->px, e->py, rndf(1.5f, 3), 0.5f, AFFIX_COLOR[AF_SHIELD]);
                if (f) { float a = rndf(0, TAU); f->vx = cosf(a) * 90; f->vy = sinf(a) * 90; }
            }
            g_shake = 5;
        }
        if (dmg <= 0) { add_tower_damage(slot, real); return; }
    }
    e->hp -= dmg;
    e->flash = 1;
    add_tower_damage(slot, real + dmg);
    if (e->hp <= 0) kill_enemy(e, fx, fy, slot);
}

/* 持续伤害（绕过护盾） */
static void damage_dot(Enemy *e, float dmg, int slot) {
    if (!e || !e->alive || e->dead) return;
    dmg *= g_modDmg;                                       /* 契约：炮塔伤害倍率 */
    if (e->type == EN_ARMOR) dmg *= 0.6f;
    e->hp -= dmg;
    add_tower_damage(slot, dmg);
    if (e->hp <= 0) kill_enemy(e, e->px, e->py, slot);
}

static void drop_item(Enemy *e, float fx, float fy) {
    float chance = e->elite ? 1.0f : (e->type == EN_BOSS ? 1.0f :
                   (e->type == EN_BIG ? 0.30f : 0.15f));
    if (rndf(0, 1) >= chance) return;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!g_items[i].alive) {
            Item *it = &g_items[i];
            it->alive = 1;
            it->x = clampf(fx + rndf(-22, 22), 30, PLAY_W - 30);
            it->y = clampf(fy + rndf(-22, 22), 30, PLAY_H - 30);
            it->t = 0;
            float r = rndf(0, 1);
            it->type = r < 0.45f ? 0 : (r < 0.70f ? 1 : (r < 0.90f ? 2 : 3));
            return;
        }
    }
}

static void kill_enemy(Enemy *e, float fx, float fy, int slot) {
    if (!e || !e->alive || e->dead) return;
    e->dead = 1;
    const EnemyDef *d = &ENEMIES[e->type];
    int elite = e->elite;
    int type = e->type;
    float dist = e->dist;
    int noCount = e->noCount;

    e->alive = 0;

    /* 连击 */
    if (g_comboT > 0) g_combo++;
    else g_combo = 1;
    g_comboT = 2.2f;
    if (g_combo > g_maxCombo) g_maxCombo = g_combo;

    /* 赏金 */
    float goldMul = elite ? 3.0f : 1.0f;
    if (g_combo >= 10) goldMul += 0.2f;
    int reward = (int)(d->reward * goldMul * g_modGold + 0.5f);   /* 契约：金币倍率 */
    g_gold += reward;
    g_goldEarned += reward;

    g_kills++;
    if (elite) g_eliteKills++;
    if (!noCount && g_waveActive) g_waveKilled++;

    /* 成就检测 */
    g_killTotal++;
    if (g_killTotal >= 200) unlock(ACH_KILLS200);
    if (g_maxCombo >= 10) unlock(ACH_COMBO10);
    if ((int)g_gold >= 500) unlock(ACH_RICH);
    if (g_eliteKills >= 10) unlock(ACH_ELITE10);
    if (type == EN_BOSS) unlock(ACH_BOSSKILL);

    wchar_t buf[48];
    swprintf_s(buf, 48, L"+%d", reward);
    float_new(fx, fy - 18, buf, elite ? C(255,216, 94) : C(255,216, 94), 0.9f, 15);
    if (elite) {
        float_new(fx, fy - 44, L"精英击杀！", C(255,179, 71), 1.4f, 16);
        g_shake = 4;
    }

    spark_burst(fx, fy, elite ? 16 : 9, d->body, 130);

    /* 史莱姆分裂 */
    if (type == EN_SLIME) {
        spawn_split(EN_SMALL, dist > 10 ? dist - 10 : 0, 0.65f);
        spawn_split(EN_SMALL, dist + 14, 0.65f);
        float_new(fx, fy - 34, L"分裂！", C(139,224,160), 1.0f, 14);
    }
    /* 精英分裂词缀 */
    if (elite) {
        for (int i = 0; i < e->affixN; i++) {
            if (e->affix[i] == AF_SPLIT) {
                int st = (type == EN_SLIME) ? EN_SMALL : type;
                spawn_split(st, dist > 12 ? dist - 12 : 0, 0.4f);
                spawn_split(st, dist + 12, 0.4f);
                float_new(fx, fy - 46, L"精英分裂！", C(255,179, 71), 1.2f, 14);
                break;
            }
        }
    }
    /* 魔王倒下 */
    if (type == EN_BOSS) {
        banner_set(L"魔王被击败！", 2.6f);
        g_shake = 12;
        for (int i = 0; i < 3; i++) fx_new(1, fx, fy, 60 + i * 40, 0.7f + i * 0.2f, C(255,160,80));
    }

    drop_item(e, fx, fy);
}

/* ---------------------------------------------------------------- 开火 */
static void fire_bullet(int slot, int target, float dmg, int kind, COLORREF col,
                        int splash, int freeze, int poison, int burn) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].alive) {
            Bullet *b = &g_bullets[i];
            memset(b, 0, sizeof(*b));
            Tower *t = &g_slots[slot];
            b->alive = 1;
            b->kind = kind;
            b->x = (float)g_slotX[slot];
            b->y = (float)g_slotY[slot] - 14;
            b->targetIdx = target;
            b->tx = g_enemies[target].px;
            b->ty = g_enemies[target].py;
            b->speed = kind == 2 ? 330.0f : 520.0f;
            b->dmg = dmg;
            b->splash = splash;
            b->color = col;
            b->freeze = freeze;
            b->poison = poison;
            b->burn = burn;
            b->towerSlot = slot;
            b->ang = atan2f(b->ty - b->y, b->tx - b->x);
            t->ang = b->ang;
            return;
        }
    }
}

static void tower_fire(int slot) {
    Tower *t = &g_slots[slot];
    const TowerDef *def = &TOWERS[t->type];
    int sx = g_slotX[slot], sy = g_slotY[slot] - 12;
    int range = tower_range(t->type, t->level);
    float dmg = tower_dmg(t->type, t->level);

    int ti = find_target(sx, sy, range, t->mode, NULL);
    if (ti < 0) { t->beamIdx = -1; t->beamHeat = 0; return; }
    Enemy *e = &g_enemies[ti];
    t->ang = atan2f(e->py - sy, e->px - sx);

    switch (def->kind) {
    case TK_BULLET:
        fire_bullet(slot, ti, dmg, 0, def->color, 0, 0, 0, 0);
        break;
    case TK_GATLING:
        fire_bullet(slot, ti, dmg, 0, def->color, 0, 0, 0, 0);
        break;
    case TK_SPLASH:
        fire_bullet(slot, ti, dmg, 1, def->color, 46, 0, 0, 0);
        break;
    case TK_BURN:
        fire_bullet(slot, ti, dmg, 1, def->color, 34, 0, 0, 1);
        break;
    case TK_FREEZE:
        fire_bullet(slot, ti, dmg, 0, def->color, 0, 1, 0, 0);
        break;
    case TK_POISON:
        fire_bullet(slot, ti, dmg, 0, def->color, 0, 0, 1, 0);
        break;
    case TK_MISSILE:
        fire_bullet(slot, ti, dmg, 2, def->color, 74, 0, 0, 0);
        fx_new(0, sx, sy, 12, 0.2f, C(255,220,150));
        break;
    case TK_PIERCE: {
        /* 直线贯穿：起点到目标方向，长度 = 射程 + 60 */
        float ang = t->ang;
        float ex = sx + cosf(ang) * (range + 60), ey = sy + sinf(ang) * (range + 60);
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *q = &g_enemies[i];
            if (!q->alive) continue;
            float vx = ex - sx, vy = ey - sy;
            float wx = q->px - sx, wy = q->py - sy;
            float proj = (wx * vx + wy * vy) / (vx * vx + vy * vy);
            if (proj < 0 || proj > 1.15f) continue;
            float cx = sx + vx * proj, cy = sy + vy * proj;
            if (lenf(q->px - cx, q->py - cy) < 20 + ENEMIES[q->type].r * 0.3f)
                damage(q, dmg, q->px, q->py, slot);
        }
        Fx *f = fx_new(3, sx, sy, 3.2f, 0.18f, def->color);
        if (f) { f->x2 = ex; f->y2 = ey; }
        break;
    }
    case TK_CHAIN: {
        int hit[3]; float hx[3], hy[3]; int cnt = 0;
        int cur = ti;
        float px = e->px, py = e->py;
        float fromX = sx, fromY = sy;
        for (int step = 0; step < 3 && cur >= 0; step++) {
            Enemy *c = &g_enemies[cur];
            hit[cnt] = cur; hx[cnt] = c->px; hy[cnt] = c->py; cnt++;
            damage(c, dmg, c->px, c->py, slot);
            Fx *f = fx_new(3, fromX, fromY, 3.0f, 0.16f, def->color);
            if (f) { f->x2 = c->px; f->y2 = c->py; }
            fromX = c->px; fromY = c->py;
            px = c->px; py = c->py;
            /* 找下一个最近的目标 */
            int next = -1; float bd = 130 * 130;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (!g_enemies[i].alive) continue;
                int dup = 0;
                for (int k = 0; k < cnt; k++) if (hit[k] == i) dup = 1;
                if (dup) continue;
                float dx = g_enemies[i].px - px, dy = g_enemies[i].py - py;
                float d2 = dx * dx + dy * dy;
                if (d2 < bd) { bd = d2; next = i; }
            }
            cur = next;
        }
        break;
    }
    case TK_PUSH: {
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *q = &g_enemies[i];
            if (!q->alive) continue;
            if (lenf(q->px - e->px, q->py - e->py) < 62) {
                damage(q, dmg, q->px, q->py, slot);
                if (q->alive) {
                    q->dist -= 20;
                    if (q->dist < 0) q->dist = 0;
                    path_point(q->dist, &q->px, &q->py, NULL);
                }
            }
        }
        Fx *f = fx_new(1, e->px, e->py, 66, 0.35f, def->color);
        if (f) f->w = 3;
        break;
    }
    case TK_SNOW: {
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *q = &g_enemies[i];
            if (!q->alive) continue;
            if (lenf(q->px - sx, q->py - sy) <= range) {
                damage(q, dmg, q->px, q->py, slot);
                if (q->alive) q->slowT = g_time + 1.6f;
            }
        }
        Fx *f = fx_new(1, sx, sy, range, 0.45f, C(200,240,255));
        if (f) f->w = 4;
        break;
    }
    case TK_BEAM: {
        t->beamIdx = ti;
        /* 持续光束：每帧结算，倍率随时间升温 */
        break;
    }
    }
}

/* ---------------------------------------------------------------- 更新 */
static void update_items(float dt) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        Item *it = &g_items[i];
        if (!it->alive) continue;
        it->t += dt;
        if (it->t > 1.0f) {
            switch (it->type) {
            case 0: g_gold += 40; g_goldEarned += 40;
                    float_new(it->x, it->y - 12, L"+40 金币", C(255,216, 94), 1.1f, 14); break;
            case 1:
                for (int k = 0; k < MAX_ENEMIES; k++)
                    if (g_enemies[k].alive) g_enemies[k].freezeT = g_time + 3.0f;
                float_new(it->x, it->y - 12, L"全场冰冻 3 秒", C(159,224,255), 1.3f, 14);
                break;
            case 2: g_frenzyT = 5.0f;
                    float_new(it->x, it->y - 12, L"火力全开 5 秒", C(255,138, 92), 1.3f, 14); break;
            case 3: if (g_lives < g_maxLives) g_lives++;
                    float_new(it->x, it->y - 12, L"生命 +1", C(255,120,150), 1.2f, 14); break;
            }
            it->alive = 0;
        }
    }
}

static void update_enemies(float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (!e->alive) continue;
        const EnemyDef *d = &ENEMIES[e->type];

        if (e->flash > 0) e->flash -= dt * 4;
        if (e->shieldCd > 0) {
            e->shieldCd -= dt;
            if (e->shieldCd <= 0 && e->type == EN_BOSS && e->shield <= 0) {
                e->shield = e->shieldMax;
                e->shieldCd = 9.0f;
                fx_new(1, e->px, e->py, 46, 0.5f, AFFIX_COLOR[AF_SHIELD]);
                float_new(e->px, e->py - 44, L"护盾！", C(159,224,255), 1.0f, 13);
            }
        }
        /* 魔王：半血狂暴 */
        if (e->type == EN_BOSS && !e->rage && e->hp < e->maxHp * 0.5f) {
            e->rage = 1;
            banner_set(L"魔王狂暴！", 2.2f);
            g_shake = 10;
            float_new(e->px, e->py - 52, L"狂暴！", C(255,70,70), 1.4f, 17);
        }
        /* 魔王：咆哮给周围小怪加速 */
        if (e->type == EN_BOSS) {
            e->roarCd -= dt;
            if (e->roarCd <= 0) {
                e->roarCd = e->rage ? 6.0f : 8.5f;
                for (int k = 0; k < MAX_ENEMIES; k++) {
                    Enemy *q = &g_enemies[k];
                    if (!q->alive || q == e) continue;
                    if (lenf(q->px - e->px, q->py - e->py) < 200) q->boostT = g_time + 3.0f;
                }
                fx_new(1, e->px, e->py, 200, 0.55f, C(255,80,80));
                fx_new(1, e->px, e->py, 130, 0.4f, C(255,140,140));
                g_shake = 6;
            }
        }
        /* 精英：再生 */
        if (e->elite) {
            for (int a = 0; a < e->affixN; a++) {
                if (e->affix[a] == AF_REGEN) {
                    e->hp += e->maxHp * 0.018f * dt;
                    if (e->hp > e->maxHp) e->hp = e->maxHp;
                    break;
                }
            }
        }
        /* 持续伤害 */
        if (e->burnT > g_time && e->hp > 0) {
            damage_dot(e, e->burnDps * dt, e->burnSlot);
            if (!e->alive) continue;
        }
        if (e->poisonT > g_time && e->hp > 0) {
            damage_dot(e, e->poisonDps * dt, e->poisonSlot);
            if (!e->alive) continue;
        }

        /* 移速 */
        float sp = d->speed * e->speedMul;
        if (e->slowT > g_time) sp *= 0.45f;
        if (e->boostT > g_time) sp *= 1.4f;
        if (e->rage) sp *= 1.3f;
        if (e->freezeT > g_time) sp = 0;
        e->dist += sp * dt;
        e->wob += dt * (6.0f + sp * 0.02f);
        path_point(e->dist, &e->px, &e->py, NULL);

        /* 血条残影收敛 */
        if (e->hp > e->maxHp) e->lagHp = e->maxHp;
        if (e->lagHp < e->hp) e->lagHp = e->hp;
        else {
            e->lagHp -= (e->lagHp - e->hp) * clampf(dt * 4.5f, 0, 1);
            if (e->lagHp < e->hp) e->lagHp = e->hp;
        }

        /* 漏怪 */
        if (e->dist >= g_totalLen) {
            e->alive = 0;
            e->dead = 1;
            g_lives -= d->dmg;
            g_carrotFlash = 0.7f;
            g_shake = 7;
            if (!e->noCount && g_waveActive) g_waveKilled++;
            wchar_t buf[32];
            swprintf_s(buf, 32, L"-%d 生命", d->dmg);
            float_new((float)g_pathX[g_pathN - 1], (float)g_pathY[g_pathN - 1] - 40, buf, C(255,90,90), 1.2f, 16);
            if (g_lives <= 0) {
                g_lives = 0;
                g_over = 1;
                g_won = 0;
                g_screen = SC_OVER;
            }
        }
    }
}

static void update_bullets(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g_bullets[i];
        if (!b->alive) continue;
        b->t += dt;

        /* 目标还活着就用实时坐标，死了飞向最后位置 */
        Enemy *tg = (b->targetIdx >= 0) ? &g_enemies[b->targetIdx] : NULL;
        if (tg && tg->alive) { b->tx = tg->px; b->ty = tg->py; }
        float dx = b->tx - b->x, dy = b->ty - b->y;
        float dist = lenf(dx, dy);
        float step = b->speed * dt;
        if (dist <= step + 6 || b->t > 3.0f) {
            /* 命中 */
            float hx = b->tx, hy = b->ty;
            if (b->splash > 0) {
                fx_new(1, hx, hy, (float)b->splash, 0.32f, b->color);
                for (int k = 0; k < MAX_ENEMIES; k++) {
                    Enemy *q = &g_enemies[k];
                    if (!q->alive) continue;
                    float ddx = q->px - hx, ddy = q->py - hy;
                    if (ddx * ddx + ddy * ddy <= (float)b->splash * b->splash) {
                        damage(q, b->dmg, q->px, q->py, b->towerSlot);
                        if (!q->alive) continue;
                        if (b->freeze) q->freezeT = g_time + 1.6f;
                        if (b->burn) {
                            q->burnT = g_time + 2.5f;
                            q->burnDps = b->dmg * 0.35f;
                            q->burnSlot = b->towerSlot;
                        }
                    }
                }
                if (b->kind == 2) g_shake = 3.2f;
            } else if (tg && tg->alive) {
                damage(tg, b->dmg, hx, hy, b->towerSlot);
                if (tg->alive) {
                    if (b->freeze) { tg->freezeT = g_time + 1.8f;
                                     fx_new(1, hx, hy, 22, 0.3f, C(190,235,255)); }
                    if (b->poison) { tg->poisonT = g_time + 3.0f;
                                     tg->poisonDps = b->dmg * 0.25f;
                                     tg->poisonSlot = b->towerSlot; }
                    if (b->burn) { tg->burnT = g_time + 2.5f;
                                   tg->burnDps = b->dmg * 0.35f;
                                   tg->burnSlot = b->towerSlot; }
                }
            }
            spark_burst(hx, hy, 4, b->color, 90);
            b->alive = 0;
            continue;
        }
        b->ang = atan2f(dy, dx);
        b->x += dx / dist * step;
        b->y += dy / dist * step;
    }
}

static void update_towers(float dt) {
    for (int i = 0; i < g_slotCount; i++) {
        Tower *t = &g_slots[i];
        if (!t->used) continue;
        if (t->flash > 0) t->flash--;
        float rateMul = (g_frenzyT > 0) ? 2.0f : 1.0f;
        t->cd -= dt * rateMul;

        const TowerDef *def = &TOWERS[t->type];
        if (def->kind == TK_BEAM) {
            int sx = g_slotX[i], sy = g_slotY[i] - 12;
            int range = tower_range(t->type, t->level);
            if (t->beamIdx < 0 || !g_enemies[t->beamIdx].alive) {
                t->beamIdx = find_target(sx, sy, range, t->mode, NULL);
                t->beamHeat = 0;
            }
            if (t->beamIdx >= 0) {
                Enemy *e = &g_enemies[t->beamIdx];
                if (lenf(e->px - sx, e->py - sy) > range + 12) { t->beamIdx = -1; }
                else {
                    t->beamHeat = clampf(t->beamHeat + dt, 0, 3.0f);
                    float mul = 1.0f + t->beamHeat * 0.33f;   /* 最高 2 倍 */
                    t->cd -= dt;                              /* 光束按帧结算 */
                    float dmg = tower_dmg(t->type, t->level) * mul * dt * 2.6f;
                    damage(e, dmg, e->px, e->py, i);
                    t->ang = atan2f(e->py - sy, e->px - sx);
                    if (rndf(0, 1) < dt * 18)
                        fx_new(2, e->px + rndf(-8, 8), e->py + rndf(-8, 8), rndf(1, 2.4f), 0.3f, def->color);
                }
            }
            continue;
        }

        if (t->cd <= 0) {
            tower_fire(i);
            t->cd = def->cd;
            t->flash = 2;
        }
    }
}

static void update_fx(float dt) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &g_fx[i];
        if (!f->alive) continue;
        f->life -= dt;
        if (f->life <= 0) { f->alive = 0; continue; }
        if (f->kind == 2) { f->x += f->vx * dt; f->y += f->vy * dt;
                            f->vx *= 0.94f; f->vy *= 0.94f; }
        if (f->kind == 1) f->r += dt * (f->kind == 1 ? 40 : 0);
    }
    for (int i = 0; i < MAX_FLOATS; i++) {
        FloatText *f = &g_floats[i];
        if (!f->alive) continue;
        f->life -= dt;
        f->y += f->vy * dt;
        f->vy *= 0.96f;
        if (f->life <= 0) f->alive = 0;
    }
}

/* 单个逻辑步（dt 已限制在 1/30 内） */
static void step(float dt) {
    g_time += dt;
    if (g_comboT > 0) { g_comboT -= dt; if (g_comboT <= 0) g_combo = 0; }
    if (g_frenzyT > 0) g_frenzyT -= dt;
    if (g_bannerT > 0) g_bannerT -= dt;
    if (g_shake > 0) g_shake = g_shake > dt * 40 ? g_shake - dt * 40 : 0;
    if (g_carrotFlash > 0) g_carrotFlash -= dt;

    /* 出怪 */
    for (int i = 0; i < g_spawnCount; i++) {
        SpawnEntry *s = &g_spawn[i];
        if (s->type < 0) continue;
        if (g_time >= s->time) {
            spawn_enemy(s->type, 0, 1.0f, s->elite);
            s->type = -1;
        }
    }

    update_enemies(dt);
    if (g_screen != SC_GAME) return;
    update_towers(dt);
    update_bullets(dt);
    update_items(dt);
    update_fx(dt);

    /* 威胁值（最前怪距终点的比例） */
    float maxd = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g_enemies[i].alive && g_enemies[i].dist > maxd) maxd = g_enemies[i].dist;
    float ratio = g_totalLen > 0 ? maxd / g_totalLen : 0;
    float target = 0;
    if (ratio > 0.8f) target = clampf((ratio - 0.8f) / 0.18f, 0, 1);
    g_threat += (target - g_threat) * clampf(dt * 3.0f, 0, 1);

    /* 波次流程 */
    if (g_waveActive) {
        int remain = 0;
        for (int i = 0; i < g_spawnCount; i++) if (g_spawn[i].type >= 0) { remain = 1; break; }
        if (!remain) {
            for (int i = 0; i < MAX_ENEMIES; i++) if (g_enemies[i].alive) { remain = 1; break; }
        }
        if (!remain) {
            g_waveActive = 0;
            int bonus = 50 + (g_wave + 1) * 5;
            g_gold += bonus;
            g_goldEarned += bonus;
            wchar_t buf[48];
            swprintf_s(buf, 48, L"本波清除  +%d", bonus);
            banner_set(buf, 1.8f);
            g_betweenTimer = 6.0f;
            /* 成就：波次里程碑与通关 */
            if (g_wave == 0) unlock(ACH_FIRSTWAVE);
            if (g_wave >= 9) unlock(ACH_WAVE10);
            if (g_endless && g_wave + 1 >= 20) unlock(ACH_ENDLESS10);
            if (g_wave + 1 > g_metaBest[g_map]) { g_metaBest[g_map] = g_wave + 1; save_meta(); }
            if (!g_endless && g_wave >= 14) {
                unlock(ACH_CLEAR);
                if (g_lives >= g_maxLives) unlock(ACH_PERFECT);
                if (g_contract >= 0 && CONTRACTS[g_contract].stars == 3) unlock(ACH_HARDCORE);
                g_over = 1; g_won = 1; g_screen = SC_OVER;
            }
        }
    } else {
        g_betweenTimer -= dt;
        if (g_betweenTimer <= 0) start_wave();
    }
}

static void update_game(float dt) {
    if (g_speed > 1.5f) {
        /* 高倍速拆成小步，保证物理稳定 */
        int n = (int)g_speed;
        for (int i = 0; i < n; i++) {
            if (g_screen != SC_GAME) break;
            step(dt);
        }
    } else {
        step(dt);
    }
}
/* ============================ 渲染层 ==================================== */

static HDC     g_memDC;
static HBITMAP g_memBmp;
static int    *g_memBits;
static int     g_memW, g_memH;

static HDC     g_backDC;
static HBITMAP g_backBmp;
static int    *g_backBits;

static HBITMAP g_warnBmp;
static HDC     g_warnDC;

static HFONT g_fontTitle, g_fontBig, g_fontMid, g_fontSmall, g_fontTiny, g_fontNum;
static HBRUSH g_brushTmp;

/* 按钮 */
enum {
    B_NONE = 0, B_START, B_CONTINUE, B_MAP0, B_MAP1, B_MAP2, B_MAP3, B_MODE,
    B_SPEED, B_PAUSE, B_UP, B_SELL, B_TARGET, B_RESUME, B_RESTART, B_MENU,
    B_RETRY, B_OVERMENU, B_HELP, B_CLOSEHELP,
    B_DEX, B_DEXBACK, B_TAB0, B_TAB1, B_TAB2,
    B_CONTRACT0, B_CONTRACT1, B_CONTRACT2, B_SKIPCONTRACT
};

typedef struct { int id, x, y, w, h, enabled; } Btn;
static Btn g_btns[80];
static int g_btnCount;

static void btn_add(int id, int x, int y, int w, int h, int enabled) {
    if (g_btnCount >= 80) return;
    Btn *b = &g_btns[g_btnCount++];
    b->id = id; b->x = x; b->y = y; b->w = w; b->h = h; b->enabled = enabled;
}

/* ---------------------------------------------------------------- 基本绘制 */
static COLORREF shade(COLORREF c, float f) {
    int r = (int)(GetRValue(c) * f);
    int g = (int)(GetGValue(c) * f);
    int b = (int)(GetBValue(c) * f);
    if (r > 255) r = 255; if (r < 0) r = 0;
    if (g > 255) g = 255; if (g < 0) g = 0;
    if (b > 255) b = 255; if (b < 0) b = 0;
    return RGB(r, g, b);
}

/* ---- GDI 对象缓存 ----------------------------------------------------
   每帧要画上千个色块/圆，若每个都 Create+Delete，GDI 句柄分配会成为
   主要开销（实测主菜单空转就吃满半核）。这里按颜色缓存画刷/画笔，
   命中则直接复用；缓存满时淘汰最早的一项。进程退出时由系统统一释放。 */
#define GDIO_CACHE_N 320

static struct { COLORREF key; HBRUSH h; } s_brushCache[GDIO_CACHE_N];
static struct { COLORREF key; int w; HPEN h; } s_penCache[GDIO_CACHE_N];
static int s_brushN, s_penN, s_brushNext, s_penNext;

static HBRUSH get_brush(COLORREF c) {
    for (int i = 0; i < s_brushN; i++)
        if (s_brushCache[i].key == c) return s_brushCache[i].h;
    HBRUSH h = CreateSolidBrush(c);
    if (!h) return (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (s_brushN < GDIO_CACHE_N) {
        s_brushCache[s_brushN].key = c;
        s_brushCache[s_brushN].h = h;
        return s_brushCache[s_brushN++].h;
    }
    /* 满了：轮转淘汰 */
    HBRUSH old = s_brushCache[s_brushNext].h;
    s_brushCache[s_brushNext].key = c;
    s_brushCache[s_brushNext].h = h;
    s_brushNext = (s_brushNext + 1) % GDIO_CACHE_N;
    if (old) DeleteObject(old);
    return h;
}

static HPEN get_pen(int w, COLORREF c) {
    if (w < 1) w = 1;
    for (int i = 0; i < s_penN; i++)
        if (s_penCache[i].key == c && s_penCache[i].w == w) return s_penCache[i].h;
    HPEN h = CreatePen(PS_SOLID, w, c);
    if (!h) return (HPEN)GetStockObject(NULL_PEN);
    if (s_penN < GDIO_CACHE_N) {
        s_penCache[s_penN].key = c;
        s_penCache[s_penN].w = w;
        s_penCache[s_penN].h = h;
        return s_penCache[s_penN++].h;
    }
    HPEN old = s_penCache[s_penNext].h;
    s_penCache[s_penNext].key = c;
    s_penCache[s_penNext].w = w;
    s_penCache[s_penNext].h = h;
    s_penNext = (s_penNext + 1) % GDIO_CACHE_N;
    if (old) DeleteObject(old);
    return h;
}

/* 圆角矩形：填充+描边一次完成（比两次绘制省一半 GDI 调用） */
static void fill_rrect(HDC dc, int x, int y, int w, int h, int rad, COLORREF fill, COLORREF line) {
    if (w <= 0 || h <= 0) return;
    HGDIOBJ ob = SelectObject(dc, get_brush(fill));
    HGDIOBJ op = SelectObject(dc, line ? get_pen(1, line) : GetStockObject(NULL_PEN));
    RoundRect(dc, x, y, x + w, y + h, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
}

static void fill_rect(HDC dc, int x, int y, int w, int h, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    RECT r = { x, y, x + w, y + h };
    FillRect(dc, &r, get_brush(c));
}

static void fill_circle(HDC dc, float cx, float cy, float r, COLORREF c) {
    if (r < 0.5f) return;
    HGDIOBJ ob = SelectObject(dc, get_brush(c));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r));
    SelectObject(dc, ob); SelectObject(dc, op);
}

static void fill_ellipse(HDC dc, float cx, float cy, float rx, float ry, COLORREF c) {
    if (rx < 0.5f || ry < 0.5f) return;
    HGDIOBJ ob = SelectObject(dc, get_brush(c));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, (int)(cx - rx), (int)(cy - ry), (int)(cx + rx), (int)(cy + ry));
    SelectObject(dc, ob); SelectObject(dc, op);
}

static void stroke_circle(HDC dc, float cx, float cy, float r, int w, COLORREF c) {
    if (r < 0.5f) return;
    HGDIOBJ op = SelectObject(dc, get_pen(w, c));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r));
    SelectObject(dc, op); SelectObject(dc, ob);
}

static void draw_line(HDC dc, float x1, float y1, float x2, float y2, int w, COLORREF c) {
    HGDIOBJ op = SelectObject(dc, get_pen(w, c));
    MoveToEx(dc, (int)x1, (int)y1, NULL);
    LineTo(dc, (int)x2, (int)y2);
    SelectObject(dc, op);
}

static void draw_poly(HDC dc, POINT *pts, int n, COLORREF fill, COLORREF line) {
    HGDIOBJ ob = SelectObject(dc, get_brush(fill));
    HGDIOBJ op = SelectObject(dc, line ? get_pen(1, line) : GetStockObject(NULL_PEN));
    Polygon(dc, pts, n);
    SelectObject(dc, ob); SelectObject(dc, op);
}

static void set_font(HDC dc, HFONT f) { SelectObject(dc, f); }

static void draw_text(HDC dc, const wchar_t *s, int x, int y, int w, int h,
                      COLORREF c, HFONT f, UINT fmt) {
    RECT r = { x, y, x + w, y + h };
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt);
}

#define TXT_CENTER (DT_CENTER | DT_VCENTER | DT_SINGLELINE)

static void draw_text_shadow(HDC dc, const wchar_t *s, int x, int y, int w, int h,
                             COLORREF c, HFONT f, UINT fmt) {
    draw_text(dc, s, x + 1, y + 1, w, h, RGB(0, 0, 0), f, fmt);
    draw_text(dc, s, x, y, w, h, c, f, fmt);
}

/* ---------------------------------------------------------------- 像素/位图 */
static HBITMAP make_dib(HDC refDC, int w, int h, int **bits) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   /* 自上而下 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *p = NULL;
    HBITMAP bmp = CreateDIBSection(refDC, &bi, DIB_RGB_COLORS, &p, NULL, 0);
    if (bits) *bits = (int *)p;
    return bmp;
}

static void set_px(int x, int y, COLORREF c, int alpha) {
    if (!g_memBits) return;
    if (x < 0 || y < 0 || x >= g_memW || y >= g_memH) return;
    int *p = &g_memBits[y * g_memW + x];
    int r0 = GetRValue(*p), g0 = GetGValue(*p), b0 = GetBValue(*p);
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    int rr = (r * alpha + r0 * (255 - alpha)) / 255;
    int gg = (g * alpha + g0 * (255 - alpha)) / 255;
    int bb = (b * alpha + b0 * (255 - alpha)) / 255;
    *p = RGB(rr, gg, bb);
}

/* 软边光斑：用像素混合出真实半透明感 */
static void soft_glow(float cx, float cy, float r, COLORREF c, int peak) {
    int x0 = (int)(cx - r), x1 = (int)(cx + r);
    int y0 = (int)(cy - r), y1 = (int)(cy + r);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx, dy = y - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > r) continue;
            float t = 1.0f - d / r;
            set_px(x, y, c, (int)(peak * t * t));
        }
    }
}

static void soft_shadow(float cx, float cy, float rx, float ry, int alpha) {
    int x0 = (int)(cx - rx), x1 = (int)(cx + rx);
    int y0 = (int)(cy - ry), y1 = (int)(cy + ry);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy > 1) continue;
            set_px(x, y, RGB(0, 0, 0), alpha);
        }
    }
}

/* ---------------------------------------------------------------- 图形初始化 */
static void init_graphics(void) {
    HDC screen = GetDC(NULL);
    g_memBmp = make_dib(screen, CLIENT_W, CLIENT_H, &g_memBits);
    g_memDC = CreateCompatibleDC(screen);
    SelectObject(g_memDC, g_memBmp);
    g_memW = CLIENT_W; g_memH = CLIENT_H;

    g_backBmp = make_dib(screen, PLAY_W, PLAY_H, &g_backBits);
    g_backDC = CreateCompatibleDC(screen);
    SelectObject(g_backDC, g_backBmp);

    /* 红色警戒遮罩：四周浓、中间透（alpha 作为遮罩强度） */
    {
        int *bits = NULL;
        g_warnBmp = make_dib(screen, PLAY_W, PLAY_H, &bits);
        for (int y = 0; y < PLAY_H; y++) {
            for (int x = 0; x < PLAY_W; x++) {
                float nx = (x - PLAY_W * 0.5f) / (PLAY_W * 0.5f);
                float ny = (y - PLAY_H * 0.5f) / (PLAY_H * 0.5f);
                float d = sqrtf(nx * nx + ny * ny);
                float t = clampf((d - 0.52f) / 0.72f, 0, 1);
                t = t * t;
                unsigned a = (unsigned)(t * 255);
                /* 预乘 alpha，避免半透明遮罩边缘发白 */
                unsigned cr = 0xC8u * a / 255, cg = 0x16u * a / 255, cb = 0x16u * a / 255;
                bits[y * PLAY_W + x] = (int)((a << 24) | (cr << 16) | (cg << 8) | cb);
            }
        }
        g_warnDC = CreateCompatibleDC(screen);
        SelectObject(g_warnDC, g_warnBmp);
    }
    ReleaseDC(NULL, screen);

    g_fontTitle = CreateFontW(-40, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    g_fontBig = CreateFontW(-26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    g_fontMid = CreateFontW(-17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    g_fontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    g_fontTiny = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    g_fontNum = CreateFontW(-20, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Consolas");
}

/* ---------------------------------------------------------------- 距离/装饰 */
static float seg_dist(float px, float py, float x1, float y1, float x2, float y2) {
    float vx = x2 - x1, vy = y2 - y1;
    float wx = px - x1, wy = py - y1;
    float l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
    t = clampf(t, 0, 1);
    float dx = px - (x1 + vx * t), dy = py - (y1 + vy * t);
    return sqrtf(dx * dx + dy * dy);
}

static float dist_to_path(float x, float y) {
    float best = 1e9f;
    for (int i = 0; i < g_pathN - 1; i++) {
        float d = seg_dist(x, y, (float)g_pathX[i], (float)g_pathY[i],
                           (float)g_pathX[i + 1], (float)g_pathY[i + 1]);
        if (d < best) best = d;
    }
    return best;
}

static int near_slot(int x, int y, float r) {
    for (int i = 0; i < g_slotCount; i++) {
        float dx = (float)(x - g_slotX[i]), dy = (float)(y - g_slotY[i]);
        if (dx * dx + dy * dy < r * r) return 1;
    }
    return 0;
}

static void draw_tree(HDC dc, int x, int y, float s, COLORREF leaf) {
    fill_rect(dc, x - (int)(4 * s), y, (int)(8 * s), (int)(22 * s), RGB(120, 84, 52));
    fill_circle(dc, (float)x, (float)(y - 8 * s), 20 * s, shade(leaf, 0.82f));
    fill_circle(dc, (float)(x - 12 * s), (float)(y + 2 * s), 15 * s, leaf);
    fill_circle(dc, (float)(x + 12 * s), (float)(y + 2 * s), 15 * s, leaf);
    fill_circle(dc, (float)x, (float)(y - 14 * s), 15 * s, shade(leaf, 1.12f));
}

static void draw_bush(HDC dc, int x, int y, float s, COLORREF leaf) {
    fill_circle(dc, (float)(x - 10 * s), (float)y, 12 * s, shade(leaf, 0.9f));
    fill_circle(dc, (float)(x + 10 * s), (float)y, 12 * s, shade(leaf, 0.9f));
    fill_circle(dc, (float)x, (float)(y - 6 * s), 14 * s, leaf);
}

/* ---------------------------------------------------------------- 背景生成 */
static void build_background(int mapIdx) {
    HDC dc = g_backDC;
    const MapDef *m = &MAPS[mapIdx];
    COLORREF g1, g2, p1, p2;
    switch (m->theme) {
    case 0: g1 = C(143,206,110); g2 = C(155,217,124); p1 = C(201,163,107); p2 = C(226,194,144); break;
    case 1: g1 = C(107, 95, 99); g2 = C(117,105,112); p1 = C(143,130,134); p2 = C(163,149,154); break;
    case 2: g1 = C(215,233,245); g2 = C(230,242,251); p1 = C(169,198,216); p2 = C(203,223,240); break;
    default:g1 = C(226,192,124); g2 = C(236,210,148); p1 = C(195,154, 92); p2 = C(230,203,147); break;
    }

    fill_rect(dc, 0, 0, PLAY_W, PLAY_H, g1);
    /* 棋盘格纹理 */
    for (int y = 0; y < PLAY_H; y += 60)
        for (int x = 0; x < PLAY_W; x += 60)
            if (((x / 60) + (y / 60)) % 2 == 0)
                fill_rect(dc, x, y, 60, 60, g2);

    srand(1000 + mapIdx * 77);
    if (m->theme == 0) {
        for (int i = 0; i < 16; i++) {
            int x = rndi(20, PLAY_W - 20), y = rndi(30, PLAY_H - 20);
            if (dist_to_path((float)x, (float)y) < 58 || near_slot(x, y, 44)) continue;
            if (i % 4 == 0) draw_tree(dc, x, y, rndf(0.7f, 1.0f), C(88,178, 84));
            else draw_bush(dc, x, y, rndf(0.6f, 1.0f), C(104,196, 96));
        }
        for (int i = 0; i < 26; i++) {
            int x = rndi(10, PLAY_W - 10), y = rndi(20, PLAY_H - 10);
            if (dist_to_path((float)x, (float)y) < 40) continue;
            fill_circle(dc, (float)x, (float)y, 2.2f, i % 3 ? C(255,246,180) : C(255,168,200));
        }
        /* 草丛簇：三撇小草，让地面不再空旷 */
        for (int i = 0; i < 34; i++) {
            int x = rndi(14, PLAY_W - 14), y = rndi(24, PLAY_H - 12);
            if (dist_to_path((float)x, (float)y) < 42 || near_slot(x, y, 40)) continue;
            COLORREF gc = shade(g1, i % 2 ? 0.86f : 0.94f);
            draw_line(dc, (float)x - 5, (float)y + 3, (float)x - 8, (float)y - 7, 2, gc);
            draw_line(dc, (float)x,     (float)y + 3, (float)x,     (float)y - 9, 2, gc);
            draw_line(dc, (float)x + 5, (float)y + 3, (float)x + 8, (float)y - 7, 2, gc);
        }
    } else if (m->theme == 1) {
        for (int i = 0; i < 18; i++) {
            int x = rndi(20, PLAY_W - 20), y = rndi(30, PLAY_H - 20);
            if (dist_to_path((float)x, (float)y) < 58 || near_slot(x, y, 44)) continue;
            POINT pts[6];
            int n = 5, rad = rndi(12, 26);
            for (int k = 0; k < n; k++) {
                float a = k * TAU / n + rndf(0, 0.5f);
                pts[k].x = x + (int)(cosf(a) * rad * rndf(0.7f, 1.1f));
                pts[k].y = y + (int)(sinf(a) * rad * 0.7f * rndf(0.7f, 1.1f));
            }
            draw_poly(dc, pts, n, C(84, 76, 82), C(58, 50, 56));
        }
        for (int i = 0; i < 10; i++) {
            int x = rndi(30, PLAY_W - 30), y = rndi(30, PLAY_H - 30);
            if (dist_to_path((float)x, (float)y) < 50) continue;
            draw_line(dc, (float)x, (float)y, (float)(x + rndi(-30, 30)), (float)(y + rndi(-16, 16)), 2, C(224, 96, 40));
        }
    } else if (m->theme == 2) {
        for (int i = 0; i < 14; i++) {
            int x = rndi(20, PLAY_W - 20), y = rndi(30, PLAY_H - 20);
            if (dist_to_path((float)x, (float)y) < 56 || near_slot(x, y, 44)) continue;
            POINT pts[4];
            pts[0].x = x - 22; pts[0].y = y + 10;
            pts[1].x = x - 8;  pts[1].y = y - 14;
            pts[2].x = x + 18; pts[2].y = y - 6;
            pts[3].x = x + 24; pts[3].y = y + 12;
            draw_poly(dc, pts, 4, C(238,250,255), C(196,222,238));
        }
        for (int i = 0; i < 8; i++) {
            int x = rndi(24, PLAY_W - 24), y = rndi(30, PLAY_H - 30);
            if (dist_to_path((float)x, (float)y) < 50) continue;
            POINT pts[3] = { {x, y - 16}, {x + 10, y + 8}, {x - 10, y + 8} };
            draw_poly(dc, pts, 3, C(224,244,255), C(176,208,228));
        }
    } else {
        /* 沙漠：沙丘弧线 + 金字塔 + 仙人掌 + 绿洲 */
        for (int i = 0; i < 8; i++) {
            int x = rndi(-40, PLAY_W), y = rndi(30, PLAY_H - 20);
            if (dist_to_path((float)x, (float)y) < 60) continue;
            HBRUSH b = CreateSolidBrush(C(212,176,110));
            HPEN p = CreatePen(PS_SOLID, 3, C(212,176,110));
            HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
            Arc(dc, x, y, x + 220, y + 90, x + 220, y + 45, x, y + 45);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(b); DeleteObject(p);
        }
        for (int i = 0; i < 3; i++) {
            int x = rndi(90, PLAY_W - 90), y = rndi(120, PLAY_H - 70);
            if (dist_to_path((float)x, (float)y) < 90) continue;
            POINT pts[3] = { {x, y - 60}, {x + 66, y + 40}, {x - 66, y + 40} };
            draw_poly(dc, pts, 3, C(214,176,104), C(178,138, 74));
            POINT inner[3] = { {x, y - 60}, {x + 26, y + 40}, {x - 26, y + 40} };
            draw_poly(dc, inner, 3, C(232,198,134), C(178,138, 74));
        }
        for (int i = 0; i < 10; i++) {
            int x = rndi(20, PLAY_W - 20), y = rndi(40, PLAY_H - 20);
            if (dist_to_path((float)x, (float)y) < 52 || near_slot(x, y, 44)) continue;
            fill_rect(dc, x - 4, y - 30, 9, 34, C(96,158, 92));
            fill_rect(dc, x - 18, y - 18, 12, 8, C(96,158, 92));
            fill_rect(dc, x - 18, y - 22, 8, 14, C(96,158, 92));
            fill_rect(dc, x + 6, y - 26, 11, 7, C(96,158, 92));
            fill_rect(dc, x + 10, y - 30, 8, 13, C(96,158, 92));
        }
        /* 绿洲 */
        fill_circle(dc, 120.0f, 520.0f, 52.0f, C(120,190,170));
        fill_circle(dc, 120.0f, 520.0f, 44.0f, C(150,212,190));
    }

    /* 路径：投影 → 外描边 → 主色 → 内亮 → 虚线中心 */
    POINT pts[12];
    for (int i = 0; i < g_pathN; i++) { pts[i].x = g_pathX[i]; pts[i].y = g_pathY[i]; }
    LOGBRUSH lb;
    /* 投影：整体向下偏移，让路从地面“浮”起来 */
    {
        POINT sh[12];
        for (int i = 0; i < g_pathN; i++) { sh[i].x = pts[i].x + 3; sh[i].y = pts[i].y + 5; }
        lb.lbStyle = BS_SOLID; lb.lbColor = shade(g1, 0.80f); lb.lbHatch = 0;
        HPEN sp = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_JOIN_ROUND | PS_ENDCAP_ROUND, 56, &lb, 0, NULL);
        HGDIOBJ osp = SelectObject(dc, sp);
        HGDIOBJ obs = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Polyline(dc, sh, g_pathN);
        SelectObject(dc, osp); SelectObject(dc, obs);
        DeleteObject(sp);
    }
    lb.lbStyle = BS_SOLID; lb.lbColor = shade(p1, 0.82f); lb.lbHatch = 0;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_JOIN_ROUND | PS_ENDCAP_ROUND, 52, &lb, 0, NULL);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Polyline(dc, pts, g_pathN);
    SelectObject(dc, op);
    DeleteObject(pen);

    lb.lbColor = p1;
    pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_JOIN_ROUND | PS_ENDCAP_ROUND, 44, &lb, 0, NULL);
    op = SelectObject(dc, pen);
    Polyline(dc, pts, g_pathN);
    SelectObject(dc, op);
    DeleteObject(pen);

    lb.lbColor = p2;
    pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_JOIN_ROUND | PS_ENDCAP_ROUND, 30, &lb, 0, NULL);
    op = SelectObject(dc, pen);
    Polyline(dc, pts, g_pathN);
    SelectObject(dc, op);
    DeleteObject(pen);

    /* 路面虚线 */
    HPEN dash = CreatePen(PS_DOT, 3, shade(p1, 0.75f));
    op = SelectObject(dc, dash);
    Polyline(dc, pts, g_pathN);
    SelectObject(dc, op);
    DeleteObject(dash);

    /* 炮塔位：石台 */
    for (int i = 0; i < g_slotCount; i++) {
        int x = g_slotX[i], y = g_slotY[i];
        /* 双层柔化阴影，取代生硬的纯黑圆 */
        fill_circle(dc, (float)(x + 2), (float)(y + 6), 27, shade(g1, 0.78f));
        fill_circle(dc, (float)(x + 1), (float)(y + 4), 26, shade(g1, 0.72f));
        fill_circle(dc, (float)x, (float)y, 24, C(120,124,116));
        fill_circle(dc, (float)x, (float)y, 20, C(168,172,162));
        fill_circle(dc, (float)x, (float)(y - 1), 16, C(196,200,188));
        stroke_circle(dc, (float)x, (float)y, 24, 2, C(96,100,92));
    }

    /* 起点：恶魔出口 */
    {
        int x = g_pathX[0], y = g_pathY[0];
        int bx = x < 40 ? 24 : (x > PLAY_W - 40 ? PLAY_W - 24 : x);
        int by = y;
        fill_circle(dc, (float)bx, (float)by, 30, C(48, 26, 42));
        fill_circle(dc, (float)bx, (float)by, 24, C(30, 16, 26));
        /* 犄角 */
        POINT horn1[3] = { {bx - 26, by - 16}, {bx - 16, by - 40}, {bx - 8, by - 18} };
        POINT horn2[3] = { {bx + 26, by - 16}, {bx + 16, by - 40}, {bx + 8, by - 18} };
        draw_poly(dc, horn1, 3, C(70, 34, 54), C(24, 12, 20));
        draw_poly(dc, horn2, 3, C(70, 34, 54), C(24, 12, 20));
        fill_circle(dc, (float)(bx - 8), (float)(by - 2), 4.0f, C(255, 92, 68));
        fill_circle(dc, (float)(bx + 8), (float)(by - 2), 4.0f, C(255, 92, 68));
    }

    /* 终点：萝卜城堡 */
    {
        int x = g_pathX[g_pathN - 1], y = g_pathY[g_pathN - 1];
        int bx = x > PLAY_W - 46 ? PLAY_W - 46 : x;
        int by = y;
        fill_circle(dc, (float)bx, (float)(by + 20), 36, RGB(0, 0, 0));
        fill_circle(dc, (float)bx, (float)(by + 18), 34, C(96, 150, 78));
        /* 城堡身体 */
        fill_rrect(dc, bx - 26, by - 26, 52, 46, 10, C(222, 168, 96), C(160, 112, 58));
        /* 城垛 */
        for (int i = 0; i < 3; i++) fill_rect(dc, bx - 26 + i * 18, by - 36, 12, 12, C(222, 168, 96));
        /* 萝卜本体 */
        POINT body[3] = { {bx, by - 58}, {bx + 20, by - 14}, {bx - 20, by - 14} };
        draw_poly(dc, body, 3, C(255, 138, 62), C(214, 96, 38));
        /* 叶子 */
        fill_circle(dc, (float)(bx - 12), (float)(by - 62), 9, C(88, 190, 96));
        fill_circle(dc, (float)(bx + 2), (float)(by - 68), 10, C(110, 210, 110));
        fill_circle(dc, (float)(bx + 14), (float)(by - 61), 9, C(88, 190, 96));
        /* 眼睛 */
        fill_circle(dc, (float)(bx - 7), (float)(by - 32), 4, C(40, 30, 26));
        fill_circle(dc, (float)(bx + 7), (float)(by - 32), 4, C(40, 30, 26));
    }
}

/* ---------------------------------------------------------------- 氛围粒子 */
static void draw_ambient(HDC dc) {
    int theme = MAPS[g_map].theme;
    for (int i = 0; i < 26; i++) {
        float seed = i * 97.13f;
        float x = fmodf(seed * 13.7f + i * 41.0f, (float)PLAY_W);
        float y = fmodf(seed * 7.3f + i * 57.0f, (float)PLAY_H);
        if (theme == 0) {
            float px = x + sinf(g_time * 0.7f + i) * 30;
            float py = y + cosf(g_time * 0.5f + i) * 18;
            float tw = 0.4f + 0.6f * fabsf(sinf(g_time * 2.2f + i));
            if (i < 12) {
                fill_circle(dc, px, py, 4.0f, C(255, 245, 160));
                fill_circle(dc, px, py, 1.8f, C(255, 255, 224));
            } else {
                fill_circle(dc, px, py, 2.4f, C(215, 240, 200));
            }
        } else if (theme == 1) {
            float py = PLAY_H - fmodf(g_time * (30 + i * 2) + y, (float)PLAY_H + 40);
            float px = x + sinf(g_time * 2 + i) * 12;
            float fl = 0.55f + 0.45f * sinf(g_time * 6 + i);
            fill_circle(dc, px, py, 1.6f, RGB((int)(255), (int)(120 * fl + 40), 40));
        } else if (theme == 2) {
            float py = fmodf(g_time * (28 + i * 1.5f) + y, (float)PLAY_H + 20) - 10;
            float px = x + sinf(g_time * 1.2f + i) * 22;
            fill_circle(dc, px, py, 1.2f + (i % 3) * 0.6f, C(255, 255, 255));
        } else {
            float px = fmodf(g_time * (90 + i * 6) + x, (float)PLAY_W + 60) - 30;
            float py = y + sinf(g_time * 1.6f + i) * 10;
            draw_line(dc, px - 16, py, px, py, 2, C(255, 242, 206));
        }
    }
    /* 云影 */
    for (int i = 0; i < 2; i++) {
        float cx = fmodf(PLAY_W + 400 - g_time * (7 + i * 3), PLAY_W + 800) - 300;
        float cy = 120 + i * 300;
        soft_shadow(PLAY_X + cx, PLAY_Y + cy, 150, 46, 12);
    }
}
/* ============================ 角色与世界绘制 ============================ */

static void draw_coin(HDC dc, int x, int y, int r) {
    fill_circle(dc, (float)x, (float)y, (float)r, C(226, 168, 44));
    fill_circle(dc, (float)x, (float)y, r - 2.0f, C(255, 206, 74));
    fill_rect(dc, x - 1, y - r + 4, 3, r * 2 - 8, C(206, 148, 30));
}

static void draw_heart(HDC dc, int x, int y, int r, COLORREF c) {
    fill_circle(dc, (float)(x - r / 2), (float)(y - r / 4), r * 0.58f, c);
    fill_circle(dc, (float)(x + r / 2), (float)(y - r / 4), r * 0.58f, c);
    POINT p[3] = { {x - r, y - r / 6}, {x + r, y - r / 6}, {x, y + r} };
    draw_poly(dc, p, 3, c, c);
}

static void star_points(int cx, int cy, float r, POINT *pts) {
    for (int i = 0; i < 10; i++) {
        float a = -PI / 2 + i * PI / 5;
        float rr = (i % 2 == 0) ? r : r * 0.45f;
        pts[i].x = cx + (int)(cosf(a) * rr);
        pts[i].y = cy + (int)(sinf(a) * rr);
    }
}

/* 武器图标（也是炮塔顶部符号） */
static void draw_glyph(HDC dc, int type, int cx, int cy, float s, COLORREF col) {
    COLORREF dark = shade(col, 0.62f);
    switch (type) {
    case TK_BULLET: {  /* 瓶子炮 */
        fill_rrect(dc, cx - (int)(5 * s), cy - (int)(9 * s), (int)(10 * s), (int)(16 * s), (int)(3 * s), col, dark);
        fill_rect(dc, cx - (int)(2 * s), cy - (int)(14 * s), (int)(4 * s), (int)(5 * s), dark);
        fill_circle(dc, (float)cx, (float)(cy - 2 * s), 3.2f * s, C(255, 255, 255));
        break;
    }
    case TK_SNOW: {    /* 雪花 */
        for (int i = 0; i < 3; i++) {
            float a = i * PI / 3;
            draw_line(dc, (float)(cx - cosf(a) * 11 * s), (float)(cy - sinf(a) * 11 * s),
                          (float)(cx + cosf(a) * 11 * s), (float)(cy + sinf(a) * 11 * s),
                          (int)(2 * s) + 1, C(255, 255, 255));
        }
        fill_circle(dc, (float)cx, (float)cy, 2.6f * s, C(255, 255, 255));
        break;
    }
    case TK_SPLASH: {  /* 星星 */
        POINT p[10];
        star_points(cx, cy, 12 * s, p);
        draw_poly(dc, p, 10, col, dark);
        break;
    }
    case TK_BURN: {    /* 火焰 */
        POINT p[3] = { {cx, cy - (int)(12 * s)}, {cx + (int)(9 * s), cy + (int)(8 * s)},
                       {cx - (int)(9 * s), cy + (int)(8 * s)} };
        draw_poly(dc, p, 3, C(255, 176, 60), C(224, 92, 34));
        POINT q[3] = { {cx, cy - (int)(4 * s)}, {cx + (int)(5 * s), cy + (int)(8 * s)},
                       {cx - (int)(5 * s), cy + (int)(8 * s)} };
        draw_poly(dc, q, 3, C(255, 232, 130), C(255, 196, 80));
        break;
    }
    case TK_PIERCE: {  /* 巨箭 */
        draw_line(dc, (float)(cx - 12 * s), (float)(cy + 8 * s), (float)(cx + 8 * s), (float)(cy - 8 * s), (int)(3 * s) + 1, C(226, 214, 255));
        POINT p[3] = { {cx + (int)(14 * s), cy - (int)(14 * s)}, {cx + (int)(2 * s), cy - (int)(10 * s)},
                       {cx + (int)(10 * s), cy - (int)(2 * s)} };
        draw_poly(dc, p, 3, col, dark);
        break;
    }
    case TK_CHAIN: {   /* 闪电 */
        POINT p[4] = { {cx - (int)(4 * s), cy - (int)(12 * s)}, {cx + (int)(4 * s), cy - (int)(2 * s)},
                       {cx - (int)(2 * s), cy + (int)(1 * s)}, {cx + (int)(5 * s), cy + (int)(12 * s)} };
        for (int i = 0; i < 3; i++)
            draw_line(dc, (float)p[i].x, (float)p[i].y, (float)p[i + 1].x, (float)p[i + 1].y, (int)(3 * s) + 1, C(255, 255, 255));
        break;
    }
    case TK_PUSH: {    /* 旋风 */
        for (int i = 0; i < 3; i++) {
            HBRUSH b = (HBRUSH)GetStockObject(NULL_BRUSH);
            HPEN pn = CreatePen(PS_SOLID, (int)(2 * s) + 1, i == 1 ? C(255, 255, 255) : col);
            HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pn);
            int rr = (int)((4 + i * 4) * s);
            Arc(dc, cx - rr, cy - rr * 2, cx + rr, cy + rr * 2, cx + rr, cy, cx - rr, cy);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(pn);
        }
        break;
    }
    case TK_BEAM: {    /* 菱形晶体 */
        POINT p[4] = { {cx, cy - (int)(12 * s)}, {cx + (int)(8 * s), cy},
                       {cx, cy + (int)(12 * s)}, {cx - (int)(8 * s), cy} };
        draw_poly(dc, p, 4, C(255, 214, 246), col);
        fill_circle(dc, (float)cx, (float)cy, 2.4f * s, C(255, 255, 255));
        break;
    }
    case TK_GATLING: { /* 加特林 */
        for (int i = -1; i <= 1; i++)
            fill_rect(dc, cx + (int)(i * 5 * s) - (int)(1.6f * s), cy - (int)(10 * s),
                      (int)(3.2f * s), (int)(20 * s), i == 0 ? C(240, 244, 250) : col);
        fill_circle(dc, (float)cx, (float)cy, 3 * s, dark);
        break;
    }
    case TK_FREEZE: {  /* 冰晶 */
        POINT p[4] = { {cx, cy - (int)(12 * s)}, {cx + (int)(8 * s), cy},
                       {cx, cy + (int)(12 * s)}, {cx - (int)(8 * s), cy} };
        draw_poly(dc, p, 4, C(230, 250, 255), C(140, 200, 230));
        draw_line(dc, (float)cx, (float)(cy - 9 * s), (float)cx, (float)(cy + 9 * s), 2, C(255, 255, 255));
        break;
    }
    case TK_POISON: {  /* 毒液 */
        POINT p[3] = { {cx, cy - (int)(13 * s)}, {cx + (int)(8 * s), cy + (int)(4 * s)},
                       {cx - (int)(8 * s), cy + (int)(4 * s)} };
        draw_poly(dc, p, 3, col, dark);
        fill_circle(dc, (float)cx, (float)(cy + 4 * s), 8.0f * s, col);
        stroke_circle(dc, (float)cx, (float)(cy + 4 * s), 8.0f * s, 1, dark);
        fill_circle(dc, (float)(cx - 2 * s), (float)(cy + 2 * s), 2.0f * s, C(220, 255, 200));
        break;
    }
    case TK_MISSILE: { /* 火箭 */
        fill_rrect(dc, cx - (int)(5 * s), cy - (int)(6 * s), (int)(10 * s), (int)(16 * s), (int)(4 * s), C(240, 240, 240), C(180, 180, 190));
        POINT p[3] = { {cx, cy - (int)(15 * s)}, {cx + (int)(5 * s), cy - (int)(5 * s)},
                       {cx - (int)(5 * s), cy - (int)(5 * s)} };
        draw_poly(dc, p, 3, C(255, 90, 70), C(200, 60, 50));
        POINT f[3] = { {cx - (int)(4 * s), cy + (int)(10 * s)}, {cx + (int)(4 * s), cy + (int)(10 * s)},
                       {cx, cy + (int)(17 * s)} };
        draw_poly(dc, f, 3, C(255, 200, 90), C(240, 140, 60));
        break;
    }
    }
}

/* ---------------------------------------------------------------- 炮塔 */
static void draw_tower(HDC dc, int si) {
    Tower *t = &g_slots[si];
    if (!t->used) return;
    const TowerDef *d = &TOWERS[t->type];
    int x = g_slotX[si], y = g_slotY[si];
    float s = 1.0f;

    /* 选中/悬停光环 */
    if (si == g_selSlot) {
        stroke_circle(dc, (float)x, (float)y, 32, 2, C(255, 216, 94));
        stroke_circle(dc, (float)x, (float)y, 30, 1, C(255, 240, 170));
    }
    /* 底座 */
    fill_circle(dc, (float)x, (float)(y + 4), 22, C(60, 66, 58));
    fill_circle(dc, (float)x, (float)y, 21, shade(d->color, 0.55f));
    fill_circle(dc, (float)x, (float)y, 18, shade(d->color, 0.8f));
    stroke_circle(dc, (float)x, (float)y, 20, 2, shade(d->color, 0.45f));

    /* 炮管 */
    float ang = t->ang;
    float bx = (float)x, by = (float)(y - 12);
    float tipx = bx + cosf(ang) * 22, tipy = by + sinf(ang) * 22 * 0.7f;
    draw_line(dc, bx, by, tipx, tipy, 9, shade(d->color, 0.92f));
    draw_line(dc, bx, by, tipx, tipy, 4, shade(d->color, 1.25f));
    fill_circle(dc, tipx, tipy, 7.0f, d->color);
    stroke_circle(dc, tipx, tipy, 7.0f, 2, shade(d->color, 0.55f));

    /* 顶部符号 */
    draw_glyph(dc, t->type, (int)bx, (int)(by - 5), 0.52f, d->color);

    /* 等级星 */
    for (int i = 0; i < t->level; i++) {
        POINT p[10];
        star_points(x - 12 + i * 12, y + 24, 5.0f, p);
        draw_poly(dc, p, 10, C(255, 216, 94), C(180, 140, 30));
    }
    /* 目标模式徽章 */
    if (t->mode != 0) {
        const COLORREF mc = (t->mode == 1) ? C(255, 120, 120) : (t->mode == 2) ? C(120, 210, 255) : C(180, 240, 150);
        fill_circle(dc, (float)(x + 18), (float)(y + 16), 8.0f, C(30, 40, 34));
        fill_circle(dc, (float)(x + 18), (float)(y + 16), 6.0f, mc);
    }
    /* 攻击闪光 */
    if (t->flash > 0) {
        stroke_circle(dc, tipx, tipy, 11.0f, 2, C(255, 255, 220));
    }
}

/* ---------------------------------------------------------------- 怪物 */
static void draw_enemy(HDC dc, Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    float r = d->r * (e->elite ? 1.18f : 1.0f);
    float x = e->px, y = e->py;
    float squash = 1.0f + sinf(e->wob) * 0.05f;
    int elite = e->elite;
    int boss = (e->type == EN_BOSS);
    COLORREF body = d->body;

    if (e->freezeT > g_time) body = C(180, 225, 240);
    else if (e->slowT > g_time) body = shade(d->body, 0.82f);

    /* 地面阴影 */
    {
        HBRUSH b = CreateSolidBrush(RGB(30, 40, 30));
        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        for (int i = 2; i >= 0; i--) {
            int rr = (int)(r * (1.0f + i * 0.12f));
            Ellipse(dc, (int)(x - rr), (int)(y + r * 0.72f), (int)(x + rr), (int)(y + r * 0.95f));
        }
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(b);
    }

    /* 魔王光晕 */
    if (boss) {
        for (int i = 3; i >= 1; i--)
            stroke_circle(dc, x, y, r + i * 7.0f, 2, e->rage ? C(210, 40, 40) : C(130, 40, 50));
    }
    /* 精英金环（必须显式设置画笔与空画刷，否则会用 DC 默认黑色刷子糊出扇形） */
    if (elite) {
        int sv = SaveDC(dc);
        HPEN ep = CreatePen(PS_SOLID, 3, C(255, 216, 94));
        SelectObject(dc, ep);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        int rad = (int)(r + 12);
        for (int k = 0; k < 12; k += 2) {
            float a1 = g_time * 1.6f + k * TAU / 12;
            float a2 = a1 + TAU / 24;
            Arc(dc, (int)(x - rad), (int)(y - rad), (int)(x + rad), (int)(y + rad),
                (int)(x + cosf(a1) * rad), (int)(y + sinf(a1) * rad),
                (int)(x + cosf(a2) * rad), (int)(y + sinf(a2) * rad));
        }
        RestoreDC(dc, sv);
        DeleteObject(ep);
    }

    /* 身体（squash：行走时的纵向弹性） */
    fill_ellipse(dc, x, y, r, r * squash, shade(body, 0.75f));
    fill_ellipse(dc, x, y - r * 0.06f, r * 0.92f, r * 0.92f * squash, body);
    fill_circle(dc, x - r * 0.25f, y - r * 0.3f, r * 0.34f, shade(body, 1.18f));

    /* 史莱姆：半透明感（叠一层亮色） */
    if (e->type == EN_SLIME)
        fill_circle(dc, x, y + r * 0.25f, r * 0.6f, shade(body, 1.1f));

    /* 眼睛 */
    float eyeDx = r * 0.32f, eyeY = y - r * 0.12f;
    fill_circle(dc, x - eyeDx, eyeY, r * 0.24f, C(255, 255, 255));
    fill_circle(dc, x + eyeDx, eyeY, r * 0.24f, C(255, 255, 255));
    fill_circle(dc, x - eyeDx + 1, eyeY + 1, r * 0.11f, C(40, 36, 40));
    fill_circle(dc, x + eyeDx + 1, eyeY + 1, r * 0.11f, C(40, 36, 40));

    if (boss) {
        /* 犄角 + 怒眉 */
        POINT h1[3] = { {(int)(x - r * 0.7f), (int)(y - r * 0.55f)}, {(int)(x - r * 1.05f), (int)(y - r * 1.35f)},
                        {(int)(x - r * 0.35f), (int)(y - r * 0.95f)} };
        POINT h2[3] = { {(int)(x + r * 0.7f), (int)(y - r * 0.55f)}, {(int)(x + r * 1.05f), (int)(y - r * 1.35f)},
                        {(int)(x + r * 0.35f), (int)(y - r * 0.95f)} };
        draw_poly(dc, h1, 3, C(120, 40, 46), C(70, 20, 26));
        draw_poly(dc, h2, 3, C(120, 40, 46), C(70, 20, 26));
        draw_line(dc, x - eyeDx - 6, eyeY - 10, x - eyeDx + 6, eyeY - 4, 3, C(70, 20, 26));
        draw_line(dc, x + eyeDx + 6, eyeY - 10, x + eyeDx - 6, eyeY - 4, 3, C(70, 20, 26));
        /* 獠牙 */
        POINT t1[3] = { {(int)(x - 6), (int)(y + r * 0.35f)}, {(int)(x), (int)(y + r * 0.62f)},
                        {(int)(x + 6), (int)(y + r * 0.35f)} };
        draw_poly(dc, t1, 3, C(255, 246, 230), C(180, 160, 150));
    }

    /* 冰冻壳 */
    if (e->freezeT > g_time) {
        stroke_circle(dc, x, y, r + 3, 2, C(220, 245, 255));
        stroke_circle(dc, x, y, r + 7, 1, C(170, 220, 245));
    }
    /* 受击白闪 */
    if (e->flash > 0.35f) {
        stroke_circle(dc, x, y, r * 0.98f, 2, C(255, 255, 255));
    }

    /* 血条 */
    float hpr = e->hp / e->maxHp;
    if (hpr > 1) hpr = 1;
    if (hpr < 0) hpr = 0;
    float bw = boss ? r * 2.6f : r * 2.2f;
    if (bw < 34) bw = 34;
    float bx = x - bw / 2;
    float by = y - r - 16;
    fill_rect(dc, (int)bx - 1, (int)by - 1, (int)bw + 2, 7, C(20, 26, 22));
    fill_rect(dc, (int)bx, (int)by, (int)bw, 5, C(58, 64, 56));
    /* 掉血残影 */
    float lagr = e->lagHp / e->maxHp;
    if (lagr > 1) lagr = 1;
    if (lagr > hpr)
        fill_rect(dc, (int)bx, (int)by, (int)(bw * lagr), 5, C(255, 255, 255));
    COLORREF hc = hpr > 0.5f ? C(124, 252, 110) : (hpr > 0.25f ? C(255, 210, 63) : C(255, 106, 94));
    fill_rect(dc, (int)bx, (int)by, (int)(bw * hpr), 5, hc);
    if (elite) {
        HPEN p = CreatePen(PS_SOLID, 1, C(255, 216, 94));
        HGDIOBJ op = SelectObject(dc, p);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, (int)bx - 2, (int)by - 2, (int)(bx + bw + 2), (int)(by + 7));
        SelectObject(dc, op); SelectObject(dc, ob);
        DeleteObject(p);
    }
    /* 护盾条 */
    if (e->shield > 0) {
        float sr = e->shieldMax > 0 ? e->shield / e->shieldMax : 0;
        fill_rect(dc, (int)bx, (int)by - 5, (int)bw, 3, C(30, 46, 60));
        fill_rect(dc, (int)bx, (int)by - 5, (int)(bw * sr), 3, C(120, 200, 245));
    }
    /* 词缀小点 */
    if (elite) {
        for (int i = 0; i < e->affixN; i++) {
            int ax = (int)(bx + bw) - 6 - i * 9;
            fill_circle(dc, (float)ax, by - 10, 3.6f, AFFIX_COLOR[e->affix[i]]);
        }
    }
    /* 状态点 */
    if (e->slowT > g_time) fill_circle(dc, (int)(bx - 6), by + 2, 2.4f, C(140, 210, 255));
    if (e->burnT > g_time) fill_circle(dc, (int)(bx - 6), by - 4, 2.4f, C(255, 150, 60));
    if (e->poisonT > g_time) fill_circle(dc, (int)(bx - 6), by + 8, 2.4f, C(140, 220, 90));
}

/* ---------------------------------------------------------------- 子弹/特效 */
static void draw_bullets(HDC dc) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g_bullets[i];
        if (!b->alive) continue;
        if (b->kind == 2) {
            /* 导弹拖尾 */
            draw_line(dc, b->x - cosf(b->ang) * 16, b->y - sinf(b->ang) * 16, b->x, b->y, 3, C(255, 190, 110));
            fill_circle(dc, b->x, b->y, 5.0f, C(240, 240, 245));
            fill_circle(dc, b->x, b->y, 3.0f, C(255, 120, 80));
        } else {
            fill_circle(dc, b->x, b->y, 6.0f, shade(b->color, 1.4f));
            fill_circle(dc, b->x, b->y, 3.6f, b->color);
            fill_circle(dc, b->x, b->y, 1.6f, C(255, 255, 255));
        }
    }
}

static void draw_fx(HDC dc) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &g_fx[i];
        if (!f->alive) continue;
        float t = f->life / f->maxlife;
        if (f->kind == 0) {
            stroke_circle(dc, f->x, f->y, f->r * (1.6f - t), (int)(3 * t) + 1, (COLORREF)f->color);
        } else if (f->kind == 1) {
            stroke_circle(dc, f->x, f->y, f->r * (1.15f - t * 0.15f), f->w > 0 ? (int)f->w : 3, (COLORREF)f->color);
            if (f->r > 60)
                stroke_circle(dc, f->x, f->y, f->r * (1.15f - t * 0.15f) * 0.86f, 1, shade((COLORREF)f->color, 1.2f));
        } else if (f->kind == 2) {
            fill_circle(dc, f->x, f->y, f->r * t, (COLORREF)f->color);
        } else if (f->kind == 3) {
            /* 电弧：折线 */
            float dx = f->x2 - f->x, dy = f->y2 - f->y;
            float len = lenf(dx, dy);
            if (len < 1) continue;
            int segs = (int)(len / 16) + 2;
            float px = f->x, py = f->y;
            for (int s = 0; s < segs; s++) {
                float tt = (s + 1) / (float)segs;
                float nx = f->x + dx * tt, ny = f->y + dy * tt;
                if (s < segs - 1) {
                    nx += rndf(-5, 5);
                    ny += rndf(-5, 5);
                }
                draw_line(dc, px, py, nx, ny, 3, (COLORREF)f->color);
                draw_line(dc, px, py, nx, ny, 1, C(255, 255, 255));
                px = nx; py = ny;
            }
        }
    }
}

static void draw_floats(HDC dc) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        FloatText *f = &g_floats[i];
        if (!f->alive) continue;
        HFONT fo = f->size >= 16 ? g_fontMid : g_fontSmall;
        int w = 160, h = 22;
        draw_text(dc, f->text, (int)f->x - w / 2 + 1, (int)f->y - 11 + 1, w, h, RGB(20, 20, 20), fo, TXT_CENTER);
        draw_text(dc, f->text, (int)f->x - w / 2, (int)f->y - 11, w, h, (COLORREF)f->color, fo, TXT_CENTER);
    }
}

static void draw_items(HDC dc) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        Item *it = &g_items[i];
        if (!it->alive) continue;
        float bob = sinf(g_time * 4 + i) * 3;
        fill_circle(dc, it->x, it->y + bob + 8, 13.0f, RGB(40, 50, 40));
        COLORREF c = it->type == 0 ? C(255, 206, 74) : it->type == 1 ? C(170, 230, 255) :
                     it->type == 2 ? C(255, 150, 90) : C(255, 120, 150);
        fill_circle(dc, it->x, it->y + bob, 13.0f, c);
        stroke_circle(dc, it->x, it->y + bob, 13.0f, 2, shade(c, 0.6f));
        const wchar_t *ch = it->type == 0 ? L"金" : it->type == 1 ? L"冰" : it->type == 2 ? L"火" : L"心";
        draw_text(dc, ch, (int)it->x - 10, (int)(it->y + bob) - 9, 20, 18, C(60, 46, 20), g_fontTiny, TXT_CENTER);
    }
}

/* ---------------------------------------------------------------- 选中塔面板 */
static void draw_tower_panel(HDC dc) {
    if (g_selSlot < 0 || g_selSlot >= g_slotCount) return;
    Tower *t = &g_slots[g_selSlot];
    if (!t->used) return;

    int tw = 196, th = 118;
    /* 注意：本函数在 draw_world 的视口内绘制，坐标一律用战场相对坐标；
       按钮登记时必须换算成屏幕坐标，否则点击判定会偏移 (PLAY_X, PLAY_Y) */
    int bx = g_slotX[g_selSlot] + 30;
    int by = g_slotY[g_selSlot] - 46;
    if (bx + tw > PLAY_W - 6) bx = g_slotX[g_selSlot] - 30 - tw;
    if (bx < 6) bx = 6;
    if (by < 6) by = 6;
    if (by + th > PLAY_H - 6) by = PLAY_H - 6 - th;

    fill_rrect(dc, bx, by, tw, th, 12, C(18, 32, 24), C(120, 170, 130));
    wchar_t buf[96];
    swprintf_s(buf, 96, L"%s  Lv.%d", TOWERS[t->type].name, t->level);
    draw_text(dc, buf, bx + 10, by + 6, 120, 20, C(240, 250, 240), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int dmgNow = (int)tower_dmg(t->type, t->level);
    int rngNow = tower_range(t->type, t->level);
    if (t->level < MAX_LEVEL) {
        int d2 = (int)tower_dmg(t->type, t->level + 1);
        int r2 = tower_range(t->type, t->level + 1);
        swprintf_s(buf, 96, L"伤害 %d→%d   射程 %d→%d", dmgNow, d2, rngNow, r2);
        draw_text(dc, buf, bx + 10, by + 28, tw - 20, 18, C(150, 230, 160), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        swprintf_s(buf, 96, L"伤害 %d   射程 %d   已满级", dmgNow, rngNow);
        draw_text(dc, buf, bx + 10, by + 28, tw - 20, 18, C(200, 215, 200), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 升级按钮 */
    int cy = by + 50;
    int cost = t->level < MAX_LEVEL ? upgrade_cost(t->type, t->level) : 0;
    int can = (t->level < MAX_LEVEL && g_gold >= cost);
    swprintf_s(buf, 96, t->level < MAX_LEVEL ? L"升级  %d" : L"已满级");
    fill_rrect(dc, bx + 10, cy, 86, 28, 8, can ? C(70, 150, 90) : C(48, 60, 52), can ? C(140, 220, 160) : C(80, 92, 84));
    draw_text(dc, buf, bx + 10, cy, 86, 28, can ? C(240, 255, 240) : C(150, 160, 150), g_fontTiny, TXT_CENTER);
    btn_add(B_UP, bx + 10 + PLAY_X, cy + PLAY_Y, 86, 28, can);

    int sv = sell_value(t->type, t->level);
    swprintf_s(buf, 96, L"出售 +%d", sv);
    fill_rrect(dc, bx + 102, cy, 84, 28, 8, C(150, 84, 60), C(210, 140, 100));
    draw_text(dc, buf, bx + 102, cy, 84, 28, C(255, 240, 230), g_fontTiny, TXT_CENTER);
    btn_add(B_SELL, bx + 102 + PLAY_X, cy + PLAY_Y, 84, 28, 1);

    /* 目标优先级 */
    static const wchar_t *TM[4] = { L"最前方", L"最强", L"最近", L"最弱" };
    swprintf_s(buf, 96, L"目标：%s  (T 切换)", TM[t->mode & 3]);
    fill_rrect(dc, bx + 10, cy + 34, 176, 24, 7, C(38, 62, 48), C(90, 130, 100));
    draw_text(dc, buf, bx + 10, cy + 34, 176, 24, C(200, 235, 200), g_fontTiny, TXT_CENTER);
    btn_add(B_TARGET, bx + 10 + PLAY_X, cy + 34 + PLAY_Y, 176, 24, 1);
}

/* ---------------------------------------------------------------- 世界 */
static void draw_world(HDC dc) {
    int shx = 0, shy = 0;
    if (g_shake > 0.4f) {
        shx = (int)(rndf(-1, 1) * g_shake * 0.55f);
        shy = (int)(rndf(-1, 1) * g_shake * 0.55f);
    }
    int save = SaveDC(dc);
    IntersectClipRect(dc, PLAY_X, PLAY_Y, PLAY_X + PLAY_W, PLAY_Y + PLAY_H);
    SetViewportOrgEx(dc, PLAY_X + shx, PLAY_Y + shy, NULL);

    BitBlt(dc, 0, 0, PLAY_W, PLAY_H, g_backDC, 0, 0, SRCCOPY);
    draw_ambient(dc);

    /* 建造预览 */
    if (g_buildType >= 0) {
        for (int i = 0; i < g_slotCount; i++) {
            if (g_slots[i].used) continue;
            stroke_circle(dc, (float)g_slotX[i], (float)g_slotY[i], 26, 2, C(255, 216, 94));
        }
    }
    /* 悬停炮塔位 */
    if (g_hoverSlot >= 0) {
        stroke_circle(dc, (float)g_slotX[g_hoverSlot], (float)g_slotY[g_hoverSlot], 28, 2, C(255, 255, 255));
    }
    /* 选中塔射程 */
    if (g_selSlot >= 0 && g_slots[g_selSlot].used) {
        Tower *t = &g_slots[g_selSlot];
        int rg = tower_range(t->type, t->level);
        stroke_circle(dc, (float)g_slotX[g_selSlot], (float)g_slotY[g_selSlot], (float)rg, 1, C(180, 220, 190));
    }

    /* 炮塔 */
    for (int i = 0; i < g_slotCount; i++) draw_tower(dc, i);

    /* 怪物：按 y 排序，靠下的后画 */
    {
        int order[MAX_ENEMIES], n = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) if (g_enemies[i].alive) order[n++] = i;
        for (int i = 1; i < n; i++) {
            int key = order[i], j = i - 1;
            while (j >= 0 && g_enemies[order[j]].py > g_enemies[key].py) { order[j + 1] = order[j]; j--; }
            order[j + 1] = key;
        }
        for (int i = 0; i < n; i++) draw_enemy(dc, &g_enemies[order[i]]);
    }

    draw_items(dc);
    draw_bullets(dc);
    draw_fx(dc);
    draw_floats(dc);

    /* 建造圆环菜单（选中空位且没在建造） */
    draw_tower_panel(dc);

    /* 波次横幅 */
    if (g_bannerT > 0 && g_banner[0]) {
        float a = clampf(g_bannerT / 0.5f, 0, 1);
        int bw = 560, bh = 52;
        int bx = PLAY_W / 2 - bw / 2, by = 96;
        if (a > 0.99f) a = 1;
        fill_rrect(dc, bx, by, bw, bh, 14, RGB(18, 34, 26), C(140, 200, 150));
        draw_text(dc, g_banner, bx, by, bw, bh, C(255, 236, 160), g_fontBig, TXT_CENTER);
    }

    RestoreDC(dc, save);

    /* 红色威胁警戒（屏幕空间） */
    if (g_threat > 0.02f) {
        float pulse = 0.55f + 0.3f * sinf(g_time * 7);
        int alpha = (int)(clampf(g_threat * pulse, 0, 1) * 255);
        /* 红色渐变遮罩 × 呼吸强度 */
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
        HDC tmp = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(tmp, g_warnBmp);
        AlphaBlend(dc, PLAY_X, PLAY_Y, PLAY_W, PLAY_H, tmp, 0, 0, PLAY_W, PLAY_H, bf);
        SelectObject(tmp, old);
        DeleteDC(tmp);
        /* 城堡警戒圈 */
        int cx = PLAY_X + g_pathX[g_pathN - 1], cy = PLAY_Y + g_pathY[g_pathN - 1];
        stroke_circle(dc, (float)cx, (float)cy, 40 + sinf(g_time * 6) * 5, 3, C(230, 60, 60));
    }

}
/* ============================ HUD / 菜单 / 输入 / 主程序 ================== */

static HWND  g_hwnd;
static HDC   g_dimDC;
static HBITMAP g_dimBmp;
static int   g_hasSave;
static wchar_t g_savePath[MAX_PATH];
static wchar_t g_tip[128];
static float g_tipT;
static int   g_palHover = -1;

static void init_ui(void) {
    HDC s = GetDC(NULL);
    int *bits = NULL;
    g_dimBmp = make_dib(s, CLIENT_W, CLIENT_H, &bits);
    /* AC_SRC_ALPHA 要求颜色预乘 alpha，且 (a<<24) 在 int 上是 UB，必须用无符号 */
    {
        unsigned a = 200, cr = 10u * a / 255, cg = 20u * a / 255, cb = 16u * a / 255;
        unsigned px = (a << 24) | (cr << 16) | (cg << 8) | cb;
        for (int i = 0; i < CLIENT_W * CLIENT_H; i++) bits[i] = (int)px;
    }
    g_dimDC = CreateCompatibleDC(s);
    SelectObject(g_dimDC, g_dimBmp);
    ReleaseDC(NULL, s);
}

#define CARD_W 108
#define CARD_H 52
#define CARD_X 10
#define CARD_Y 96
#define CARD_GX 8
#define CARD_GY 6

static int card_rect(int i, int *x, int *y) {
    int col = i % 2, row = i / 2;
    *x = CARD_X + col * (CARD_W + CARD_GX);
    *y = CARD_Y + row * (CARD_H + CARD_GY);
    return 1;
}

static void tip(const wchar_t *s) {
    wcsncpy_s(g_tip, 128, s, _TRUNCATE);
    g_tipT = 2.4f;
}

/* ---------------------------------------------------------------- 存档 */
static void init_paths(void) {
    GetModuleFileNameW(NULL, g_savePath, MAX_PATH);
    wchar_t *p = wcsrchr(g_savePath, L'\\');
    if (p) *(p + 1) = 0;
    wcscpy_s(g_metaPath, MAX_PATH, g_savePath);
    wcscat_s(g_savePath, MAX_PATH, L"carrot_c.sav");
    wcscat_s(g_metaPath, MAX_PATH, L"carrot_meta.sav");
    load_meta();
    FILE *f = _wfopen(g_savePath, L"rb");
    if (f) { g_hasSave = 1; fclose(f); }
}

static void save_game(void) {
    if (g_screen != SC_GAME && g_screen != SC_PAUSE) return;
    FILE *f = _wfopen(g_savePath, L"wb");
    if (!f) return;
    fprintf(f, "CARROT_C 3\n");
    fprintf(f, "%d %d %d %d %.2f %d %d %.2f %.2f\n",
            g_map, g_endless, g_lives, g_maxLives, g_gold,
            g_wave, g_waveActive, g_betweenTimer, g_time);
    /* 统计：g_goldEarned 是 float，必须用 %.2f，之前误用 %d 导致存档数据错乱 */
    fprintf(f, "%d %.2f %d %d %d %d\n",
            g_kills, g_goldEarned, g_maxCombo, g_eliteKills, g_waveTotal, g_waveKilled);
    /* 本波尚未出场的怪物队列（相对时间，读档后从 0 开始计时） */
    int remain = 0;
    for (int i = 0; i < g_spawnCount; i++) if (g_spawn[i].type >= 0) remain++;
    fprintf(f, "%d\n", remain);
    for (int i = 0; i < g_spawnCount; i++) {
        if (g_spawn[i].type < 0) continue;
        float rel = g_spawn[i].time - g_time;
        if (rel < 0.15f) rel = 0.15f;
        fprintf(f, "%d %d %.2f\n", g_spawn[i].type, g_spawn[i].elite, rel);
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        Tower *t = &g_slots[i];
        if (t->used) fprintf(f, "%d %d %d\n", t->type, t->level, t->mode);
        else fprintf(f, "-1 0 0\n");
    }
    fclose(f);
    g_hasSave = 1;
}

static int load_game(void) {
    FILE *f = _wfopen(g_savePath, L"rb");
    if (!f) return 0;
    char head[32] = {0};
    int map = 0, endless = 0, lives = 10, maxLives = 10;
    float gold = 220, bt = 4, tm = 0;
    int wave = -1, waveActive = 0;
    int kills = 0, mc = 0, ek = 0, wTotal = 0, wKilled = 0;
    float ge = 0;
    if (fscanf(f, "%31s", head) != 1 || strncmp(head, "CARROT_C", 8) != 0) { fclose(f); return 0; }
    int ver = 0;
    if (fscanf(f, "%d", &ver) != 1) { fclose(f); return 0; }
    if (fscanf(f, "%d %d %d %d %f %d %d %f %f",
               &map, &endless, &lives, &maxLives, &gold,
               &wave, &waveActive, &bt, &tm) != 9) { fclose(f); return 0; }
    if (ver >= 3) {
        if (fscanf(f, "%d %f %d %d %d %d", &kills, &ge, &mc, &ek, &wTotal, &wKilled) != 6) {
            fclose(f); return 0;
        }
    } else {
        /* 旧档：统计字段曾用 %d 写入 float，读成 int 会错位，直接放弃这项统计 */
        int d1 = 0, d2 = 0, d3 = 0, d4 = 0;
        if (fscanf(f, "%d %d %d %d", &d1, &d2, &d3, &d4) == 4) {
            kills = d1; mc = d3; ek = d4;
        }
    }

    /* 本波剩余出怪队列先读到临时区，new_game 清空后再写回 */
    SpawnEntry tmpSpawn[240];
    int tmpN = 0;
    if (ver >= 3) {
        int remain = 0;
        if (fscanf(f, "%d", &remain) == 1 && remain > 0) {
            if (remain > 240) remain = 240;
            for (int i = 0; i < remain; i++) {
                int tp = 0, el = 0;
                float rel = 0;
                if (fscanf(f, "%d %d %f", &tp, &el, &rel) != 3) break;
                if (tp < 0 || tp >= 6) continue;
                tmpSpawn[tmpN].type = tp;
                tmpSpawn[tmpN].elite = el ? 1 : 0;
                tmpSpawn[tmpN].time = rel > 0.15f ? rel : 0.15f;
                tmpN++;
            }
        }
    }

    clear_contract();                     /* 读档不还原契约 */
    new_game(map, endless);
    g_lives = lives; g_maxLives = maxLives; g_gold = gold;
    g_wave = wave; g_waveActive = waveActive; g_betweenTimer = bt;
    g_kills = kills; g_goldEarned = ge; g_maxCombo = mc; g_eliteKills = ek;
    g_waveTotal = wTotal; g_waveKilled = wKilled;
    if (tmpN > 0) {
        g_spawnCount = tmpN;
        for (int i = 0; i < tmpN; i++) g_spawn[i] = tmpSpawn[i];
        g_waveActive = 1;
    } else if (g_waveActive) {
        /* 旧档无法还原队列：退回波间休整，避免白拿一波结算奖励 */
        g_waveActive = 0;
        if (g_betweenTimer <= 0) g_betweenTimer = 4.0f;
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        int type = -1, level = 0, mode = 0;
        if (fscanf(f, "%d %d %d", &type, &level, &mode) != 3) break;
        if (type >= 0 && type < TOWER_COUNT && i < g_slotCount) {
            g_slots[i].used = 1;
            g_slots[i].type = type;
            g_slots[i].level = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
            g_slots[i].mode = mode & 3;
            g_slots[i].cd = 0;
            g_slots[i].beamIdx = -1;
        }
    }
    fclose(f);
    g_time = 0;
    g_betweenTimer = bt > 0 ? bt : 3.0f;
    g_screen = SC_GAME;
    return 1;
}

static void delete_save(void) {
    _wremove(g_savePath);
    g_hasSave = 0;
}

/* ---------------------------------------------------------------- 顶部栏 */
static void draw_topbar(HDC dc) {
    /* 渐变背景：顶部深、底部略亮，比纯色更有质感 */
    {
        int steps = 8;
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            COLORREF c = C((int)(22 + t * 18), (int)(50 + t * 26), (int)(36 + t * 18));
            fill_rect(dc, 0, TOP_H * i / steps, CLIENT_W, TOP_H / steps + 1, c);
        }
    }
    fill_rect(dc, 0, TOP_H - 3, CLIENT_W, 3, C(10, 26, 18));
    fill_rect(dc, 0, 0, CLIENT_W, 2, C(46, 88, 62));

    /* 生命 */
    int lifeFlash = (g_livesPulse > 0 && (int)(g_livesPulse * 8) % 2 == 0);
    draw_heart(dc, 34, 30, 11, lifeFlash ? C(255, 170, 180) : C(255, 96, 110));
    wchar_t buf[64];
    if (g_lives != g_livesShown) { g_livesShown = g_lives; g_livesPulse = 1.0f; }
    swprintf_s(buf, 64, L"%d", g_lives);
    draw_text(dc, buf, 52, 18, 60, 26, g_lives <= 3 ? C(255, 120, 120) : C(240, 250, 240), g_fontNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 金币（变化时脉冲闪光） */
    if ((int)g_gold != g_goldShown) { g_goldShown = (int)g_gold; g_goldPulse = 1.0f; }
    if (g_goldPulse > 0) {
        int gw = 120;
        COLORREF pc = C((int)(60 + 60 * g_goldPulse), (int)(66 + 70 * g_goldPulse), (int)(40 + 30 * g_goldPulse));
        fill_rrect(dc, 118, 12, gw, 36, 10, pc, C(255, 216, 94));
    }
    draw_coin(dc, 126, 30, 11);
    swprintf_s(buf, 64, L"%d", (int)g_gold);
    draw_text(dc, buf, 144, 18, 90, 26, g_goldPulse > 0.3f ? C(255, 245, 150) : C(255, 216, 94), g_fontNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 波次 */
    swprintf_s(buf, 64, g_endless ? L"第 %d 波 · 无尽" : L"第 %d / 15 波", g_wave + 1);
    draw_text(dc, buf, 280, 12, 220, 30, C(220, 240, 225), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 本波进度 */
    if (g_waveActive && g_waveTotal > 0) {
        int bx = 470, by = 30, bw = 250, bh = 9;
        int done = g_waveKilled; if (done > g_waveTotal) done = g_waveTotal;
        fill_rrect(dc, bx, by, bw, bh, 5, C(28, 50, 38), C(60, 90, 70));
        fill_rrect(dc, bx, by, (int)(bw * (float)done / g_waveTotal), bh, 5,
                   g_bossWave ? C(230, 80, 80) : C(120, 210, 140), 0);
        swprintf_s(buf, 64, L"剩余 %d", g_waveTotal - done);
        draw_text(dc, buf, bx + bw + 8, by - 6, 90, 20, C(190, 220, 200), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (!g_waveActive && g_wave >= 0 && !g_over) {
        /* 倒计时：环形进度 + 秒数 */
        int cx = 482, cy = 30, r = 11;
        float total = 6.0f;
        float prog = clampf((total - g_betweenTimer) / total, 0, 1);
        stroke_circle(dc, (float)cx, (float)cy, r, 3, C(48, 76, 58));
        int segs = 20;
        int upto = (int)(prog * segs + 0.5f);
        for (int k = 0; k < upto; k++) {
            float a1 = -TAU / 4 + k * TAU / segs;
            float a2 = a1 + TAU / segs + 0.01f;
            draw_line(dc, cx + cosf(a1) * r, cy + sinf(a1) * r,
                      cx + cosf(a2) * r, cy + sinf(a2) * r, 3, C(120, 210, 140));
        }
        swprintf_s(buf, 64, L"下一波 %.1f 秒", g_betweenTimer);
        draw_text(dc, buf, 502, 24, 250, 22, C(200, 230, 205), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 速度按钮 */
    swprintf_s(buf, 32, L"%gx", g_speed);
    fill_rrect(dc, 1024, 12, 58, 32, 9, C(38, 66, 50), C(88, 130, 100));
    draw_text(dc, buf, 1024, 12, 58, 32, C(220, 245, 225), g_fontMid, TXT_CENTER);
    btn_add(B_SPEED, 1024, 12, 58, 32, 1);

    /* 暂停按钮 */
    fill_rrect(dc, 1090, 12, 42, 32, 9, g_paused ? C(150, 110, 40) : C(38, 66, 50), C(88, 130, 100));
    if (g_paused) {
        POINT p[3] = { {1102, 20}, {1122, 28}, {1102, 36} };
        draw_poly(dc, p, 3, C(255, 236, 170), C(255, 236, 170));
    } else {
        fill_rect(dc, 1103, 20, 5, 16, C(220, 245, 225));
        fill_rect(dc, 1113, 20, 5, 16, C(220, 245, 225));
    }
    btn_add(B_PAUSE, 1090, 12, 42, 32, 1);

    /* 菜单按钮 */
    fill_rrect(dc, 1140, 12, 44, 32, 9, C(38, 66, 50), C(88, 130, 100));
    for (int i = 0; i < 3; i++) fill_rect(dc, 1150, 21 + i * 6, 24, 2, C(220, 245, 225));
    btn_add(B_MENU, 1140, 12, 44, 32, 1);
}

/* ---------------------------------------------------------------- 武器库 */
static void draw_panel(HDC dc) {
    fill_rect(dc, 0, TOP_H, PANEL_W, CLIENT_H - TOP_H, C(13, 30, 22));
    fill_rect(dc, PANEL_W - 2, TOP_H, 2, CLIENT_H - TOP_H, C(30, 58, 42));
    draw_text(dc, L"武 器 库", 0, TOP_H + 10, PANEL_W, 24, C(180, 230, 190), g_fontMid, TXT_CENTER);

    for (int i = 0; i < TOWER_COUNT; i++) {
        int x, y;
        card_rect(i, &x, &y);
        const TowerDef *d = &TOWERS[i];
        int afford = g_gold >= d->cost;
        int sel = (g_buildType == i);
        int hov = (g_palHover == i);
        if (hov) y -= 2;                                   /* 悬停上浮 */
        COLORREF bg = sel ? C(58, 96, 60) : (hov ? C(44, 78, 52) : C(24, 48, 33));
        COLORREF line = sel ? C(255, 216, 94) : (hov && afford ? C(120, 200, 140) : (afford ? C(52, 86, 60) : C(38, 52, 42)));
        fill_rrect(dc, x, y, CARD_W, CARD_H, 9, bg, line);
        /* 顶部高光条，制造立体感 */
        fill_rrect(dc, x + 2, y + 2, CARD_W - 4, 6, 4, shade(bg, 1.45f), 0);
        /* 图标 */
        fill_circle(dc, (float)(x + 24), (float)(y + 24), 17.0f, C(20, 40, 28));
        draw_glyph(dc, i, x + 24, y + 24, 0.62f, afford ? d->color : shade(d->color, 0.6f));
        /* 名称 */
        draw_text(dc, d->name, x + 44, y + 6, 60, 18,
                  afford ? C(228, 242, 230) : C(120, 140, 125), g_fontTiny,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        /* 价格 */
        draw_coin(dc, x + 47, y + 33, 6);
        wchar_t buf[16];
        swprintf_s(buf, 16, L"%d", d->cost);
        draw_text(dc, buf, x + 56, y + 22, 50, 20,
                  afford ? C(255, 216, 94) : C(150, 120, 60), g_fontTiny,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 说明区：优先显示选中武器，其次悬停的武器（不用点开就能看数值） */
    int iy = 460;
    int showIdx = g_buildType >= 0 ? g_buildType : g_palHover;
    fill_rrect(dc, 10, iy, PANEL_W - 20, 118, 10, C(20, 42, 30), C(44, 74, 54));
    if (showIdx >= 0) {
        const TowerDef *d = &TOWERS[showIdx];
        draw_text(dc, d->name, 22, iy + 8, 140, 22, C(255, 226, 140), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, d->desc, 22, iy + 32, PANEL_W - 44, 20, C(200, 228, 206), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        wchar_t buf[64];
        swprintf_s(buf, 64, L"伤害 %d   射程 %d   造价 %d", d->dmg, d->range, d->cost);
        draw_text(dc, buf, 22, iy + 54, PANEL_W - 44, 20, C(160, 210, 170), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        swprintf_s(buf, 64, L"攻速 %.1f 次/秒", 1.0f / d->cd);
        draw_text(dc, buf, 22, iy + 74, PANEL_W - 44, 20, C(160, 210, 170), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, g_buildType >= 0 ? L"点击空炮塔位建造 · 右键取消"
                                       : L"点击选中武器（数据预览中）", 22, iy + 94, PANEL_W - 44, 18, C(255, 216, 120), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        draw_text(dc, L"操作提示", 22, iy + 8, 140, 22, C(255, 226, 140), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"① 点左侧武器选中", 22, iy + 34, PANEL_W - 44, 18, C(200, 228, 206), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"② 点空炮塔位建造", 22, iy + 54, PANEL_W - 44, 18, C(200, 228, 206), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"③ 点已有武器可升级/出售", 22, iy + 74, PANEL_W - 44, 18, C(200, 228, 206), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"空格暂停 · ESC 菜单", 22, iy + 94, PANEL_W - 44, 18, C(255, 216, 120), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_tipT > 0) {
        draw_text(dc, g_tip, 10, 586, PANEL_W - 20, 20, C(255, 120, 110), g_fontSmall, TXT_CENTER);
    }

    /* 底部信息 */
    wchar_t buf[64];
    int towers = 0;
    for (int i = 0; i < g_slotCount; i++) if (g_slots[i].used) towers++;
    swprintf_s(buf, 64, L"已建 %d / %d 座", towers, g_slotCount);
    draw_text(dc, buf, 0, 610, PANEL_W, 20, C(140, 180, 150), g_fontTiny, TXT_CENTER);
    swprintf_s(buf, 64, g_endless ? L"无尽模式" : L"经典模式");
    draw_text(dc, buf, 0, 630, PANEL_W, 20, C(110, 150, 120), g_fontTiny, TXT_CENTER);
}

/* ---------------------------------------------------------------- 底部提示 */
static void draw_bottom(HDC dc) {
    fill_rect(dc, 0, CLIENT_H - 24, CLIENT_W, 24, C(12, 26, 19));
    draw_text(dc, L"左键 建造/选中    U 升级    S 出售    T 切换目标    1/2/3 切换倍速    空格 暂停    ESC 菜单",
              PANEL_W + 8, CLIENT_H - 24, PLAY_W - 16, 24, C(120, 160, 132), g_fontTiny,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* ---------------------------------------------------------------- 游戏画面 */
static void draw_game(HDC dc) {
    draw_world(dc);
    draw_topbar(dc);
    draw_panel(dc);
    draw_bottom(dc);

    /* 魔王血条 */
    int bossIdx = -1;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g_enemies[i].alive && g_enemies[i].type == EN_BOSS) { bossIdx = i; break; }
    if (bossIdx >= 0) {
        Enemy *b = &g_enemies[bossIdx];
        int bw = 420, bx = PLAY_X + PLAY_W / 2 - bw / 2, by = TOP_H + 12;
        float hr = b->hp / b->maxHp; if (hr < 0) hr = 0;
        fill_rrect(dc, bx - 6, by - 6, bw + 12, 42, 12, RGB(34, 16, 18), b->rage ? C(220, 60, 60) : C(120, 50, 50));
        fill_rect(dc, bx, by, bw, 18, C(58, 22, 24));
        fill_rect(dc, bx, by, (int)(bw * hr), 18, b->rage ? C(220, 40, 40) : C(255, 90, 90));
        fill_rect(dc, bx, by, (int)(bw * hr), 6, C(255, 190, 190));
        if (b->shield > 0) {
            float sr = b->shieldMax > 0 ? b->shield / b->shieldMax : 0;
            fill_rect(dc, bx, by - 6, bw, 5, C(24, 40, 52));
            fill_rect(dc, bx, by - 6, (int)(bw * sr), 5, C(120, 200, 245));
        }
        wchar_t buf[96];
        swprintf_s(buf, 96, L"魔王  %d / %d%s", (int)(b->hp + 0.5f), (int)(b->maxHp + 0.5f), b->rage ? L"  · 狂暴" : L"");
        draw_text(dc, buf, bx, by, bw, 18, C(255, 240, 240), g_fontTiny, TXT_CENTER);
        if (b->shield > 0) {
            swprintf_s(buf, 96, L"护盾 %d", (int)(b->shield + 0.5f));
            draw_text(dc, buf, bx, by - 20, bw, 14, C(150, 215, 255), g_fontTiny, TXT_CENTER);
        }
    }

    /* 连击 */
    if (g_combo >= 3) {
        wchar_t buf[48];
        swprintf_s(buf, 48, L"连击 x%d", g_combo);
        int cx = PLAY_X + PLAY_W - 150, cy = TOP_H + 14;
        fill_rrect(dc, cx, cy, 130, 30, 9, C(30, 24, 14), C(255, 200, 90));
        draw_text(dc, buf, cx, cy, 130, 30, C(255, 216, 94), g_fontMid, TXT_CENTER);
    }
}

/* ---------------------------------------------------------------- 暂停/结算 */
static void dim_screen(HDC dc) {
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC tmp = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(tmp, g_dimBmp);
    AlphaBlend(dc, 0, 0, CLIENT_W, CLIENT_H, tmp, 0, 0, CLIENT_W, CLIENT_H, bf);
    SelectObject(tmp, old);
    DeleteDC(tmp);
}

static void draw_pause(HDC dc) {
    dim_screen(dc);
    int w = 420, h = 344, x = (CLIENT_W - w) / 2, y = (CLIENT_H - h) / 2;
    fill_rrect(dc, x, y, w, h, 18, C(18, 34, 26), C(120, 170, 130));
    draw_text(dc, L"游戏暂停", x, y + 22, w, 40, C(255, 236, 160), g_fontBig, TXT_CENTER);
    draw_text(dc, L"按 ESC 或空格继续", x, y + 62, w, 22, C(150, 190, 160), g_fontTiny, TXT_CENTER);

    struct { int id; const wchar_t *txt; COLORREF bg; } bs[4] = {
        { B_RESUME,  L"继续游戏",       C(62, 132, 84) },
        { B_RESTART, L"重新开始本局",   C(72, 104, 150) },
        { B_MENU,    L"保存并回主菜单", C(150, 96, 56) },
        { B_HELP,    L"操作说明",       C(60, 76, 66) },
    };
    for (int i = 0; i < 4; i++) {
        int bx = x + 60, by = y + 100 + i * 56, bw = w - 120, bh = 44;
        fill_rrect(dc, bx, by, bw, bh, 10, bs[i].bg, shade(bs[i].bg, 1.5f));
        draw_text(dc, bs[i].txt, bx, by, bw, bh, C(240, 250, 240), g_fontMid, TXT_CENTER);
        btn_add(bs[i].id, bx, by, bw, bh, 1);
    }
}

static void draw_over(HDC dc) {
    dim_screen(dc);
    int w = 560, h = 440, x = (CLIENT_W - w) / 2, y = (CLIENT_H - h) / 2;
    fill_rrect(dc, x, y, w, h, 20, C(18, 34, 26), g_won ? C(255, 216, 94) : C(120, 140, 130));
    draw_text(dc, g_won ? L"萝卜守住了！" : L"萝卜被吃掉了…", x, y + 18, w, 46,
              g_won ? C(255, 226, 130) : C(255, 150, 150), g_fontBig, TXT_CENTER);

    int stars = 0;
    if (g_won) stars = (g_lives >= g_maxLives * 0.9f) ? 3 : (g_lives >= g_maxLives * 0.5f ? 2 : 1);
    for (int i = 0; i < 3; i++) {
        POINT p[10];
        star_points(x + w / 2 - 78 + i * 78, y + 96, 26.0f, p);
        draw_poly(dc, p, 10, i < stars ? C(255, 216, 94) : C(64, 82, 70),
                  i < stars ? C(190, 150, 30) : C(48, 60, 52));
    }

    wchar_t buf[128];
    swprintf_s(buf, 128, g_endless ? L"坚持到第 %d 波" : L"第 %d / 15 波", g_wave + 1);
    draw_text(dc, buf, x, y + 136, w, 28, C(220, 240, 225), g_fontMid, TXT_CENTER);

    struct { const wchar_t *k; const wchar_t *v; } rows[4];
    wchar_t v1[32], v2[32], v3[32], v4[32];
    swprintf_s(v1, 32, L"%d", g_kills);          rows[0].k = L"击杀怪物"; rows[0].v = v1;
    swprintf_s(v2, 32, L"%d", (int)g_goldEarned);rows[1].k = L"累计金币"; rows[1].v = v2;
    swprintf_s(v3, 32, L"%d", g_maxCombo);       rows[2].k = L"最高连击"; rows[2].v = v3;
    swprintf_s(v4, 32, L"%d", g_eliteKills);     rows[3].k = L"精英击杀"; rows[3].v = v4;
    for (int i = 0; i < 4; i++) {
        int rx = x + (i % 2) * (w / 2) + 40;
        int ry = y + 182 + (i / 2) * 40;
        draw_text(dc, rows[i].k, rx, ry, 110, 26, C(150, 190, 160), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, rows[i].v, rx + 110, ry, 100, 26, C(255, 226, 130), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 本局新解锁成就 */
    {
        int newN = 0;
        for (int i = 0; i < ACH_COUNT; i++) if (g_achNew & (1ULL << i)) newN++;
        if (newN > 0) {
            wchar_t nb[64];
            swprintf_s(nb, 64, L"本局解锁成就 %d 项", newN);
            draw_text(dc, nb, x, y + 268, w, 24, C(255, 216, 94), g_fontSmall, TXT_CENTER);
        }
    }

    int by = y + h - 110;
    fill_rrect(dc, x + 50, by, 210, 48, 10, C(62, 132, 84), C(140, 220, 160));
    draw_text(dc, g_won ? L"再玩一次" : L"重新开始", x + 50, by, 210, 48, C(240, 250, 240), g_fontMid, TXT_CENTER);
    btn_add(B_RETRY, x + 50, by, 210, 48, 1);

    fill_rrect(dc, x + 300, by, 210, 48, 10, C(60, 78, 68), C(110, 140, 116));
    draw_text(dc, L"回主菜单", x + 300, by, 210, 48, C(230, 240, 230), g_fontMid, TXT_CENTER);
    btn_add(B_OVERMENU, x + 300, by, 210, 48, 1);
}

/* 操作说明浮层 */
static int g_helpOpen = 0;
static void draw_help(HDC dc) {
    dim_screen(dc);
    int w = 620, h = 420, x = (CLIENT_W - w) / 2, y = (CLIENT_H - h) / 2;
    fill_rrect(dc, x, y, w, h, 18, C(16, 32, 24), C(120, 170, 130));
    draw_text(dc, L"操作说明", x, y + 16, w, 38, C(255, 236, 160), g_fontBig, TXT_CENTER);
    const wchar_t *lines[] = {
        L"左键点击武器库中的武器，再点空炮塔位建造",
        L"点击已有炮塔：升级 / 出售 / 切换目标优先级",
        L"U 升级   S 出售   T 切换目标（最前/最强/最近/最弱）",
        L"空格 暂停   ESC 打开菜单   1 / 2 / 3 切换游戏倍速",
        L"怪物走到萝卜处会扣生命，生命为 0 即失败",
        L"精英怪（金环）带词缀，击杀金币三倍；魔王会开盾与狂暴",
    };
    for (int i = 0; i < 6; i++) {
        draw_text(dc, lines[i], x + 44, y + 74 + i * 40, w - 88, 30, C(205, 232, 210), g_fontSmall,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    fill_rrect(dc, x + w / 2 - 90, y + h - 70, 180, 44, 10, C(62, 132, 84), C(140, 220, 160));
    draw_text(dc, L"知道了", x + w / 2 - 90, y + h - 70, 180, 44, C(240, 250, 240), g_fontMid, TXT_CENTER);
    btn_add(B_CLOSEHELP, x + w / 2 - 90, y + h - 70, 180, 44, 1);
}

/* ---------------------------------------------------------------- 主菜单 */
static void draw_minimap(HDC dc, int mapIdx, int x, int y, int w, int h) {
    const MapDef *m = &MAPS[mapIdx];
    COLORREF bg = m->theme == 0 ? C(126, 190, 100) : m->theme == 1 ? C(88, 78, 84) :
                  m->theme == 2 ? C(206, 228, 242) : C(222, 192, 128);
    fill_rrect(dc, x, y, w, h, 8, bg, C(60, 76, 62));
    float sc = w / 960.0f;
    float offY = (h - 600 * sc) * 0.5f;
    POINT pts[12];
    for (int i = 0; i < m->npath; i++) {
        pts[i].x = x + (int)(m->path[i].x * sc);
        pts[i].y = y + (int)(m->path[i].y * sc + offY);
    }
    HPEN pen = CreatePen(PS_SOLID, 7, C(226, 194, 144));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Polyline(dc, pts, m->npath);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pen);
    for (int i = 0; i < m->nslots; i++) {
        int sx = x + (int)(m->slots[i].x * sc);
        int sy = y + (int)(m->slots[i].y * sc + offY);
        fill_circle(dc, (float)sx, (float)sy, 2.6f, C(180, 184, 170));
    }
    fill_circle(dc, (float)pts[m->npath - 1].x, (float)pts[m->npath - 1].y, 5.0f, C(255, 138, 62));
}

static void draw_menu(HDC dc) {
    /* 渐变背景 */
    for (int i = 0; i < CLIENT_H; i += 4) {
        float t = i / (float)CLIENT_H;
        COLORREF c = RGB((int)(24 + 10 * (1 - t)), (int)(62 + 24 * (1 - t)), (int)(44 + 14 * (1 - t)));
        fill_rect(dc, 0, i, CLIENT_W, 4, c);
    }
    /* 装饰圆 */
    for (int i = 0; i < 26; i++) {
        float x = fmodf(i * 173.7f, (float)CLIENT_W);
        float y = fmodf(i * 97.3f + 40, (float)CLIENT_H);
        float r = 6 + (i % 5) * 9;
        stroke_circle(dc, x, y, r, 1, C(60, 110, 78));
    }

    /* 标题 */
    {
        int cx = CLIENT_W / 2, ty = 44;
        /* 萝卜图标 */
        POINT body[3] = { {cx - 160, ty}, {cx - 116, ty + 34}, {cx - 204, ty + 34} };
        draw_poly(dc, body, 3, C(255, 138, 62), C(214, 96, 38));
        fill_circle(dc, (float)(cx - 150), (float)(ty - 4), 12, C(88, 190, 96));
        fill_circle(dc, (float)(cx - 172), (float)(ty - 6), 11, C(110, 210, 110));
        fill_circle(dc, (float)(cx - 132), (float)(ty - 5), 10, C(88, 190, 96));
        draw_text_shadow(dc, L"保卫萝卜", cx - 90, ty - 12, 260, 60, C(255, 240, 200), g_fontTitle, TXT_CENTER);
        draw_text(dc, L"C 语言版 · Win32 + GDI 移植", cx - 160, ty + 44, 420, 24, C(150, 196, 164), g_fontSmall, TXT_CENTER);
    }

    /* 地图卡片 */
    int cw = 206, ch = 200, gap = 20;
    int total = 4 * cw + 3 * gap;
    int sx = (CLIENT_W - total) / 2, sy = 148;
    for (int i = 0; i < 4; i++) {
        int x = sx + i * (cw + gap);
        int sel = (g_selMap == i);
        int hov = (g_mouseInside && g_mouseX >= x && g_mouseX <= x + cw &&
                   g_mouseY >= sy && g_mouseY <= sy + ch);
        int dy = sel ? -3 : (hov ? -2 : 0);              /* 选中/悬停上浮 */
        fill_rrect(dc, x, sy + dy, cw, ch, 14, sel ? C(38, 74, 50) : C(22, 48, 33),
                   sel ? C(255, 216, 94) : (hov ? C(110, 170, 130) : C(48, 82, 58)));
        /* 顶部高光 */
        fill_rrect(dc, x + 3, sy + dy + 3, cw - 6, 8, 5, sel ? C(52, 96, 66) : C(30, 60, 42), 0);
        draw_minimap(dc, i, x + 10, sy + dy + 10, cw - 20, 118);
        draw_text(dc, MAPS[i].name, x, sy + dy + 134, cw, 28, sel ? C(255, 236, 150) : C(226, 242, 230), g_fontMid, TXT_CENTER);
        const wchar_t *tag = MAPS[i].theme == 0 ? L"草原 · 入门" : MAPS[i].theme == 1 ? L"火山 · 崎岖" :
                             MAPS[i].theme == 2 ? L"冰雪 · 长路" : L"沙漠 · 折返";
        /* 最佳波次（有战绩才显示） */
        if (g_metaBest[i] > 0) {
            wchar_t tb[64];
            swprintf_s(tb, 64, L"%s · 最佳第%d波", tag, g_metaBest[i]);
            draw_text(dc, tb, x, sy + dy + 162, cw, 22, sel ? C(180, 220, 170) : C(150, 190, 158), g_fontTiny, TXT_CENTER);
        } else {
            draw_text(dc, tag, x, sy + dy + 162, cw, 22, C(140, 176, 148), g_fontTiny, TXT_CENTER);
        }
        /* 选中徽章 */
        if (sel) {
            fill_circle(dc, (float)(x + cw - 18), (float)(sy + dy + 18), 13, C(255, 216, 94));
            draw_text(dc, L"✓", x + cw - 30, sy + dy + 6, 24, 24, C(60, 44, 10), g_fontMid, TXT_CENTER);
        }
        btn_add(B_MAP0 + i, x, sy, cw, ch, 1);
    }

    /* 模式 */
    int my = sy + ch + 22;
    fill_rrect(dc, CLIENT_W / 2 - 150, my, 300, 44, 10, C(28, 58, 40), C(90, 130, 100));
    draw_text(dc, g_selMode ? L"模式：无尽挑战" : L"模式：经典 15 波", CLIENT_W / 2 - 150, my, 300, 44,
              C(220, 240, 225), g_fontMid, TXT_CENTER);
    btn_add(B_MODE, CLIENT_W / 2 - 150, my, 300, 44, 1);

    /* 开始 / 继续 */
    int by = my + 58;
    {
        int hx = CLIENT_W / 2 - 150;
        int hov = (g_mouseInside && g_mouseX >= hx && g_mouseX <= hx + 300 &&
                   g_mouseY >= by && g_mouseY <= by + 54);
        fill_rrect(dc, hx, by + (hov ? -2 : 0), 300, 54, 12,
                   hov ? C(90, 178, 108) : C(70, 152, 92),
                   hov ? C(220, 255, 220) : C(160, 230, 170));
        draw_text(dc, L"开始游戏", hx, by + (hov ? -2 : 0), 300, 54, C(250, 255, 250), g_fontBig, TXT_CENTER);
        btn_add(B_START, hx, by, 300, 54, 1);
    }

    if (g_hasSave) {
        fill_rrect(dc, CLIENT_W / 2 - 150, by + 64, 300, 40, 10, C(46, 76, 58), C(100, 146, 112));
        draw_text(dc, L"继续上次进度", CLIENT_W / 2 - 150, by + 64, 300, 40, C(220, 240, 225), g_fontSmall, TXT_CENTER);
        btn_add(B_CONTINUE, CLIENT_W / 2 - 150, by + 64, 300, 40, 1);
    }

    /* 冒险图鉴入口 */
    {
        int dy = (g_hasSave ? by + 114 : by + 64);
        fill_rrect(dc, CLIENT_W / 2 - 150, dy, 300, 40, 10, C(40, 62, 84), C(110, 140, 180));
        draw_text(dc, L"冒险图鉴", CLIENT_W / 2 - 150, dy, 300, 40, C(214, 228, 250), g_fontSmall, TXT_CENTER);
        btn_add(B_DEX, CLIENT_W / 2 - 150, dy, 300, 40, 1);
    }

    /* 成就/统计概览 */
    {
        int unlockedN = 0;
        for (int i = 0; i < ACH_COUNT; i++) if (g_ach & (1ULL << i)) unlockedN++;
        wchar_t stbuf[96];
        swprintf_s(stbuf, 96, L"成就 %d / %d      累计击杀 %d", unlockedN, ACH_COUNT, g_killTotal);
        draw_text(dc, stbuf, CLIENT_W / 2 - 300, CLIENT_H - 78, 600, 22, C(160, 190, 165), g_fontTiny, TXT_CENTER);
    }

    draw_text(dc, L"12 种武器 · 6 种怪物 · 精英词缀 · 魔王技能 · 4 张地图",
              CLIENT_W / 2 - 400, CLIENT_H - 52, 800, 22, C(130, 172, 140), g_fontTiny, TXT_CENTER);
    draw_text(dc, L"左键操作    ESC 菜单    空格 暂停",
              CLIENT_W / 2 - 400, CLIENT_H - 30, 800, 22, C(110, 148, 120), g_fontTiny, TXT_CENTER);
}

/* ---------------------------------------------------------------- 战前契约 */
static void draw_contract(HDC dc) {
    /* 渐变背景 */
    for (int i = 0; i < CLIENT_H; i += 4) {
        float t = i / (float)CLIENT_H;
        COLORREF c = RGB((int)(20 + 12 * (1 - t)), (int)(50 + 26 * (1 - t)), (int)(38 + 16 * (1 - t)));
        fill_rect(dc, 0, i, CLIENT_W, 4, c);
    }
    draw_text(dc, L"战前契约 · 三选一", 0, 34, CLIENT_W, 46, C(255, 236, 160), g_fontBig, TXT_CENTER);
    draw_text(dc, L"每份契约都是收益与代价的交易，星数越高越危险，奖励也越丰厚",
              0, 84, CLIENT_W, 24, C(170, 200, 180), g_fontSmall, TXT_CENTER);

    int cw = 320, ch = 380, gap = 40;
    int sx = (CLIENT_W - 3 * cw - 2 * gap) / 2, sy = 140;
    for (int i = 0; i < 3; i++) {
        int x = sx + i * (cw + gap), y = sy;
        int id = g_contractRoll[i];
        if (id < 0) continue;
        const ContractDef *c = &CONTRACTS[id];
        COLORREF accent = c->stars == 1 ? C(159, 224, 255) : c->stars == 2 ? C(255, 179, 71) : C(255, 106, 106);
        fill_rrect(dc, x, y, cw, ch, 16, C(20, 40, 30), accent);

        /* 图标 */
        fill_circle(dc, (float)(x + cw / 2), (float)(y + 64), 34, shade(accent, 0.35f));
        wchar_t ic[4]; ic[0] = c->icon[0]; ic[1] = 0;
        draw_text(dc, ic, x + cw / 2 - 34, y + 64 - 22, 68, 44, C(255, 240, 210), g_fontBig, TXT_CENTER);
        draw_text(dc, c->name, x, y + 110, cw, 36, C(255, 236, 160), g_fontBig, TXT_CENTER);

        /* 星级 */
        for (int s = 0; s < 3; s++) {
            POINT p[10];
            star_points(x + cw / 2 - 60 + s * 44, y + 154, 13.0f, p);
            draw_poly(dc, p, 10, s < c->stars ? C(255, 216, 94) : C(56, 74, 62),
                      s < c->stars ? C(190, 150, 30) : C(48, 60, 52));
        }

        /* 收益 */
        int ty = y + 190;
        for (int k = 0; k < 2; k++) {
            if (!c->good[k]) continue;
            wchar_t line[64];
            swprintf_s(line, 64, L"▲ %s", c->good[k]);
            draw_text(dc, line, x + 30, ty, cw - 60, 26, C(150, 230, 160), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            ty += 32;
        }
        ty += 8;
        for (int k = 0; k < 2; k++) {
            if (!c->bad[k]) continue;
            wchar_t line[64];
            swprintf_s(line, 64, L"▼ %s", c->bad[k]);
            draw_text(dc, line, x + 30, ty, cw - 60, 26, C(240, 140, 140), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            ty += 32;
        }

        /* 签下按钮 */
        int byy = y + ch - 62;
        fill_rrect(dc, x + 50, byy, cw - 100, 44, 10, shade(accent, 0.55f), accent);
        draw_text(dc, L"签 下", x + 50, byy, cw - 100, 44, C(250, 252, 250), g_fontMid, TXT_CENTER);
        btn_add(B_CONTRACT0 + i, x, y, cw, ch, 1);
    }

    fill_rrect(dc, CLIENT_W / 2 - 130, sy + ch + 26, 260, 42, 10, C(36, 54, 44), C(90, 120, 100));
    draw_text(dc, L"不签契约 · 直接开局", CLIENT_W / 2 - 130, sy + ch + 26, 260, 42, C(210, 230, 215), g_fontSmall, TXT_CENTER);
    btn_add(B_SKIPCONTRACT, CLIENT_W / 2 - 130, sy + ch + 26, 260, 42, 1);
}

/* ---------------------------------------------------------------- 冒险图鉴 */
static int g_dexTab = 0;    /* 0 怪物 1 武器 2 成就 */

static void draw_dex(HDC dc) {
    for (int i = 0; i < CLIENT_H; i += 4) {
        float t = i / (float)CLIENT_H;
        COLORREF c = RGB((int)(20 + 12 * (1 - t)), (int)(46 + 24 * (1 - t)), (int)(34 + 14 * (1 - t)));
        fill_rect(dc, 0, i, CLIENT_W, 4, c);
    }

    /* 顶部标签 */
    static const wchar_t *TABS[3] = { L"怪物图鉴", L"武器图鉴", L"成就殿堂" };
    for (int i = 0; i < 3; i++) {
        int x = CLIENT_W / 2 - 330 + i * 220, y = 24;
        int sel = (g_dexTab == i);
        fill_rrect(dc, x, y, 200, 42, 10, sel ? C(46, 88, 58) : C(24, 44, 32),
                   sel ? C(255, 216, 94) : C(52, 78, 60));
        draw_text(dc, TABS[i], x, y, 200, 42, sel ? C(255, 236, 150) : C(180, 210, 190), g_fontMid, TXT_CENTER);
        btn_add(B_TAB0 + i, x, y, 200, 42, 1);
    }

    /* 返回按钮 */
    fill_rrect(dc, CLIENT_W - 150, 24, 110, 42, 10, C(52, 72, 60), C(110, 150, 120));
    draw_text(dc, L"◀ 返回", CLIENT_W - 150, 24, 110, 42, C(220, 240, 225), g_fontSmall, TXT_CENTER);
    btn_add(B_DEXBACK, CLIENT_W - 150, 24, 110, 42, 1);

    int x0 = 60, y0 = 96;

    if (g_dexTab == 0) {
        /* 怪物图鉴：2 列 x 3 行 */
        int cw = 520, ch = 148, gx = 36, gy = 12;
        for (int i = 0; i < 6; i++) {
            const EnemyDef *d = &ENEMIES[i];
            int x = x0 + (i % 2) * (cw + gx), y = y0 + (i / 2) * (ch + gy);
            fill_rrect(dc, x, y, cw, ch, 12, C(20, 40, 30), C(56, 84, 64));
            fill_circle(dc, (float)(x + 56), (float)(y + 56), 30, d->body);
            fill_circle(dc, (float)(x + 48), (float)(y + 46), 10, C(250, 250, 250));
            fill_circle(dc, (float)(x + 44), (float)(y + 44), 5, C(30, 30, 40));
            draw_text(dc, d->name, x + 104, y + 16, 180, 30, C(240, 248, 240), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            wchar_t buf[128];
            swprintf_s(buf, 128, L"生命 %.0f    速度 %.0f    赏金 %d    城堡伤害 %d",
                       d->hp, d->speed, d->reward, d->dmg);
            draw_text(dc, buf, x + 104, y + 54, cw - 124, 24, C(190, 214, 196), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        /* 精英词缀说明 */
        int ay = y0 + 3 * (ch + gy) + 6;
        fill_rrect(dc, x0, ay, 2 * cw + gx, 118, 12, C(34, 30, 22), C(255, 179, 71));
        draw_text(dc, L"精英词缀（金环 · 双倍金币）", x0 + 24, ay + 10, 600, 26, C(255, 216, 130), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (int i = 0; i < AF_COUNT; i++) {
            int px = x0 + 24 + (i % 5) * 200, py = ay + 48 + (i / 5) * 30;
            fill_circle(dc, (float)(px + 9), (float)(py + 13), 8, AFFIX_COLOR[i]);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%s", AFFIX_NAME[i]);
            draw_text(dc, buf, px + 24, py, 170, 26, C(226, 232, 226), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else if (g_dexTab == 1) {
        /* 武器图鉴：2 列 x 6 行 */
        int cw = 520, ch = 74, gx = 36, gy = 6;
        for (int i = 0; i < TOWER_COUNT; i++) {
            const TowerDef *d = &TOWERS[i];
            int x = x0 + (i % 2) * (cw + gx), y = y0 + (i / 2) * (ch + gy);
            if (y + ch > CLIENT_H - 16) break;
            fill_rrect(dc, x, y, cw, ch, 10, C(20, 40, 30), C(56, 84, 64));
            fill_circle(dc, (float)(x + 40), (float)(y + 37), 22, C(14, 30, 20));
            draw_glyph(dc, i, x + 40, y + 37, 0.8f, d->color);
            draw_text(dc, d->name, x + 76, y + 8, 150, 28, C(240, 248, 240), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            wchar_t buf[128];
            swprintf_s(buf, 128, L"造价 %d    伤害 %.0f    射程 %d    间隔 %.1f 秒",
                       d->cost, tower_dmg(i, 1), d->range, d->cd);
            draw_text(dc, buf, x + 76, y + 40, cw - 96, 22, C(190, 214, 196), g_fontTiny, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            draw_text(dc, d->desc, x + 240, y + 8, cw - 260, 28, C(160, 190, 170), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        /* 成就殿堂：列表 + 统计 */
        wchar_t buf[160];
        int unlockedN = 0;
        for (int i = 0; i < ACH_COUNT; i++) if (g_ach & (1ULL << i)) unlockedN++;
        swprintf_s(buf, 160, L"成就进度 %d / %d      累计击杀 %d      累计卖塔 %d",
                   unlockedN, ACH_COUNT, g_killTotal, g_sellTotal);
        draw_text(dc, buf, x0, y0, 1060, 28, C(255, 226, 130), g_fontMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        swprintf_s(buf, 160, L"各图最佳波次：草原 %d · 火山 %d · 冰雪 %d · 沙漠 %d",
                   g_metaBest[0], g_metaBest[1], g_metaBest[2], g_metaBest[3]);
        draw_text(dc, buf, x0, y0 + 34, 1060, 24, C(180, 208, 188), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int ly = y0 + 76, rowH = 34;
        for (int i = 0; i < ACH_COUNT; i++) {
            int y = ly + i * rowH;
            if (y > CLIENT_H - 40) break;
            int got = (g_ach & (1ULL << i)) != 0;
            fill_rrect(dc, x0, y, 1060, rowH - 5, 8,
                       got ? C(34, 58, 42) : C(20, 32, 26),
                       got ? C(255, 216, 94) : C(44, 60, 50));
            fill_circle(dc, (float)(x0 + 24), (float)(y + (rowH - 5) / 2), 9,
                        got ? C(255, 216, 94) : C(60, 76, 64));
            wchar_t name[64];
            swprintf_s(name, 64, L"%s", ACHIEVEMENTS[i].name);
            draw_text(dc, name, x0 + 44, y, 160, rowH - 5,
                      got ? C(255, 236, 150) : C(150, 170, 158), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            draw_text(dc, ACHIEVEMENTS[i].desc, x0 + 220, y, 700, rowH - 5,
                      got ? C(200, 226, 206) : C(120, 140, 128), g_fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

/* ---------------------------------------------------------------- 按钮行为 */
static int hit_button(int mx, int my) {
    for (int i = 0; i < g_btnCount; i++) {
        Btn *b = &g_btns[i];
        if (mx >= b->x && mx <= b->x + b->w && my >= b->y && my <= b->y + b->h)
            return b->enabled ? b->id : B_NONE;
    }
    return B_NONE;
}

static void do_upgrade(void) {
    if (g_selSlot < 0) return;
    Tower *t = &g_slots[g_selSlot];
    if (!t->used || t->level >= MAX_LEVEL) return;
    int cost = upgrade_cost(t->type, t->level);
    if (g_gold < cost) { tip(L"金币不足，无法升级"); return; }
    g_gold -= cost;
    t->level++;
    float_new((float)g_slotX[g_selSlot], (float)(g_slotY[g_selSlot] - 32), L"升级！", C(255, 216, 94), 1.0f, 15);
    stroke_circle(g_memDC, (float)(PLAY_X + g_slotX[g_selSlot]), (float)(PLAY_Y + g_slotY[g_selSlot]), 34, 2, C(255, 240, 160));
}

static void do_sell(void) {
    if (g_selSlot < 0) return;
    Tower *t = &g_slots[g_selSlot];
    if (!t->used) return;
    int back = sell_value(t->type, t->level);
    g_gold += back;
    g_sellTotal++;
    if (g_sellTotal >= 5) unlock(ACH_SELL5);
    save_meta();
    wchar_t buf[48];
    swprintf_s(buf, 48, L"+%d", back);
    float_new((float)g_slotX[g_selSlot], (float)(g_slotY[g_selSlot] - 28), buf, C(255, 216, 94), 1.0f, 15);
    memset(t, 0, sizeof(*t));
    t->beamIdx = -1;
    g_selSlot = -1;
}

static void cycle_target(void) {
    if (g_selSlot < 0) return;
    Tower *t = &g_slots[g_selSlot];
    if (!t->used) return;
    t->mode = (t->mode + 1) & 3;
    static const wchar_t *TM[4] = { L"最前方", L"最强", L"最近", L"最弱" };
    wchar_t buf[48];
    swprintf_s(buf, 48, L"目标：%s", TM[t->mode]);
    float_new((float)g_slotX[g_selSlot], (float)(g_slotY[g_selSlot] - 40), buf, C(180, 230, 255), 1.1f, 14);
}

static void build_at(int si) {
    if (g_buildType < 0 || si < 0 || si >= g_slotCount) return;
    if (g_slots[si].used) return;
    int cost = TOWERS[g_buildType].cost;
    if (g_gold < cost) { tip(L"金币不足"); return; }
    g_gold -= cost;
    Tower *t = &g_slots[si];
    memset(t, 0, sizeof(*t));
    t->used = 1; t->type = g_buildType; t->level = 1; t->mode = 0;
    t->cd = 0; t->beamIdx = -1;
    t->ang = -PI / 2;
    wchar_t buf[48];
    swprintf_s(buf, 48, L"%s 就位！", TOWERS[g_buildType].name);
    float_new((float)g_slotX[si], (float)(g_slotY[si] - 34), buf, C(200, 255, 210), 1.0f, 14);
    stroke_circle(g_memDC, (float)(PLAY_X + g_slotX[si]), (float)(PLAY_Y + g_slotY[si]), 40, 2, C(255, 240, 160));
    /* 成就：同时拥有 6 种不同武器 */
    {
        int kinds[12] = { 0 }, kindsN = 0;
        for (int i = 0; i < g_slotCount; i++)
            if (g_slots[i].used && !kinds[g_slots[i].type]) { kinds[g_slots[i].type] = 1; kindsN++; }
        if (kindsN >= 6) unlock(ACH_COLLECTOR);
    }
    g_selSlot = si;
    g_buildType = -1;
}

static int hit_slot(int mx, int my) {
    for (int i = 0; i < g_slotCount; i++) {
        float dx = (float)(mx - g_slotX[i]), dy = (float)(my - g_slotY[i]);
        if (dx * dx + dy * dy <= 30 * 30) return i;
    }
    return -1;
}

static void contract_pick(int idx);

static void start_selected(void) {
    roll_contracts();
    g_screen = SC_CONTRACT;
}

static void contract_pick(int idx) {
    apply_contract(idx);
    new_game(g_selMap, g_selMode);
    if (g_contract >= 0) banner_set(CONTRACTS[g_contract].name, 2.0f);
    else banner_set(MAPS[g_selMap].name, 2.0f);
    g_tipT = 0;
}

static void on_click(int mx, int my) {
    int id = hit_button(mx, my);
    /* 操作说明浮层打开时，只允许关闭它，避免点到遮罩下的按钮 */
    if (g_helpOpen) {
        if (id == B_CLOSEHELP) g_helpOpen = 0;
        return;
    }
    switch (id) {
    case B_MAP0: case B_MAP1: case B_MAP2: case B_MAP3:
        g_selMap = id - B_MAP0; save_meta(); return;
    case B_MODE:
        g_selMode = !g_selMode; save_meta(); return;
    case B_START: start_selected(); return;
    case B_CONTINUE: if (load_game()) { banner_set(L"继续冒险", 1.8f); } else { g_hasSave = 0; } return;
    case B_SPEED:
        if (g_speed < 1.5f) g_speed = 2.0f;
        else if (g_speed < 2.5f) g_speed = 3.0f;
        else g_speed = 1.0f;
        return;
    case B_PAUSE:
        if (g_screen == SC_GAME) { g_paused = 1; g_screen = SC_PAUSE; }
        else if (g_screen == SC_PAUSE) { g_paused = 0; g_screen = SC_GAME; }
        return;
    case B_MENU:
        if (g_screen == SC_GAME || g_screen == SC_PAUSE) save_game();
        g_screen = SC_MENU;
        g_paused = 0;
        g_helpOpen = 0;
        return;
    case B_RESUME: g_paused = 0; g_screen = SC_GAME; return;
    case B_RESTART: g_paused = 0; new_game(g_map, g_endless); return;
    case B_RETRY: g_paused = 0; new_game(g_map, g_endless); return;
    case B_OVERMENU: delete_save(); g_screen = SC_MENU; return;
    case B_UP: do_upgrade(); return;
    case B_SELL: do_sell(); return;
    case B_TARGET: cycle_target(); return;
    case B_HELP: g_helpOpen = 1; return;
    case B_CLOSEHELP: g_helpOpen = 0; return;
    case B_CONTRACT0: case B_CONTRACT1: case B_CONTRACT2: contract_pick(id - B_CONTRACT0); return;
    case B_SKIPCONTRACT: contract_pick(-1); return;
    case B_DEX: g_screen = SC_DEX; g_dexTab = 0; return;
    case B_DEXBACK: g_screen = SC_MENU; return;
    case B_TAB0: case B_TAB1: case B_TAB2: g_dexTab = id - B_TAB0; return;
    }

    if (g_helpOpen) return;
    if (g_screen != SC_GAME) return;

    /* 武器库 */
    if (mx < PANEL_W && my >= TOP_H) {
        int type = -1;
        for (int i = 0; i < TOWER_COUNT; i++) {
            int x, y;
            card_rect(i, &x, &y);
            if (mx >= x && mx <= x + CARD_W && my >= y && my <= y + CARD_H) { type = i; break; }
        }
        if (type >= 0) {
            g_buildType = (g_buildType == type) ? -1 : type;
            g_selSlot = -1;
        }
        return;
    }
    /* 战场 */
    if (mx >= PLAY_X && my >= PLAY_Y && mx < PLAY_X + PLAY_W && my < PLAY_Y + PLAY_H) {
        int si = hit_slot(mx - PLAY_X, my - PLAY_Y);
        if (g_buildType >= 0) {
            if (si >= 0) build_at(si);
            else g_buildType = -1;
            return;
        }
        g_selSlot = si;
        if (si < 0) return;
    }
}

/* ---------------------------------------------------------------- 主循环 */
static void present(HDC dst) {
    if (!dst) return;
    BitBlt(dst, 0, 0, CLIENT_W, CLIENT_H, g_memDC, 0, 0, SRCCOPY);
}

static void render(void) {
    HDC dc = g_memDC;
    g_btnCount = 0;

    /* 鼠标悬停 */
    g_palHover = -1;
    g_hoverSlot = -1;
    if (g_screen == SC_GAME && g_mouseInside) {
        if (g_mouseX < PANEL_W) {
            for (int i = 0; i < TOWER_COUNT; i++) {
                int x, y;
                card_rect(i, &x, &y);
                if (g_mouseX >= x && g_mouseX <= x + CARD_W && g_mouseY >= y && g_mouseY <= y + CARD_H) { g_palHover = i; break; }
            }
        } else {
            int si = hit_slot(g_mouseX - PLAY_X, g_mouseY - PLAY_Y);
            if (si >= 0) g_hoverSlot = si;
        }
    }

    if (g_screen == SC_MENU) {
        draw_menu(dc);
    } else if (g_screen == SC_CONTRACT) {
        draw_contract(dc);
    } else if (g_screen == SC_DEX) {
        draw_dex(dc);
    } else {
        draw_game(dc);
        if (g_screen == SC_PAUSE) draw_pause(dc);
        if (g_screen == SC_OVER) draw_over(dc);
    }
    if (g_helpOpen) draw_help(dc);

    HDC wnd = g_hwnd ? GetDC(g_hwnd) : NULL;
    present(wnd);
    if (wnd) ReleaseDC(g_hwnd, wnd);
}

/* ---------------------------------------------------------------- 窗口过程 */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ACTIVATE:
        /* 切到后台自动暂停，避免回来发现已经漏怪 */
        if (LOWORD(wp) == WA_INACTIVE && g_screen == SC_GAME) {
            g_paused = 1;
            g_screen = SC_PAUSE;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        g_mouseX = GET_X_LPARAM(lp);
        g_mouseY = GET_Y_LPARAM(lp);
        if (!g_mouseInside) {
            TRACKMOUSEEVENT tme;
            memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        g_mouseInside = 1;
        return 0;
    case WM_MOUSELEAVE:
        g_mouseInside = 0;
        return 0;
    case WM_LBUTTONDOWN: {
        g_mouseX = GET_X_LPARAM(lp);
        g_mouseY = GET_Y_LPARAM(lp);
        if (g_helpOpen) {
            int id = hit_button(g_mouseX, g_mouseY);
            if (id == B_CLOSEHELP) { g_helpOpen = 0; return 0; }
        }
        on_click(g_mouseX, g_mouseY);
        return 0;
    }
    case WM_RBUTTONDOWN:
        g_buildType = -1;
        g_selSlot = -1;
        return 0;
    case WM_KEYDOWN: {
        int key = (int)wp;
        if (key == VK_ESCAPE) {
            if (g_helpOpen) { g_helpOpen = 0; return 0; }
            if (g_screen == SC_GAME) { g_paused = 1; g_screen = SC_PAUSE; }
            else if (g_screen == SC_PAUSE) { g_paused = 0; g_screen = SC_GAME; }
            else if (g_screen == SC_CONTRACT || g_screen == SC_DEX) { g_screen = SC_MENU; }
            else if (g_screen == SC_OVER) { delete_save(); g_screen = SC_MENU; }
            else if (g_screen == SC_MENU) { PostMessage(hwnd, WM_CLOSE, 0, 0); }
            return 0;
        }
        if (g_screen == SC_GAME) {
            if (key == VK_SPACE) { g_paused = 1; g_screen = SC_PAUSE; return 0; }
            if (key == '1') { g_speed = 1.0f; return 0; }
            if (key == '2') { g_speed = 2.0f; return 0; }
            if (key == '3') { g_speed = 3.0f; return 0; }
            if (key == 'U') { do_upgrade(); return 0; }
            if (key == 'S') { do_sell(); return 0; }
            if (key == 'T') { cycle_target(); return 0; }
            if (key == 'M') { save_game(); g_screen = SC_MENU; return 0; }
        } else if (g_screen == SC_PAUSE) {
            if (key == VK_SPACE) { g_paused = 0; g_screen = SC_GAME; return 0; }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------- 截图模式 */
static void fill_bits_alpha(void) {
    if (!g_memBits) return;
    for (int i = 0; i < g_memW * g_memH; i++) g_memBits[i] |= 0xFF000000;
}

static int save_bmp(const wchar_t *path) {
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    memset(&fh, 0, sizeof(fh));
    memset(&ih, 0, sizeof(ih));
    int w = CLIENT_W, h = CLIENT_H;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)(w * h * 4);
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = w * h * 4;
    FILE *f = _wfopen(path, L"wb");
    if (!f) return 0;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    for (int y = h - 1; y >= 0; y--) fwrite(g_memBits + y * w, 4, w, f);
    fclose(f);
    return 1;
}

static void shot_mode(const wchar_t *out, int mapIdx, float seconds) {
    if (mapIdx == -1) {           /* 截契约选择屏 */
        roll_contracts();
        g_screen = SC_CONTRACT;
        render();
        fill_bits_alpha();
        save_bmp(out);
        return;
    }
    if (mapIdx == -2) {           /* 截图鉴：seconds 0 怪物 1 武器 2 成就 */
        g_screen = SC_DEX;
        g_dexTab = (int)seconds;
        render();
        fill_bits_alpha();
        save_bmp(out);
        return;
    }
    if (mapIdx < 0 || mapIdx > 3) mapIdx = 0;
    if (seconds <= 0) {           /* 截主菜单 */
        g_screen = SC_MENU;
        g_selMap = mapIdx;
        render();
        fill_bits_alpha();
        save_bmp(out);
        return;
    }
    new_game(mapIdx, 0);
    g_betweenTimer = 999;
    g_selMap = mapIdx;

    /* 摆一排炮塔 */
    int picks[6] = { 0, 5, 6, 2, 11, 7 };
    g_gold = 99999;
    for (int i = 0; i < 6 && i < g_slotCount; i++) {
        g_buildType = picks[i];
        build_at(i);
        if (g_slots[i].used) g_slots[i].level = 1 + (i % 3);
    }
    g_buildType = -1;

    /* 放怪 */
    start_wave();
    float t = 0;
    while (t < seconds) {
        step(0.033f);
        t += 0.033f;
    }
    g_selSlot = 2;
    render();
    fill_bits_alpha();
    if (!save_bmp(out)) {
        /* 失败就写到 exe 目录 */
        wchar_t p[MAX_PATH];
        wcscpy_s(p, MAX_PATH, g_savePath);
        wchar_t *q = wcsrchr(p, L'\\');
        if (q) *(q + 1) = 0;
        wcscat_s(p, MAX_PATH, L"shot.bmp");
        save_bmp(p);
    }
}

/* ---------------------------------------------------------------- 自动对局模拟 */
static void sim_run(int mapIdx, float seconds) {
    new_game(mapIdx, 0);
    g_selMap = mapIdx;
    FILE *f2 = _wfopen(L"sim_log.txt", L"w");
    int lastLog = -1;
    float t = 0, buildT = 2.0f;
    while (t < seconds && !g_over) {
        buildT -= 0.033f;
        if (buildT <= 0) {
            buildT = 1.2f;
            int towersN = 0;
            for (int i = 0; i < g_slotCount; i++) if (g_slots[i].used) towersN++;
            if (towersN < 4) {
                /* 前期固定四件套：瓶子→雪花→星星→火焰，位置按 slot 顺序 */
                static const int OPENING[4] = { 0, 1, 2, 3 };
                int want = OPENING[towersN & 3];
                if (g_gold >= TOWERS[want].cost + 40) {
                    for (int i = 0; i < g_slotCount; i++) {
                        if (g_slots[i].used) continue;
                        g_buildType = want;
                        build_at(i);
                        break;
                    }
                }
            } else {
                /* 中后期：优先升级，金币充裕再补塔 */
                int upgraded = 0;
                for (int i = 0; i < g_slotCount && !upgraded; i++) {
                    Tower *tw = &g_slots[i];
                    if (!tw->used || tw->level >= MAX_LEVEL) continue;
                    int cost = upgrade_cost(tw->type, tw->level);
                    if (g_gold >= cost + 80) { g_gold -= cost; tw->level++; upgraded = 1; }
                }
                if (!upgraded && towersN < 9 && g_gold >= 420) {
                    /* 补高输出塔：优先星星/巨龙/火箭 */
                    static const int LATE[3] = { 2, 4, 11 };
                    int want = LATE[(towersN - 4) % 3];
                    if (g_gold >= TOWERS[want].cost + 60) {
                        for (int i = 0; i < g_slotCount; i++) {
                            if (g_slots[i].used) continue;
                            g_buildType = want;
                            build_at(i);
                            break;
                        }
                    }
                }
            }
            g_buildType = -1;
        }
        step(0.033f);
        t += 0.033f;
        if ((int)(t * 10) % 50 == 0 && (int)(t * 10) != lastLog) {
            lastLog = (int)(t * 10);
            int ec = 0;
            float maxd = 0;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (g_enemies[i].alive) { ec++; if (g_enemies[i].dist > maxd) maxd = g_enemies[i].dist; }
            fprintf(f2, "t=%.1f wave=%d lives=%d enemies=%d maxdist=%.0f kills=%d gold=%d\n",
                    t, g_wave + 1, g_lives, ec, maxd, g_kills, (int)g_gold);
        }
    }
    FILE *f = _wfopen(L"sim_result.txt", L"w");
    if (!f) f = stderr;
    int n = 0;
    for (int i = 0; i < g_slotCount; i++) if (g_slots[i].used) n++;
    fwprintf(f, L"map=%d wave=%d lives=%d kills=%d gold=%d elite=%d over=%d won=%d towers=%d\n",
             mapIdx, g_wave + 1, g_lives, g_kills, (int)g_gold, g_eliteKills,
             g_over, g_won, n);

    /* 存档往返自检：写入后立刻读回，比对关键字段 */
    {
        g_screen = SC_GAME;                       /* save_game 仅在该屏生效 */
        float goldBefore = g_gold, earnedBefore = g_goldEarned;
        int waveBefore = g_wave, livesBefore = g_lives, killsBefore = g_kills;
        int towersBefore = n, goldEarnedInt = (int)earnedBefore;
        save_game();
        int ok = load_game();
        int towersAfter = 0;
        for (int i = 0; i < g_slotCount; i++) if (g_slots[i].used) towersAfter++;
        int same = ok && (int)g_gold == (int)goldBefore && (int)g_goldEarned == goldEarnedInt
                   && g_wave == waveBefore && g_lives == livesBefore && g_kills == killsBefore
                   && towersAfter == towersBefore;
        fwprintf(f, L"save_roundtrip=%d gold=%d->%d earned=%d->%d wave=%d->%d towers=%d->%d\n",
                 same, (int)goldBefore, (int)g_gold, goldEarnedInt, (int)g_goldEarned,
                 waveBefore, g_wave, towersBefore, towersAfter);
    }
    if (f != stderr) fclose(f);
    if (f2) fclose(f2);
    _wremove(g_savePath);                         /* 自检不留下存档 */
}

/* ---------------------------------------------------------------- 入口 */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show) {
    (void)hPrev;
    timeBeginPeriod(1);
    init_paths();
    init_graphics();
    init_ui();
    srand((unsigned)GetTickCount());

    /* 无窗口截图模式：carrot.exe --shot out.bmp [map] [seconds] */
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && wcscmp(argv[1], L"--sim") == 0) {
        int map = argc >= 4 ? _wtoi(argv[3]) : 0;
        float sec = argc >= 5 ? (float)_wtof(argv[4]) : 420.0f;
        g_running = 0;
        sim_run(map, sec);
        if (argv) LocalFree(argv);
        return 0;
    }
    if (argc >= 3 && wcscmp(argv[1], L"--shot") == 0) {
        int map = argc >= 4 ? _wtoi(argv[3]) : 0;
        float sec = argc >= 5 ? (float)_wtof(argv[4]) : 6.0f;
        g_running = 0;
        shot_mode(argv[2], map, sec);
        if (argv) LocalFree(argv);
        return 0;
    }
    if (argv) LocalFree(argv);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"CarrotDefenseC";
    RegisterClassExW(&wc);

    RECT r = { 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    g_hwnd = CreateWindowExW(0, L"CarrotDefenseC", L"保卫萝卜 · C 版",
                             WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top,
                             NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 0;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    LARGE_INTEGER freq, prev, frameStart, frameEnd;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        QueryPerformanceCounter(&frameStart);
        float dt = (float)((frameStart.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = frameStart;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0005f) dt = 0.0005f;

        if (g_screen == SC_GAME && !g_paused && !g_over) update_game(dt);
        if (g_tipT > 0) g_tipT -= dt;
        if (g_goldPulse > 0) g_goldPulse -= dt * 3.0f;
        if (g_livesPulse > 0) g_livesPulse -= dt * 3.0f;
        render();

        /* 帧率上限：战斗 60fps，菜单/暂停/结算 30fps。
           不限制时本机会占满半个 CPU 核（纯浪费电）。 */
        QueryPerformanceCounter(&frameEnd);
        double frameCost = (frameEnd.QuadPart - frameStart.QuadPart) / (double)freq.QuadPart;
        double target = (g_screen == SC_GAME && !g_paused && !g_over) ? (1.0 / 60.0) : (1.0 / 30.0);
        if (frameCost < target) {
            DWORD waitMs = (DWORD)((target - frameCost) * 1000.0);
            if (waitMs > 0) Sleep(waitMs);
            else Sleep(0);
        }
    }

    if (g_screen == SC_GAME || g_screen == SC_PAUSE) save_game();
    timeEndPeriod(1);
    return 0;
}
