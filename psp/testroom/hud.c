#include "hud.h"
#include "menu.h"
#include <string.h>

#include <pspgu.h>
#include <pspkernel.h>

#include "hud_runtime.h"

#define SCR_W 480.0f
#define SCR_H 272.0f
/* Reticle geometry retains the PS2 scale. Cockpit parts are anchored directly
 * in PSP pixels so the 16:9 side margins are usable. */
#define HUD_SCALE (SCR_H / 448.0f)

#define EMBLEM_SIZE 18.0f
#define BAR_LEFT 46.0f
#define BAR_TOP 13.0f
#define BAR_WIDTH 150.0f
#define BAR_HEIGHT 7.0f
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

/* Remap a cockpit part into PSP pixels. GU memory is the frame's display-list
 * arena, never a heap allocation. The frame and runes retain the disc UVs. */
static void art_box(int mesh, int part, float x, float y, float w, float h) {
    const HudMesh *m = &hud_meshes[mesh];
    const HudTexture *t = &hud_textures[m->texture];
    unsigned int first = part < 0 ? 0 : m->parts[part].first;
    unsigned int count = part < 0 ? m->vert_count : m->parts[part].count;
    float x0=1e9f, x1=-1e9f, y0=1e9f, y1=-1e9f;
    unsigned int i;
    HudDrawVertex *v = sceGuGetMemory(sizeof(*v)*count);
    for(i=first;i<first+count;++i) {
        const HudVertex *a=&m->verts[i];
        if(a->x<x0)x0=a->x;
        if(a->x>x1)x1=a->x;
        if(a->y<y0)y0=a->y;
        if(a->y>y1)y1=a->y;
    }
    for(i=0;i<count;++i) {
        const HudVertex *a=&m->verts[first+i];
        v[i].u=a->u*t->width; v[i].v=a->v*t->height;
        if(mesh==HUD_BAR_FRAME) {
            float q=a->x-x0, span=x1-x0;
            /* Keep the two end ornaments at 22px as the middle grows. */
            if(q<50.771f) v[i].x=x+q/50.771f*22;
            else if(q>span-56.533f) v[i].x=x+w-22+(q-span+56.533f)/56.533f*22;
            else v[i].x=x+22+(q-50.771f)/(span-107.304f)*(w-44);
        } else v[i].x=x+(a->x-x0)/(x1-x0)*w;
        v[i].y=y+(y1-a->y)/(y1-y0)*h; v[i].z=0;
    }
    sceGuTexImage(0,t->width,t->height,t->width,t->rgba);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_2D,count,0,v);
}

static float fraction(float hp, float maximum) {
    float f=maximum>0 ? hp/maximum : 0;
    return f<0?0:f>1?1:f;
}

typedef struct { float value, previous, hold; } DamageTrail;
static float trail_step(DamageTrail *t, float hp, float dt) {
    if(hp<t->previous) t->hold=0.35f;
    if(hp>t->value) t->value=hp;
    if(t->hold>0) t->hold-=dt;
    else if(t->value>hp) { t->value-=dt*0.65f; if(t->value<hp)t->value=hp; }
    t->previous=hp;
    return t->value;
}

typedef struct { float u,v; unsigned int color; float x,y,z; } RuneVertex;

/* One textured draw for the entire rune row, including partial fill. Each
 * source part is a textured quad; cache its four UV bounds once. */
