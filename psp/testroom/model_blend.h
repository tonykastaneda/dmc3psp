#ifndef DMC3_MODEL_BLEND_H
#define DMC3_MODEL_BLEND_H
/* A model object's blend (Dmc3ModelObject.blend: its MOD record's byte 8, an index into the game's GS ALPHA table
 * at 0x566ec0 [inferred]): 2 adds (Cs*As + Cd: eye glows, Cerberus's ice), 3 subtracts (Cd - Cs*As: Arkham's veins,
 * the Abyss's overlay), 4 subtracts the colour whole (Cd - Cs, FIX 0x80). Blended objects don't write depth, as an
 * overlay doesn't hide what's drawn after it. 0 puts back normal alpha blending, on only if it was on before (was). */
#include <pspgu.h>

extern int model_blend_off;   /* test "noblend 1": every object drawn normal, for before / after pictures (enemy.c) */

static inline void model_object_blend(unsigned int blend, int was) {
    if (blend >= 2 && blend <= 4 && !model_blend_off) {
        sceGuEnable(GU_BLEND);
        if (blend == 2) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffffu);
        else if (blend == 3) sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffffu);
        else sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0xffffffffu, 0xffffffffu);
        sceGuDepthMask(GU_TRUE);
        return;
    }
    sceGuDepthMask(GU_FALSE);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    if (!was) sceGuDisable(GU_BLEND);
}

#endif
