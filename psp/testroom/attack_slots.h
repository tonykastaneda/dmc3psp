#ifndef DMC3_ATTACK_SLOTS_H
#define DMC3_ATTACK_SLOTS_H
/* Enemy-manager +0x840: native setter 0x4257f0 caps ten timed slots.
 * The callers retain their existing cooldown durations. */
typedef struct { int limit; float remaining[10]; } AttackSlots;
static void attack_slots_reset(AttackSlots *s, int limit) {
    int i;
    s->limit=limit<1?1:limit>10?10:limit;
    for(i=0;i<10;++i) s->remaining[i]=0;
}
static int attack_slots_available(const AttackSlots *s) {
    int i;
    for(i=0;i<s->limit;++i) if(s->remaining[i]<=0) return 1;
    return 0;
}
static int attack_slots_take(AttackSlots *s, float seconds) {
    int i;
    for(i=0;i<s->limit;++i) if(s->remaining[i]<=0) {
        s->remaining[i]=seconds; return 1;
    }
    return 0;
}
static void attack_slots_update(AttackSlots *s, float seconds) {
    int i;
    for(i=0;i<s->limit;++i) if(s->remaining[i]>0) s->remaining[i]-=seconds;
}
#endif
