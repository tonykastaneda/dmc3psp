#ifndef DMC3_ATTACK_SLOTS_H
#define DMC3_ATTACK_SLOTS_H
/* Enemy manager +0x840, vtable 0x60d7b0. A slot stays busy at exactly zero. */
typedef struct {
    int limit;
    float remaining[10];
    unsigned char busy[10];
} AttackSlots;

static void attack_slots_reset(AttackSlots *slots, int limit) {
    *slots = (AttackSlots){0};
    slots->limit = limit < 0 ? 0 : limit > 10 ? 10 : limit;
}
static int attack_slots_available(const AttackSlots *slots) {
    int i;
    for (i = 0; i < slots->limit; ++i)
        if (!slots->busy[i]) return 1;
    return 0;
}
static int attack_slots_take(AttackSlots *slots, float seconds) {
    int i;
    for (i = 0; i < slots->limit; ++i) {
        if (slots->busy[i]) continue;
        slots->remaining[i] = seconds;
        slots->busy[i] = 1;
        return 1;
    }
    return 0;
}
static void attack_slots_update(AttackSlots *slots, float seconds) {
    int i;
    for (i = 0; i < slots->limit; ++i) {
        if (slots->busy[i] && (slots->remaining[i] -= seconds) < 0.0f) {
            slots->remaining[i] = 0.0f;
            slots->busy[i] = 0;
        }
    }
}
#endif
