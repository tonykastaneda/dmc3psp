/* Room 216, the Temperance Wagon's ride, from its TRUCK set (CStageSetTruck, vtable 0x610b30; parsed by
 * FUN_00485c60 / FUN_00485ff0, run by FUN_004865e0 / FUN_004867b0; docs/GAME_FLOW.md). The wagon stands still; the
 * track's parts (model 0, a 90-degree arc each) turn about its base (10000, 0, 0) at speed / radius a frame
 * (40 / 12350, +0xf4 / +0xf0), the next part coming in as one passes. Gimmick command 0c 100 sets the way (0 entering
 * from 239, 1 from 240). tools/prepare_stage.py bakes three parts 90 degrees apart (tags 224-226); turning them all by
 * the ride's angle within a part (they're the same model) looks as the game's chain does. */
#include "wagon.h"

#include "stage.h"

#define WAGON_TAG 224
#define HALF_PI 1.5707963f

static int on;
static float turn, total, way;

static void pose(void) {
    static const float pivot[3] = {10000.0f, 0.0f, 0.0f}, move[3] = {0.0f, 0.0f, 0.0f};
    float t[3] = {0.0f, 0.0f, 0.0f};
    int k;
    t[1] = turn;
    for (k = 0; k < 3; ++k) stage_set_group_motion(WAGON_TAG + k, pivot, move, t);
}

void wagon_enter(int room, int backward) {
    on = room == 216;
    turn = total = 0.0f;
    way = backward ? -1.0f : 1.0f;
    if (on) pose();
}

void wagon_update(float dt) {
    float step;
    if (!on) return;
    step = way * 40.0f / 12350.0f * dt * 60.0f;
    turn += step; total += step;
    while (turn > HALF_PI * 0.5f) turn -= HALF_PI;
    while (turn < -HALF_PI * 0.5f) turn += HALF_PI;
    pose();
}

float wagon_angle(void) { return total; }
