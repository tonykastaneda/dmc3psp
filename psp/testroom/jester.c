/* The Jester's decoded pieces (CEm044, SE; docs/ENEMY_SPECS.md E). */
#include "jester.h"

#include <math.h>

float jester_max_health(int encounter) {
    static const float health[3] = {5000.0f, 6500.0f, 10000.0f};   /* 0x5e2f60, a stride of 8 */
    return health[encounter >= 0 && encounter < 3 ? encounter : 0];
}

/* 0x5e2e40: {about Dante?, radius, random spread} a row. */
static const struct { unsigned char dante; float radius, spread; } rows[8] = {
    {0, 0.0f, 50.0f}, {1, 50.0f, 500.0f}, {1, 200.0f, 1000.0f}, {1, 50.0f, 250.0f},
    {1, 100.0f, 900.0f}, {1, 150.0f, 600.0f}, {1, 100.0f, 400.0f}, {1, 150.0f, 600.0f},
};

void jester_reappear(int row, float px, float pz, float *x, float *z, float (*unit)(void)) {
    float cx = rows[row & 7].dante ? px : *x, cz = rows[row & 7].dante ? pz : *z;
    float r = rows[row & 7].radius + rows[row & 7].spread * unit();
    float a = unit() * 6.2831853f;
    int i;
    for (i = 0; i <= 0x23; ++i, a += 0.17453292f) {
        float tx = cx + sinf(a) * r, tz = cz + cosf(a) * r;
        *x = tx; *z = tz;
        if ((tx - 3500.0f) * (tx - 3500.0f) + (tz - 3500.0f) * (tz - 3500.0f) < 1000.0f * 1000.0f) return;
    }
}
