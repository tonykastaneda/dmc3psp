#include "difficulty.h"

/*
 * Behavior adapted from WadeWinningWilson/dmc3-decomp (CC0), Difficult.c.
 * Original routine addresses are retained for cross-version verification.
 */

void dmc3_difficulty_gauge_init(float *p) { /* func_00414D10 */
    float v;
    p[2] = 100.0f;
    p[1] = 100.0f;
    p[3] = 0.001f;
    p[4] = 110.0f;
    v = p[2] - 100.0f;
    p[5] = v;
    if (v < 0.0f) {
        p[5] = 0.0f;
    }
}

void dmc3_difficulty_clear(void *state) { /* func_00414BA0 */
    char *p = state;
    *(int *)(p + 4) = 0;
    *(int *)(p + 8) = 0;
    *(int *)(p + 12) = 0;
    *(int *)(p + 16) = 0;
    *(int *)(p + 20) = 0;
}

void dmc3_difficulty_pair(void *destination, const void *source) { /* func_00414CC0 */
    char *p0 = destination;
    const char *p1 = source;
    float v0 = 0.0f;
    *(float *)(p0 + 4) = *(const float *)(p1 + 0);
    *(float *)(p0 + 8) = *(const float *)(p1 + 0);
    *(float *)(p0 + 12) = *(const float *)(p1 + 8);
    *(float *)(p0 + 16) = *(const float *)(p1 + 4);
    *(float *)(p0 + 20) = (*(float *)(p0 + 8) - 100.0f);
    if (*(float *)(p0 + 20) < v0) {
        *(float *)(p0 + 20) = v0;
    }
}

void dmc3_difficulty_clamp(void *state) { /* func_00414D60 */
    char *p0 = state;
    float v0;
    *(float *)(p0 + 8) = (*(float *)(p0 + 8) - 20.0f);
    v0 = *(float *)(p0 + 20);
    if (*(float *)(p0 + 8) < v0) {
        *(float *)(p0 + 8) = v0;
    }
    *(float *)(p0 + 4) = *(float *)(p0 + 8);
}

float dmc3_difficulty_read(const void *state) { /* func_00414DA0 */
    return *(const float *)((const char *)state + 4);
}

int dmc3_gameplay_self_test(void) {
    float gauge[7];
    float state[6];
    float input[3];
    int i;

    dmc3_difficulty_gauge_init(gauge);
    if (gauge[1] != 100.0f || gauge[2] != 100.0f || gauge[3] != 0.001f ||
        gauge[4] != 110.0f || gauge[5] != 0.0f) {
        return 0;
    }

    for (i = 0; i < 6; ++i) {
        state[i] = 1.0f;
    }
    dmc3_difficulty_clear(state);
    for (i = 1; i < 6; ++i) {
        if (state[i] != 0.0f) {
            return 0;
        }
    }

    input[0] = 150.0f;
    input[1] = 25.0f;
    input[2] = 75.0f;
    dmc3_difficulty_pair(state, input);
    if (state[1] != 150.0f || state[2] != 150.0f || state[3] != 75.0f ||
        state[4] != 25.0f || state[5] != 50.0f) {
        return 0;
    }

    dmc3_difficulty_clamp(state);
    if (state[2] != 130.0f || dmc3_difficulty_read(state) != 130.0f) {
        return 0;
    }
    state[2] = 40.0f;
    dmc3_difficulty_clamp(state);
    if (state[2] != 50.0f || dmc3_difficulty_read(state) != 50.0f) {
        return 0;
    }
    return 1;
}
