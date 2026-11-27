#include "near_clip.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psputils.h>

typedef struct { float u, v, nx, ny, nz, x, y, z; } ClipVertex;

#define NEAR_CLIP_MAX_VERTICES 8192

static float mz[4];      /* the model-view matrix's z row: a point's view-space z = mz . (x, y, z, 1) */
static float plane;      /* clip just in front of the GE's near plane (its depth, a little more) */
static int stat_tested, stat_clipped, stat_added;
/* Off by default: in PPSSPP the GE draws near-crossing triangles clipped already, and the clipper cost about 2 ms a
 * frame with the camera against Dante for no visible change. Test "nearclip 1" turns it on, to check on hardware
 * (not available this run) whether the real GE drops them. */
int near_clip_off = 1;

/* The prepared model: each vertex's depth once (near_clip_prepare), and whether any crosses at all. */
static const void *cached;
static int cached_count, cached_crossing;
static float depths[NEAR_CLIP_MAX_VERTICES];

void near_clip_begin(void) {
    ScePspFMatrix4 model, view, proj, mv;
    sceGumMatrixMode(GU_MODEL); sceGumStoreMatrix(&model);
    sceGumMatrixMode(GU_VIEW); sceGumStoreMatrix(&view);
    sceGumMatrixMode(GU_PROJECTION); sceGumStoreMatrix(&proj);
    sceGumMatrixMode(GU_MODEL);
    gumMultMatrix(&mv, &view, &model);
    mz[0] = mv.x.z; mz[1] = mv.y.z; mz[2] = mv.z.z; mz[3] = mv.w.z;
    /* sceGumPerspective: z.z = (f + n) / (n - f), w.z = 2fn / (n - f), so n = w.z / (z.z - 1). */
    plane = (proj.z.z != 1.0f ? proj.w.z / (proj.z.z - 1.0f) : 10.0f) * 1.05f;
    cached = 0;
}

static float depth3(float x, float y, float z) {   /* distance in front of the eye (the view looks down -z) */
    return -(mz[0] * x + mz[1] * y + mz[2] * z + mz[3]);
}

int near_clip_sphere(const float centre[3], float radius) {
    float d;
    ++stat_tested;
    if (near_clip_off) return 0;
    d = depth3(centre[0], centre[1], centre[2]);
    return d - radius < plane && d + radius > plane;
}

int near_clip_prepare(const void *vertices, int count) {
    const ClipVertex *v = (const ClipVertex *)vertices;
    int i, before = 0, after = 0;
    cached = 0;
    if (count > NEAR_CLIP_MAX_VERTICES) return 1;   /* too big to cache: near_clip_draw works it out itself */
    for (i = 0; i < count; ++i) {
        float d = depth3(v[i].x, v[i].y, v[i].z);
        depths[i] = d;
        if (d >= plane) ++after; else ++before;
    }
    cached = vertices; cached_count = count;
    cached_crossing = before && after;   /* all on one side: no triangle can cross */
    return cached_crossing;
}

static void lerp(ClipVertex *out, const ClipVertex *a, const ClipVertex *b, float t) {
    const float *pa = &a->u, *pb = &b->u;
    float *po = &out->u;
    int k;
    for (k = 0; k < 8; ++k) po[k] = pa[k] + (pb[k] - pa[k]) * t;
}

int near_clip_draw(const void *vertices, const unsigned short *indices, int count, int vtype) {
    const ClipVertex *v = (const ClipVertex *)vertices;
    int i, crossing = 0, n = 0, use_cache = vertices == cached;
    ClipVertex *out;
    if (use_cache && !cached_crossing) return 0;
#define DEPTH(ix) (use_cache && (int)(ix) < cached_count ? depths[ix] : depth3(v[ix].x, v[ix].y, v[ix].z))
    /* First pass: how many cross (each gives at most two triangles). */
    for (i = 0; i + 2 < count; i += 3) {
        int in = (DEPTH(indices[i]) >= plane) + (DEPTH(indices[i + 1]) >= plane) + (DEPTH(indices[i + 2]) >= plane);
        crossing += in == 1 || in == 2;
    }
    if (!crossing) return 0;
    out = (ClipVertex *)sceGuGetMemory((int)(sizeof(ClipVertex) * 6 * (unsigned int)crossing));
    for (i = 0; i + 2 < count; i += 3) {
        const ClipVertex *t[3] = {&v[indices[i]], &v[indices[i + 1]], &v[indices[i + 2]]};
        float d[3] = {DEPTH(indices[i]), DEPTH(indices[i + 1]), DEPTH(indices[i + 2])};
        ClipVertex poly[4];
        int k, m = 0, in = (d[0] >= plane) + (d[1] >= plane) + (d[2] >= plane);
        if (in == 0 || in == 3) continue;
        /* Sutherland-Hodgman against the one plane, keeping the winding. */
        for (k = 0; k < 3; ++k) {
            int j = (k + 1) % 3;
            int ki = d[k] >= plane, ji = d[j] >= plane;
            if (ki) poly[m++] = *t[k];
            if (ki != ji) lerp(&poly[m++], t[k], t[j], (plane - d[k]) / (d[j] - d[k]));
        }
        out[n++] = poly[0]; out[n++] = poly[1]; out[n++] = poly[2];
        if (m == 4) { out[n++] = poly[0]; out[n++] = poly[2]; out[n++] = poly[3]; }
    }
#undef DEPTH
    sceKernelDcacheWritebackRange(out, sizeof(ClipVertex) * (unsigned int)n);
    sceGumDrawArray(GU_TRIANGLES, vtype, n, 0, out);
    ++stat_clipped;
    stat_added += n / 3;
    return n / 3;
}

void near_clip_stats(int *tested, int *clipped, int *added) {
    *tested = stat_tested; *clipped = stat_clipped; *added = stat_added;
    stat_tested = stat_clipped = stat_added = 0;
}
