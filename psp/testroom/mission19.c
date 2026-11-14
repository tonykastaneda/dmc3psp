#include "mission19.h"

/* 404, Infinity Nirvana: with the Samsara taken, the large mirror that appears takes Dante out by 403 (Lost Soul
 * Nirvana) and 406's mirror back to 405, where it's used. The port takes him straight to 405's start 0
 * [approximation: 403 and 406 are only passed through]. */
int m19_door_route(int room, int door, int has_samsara, int *next_room, int *start) {
    (void)door;
    if (room == 404 && has_samsara) { *next_room = 405; *start = 0; return 1; }
    return 0;
}
