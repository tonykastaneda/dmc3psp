#include "attack_slots.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    AttackSlots s; int floor,i,count;
    for(floor=0;floor<=9999;floor+=999) {
        count=floor/1000+1;
        attack_slots_reset(&s,count);
        for(i=0;i<count;++i) assert(attack_slots_take(&s,3));
        assert(!attack_slots_available(&s) && !attack_slots_take(&s,3));
        attack_slots_update(&s,2.5f); assert(!attack_slots_available(&s));
        attack_slots_update(&s,0.5f); assert(attack_slots_available(&s));
    }
    attack_slots_reset(&s,99); assert(s.limit==10);
    attack_slots_reset(&s,0); assert(s.limit==1);
    assert(attack_slots_take(&s,1)); assert(!attack_slots_available(&s));
    attack_slots_update(&s,1); assert(attack_slots_take(&s,3));
    attack_slots_reset(&s,2); assert(attack_slots_take(&s,1));
    assert(attack_slots_take(&s,3)); attack_slots_update(&s,1);
    assert(attack_slots_take(&s,3) && !attack_slots_available(&s));
    puts("Palace attack-slot capacity, cooldown and reset checks passed");
    return 0;
}
