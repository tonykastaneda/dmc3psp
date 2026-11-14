/* ---- The Hell Greed's coffins (CEm005, EST 20), included by enemy.c. Its fight is the Seven Hells' shared one;
 * what's its own is the summon. Its action handler (vfunc 0x1a4, FUN_00358580) throws a coffin, CEm005Shl01
 * (FUN_004845c0), for each of actions 1000, 1001 and 1002; 1003 calls them back. A coffin (update FUN_00484ec0)
 * flies four 40-frame turns of a spiral (its pitch and yaw turning 364-910 units of 65536 a frame, 5-10 cm a
 * frame), then drops; at the floor it spawns the Greed's summon (FUN_00358a20 by its spawn word: 0 a Pride, 1 a
 * Lust, 2 a Sloth, 3-7 mixes) 110 cm up, while the enemy manager's summon count (+0x898) is under its limit
 * (+0x89c). A hit in flight breaks it.
 * Not decoded, so estimated: when the Greed summons (its table only ever picks plan 27, whose override isn't
 * read), the summon limit, where the coffins come down, and how a coffin's drawn (a streak of motes: its
 * model isn't converted). Coffins can't be broken yet. */
#define COFFIN_MAX 12
#define GREED_SUMMON_EVERY 480.0f    /* [estimate] frames between summons */
#define GREED_SUMMON_LIMIT 6         /* [estimate] the manager's +0x89c */
static struct { int active; float t, from[3], to[3], spin; } coffins[COFFIN_MAX];
static float greed_clock[ENEMY_MAX];
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
/* After a Greed's own update: its summon clock. */
static void greed_tick(int slot, float f, float px, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2];
    if (!attacks_on || e->plan == P_DEATH) return;
    greed_clock[slot] += f;
    if (greed_clock[slot] < GREED_SUMMON_EVERY || dx * dx + dz * dz > 1500.0f * 1500.0f) return;
    greed_clock[slot] = ai_unit() * 120.0f;
    {
        /* Actions 1000-1002: three coffins out of it. */
        float at[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]};
        int k;
        for (k = 0; k < 3; ++k) coffin_throw(at, px, pz);
        fx_hit_spark(at);
    }
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
    memset(greed_clock, 0, sizeof(greed_clock));
    greed_summoned = 0;
}
