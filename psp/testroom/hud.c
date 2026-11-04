#include "hud.h"

#include <pspgu.h>
#include <pspkernel.h>

#include "hud_runtime.h"

#define SCR_W 480.0f
#define SCR_H 272.0f
/* The meshes are in PS2 screen units (640 x 448 at 4:3). A uniform scale keeps the
 * art's proportions on the PSP's wider screen; side elements then move out to the
 * screen edges by the margin that leaves. */
#define HUD_SCALE (SCR_H / 448.0f)
#define HUD_MARGIN ((SCR_W - 640.0f * HUD_SCALE) * 0.5f)

#define EMBLEM_SIZE 18.0f
#define BAR_LEFT 26.0f
#define BAR_TOP 8.0f
#define BAR_WIDTH 150.0f
#define BAR_HEIGHT 6.0f
#define DEVIL_WIDTH 100.0f
#define DEVIL_HEIGHT 3.0f
#define DEVIL_SEGMENTS 10
#define TAIL_PER_SECOND 0.25f
#define WEAPON_SIZE 40.0f
#define WEAPON_SHOW_SECONDS 2.0f
#define WEAPON_FADE_SECONDS 0.5f

typedef struct { float u, v, x, y, z; } HudDrawVertex;

static void draw_mesh(int mesh, float cx, float cy, float scale) {
    const HudMesh *m = &hud_meshes[mesh];
    const HudTexture *t = &hud_textures[m->texture];
    HudDrawVertex *v = sceGuGetMemory(sizeof(HudDrawVertex) * m->vert_count);
    unsigned int i;
    for (i = 0; i < m->vert_count; ++i) {
        const HudVertex *s = &m->verts[i];
        /* 2D (through) mode takes texture coordinates in texels. */
        v[i].u = s->u * (float)t->width;
        v[i].v = s->v * (float)t->height;
        v[i].x = cx + s->x * scale;
        v[i].y = cy - s->y * scale;
        v[i].z = 0.0f;
    }
    sceGuTexImage(0, t->width, t->height, t->width, t->rgba);
    sceGuTexFlush();
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, (int)m->vert_count, 0, v);
}

/* A mesh from the game's cockpit, scaled to sit in a box of the small screen. */
static void draw_fitted(int mesh, float x, float y, float size) {
    const HudMesh *m = &hud_meshes[mesh];
    float x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f, scale;
    unsigned int i;
    for (i = 0; i < m->vert_count; ++i) {
        const HudVertex *s = &m->verts[i];
        if (s->x < x0) x0 = s->x;
        if (s->x > x1) x1 = s->x;
        if (s->y < y0) y0 = s->y;
        if (s->y > y1) y1 = s->y;
    }
    scale = size / (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0);
    draw_mesh(mesh, x + size * 0.5f - (x0 + x1) * 0.5f * scale, y + size * 0.5f + (y0 + y1) * 0.5f * scale, scale);
}

typedef struct { unsigned int color; float x, y, z; } BarVertex;

/* A flat rectangle, or with two colours a vertical gradient (top, bottom). */
static void rect(float x0, float y0, float x1, float y1, unsigned int top, unsigned int bottom) {
    BarVertex *v = sceGuGetMemory(sizeof(BarVertex) * 6);
    v[0].color = top; v[0].x = x0; v[0].y = y0;
    v[1].color = top; v[1].x = x1; v[1].y = y0;
    v[2].color = bottom; v[2].x = x1; v[2].y = y1;
    v[3].color = top; v[3].x = x0; v[3].y = y0;
    v[4].color = bottom; v[4].x = x1; v[4].y = y1;
    v[5].color = bottom; v[5].x = x0; v[5].y = y1;
    v[0].z = v[1].z = v[2].z = v[3].z = v[4].z = v[5].z = 0.0f;
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
}

static void begin(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuColor(0xffffffffu);
}

static void end(void) {
    sceGuDisable(GU_BLEND);
}

