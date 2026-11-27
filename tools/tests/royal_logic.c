/* Host checks for the Damned Queen and King (psp/testroom/queen.c, king.c; docs/CHESS_ROYALS_SPEC.md). */
#include "queen.h"
#include "king.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); ++fails; } } while (0)
static float half(void) { return 0.5f; }
static float low(void) { return 0.1f; }

static void king_tests(void) {
    KingBrain k = {0};
    int n, cmd, moved = 0, blasts = 0, hit_frames = 0, attack_at = -1, blast_at = -1;
    k.body.pos[0] = 0; k.body.pos[2] = 0; k.body.floor_y = 0;
    king_spawn(&k, KING_MAX_HP, 0);
    /* Dante far away along +x: after the wait (320 + 30 at distance <= 1000, 3 a frame) the King steps 200 toward him */
    for (n = 0; n < 400 && !moved; ++n) {
        king_think(&k, 1.0f, 3000.0f, 0.0f, 0.0f, 0, half);
        if (k.body.action == ROYAL_WAIT && k.body.pos[0] > 1.0f) moved = n;
    }
    CHECK(moved, "the King never moved");
    CHECK(fabsf(k.body.pos[0] - 200.0f) < 1.0f && fabsf(k.body.pos[2]) < 0.01f, "one square: x %.1f z %.1f", k.body.pos[0], k.body.pos[2]);
    CHECK(k.body.events & ROYAL_EV_LAND, "landing event");
    /* the master's 20 starts the attack; the blast object spawns at motion frame 100-102 */
    cmd = king_think(&k, 1.0f, 3000.0f, 0.0f, 0.0f, ROYAL_CMD_RESPOND, half);
    CHECK(cmd == 0 && k.body.action == ROYAL_ATTACK, "attack on 20: action %d cmd %d", k.body.action, cmd);
    for (n = 0; n < 400; ++n) {
        k.body.frame = (float)n; k.body.done = n == 150;
        king_think(&k, 1.0f, 3000.0f, 0.0f, 0.0f, 0, half);
        if (k.body.events & KING_EV_CHARGE) attack_at = n;
        if (k.body.events & KING_EV_BLAST) { blast_at = n; ++blasts; }
        if (king_blast_reaches(&k.blast, 1200.0f, 0.0f, 0.0f)) ++hit_frames;
    }
    CHECK(attack_at == 102, "blast object starts at frame %d", attack_at);   /* spawned at 101, its update next frame */
    CHECK(blasts == 1 && blast_at - attack_at == 61, "blast %d frames after the charge", blast_at - attack_at);
    CHECK(hit_frames == 28,   /* 179 -> 178 is the first: the count drops before the check, as 0x43ebe0 */
           "hits for %d frames", hit_frames);
    CHECK(!king_blast_reaches(&k.blast, 1600.0f, 0.0f, 0.0f), "outside 1500");
    /* hurt with Dante within 500: attacks at once from the wait */
    king_spawn(&k, KING_MAX_HP, 0);
    king_think(&k, 1.0f, 400.0f, 0.0f, 0.0f, 0, half);
    k.body.hp -= 100.0f;
    king_think(&k, 1.0f, 400.0f, 0.0f, 0.0f, 0, half);
    CHECK(k.body.action == ROYAL_ATTACK, "hurt near: action %d", k.body.action);
}

static void queen_tests(void) {
    QueenBrain q = {0};
    int n, cmd, rose = 0, landed = 0, windup = -1, charge = -1, end = -1;
    float x0;
    q.body.floor_y = 0;
    queen_spawn(&q, QUEEN_MAX_HP, 0);
    /* Dante at (1000, 600): pick 0.1 = the first rook point (Dante's x, her z) -> straight along +x */
    for (n = 0; n < 600 && !landed; ++n) {
        queen_think(&q, 1.0f, 1000.0f, 0.0f, 600.0f, 0, low);
        if (q.body.pos[1] > 39.0f) rose = 1;
        if (q.body.events & ROYAL_EV_LAND) landed = n;
    }
    CHECK(rose, "she rises 40 cm first");
    CHECK(landed, "she never landed");
    CHECK(fabsf(q.body.pos[2]) < 0.01f && q.body.pos[0] > 1000.0f, "rook move along +x: x %.0f z %.0f", q.body.pos[0], q.body.pos[2]);
    /* the charge on 20: winds up 60 frames, charges 35-50 cm a frame toward the nearest line */
    q.body.pos[0] = 0; q.body.pos[2] = 0; q.body.yaw = 0;
    cmd = queen_think(&q, 1.0f, 500.0f, 0.0f, 300.0f, ROYAL_CMD_RESPOND, half);
    CHECK(cmd == 0 && q.body.action == ROYAL_ATTACK, "charge on 20: action %d cmd %d", q.body.action, cmd);
    x0 = 0;
    {
        float z0 = 0;
    for (n = 0; n < 400 && q.body.action == ROYAL_ATTACK; ++n) {
        queen_think(&q, 1.0f, 500.0f, 0.0f, 300.0f, 0, half);
        if (q.body.events & QUEEN_EV_WINDUP) windup = n;
        if (q.body.events & QUEEN_EV_CHARGE) { charge = n; x0 = q.body.pos[0]; z0 = q.body.pos[2]; }
        if (q.body.events & QUEEN_EV_CHARGE_END) end = n;
        if (q.hitting) {
            float ahead[3] = {q.body.pos[0] + sinf(q.body.yaw) * 60.0f, 0.0f, q.body.pos[2] + cosf(q.body.yaw) * 60.0f};
            CHECK(queen_reaches(&q, ahead[0], q.body.pos[1], ahead[2]), "her charge reaches 60 ahead");
            CHECK(!queen_reaches(&q, ahead[0] + 400.0f, q.body.pos[1], ahead[2]), "not 400 aside");
        } else CHECK(n > end || !queen_reaches(&q, q.body.pos[0], q.body.pos[1], q.body.pos[2]), "no hits off the charge");
    }
    CHECK(windup > 0 && charge - windup == 60, "wind-up %d frames", charge - windup);
    CHECK(end - charge == 75, "charge lasts %d frames (50 + 50 x 0.5)", end - charge);
    {
        float run = hypotf(q.body.pos[0] - x0, q.body.pos[2] - z0);
        CHECK(run > 75 * 35.0f && run < 75 * 50.0f + 1.0f, "charged %.0f", run);
    }
    }
    /* the nearest of the four line points to Dante (500, 300) is the bishop point (400, 400): the diagonal */
    CHECK(fabsf(q.body.yaw - 3.14159265f / 4.0f) < 0.01f, "faces the diagonal: yaw %.2f", q.body.yaw);
    /* a launcher knocks her only while she waits */
    queen_spawn(&q, QUEEN_MAX_HP, 0);
    royal_hit(&q.body, 1, 0, 0.0f, -300.0f);
    CHECK(q.body.action == ROYAL_KNOCK, "launch knock: %d", q.body.action);
    for (n = 0; n < 200 && q.body.action != ROYAL_WAIT; ++n) queen_think(&q, 1.0f, 0.0f, 0.0f, -300.0f, 0, half);
    CHECK(q.body.action == ROYAL_WAIT && q.body.pos[2] > 1.0f, "knocked away from Dante: z %.1f", q.body.pos[2]);
}

int main(void) {
    king_tests();
    queen_tests();
    if (fails) { printf("%d failures\n", fails); return 1; }
    printf("royal logic: all checks passed\n");
    return 0;
}