static void draw_runes(const Player *p) {
    static float uv[10][4];
    static int ready;
    const HudMesh *m=&hud_meshes[HUD_GEMS];
    const HudTexture *t=&hud_textures[m->texture];
    RuneVertex *v=sceGuGetMemory(sizeof(*v)*120);
    int k, n=0, count=(int)p->dt_max;
    if(count<0)count=0;
    if(count>10)count=10;
    if(!ready) {
        for(k=0;k<10;++k) {
            unsigned int j;
            uv[k][0]=uv[k][2]=1e9f; uv[k][1]=uv[k][3]=-1e9f;
            for(j=0;j<m->parts[k+1].count;++j) {
                const HudVertex *a=&m->verts[m->parts[k+1].first+j];
                if(a->u<uv[k][0])uv[k][0]=a->u;
                if(a->u>uv[k][1])uv[k][1]=a->u;
                if(a->v<uv[k][2])uv[k][2]=a->v;
                if(a->v>uv[k][3])uv[k][3]=a->v;
            }
        }
        ready=1;
    }
    for(k=0;k<count;++k) {
        float fill=fraction(p->dt_energy-k,1), x=BAR_LEFT-2+k*13;
        int half;
        for(half=0;half<2;++half) {
            float left=half?fill:0, right=half?1:fill;
            unsigned int color=half?0xff403840u:p->devil?0xffffb8ffu:0xffff70d0u;
            static const int corner[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
            int j;
            if(right<=left)continue;
            for(j=0;j<6;++j) {
                float q=corner[j][0]?right:left;
                v[n].u=(uv[k][0]+(uv[k][1]-uv[k][0])*q)*t->width;
                v[n].v=uv[k][corner[j][1]?3:2]*t->height;
                v[n].color=color; v[n].x=x+q*12;
                v[n].y=28+corner[j][1]*13; v[n].z=0; ++n;
            }
        }
    }
    sceGuTexImage(0,t->width,t->height,t->width,t->rgba);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,n,0,v);
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
    /* Weapon icons show when switched, then fade. */
    static int last_melee = -1, last_gun = -1;
    static float melee_time, gun_time;
    static DamageTrail trail = {1,1,0};
    static unsigned int last_us;
    unsigned int now = sceKernelGetSystemTimeLow();
    float dt = (float)(now - last_us) / 1000000.0f, health = fraction(player->health, player->health_max), width = BAR_WIDTH * player->health_max / PLAYER_HEALTH_MAX, tail;
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
    tail = trail_step(&trail, health, dt);

    begin();
    draw_fitted(emblems[player->style], 7.0f, 8.0f, EMBLEM_SIZE);
    sceGuDisable(GU_TEXTURE_2D);
    rect(BAR_LEFT, BAR_TOP, BAR_LEFT+width, BAR_TOP+BAR_HEIGHT, 0xff201018u, 0xff100810u);
    rect(BAR_LEFT, BAR_TOP, BAR_LEFT+width*tail, BAR_TOP+BAR_HEIGHT, 0xff80a8f0u, 0xff305090u);
    rect(BAR_LEFT, BAR_TOP, BAR_LEFT+width*health, BAR_TOP+BAR_HEIGHT, 0xff6868f8u, 0xff151580u);
    sceGuEnable(GU_TEXTURE_2D);
    art_box(HUD_BAR_FRAME,-1,BAR_LEFT-22,BAR_TOP-5,width+44,18);
    draw_runes(player);
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

void hud_draw_boss(const char *name, float health, float maximum) {
    static char last_name[64];
    static DamageTrail trail;
    static unsigned int last_us;
    unsigned int now=sceKernelGetSystemTimeLow();
    float dt=(float)(now-last_us)/1000000.0f, hp=fraction(health,maximum), tail;
    last_us=now;
    if(!name || health<=0 || maximum<=0) { last_name[0]=0; return; }
    if(dt>0.1f)dt=0.1f;
    if(strcmp(last_name,name)) {
        strncpy(last_name,name,sizeof(last_name)-1); last_name[sizeof(last_name)-1]=0;
        trail.value=trail.previous=hp; trail.hold=0;
    }
    tail=trail_step(&trail,hp,dt);
    begin();
    sceGuDisable(GU_TEXTURE_2D);
    rect(69,253,411,260,0xff201018u,0xff100810u);
    rect(69,253,69+342*tail,260,0xff80a8f0u,0xff305090u);
    rect(69,253,69+342*hp,260,0xff6060e8u,0xff181880u);
    sceGuEnable(GU_TEXTURE_2D);
    art_box(HUD_BAR_FRAME,-1,47,248,386,19);
    if(menu_loaded()) {
        float size=14, width=menu_font_width(size,name);
        if(width>340)size*=340/width;
        menu_font_draw((SCR_W-menu_font_width(size,name))*0.5f+1,233, size,name,0xff000000u);
        menu_font_draw((SCR_W-menu_font_width(size,name))*0.5f,232, size,name,0xffddddeeu);
    }
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