void hud_draw(const Player *player) {
    /* By colour: the styles' order in the pack isn't confirmed from the code. */
    static const int emblems[STYLE_COUNT] = {
        [STYLE_TRICKSTER] = HUD_STYLE_GOLD, [STYLE_SWORDMASTER] = HUD_STYLE_RED,
        [STYLE_GUNSLINGER] = HUD_STYLE_BLUE, [STYLE_ROYALGUARD] = HUD_STYLE_GREEN,
        /* Their own emblems aren't picked out of the pack yet. */
        [STYLE_QUICKSILVER] = HUD_STYLE_BLUE, [STYLE_DOPPELGANGER] = HUD_STYLE_RED,
    };
    static const int melee[5] = {HUD_MELEE_REBELLION, HUD_MELEE_CERBERUS, HUD_MELEE_AGNI, HUD_MELEE_NEVAN, HUD_MELEE_BEOWULF};
    static const int guns[5] = {HUD_GUN_EBONY_IVORY, HUD_GUN_SHOTGUN, HUD_GUN_ARTEMIS, HUD_GUN_SPIRAL, HUD_GUN_KALINA};
    /* Sized for a small screen, after Monster Hunter's: a slim bar in the corner, nothing else until
     * it matters. Weapon icons show when switched, then fade. */
    static int last_melee = -1, last_gun = -1;
    static float melee_time, gun_time, tail = 1.0f;
    static unsigned int last_us;
    unsigned int now = sceKernelGetSystemTimeLow();
    float dt = (float)(now - last_us) / 1000000.0f, health = player->health / PLAYER_HEALTH_MAX;
    last_us = now;
    if (dt > 0.1f) dt = 0.1f;
    if (health < 0.0f) health = 0.0f;
    if (player->melee_weapon != last_melee) {
        if (last_melee >= 0) melee_time = WEAPON_SHOW_SECONDS;
        last_melee = player->melee_weapon;
    }
    if (player->gun_weapon != last_gun) {
        if (last_gun >= 0) gun_time = WEAPON_SHOW_SECONDS;
        last_gun = player->gun_weapon;
    }
    /* The red tail shows what the last hits took, then catches up. */
    if (tail < health) tail = health;
    else if (tail > health) { tail -= TAIL_PER_SECOND * dt; if (tail < health) tail = health; }

    begin();
    draw_fitted(emblems[player->style], BAR_LEFT - EMBLEM_SIZE - 3.0f, BAR_TOP - 3.0f, EMBLEM_SIZE);
    sceGuDisable(GU_TEXTURE_2D);
    rect(BAR_LEFT - 1.0f, BAR_TOP - 1.0f, BAR_LEFT + BAR_WIDTH + 1.0f, BAR_TOP + BAR_HEIGHT + 1.0f, 0xc0000000u, 0xc0000000u);
    rect(BAR_LEFT, BAR_TOP, BAR_LEFT + BAR_WIDTH, BAR_TOP + BAR_HEIGHT, 0xc0201808u, 0xc0100c04u);
    rect(BAR_LEFT, BAR_TOP, BAR_LEFT + BAR_WIDTH * tail, BAR_TOP + BAR_HEIGHT, 0xff3030e0u, 0xff1818a0u);
    rect(BAR_LEFT, BAR_TOP, BAR_LEFT + BAR_WIDTH * health, BAR_TOP + BAR_HEIGHT, 0xff60e878u, 0xff208838u);
    {
        /* Devil Trigger: a thin row of segments under the bar. */
        float top = BAR_TOP + BAR_HEIGHT + 2.0f, each = (DEVIL_WIDTH - (DEVIL_SEGMENTS - 1)) / DEVIL_SEGMENTS;
        int k;
        rect(BAR_LEFT - 1.0f, top - 1.0f, BAR_LEFT + DEVIL_WIDTH + 1.0f, top + DEVIL_HEIGHT + 1.0f, 0xc0000000u, 0xc0000000u);
        for (k = 0; k < DEVIL_SEGMENTS; ++k) {
            float x = BAR_LEFT + (each + 1.0f) * (float)k;
            rect(x, top, x + each, top + DEVIL_HEIGHT, player->devil ? 0xffff90d0u : 0xffe050a0u, 0xff901860u);
        }
    }
    sceGuEnable(GU_TEXTURE_2D);
    if (gun_time > 0.0f) {
        float a = gun_time < WEAPON_FADE_SECONDS ? gun_time / WEAPON_FADE_SECONDS : 1.0f;
        sceGuColor(((unsigned int)(a * 255.0f) << 24) | 0x00ffffffu);
        draw_fitted(guns[player->gun_weapon - 5], 8.0f, SCR_H - WEAPON_SIZE - 6.0f, WEAPON_SIZE);
        gun_time -= dt;
    }
    if (melee_time > 0.0f) {
        float a = melee_time < WEAPON_FADE_SECONDS ? melee_time / WEAPON_FADE_SECONDS : 1.0f;
        sceGuColor(((unsigned int)(a * 255.0f) << 24) | 0x00ffffffu);
        draw_fitted(melee[player->melee_weapon], SCR_W - WEAPON_SIZE - 8.0f, SCR_H - WEAPON_SIZE - 6.0f, WEAPON_SIZE);
        melee_time -= dt;
    }
    sceGuColor(0xffffffffu);
    end();
}

float hud_reticle_scale(float distance) {
    if (distance < 300.0f) return 1.0f;
    if (distance > 1000.0f) return 0.5f;
    return ((700.0f - (distance - 300.0f)) / 700.0f + 1.0f) * 0.5f;
}

void hud_draw_reticle(float x, float y, float scale) {
    begin();
    draw_mesh(HUD_RETICLE, x, y, HUD_SCALE * scale);
    end();
}
