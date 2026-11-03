#ifndef DMC3PSP_TESTROOM_SWIZZLE_H
#define DMC3PSP_TESTROOM_SWIZZLE_H

#include <malloc.h>
#include <psputils.h>
#include <string.h>

/* Rearranges a 32-bit texture in place into the GE's swizzled layout
 * (16-byte x 8-row blocks, row-major), which the GE reads far faster from
 * main RAM. Bind it with sceGuTexMode(GU_PSM_8888, ..., 1). Needs
 * width % 4 == 0 and height % 8 == 0; returns 0 (texture untouched) if not. */
static inline int swizzle_texture32(void *texels, int width, int height) {
    unsigned int row = (unsigned int)width * 4u, bx, by, y;
    unsigned char *src = texels, *tmp, *out;
    if (width % 4 || height % 8) return 0;
    tmp = malloc(row * (unsigned int)height);
    if (!tmp) return 0;
    out = tmp;
    for (by = 0; by < (unsigned int)height / 8u; ++by)
        for (bx = 0; bx < row / 16u; ++bx)
            for (y = 0; y < 8u; ++y) {
                memcpy(out, src + (by * 8u + y) * row + bx * 16u, 16);
                out += 16;
            }
    memcpy(src, tmp, row * (unsigned int)height);
    free(tmp);
    sceKernelDcacheWritebackRange(src, row * (unsigned int)height);
    return 1;
}

#endif
