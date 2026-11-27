/* ---- The Damned Queen and King (EST 39 / 40, CEm021; em021.pac at GDATA 128), Mission 18's chess board in 401.
 * Included by enemy.c. Their brains are queen.c / king.c / royal.c (docs/CHESS_ROYALS_SPEC.md); this is enemy.c's
 * side: motions, the floor, Dante's hits on them, theirs on him, and their effects (nfx banks 25 / 26). */
#include "queen.h"
#include "king.h"
static QueenBrain queen_brain[ENEMY_MAX];
static KingBrain king_brain[ENEMY_MAX];
static int royal_fx[ENEMY_MAX][4];          /* live V effects: the Queen's bone, lane and charge; the castle's */
static int royal_loop[ENEMY_MAX];
static int royal_mot[4] = {-2};             /* qn00 qn01 qn02 kg00 */

static RoyalBody *royal_body(void) {
    return e->kind == ENEMY_QUEEN ? &queen_brain[e - enemies].body : &king_brain[e - enemies].body;
}
static void royal_fx_kill(int k) {
    int *h = &royal_fx[e - enemies][k];
    if (*h > 0) nfx_kill(*h);
    *h = 0;
}
/* The motion scripts (em021 [4] / [9]): which MOT a motion id plays, and whether it loops. */
static void royal_play(int id, int restart) {
    int mot, loop, slot = (int)(e - enemies);
    if (royal_mot[0] == -2) {
        royal_mot[0] = motion_find("qn00"); royal_mot[1] = motion_find("qn01");
        royal_mot[2] = motion_find("qn02"); royal_mot[3] = motion_find("kg00");
    }
    if (e->kind == ENEMY_KING) { mot = royal_mot[3]; loop = id <= 2; }
    else { mot = royal_mot[id == 7 ? 2 : id == 4 || id == 8 ? 1 : 0]; loop = 0; }
    royal_loop[slot] = loop;
    if (mot < 0) return;
    if (mot != e->motion || restart) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 8.0f;
        e->motion = mot; e->frame = 0.0f; e->done = 0;
    }
}
static void royal_spawn_glue(void) {
    RoyalBody *b = royal_body();
    int slot = (int)(e - enemies), k;
    for (k = 0; k < 4; ++k) royal_fx[slot][k] = 0;
    e->motion = -1;
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw; b->floor_y = e->pos[1];
    {
        ChessPiece *p = enemy_chess_actor();
        memset(p, 0, sizeof *p);
        p->est = e->kind == ENEMY_QUEEN ? 39 : 40;
        p->alternate_percent = chess_alternate_chance(p->est, 0);
    }
    if (e->kind == ENEMY_QUEEN) queen_spawn(&queen_brain[slot], (float)e->health, 0);
    else king_spawn(&king_brain[slot], (float)e->health, 0);
    royal_play(b->motion, 1);
    b->restart = 0;
    e->fade = 0.0f;
    e->timer = 0.0f;
}
static void royal_events(RoyalBody *b, float px, float py, float pz) {
    int slot = (int)(e - enemies);
    float at[3] = {b->pos[0], b->pos[1], b->pos[2]};
    float bone[3] = {b->pos[0], b->pos[1] + 150.0f, b->pos[2]};   /* [stand-in] +0x504 +0x104's bone isn't read */
    unsigned ev = b->events;
    (void)px; (void)py; (void)pz;
    if (ev & ROYAL_EV_LAND) nfx_spawn_p(NFX_STATIC, 44, at, b->yaw, 0);   /* FUN_0040a2a0's P44 */
    if (ev & ROYAL_EV_CASTLE) { royal_fx_kill(3); royal_fx[slot][3] = nfx_spawn(NFX_CHESS, 0x390, at, b->yaw, 1); }
    if (ev & ROYAL_EV_CASTLE_DOWN) { royal_fx_kill(3); nfx_spawn(NFX_CHESS, 0x1fa, at, b->yaw, 0); }
    if (b->action != ROYAL_CASTLE) royal_fx_kill(3);
    else if (royal_fx[slot][3] > 0) nfx_move(royal_fx[slot][3], at, b->yaw);
    if (e->kind == ENEMY_QUEEN) {
        if (ev & QUEEN_EV_WINDUP) {
            royal_fx_kill(0); royal_fx_kill(1);
            royal_fx[slot][0] = nfx_spawn(NFX_ROYAL, 0x31b, bone, b->yaw, 1);
            royal_fx[slot][1] = nfx_spawn(NFX_ROYAL, 0x242, at, b->yaw, 1);   /* [stand-in] not stretched (1, 1, 30) */
        }
        if (ev & QUEEN_EV_CHARGE) { royal_fx_kill(2); royal_fx[slot][2] = nfx_spawn(NFX_CHESS, 0x390, bone, b->yaw, 1); }
        if ((ev & QUEEN_EV_CHARGE_END) || b->action != ROYAL_ATTACK) { royal_fx_kill(0); royal_fx_kill(1); royal_fx_kill(2); }
        if (royal_fx[slot][0] > 0) nfx_move(royal_fx[slot][0], bone, b->yaw);
        if (royal_fx[slot][1] > 0) nfx_move(royal_fx[slot][1], at, b->yaw);
        if (royal_fx[slot][2] > 0) nfx_move(royal_fx[slot][2], bone, b->yaw);
    } else {
        const KingBlast *x = &king_brain[slot].blast;
        float foot[3] = {x->pos[0], x->pos[1], x->pos[2]};
        if (ev & KING_EV_CHARGE) { royal_fx_kill(0); royal_fx[slot][0] = nfx_spawn(NFX_ROYAL, 0x36c, foot, b->yaw, 1); }
        if (ev & KING_EV_BLAST) { royal_fx_kill(0); royal_fx[slot][0] = nfx_spawn(NFX_ROYAL, 0x233, foot, b->yaw, 1); }
        if ((ev & KING_EV_BLAST_END) || !x->active) royal_fx_kill(0);
        else if (royal_fx[slot][0] > 0) nfx_move(royal_fx[slot][0], foot, b->yaw);
    }
}
static int royal_command(int command);   /* the chess master's +0x488 for this piece (chess.c), in and back out */
/* Tests: each action change and event, in the FOCUS_DONE report (enemy_giga_log). */
static float royal_t[ENEMY_MAX];
static int royal_last[ENEMY_MAX];
static void royal_note(const RoyalBody *b, float px, float pz) {
    int slot = (int)(e - enemies);
    float dx = px - b->pos[0], dz = pz - b->pos[2];
    if ((b->action == royal_last[slot] && !b->events) || giga_log_len >= (int)sizeof(giga_log_text) - 120) return;
    royal_last[slot] = b->action;
    giga_log_len += snprintf(giga_log_text + giga_log_len, sizeof(giga_log_text) - giga_log_len,
                             "%s t=%.0f action=%d sub=%d ev=%#x at %.0f %.0f %.0f d=%.0f hp=%d\n",
                             e->kind == ENEMY_QUEEN ? "queen" : "king", royal_t[slot], b->action, b->sub, b->events,
                             b->pos[0], b->pos[1], b->pos[2], sqrtf(dx * dx + dz * dz), e->health);
}
static void royal_update_glue(float f, float px, float py, float pz) {
    RoyalBody *b = royal_body();
    int slot = (int)(e - enemies), k;
    float g, old[3];
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->motion >= 0) {
        const Motion *m = motion_get(e->motion);
        e->frame += f;
        if (e->frame >= m->end_frame) { e->done = 1; e->frame = royal_loop[slot] ? motion_wrap(m, e->frame) : m->end_frame; }
    }
    if (e->plan == P_DEATH) {
        /* [stand-in] the death (CEm017's 0x406d50) isn't read: it sinks 60 frames and goes. */
        e->timer += f;
        e->pos[1] -= 2.0f * f;
        for (k = 0; k < 4; ++k) royal_fx_kill(k);
        if (e->timer > 60.0f) e->active = 0;
        return;
    }
    old[0] = b->pos[0] = e->pos[0]; old[1] = b->pos[1] = e->pos[1]; old[2] = b->pos[2] = e->pos[2];
    b->yaw = e->yaw;
    b->frame = e->frame; b->done = e->done; b->hp = (float)e->health;
    if (floor_under(e->pos[0], e->pos[1] + 100.0f, e->pos[2], &g)) b->floor_y = e->floor_y = g;
    {
        ChessPiece *p = enemy_chess_actor();
        /* the King gone: the master's death cascade (+0x3bc bit 0x8000) */
        if (p->kill) {
            p->kill = 0; e->health = 0; e->plan = P_DEATH; e->timer = 0.0f; p->busy = 0;
            for (k = 0; k < 4; ++k) royal_fx_kill(k);
            died();
            return;
        }
        if (p->command == ROYAL_CMD_CASTLE) { b->dest[0] = p->destination[0]; b->dest[1] = p->destination[1]; b->dest[2] = p->destination[2]; }
    }
    if (e->kind == ENEMY_QUEEN) royal_command(queen_think(&queen_brain[slot], f, px, py, pz, royal_command(-1), ai_unit));
    else royal_command(king_think(&king_brain[slot], f, px, py, pz, royal_command(-1), ai_unit));
    /* [stand-in] walls: the room's floor ends them (no floor under the new place puts the piece back and stops a
     * charge), not the collision's +0xcac flags. */
    b->wall = 0;
    if (!floor_under(b->pos[0], b->pos[1] + 300.0f, b->pos[2], &g)) {
        b->pos[0] = old[0]; b->pos[2] = old[2]; b->wall = 1;
    }
    e->pos[0] = b->pos[0]; e->pos[1] = b->pos[1]; e->pos[2] = b->pos[2]; e->yaw = b->yaw;
    e->air = e->pos[1] > e->floor_y + 5.0f;
    /* For the master: the castle is busy (+0x110 0x48000000); the royals never queue for a turn (their wait ends in
     * 004095f0 on its own, not sub-state 2's grant). */
    { ChessPiece *p = enemy_chess_actor(); p->busy = b->busy; p->swap_blocked = b->busy; p->waiting = 0; }
    if (b->restart || e->done) { royal_play(b->motion, b->restart); b->restart = 0; }
    royal_events(b, px, py, pz);
    royal_t[slot] += f;
    royal_note(b, px, pz);
    /* Their hits on Dante: the Queen's charge capsules (motion 7), the King's blast. */
    {
        int live = e->kind == ENEMY_QUEEN ? queen_brain[slot].hitting : king_brain[slot].blast.hitting;
        if (!live) e->hit_done = 0;
        else if (!e->hit_done && attacks_on) {
            int reach = e->kind == ENEMY_QUEEN ? queen_reaches(&queen_brain[slot], px, py, pz)
                                               : king_blast_reaches(&king_brain[slot].blast, px, py, pz);
            if (reach) {
                e->hit_done = 1;
                land_attack(1);
                landed_damage = e->kind == ENEMY_QUEEN ? QUEEN_DAMAGE : KING_DAMAGE;
            }
        }
    }
}
static void royal_hit_glue(HitKind kind, float from_x, float from_z) {
    RoyalBody *b = royal_body();
    float at[3] = {e->pos[0], e->pos[1] + 120.0f, e->pos[2]};
    int heavy = !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL), k;
    fx_hit_spark(at);
    if (e->plan == P_DEATH) return;
    if (mortal) {
        e->health = (short)(e->health - (heavy ? 2 : 1));
        if (e->health <= 0) {
            e->health = 0;
            e->plan = P_DEATH;
            e->timer = 0.0f;
            enemy_chess_actor()->busy = 0;
            for (k = 0; k < 4; ++k) royal_fx_kill(k);
            died();
            return;
        }
    }
    b->hp = (float)e->health;
    /* 00407fe8: native kind 3 the light knock, 4 / 7 the heavy one [the port's kinds mapped by their reactions] */
    royal_hit(b, kind == HIT_LAUNCH, kind == HIT_HEAVY || kind == HIT_SLAM, from_x, from_z);
    e->pos[0] = b->pos[0]; e->pos[2] = b->pos[2];
}
/* The chess master's command for this piece (chess.c's actor +0x488): -1 reads it, else writes back what the brain
 * left (cleared when taken). Tests' "royalcmd" lines are handed in as if the master gave them. */
static int royal_test_cmd[ENEMY_MAX];
static int royal_command(int command) {
    int slot = (int)(e - enemies);
    ChessPiece *p = enemy_chess_actor();
    if (command < 0) {
        if (royal_test_cmd[slot]) { p->command = royal_test_cmd[slot]; royal_test_cmd[slot] = 0; }
        return p->command;
    }
    p->command = command;
    return command;
}
void enemy_royal_command(int command) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && (enemies[i].kind == ENEMY_QUEEN || enemies[i].kind == ENEMY_KING)) royal_test_cmd[i] = command;
}
