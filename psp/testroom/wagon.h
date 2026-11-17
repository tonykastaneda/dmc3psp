#ifndef DMC3PSP_TESTROOM_WAGON_H
#define DMC3PSP_TESTROOM_WAGON_H
/* Room 216, the Temperance Wagon's ride (Missions 11 and 12): its TRUCK set's track turning past the wagon. */
void wagon_enter(int room, int backward);   /* backward: riding from 240 back toward 239 */
void wagon_update(float dt);
float wagon_angle(void);                    /* for tests: radians the track has turned */
#endif
