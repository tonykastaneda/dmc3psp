#ifndef DMC3PSP_TESTROOM_MOTION_H
#define DMC3PSP_TESTROOM_MOTION_H

/* DMC3 MOT playback (format: tools/mot_ps2.py). Motions are authored at
 * 60 frames per second. Channels 3-5 are X/Y/Z rotation in radians applied
 * X first (R = Rz * Ry * Rx); channels 6-8 replace the bone's rest offset.
 * Bone 0 translation is root motion. */

#define MOTION_MAX_BONES 24
#define MOTION_MAX_CHANNELS 80
#define MOTION_FPS 60.0f

#define MOTION_IDLE 0  /* "2-00" */
#define MOTION_RUN 2   /* "2-02": 48-frame loop, 408 cm of root motion */

typedef struct {
    unsigned short bone, channel, key_count, kind; /* kind 3 = Hermite, 2 = linear */
    float params[6];             /* value / in / out tangent: base, scale */
    const unsigned short *keys;  /* Hermite: {frame, v, in, out}; linear: {frame, v} */
} MotionChannel;

typedef struct {
    const char *name;
    float loop_start, end_frame;   /* header +0x08 / +0x0C: the motion's first / last frame */
    /* Header +0x10 / +0x14 / +0x18: loop range and loop flag. A weapon's motions
     * share one frame timeline (rb-04 runs 23-127, Million Stab rb-08 80-149
     * looping 104-149), and motion scripts address these absolute frames. */
    float loop_from, loop_to;
    int looping;
    int channel_count;
    MotionChannel channels[MOTION_MAX_CHANNELS];
} Motion;

/* Per-bone sampled values; has_* flags say which channels the motion drives. */
typedef struct {
    float rot[MOTION_MAX_BONES][3];
    float trans[MOTION_MAX_BONES][3];
    unsigned char has_rot[MOTION_MAX_BONES][3];
    unsigned char has_trans[MOTION_MAX_BONES][3];
} MotionSample;

int motion_init(void);           /* parses every embedded MOT; returns count, <0 on error */
int motion_count(void);
int motion_find(const char *name);  /* index by name, -1 if absent */
const Motion *motion_get(int index);
/* Frame with looping: past end_frame wraps into [loop_start, end_frame). */
float motion_wrap(const Motion *motion, float frame);
void motion_sample(const Motion *motion, float frame, MotionSample *out);
/* Bone 0 translation at frame (root motion), for speed matching / viewer. */
void motion_root(const Motion *motion, float frame, float out[3]);

#endif
