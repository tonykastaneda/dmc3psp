#include "mission12.h"

#include "geryon_boss.h"

/* 216's script (EventTbl12): after the waves (groups 2, 3, 4) the ride ends with goto_room 239, start 3. */
int m12_room_done(int room, int *next_room, int *start) {
    if (room != 216) return 0;
    *next_room = 239;
    *start = 3;
    return 1;
}

/* 228: at 80% Geryon runs off the bridge, it falls (demo 33, not played) and the script puts Dante at start 2. */
int m12_warp(int room, float out[4]) {
    if (room != 228) return 0;
    return geryon_boss_take_warp(out);
}
