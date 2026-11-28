/* ---- The Hell Greed's coffins (CEm005, EST 20), included by enemy.c. Its fight is the Seven Hells' shared one;
 * what's its own is the summon. Its action handler (vfunc 0x1a4, FUN_00358580) throws a coffin, CEm005Shl01
 * (FUN_004845c0), for each of actions 1000, 1001 and 1002; 1003 calls them back. A coffin (update FUN_00484ec0)
 * flies four 40-frame turns of a spiral (its pitch and yaw turning 364-910 units of 65536 a frame, 5-10 cm a
 * frame), then drops; at the floor it spawns the Greed's summon (FUN_00358a20 by its spawn word: 0 a Pride, 1 a
 * Lust, 2 a Sloth, 3-7 mixes) 110 cm up, while the enemy manager's summon count (+0x898) is under its limit
 * (+0x89c). A hit in flight breaks it.
 * The flight path, landing point, spawn cap and visuals remain approximations. */
#define COFFIN_MAX 12
#define GREED_SUMMON_LIMIT 6
static struct { int active; float t, from[3], to[3], spin; } coffins[COFFIN_MAX];
static int greed_summoned;

static void coffin_throw(const float from[3], float px, float pz) {
    int i;
    for (i = 0; i < COFFIN_MAX; ++i) if (!coffins[i].active) {
        float a = ai_unit() * 6.2831853f, r = 150.0f + ai_unit() * 250.0f;
        coffins[i].active = 1;
        coffins[i].t = 0.0f;
        memcpy(coffins[i].from, from, sizeof(coffins[i].from));
        coffins[i].to[0] = px + sinf(a) * r;
        coffins[i].to[1] = from[1];
        coffins[i].to[2] = pz + cosf(a) * r;
        coffins[i].spin = a;
        return;
    }
}
/* CComEm005 plan 27 (003dd540) runs action 6, then action 5. After
 * script 1:3 finishes, the coffin releases are timed separately (003de480). */
static int greed_cast(float f, float px, float pz) {
    if (e->step == 0 && e->done) {
        script_play(103);
        e->step = 1;
    } else if (e->step == 1 && e->done) {
        e->timer = 0;
        e->step = 2;
    } else if (e->step >= 2 && e->step <= 4) {
        float from[3] = {e->pos[0], e->pos[1] + 150, e->pos[2]};
        e->timer += f;
        if (e->timer > 60 * (e->step - 1)) {
            /* Only rank buckets 3, 4 and 5 release the middle coffin. */
            if (e->step != 3) coffin_throw(from, px, pz);
            ++e->step;
        }
    } else if (e->step == 5) {
        script_play(104);
        e->step = 6;
    } else if (e->step == 6 && e->done) {
        action_enter(A_NONE);
    }
    return e->step == 0 || (e->step == 1 && e->frame < 140) || e->step == 6;
}
static void coffins_update(float f) {
    int i;
    for (i = 0; i < COFFIN_MAX; ++i) {
        float u, p[3], v[3] = {0.0f, 0.0f, 0.0f};
        if (!coffins[i].active) continue;
        coffins[i].t += f;
        /* 160 frames of spiral (four turns of 40), rising then falling onto its spot. */
        u = fminf(1.0f, coffins[i].t / 160.0f);
        coffins[i].spin += 0.05f * f;
        p[0] = coffins[i].from[0] + (coffins[i].to[0] - coffins[i].from[0]) * u + sinf(coffins[i].spin) * 100.0f * (1.0f - u);
        p[2] = coffins[i].from[2] + (coffins[i].to[2] - coffins[i].from[2]) * u + cosf(coffins[i].spin) * 100.0f * (1.0f - u);
        p[1] = coffins[i].from[1] + sinf(u * 3.1415927f) * 400.0f - 150.0f * u;
        fx_mote(p, v, 70.0f, 30.0f, 0.3f, 0x502020ffu);
        if (u >= 1.0f) {
            coffins[i].active = 0;
            fx_explosion(p);
            if (greed_summoned < GREED_SUMMON_LIMIT && enemy_add_kind(ENEMY_PRIDE, coffins[i].to[0], coffins[i].to[2]))
                ++greed_summoned;
        }
    }
}
static void coffins_reset(void) {
    memset(coffins, 0, sizeof(coffins));
    greed_summoned = 0;
}
