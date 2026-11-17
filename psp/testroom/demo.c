/* The game's real-time cutscenes ("demos"; docs/CUTSCENES.md), as tools/prepare_demo.py bakes them: DEMO_nnn.BIN
 * holds the camera per demo frame (the camera track's clips, each a .cam played by CCameraClip / CCameraMotion:
 * eye = channels 6-8 + 0-2, target = 9-11 + 0-2, field of view = 12), DSTG_nnn.BIN the demo's stage built as a room.
 * The demo's clock runs at 60 frames a second. Not yet: the characters (models, motions, hide tracks), effects,
 * messages, fades and sound. */
#include "demo.h"

#include "stage.h"

#include <malloc.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

typedef struct { char magic[4]; unsigned int frames, camera, reserved[5]; } DemoHeader;
typedef struct { float eye[3], target[3], fov, roll; } DemoCamera;

static unsigned char *blob;
static const DemoHeader *head;
static float clock;
static int playing, id = -1;

static void *read_file(const char *path, int *size_out) {
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int size, got;
    void *data;
    if (fd < 0) return 0;
    size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    data = memalign(64, (size_t)size);
    if (!data) { sceIoClose(fd); return 0; }
    got = sceIoRead(fd, data, (unsigned int)size);
    sceIoClose(fd);
    if (got != size) { free(data); return 0; }
    if (size_out) *size_out = size;
    return data;
}

int demo_start(int demo, const char *dir) {
    char path[256];
    int size = 0;
    demo_stop();
    snprintf(path, sizeof path, "%s/DEMO_%03d.BIN", dir, demo);
    blob = read_file(path, &size);
    if (!blob) return 0;
    head = (const DemoHeader *)blob;
    if (size < (int)sizeof(DemoHeader) || memcmp(head->magic, "DMO1", 4) != 0 ||
        head->camera + head->frames * sizeof(DemoCamera) > (unsigned int)size) {
        demo_stop();
        return 0;
    }
    snprintf(path, sizeof path, "%s/DSTG_%03d.BIN", dir, demo);
    stage_load(path);   /* a demo without its own stage plays over the room's */
    clock = 0.0f;
    playing = 1;
    id = demo;
    return 1;
}

void demo_stop(void) {
    if (blob) free(blob);
    blob = 0;
    head = 0;
    playing = 0;
    id = -1;
}

int demo_active(void) { return playing; }
int demo_id(void) { return id; }
float demo_frame(void) { return clock; }

int demo_update(float dt, FollowCamera *camera) {
    const DemoCamera *cams, *c;
    unsigned int f;
    if (!playing) return 0;
    clock += dt * 60.0f;
    if (clock >= (float)head->frames) { playing = 0; return 0; }
    cams = (const DemoCamera *)(blob + head->camera);
    f = (unsigned int)clock;
    c = &cams[f];
    camera->x = c->eye[0]; camera->y = c->eye[1]; camera->z = c->eye[2];
    camera->tx = c->target[0]; camera->ty = c->target[1]; camera->tz = c->target[2];
    camera->fov = c->fov * (3.14159265f / 180.0f);   /* [inferred: vertical, as the port's] */
    camera->roll = 0.0f;
    return 1;
}
