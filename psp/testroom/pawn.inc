/* ---- The Damned Pawn (CEm017, EST 35; em017.pac, GDATA 127), Mission 7's chessman in 115 and 135. Included by
 * enemy.c. The class is CEm017 (constructor FUN_00404e80, update FUN_00405030 -> alive FUN_00406730, which runs
 * the piece's own logic by EST id: the Pawn's is FUN_004383e0). Decoded at rank 0:
 *   - a hop (FUN_00438840, table 0x5c2d30): it rises at 4 cm a frame, slowing by 0.1 a frame, and glides 5 cm a frame
 *     toward Dante (turning to him) while it rises: 40 frames, 80 cm up, one 200 cm square. Then it drops,
 *     gaining 3.2 cm a frame each frame, to the floor: dust (effect 0x14) and a 5-frame shake;
 *   - a double hop (FUN_00438b70, table 0x5c2d60): two of them at 10 cm a frame, rising 4 slowing 0.2;
 *   - landed, if Dante is within 170 cm (FUN_004387f0) and within 90 degrees of its front, it attacks
 *     (FUN_00438a50): it waits 40 frames (40 - rank x 6), then plays its motion; else it thinks again;
 *   - between moves (FUN_00408320) a wait counted down by 3 x 1000 / max(distance, 1000) a frame.
 * The pieces take their turns from a chess master (commands at +0x488) that isn't ported: the wait's length, the
 * choice of the double hop, the strike's reach and its damage, and the Pawn's health are estimates. */
#define PAWN_HEALTH (ENEMY_HEALTH * 300 / 250)
#define PAWN_WAIT 90.0f          /* [estimate] the master's turn */
#define PAWN_REACH 170.0f        /* FUN_004387f0: 170 cm, a quarter turn either side */
enum { PW_THINK, PW_RISE, PW_FALL, PW_WAIT, PW_STRIKE, PW_DEATH };
static int pawn_motion = -1;

static int pawn_init(void) {
    pawn_motion = motion_find("dp00");
    return pawn_motion >= 0;
}
static void pawn_state(int s) {
    e->action = (unsigned char)s;
    e->timer = 0.0f;
    e->step = 0;
    e->done = 0;
}
static void pawn_spawn(void) {
    e->plan = P_WATCH;
    e->motion = pawn_motion;
    e->frame = 0.0f;
    e->fade = 0.0f;
    e->vel[0] = e->vel[1] = e->vel[2] = 0.0f;
    pawn_state(PW_THINK);
    e->timer = -ai_unit() * PAWN_WAIT;   /* not all on the same beat */
}
static int pawn_facing(float px, float pz, float reach) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz);
    if (d > reach) return 0;
    return d < 1.0f || (dx * sinf(e->yaw) + dz * cosf(e->yaw)) / d > 0.0f;
}
static void pawn_hop(int doubled) {
    /* (rise, glide, slowing): 0x5c2d30 / 0x5c2d60, rank 0. */
    e->vel[1] = 4.0f;
    e->vel[2] = doubled ? 10.0f : 5.0f;
    e->vel[0] = doubled ? 0.2f : 0.1f;
    pawn_state(PW_RISE);
    e->step = doubled ? 2 : 1;
}
static void pawn_update(float f, float px, float py, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g;
    e->timer += f;
    if (floor_under(e->pos[0], e->pos[1] + 100.0f, e->pos[2], &g)) e->floor_y = g;
    if (e->plan == P_DEATH) {
        if (e->timer > 30.0f) e->active = 0;
        return;
    }
    switch (e->action) {
    case PW_THINK:
        e->frame = 0.0f;
        if (e->timer > 0.0f) e->timer += f * (3.0f * 1000.0f / fmaxf(d, 1000.0f) - 1.0f);
        if (e->timer > PAWN_WAIT) pawn_hop(d > 600.0f && ai_unit() < 0.3f);   /* [estimate] the double hop when far */
        e->pos[1] = e->floor_y;
        break;
    case PW_RISE:
        goyle_turn_to(px, pz, 0.08f, f);
        e->pos[0] += sinf(e->yaw) * e->vel[2] * f;
        e->pos[2] += cosf(e->yaw) * e->vel[2] * f;
        e->pos[1] += e->vel[1] * f;
        e->vel[1] -= e->vel[0] * f;
        if (e->vel[1] <= 0.0f) { e->vel[1] = 0.0f; e->action = PW_FALL; }
        break;
    case PW_FALL:
        e->vel[1] -= 3.2f * f;
        e->pos[1] += e->vel[1] * f;
        if (e->pos[1] <= e->floor_y) {
            float at[3] = {e->pos[0], e->floor_y + 10.0f, e->pos[2]};
            e->pos[1] = e->floor_y;
            fx_hit_spark(at);   /* stands in for its dust, effect 0x14 */
            /* Landing on Dante [inferred: the drop's own hit]. */
            if (attacks_on && d < 90.0f && fabsf(py - e->pos[1]) < 150.0f) land_attack(0);
            if (e->step == 2) { pawn_hop(0); e->vel[2] = 10.0f; e->vel[0] = 0.2f; e->step = 3; break; }
            if (pawn_facing(px, pz, PAWN_REACH)) pawn_state(PW_WAIT);
            else pawn_state(PW_THINK);
        }
        break;
    case PW_WAIT:
        e->pos[1] = e->floor_y;
        if (e->timer > 40.0f) { pawn_state(PW_STRIKE); e->frame = 0.0f; }
        break;
    case PW_STRIKE: {
        const Motion *m = motion_get(pawn_motion);
        e->frame += f;
        if (!e->step && e->frame > m->end_frame * 0.5f) {
            e->step = 1;
            if (attacks_on && pawn_facing(px, pz, PAWN_REACH + 30.0f) && fabsf(py - e->pos[1]) < 200.0f) land_attack(0);
        }
        if (e->frame >= m->end_frame) { e->frame = 0.0f; pawn_state(PW_THINK); }
        e->pos[1] = e->floor_y;
        break;
    }
    default:
        pawn_state(PW_THINK);
    }
}
static void pawn_hit(HitKind kind) {
    float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
    if (e->plan == P_DEATH) return;
    ++hits;
    fx_hit_spark(at);
    if (mortal) {
        int dmg = kind == HIT_PISTOL ? (ai_unit() < 0.2f) : (kind == HIT_LIGHT || kind == HIT_AIR) ? 1 : 2;
        e->health = (signed char)(e->health - dmg);
        if (e->health <= 0) {
            int k;
            e->plan = P_DEATH;
            pawn_state(PW_DEATH);
            for (k = 0; k < 8; ++k) {
                float p[3] = {e->pos[0] + ai_unit() * 100.0f - 50.0f, e->pos[1] + ai_unit() * 200.0f, e->pos[2] + ai_unit() * 100.0f - 50.0f};
                float v[3] = {0.0f, 40.0f + ai_unit() * 40.0f, 0.0f};
                fx_mote(p, v, 60.0f, 25.0f, 0.4f, 0xc0c0c0ffu);
            }
            died();
        }
    }
    /* A statue: hits don't stagger it [inferred from the walkthrough's "easy to defeat"; its reactions aren't read]. */
}
