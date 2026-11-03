#include "hud.h"

#include <pspgu.h>

#include "hud_runtime.h"

#define SCR_W 480.0f
#define SCR_H 272.0f
/* The meshes are in PS2 screen units (640 x 448 at 4:3). A uniform scale keeps the
 * art's proportions on the PSP's wider screen; side elements then move out to the
 * screen edges by the margin that leaves. */
#define HUD_SCALE (SCR_H / 448.0f)
#define HUD_MARGIN ((SCR_W - 640.0f * HUD_SCALE) * 0.5f)

enum { ANCHOR_LEFT = -1, ANCHOR_CENTER = 0, ANCHOR_RIGHT = 1 };

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

static void draw_anchored(int mesh, int anchor) {
    draw_mesh(mesh, SCR_W * 0.5f + (float)anchor * HUD_MARGIN, SCR_H * 0.5f, HUD_SCALE);
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
    };
    static const int melee[5] = {HUD_MELEE_REBELLION, HUD_MELEE_CERBERUS, HUD_MELEE_AGNI, HUD_MELEE_NEVAN, HUD_MELEE_BEOWULF};
    static const int guns[5] = {HUD_GUN_EBONY_IVORY, HUD_GUN_SHOTGUN, HUD_GUN_ARTEMIS, HUD_GUN_SPIRAL, HUD_GUN_KALINA};
    begin();
    draw_anchored(HUD_BAR_FRAME, ANCHOR_LEFT);
    draw_anchored(HUD_GEMS, ANCHOR_LEFT);
    draw_anchored(emblems[player->style], ANCHOR_LEFT);
    draw_anchored(HUD_ORBS, ANCHOR_RIGHT);
    draw_anchored(guns[player->gun_weapon - 5], ANCHOR_LEFT);
    draw_anchored(melee[player->melee_weapon], ANCHOR_RIGHT);
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
