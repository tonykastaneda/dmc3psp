# Divinity Statue shop: logic and data (DMC3 SE, PS2 `SLUS_213.61`)

Every address is an EE virtual address in `SLUS_213.61`. **[decoded]** means read from code or data in this
build. **[inferred]** means a reasoned guess that hasn't been checked in the running game.
The tables are in `shop_tables.json`, made by `shop_dump.py` (with `elf.py`). Run `python build/hdref/shop/shop_dump.py`.
Message ids are per file. The English text comes from `msgs_all.json` (made by the shop UI agent): `id600.pac` is the
index, `id822.pac` the items, `id700.pac` the skills, `id3000.pac` the guns and `id620.pac` the equipment page.

## 0. Classes

The C++ classes come from the RTTI (type_info, then the vtable):

| Class | type_info | vtable | Role |
|---|---|---|---|
| `CCustomize` | 5fb280 | 60ccc0 | The shop. Owns the page state machine. Embedded at stage+0x190a8, and in `CSceneMisStart` at +0x68 |
| `CCustomizeData` | 5fb2d8 | 60ce38 | A working copy of the save while the shop is open (CCustomize+0x5c) |
| `CUIDCustomIndex` | 5fb298 | 60ccd0 | The top menu |
| `CUIDStyleSelectV` | 600478 | 6180d0 | Page 1, style |
| `CUIDCustomWeapon` / `CUIDVergilWeapon` | 5fb738 / 6001f8 | 60d720 / 617650 | Page 2, equipment (Dante / Vergil) |
| `CUIDCustomItem` | 5fe358 | 612940 | Page 3, items |
| `CUIDCustomSkill` | 5fc6e0 | 60f000 | Page 4, skills ("Action") |
| `CUIDCustomGun` / `CUIDCustomGunV` | 5fd3e8 / 6003b0 | 610f90 / 617e20 | Page 5, guns / Vergil's Summoned Swords |

## 1. What opens the shop

### In a mission: event op 0x7a [decoded]
- Op 0x7a takes no arguments. In `FUN_0020be20` it calls `FUN_0023e820(evt+0x784, 0)` (`FUN_002103d0` does the same).
  That function sets `stage+0x19248 = 0` (the shop's open mode), `stage+0x18 = 9` (stage state 9) and `stage+0x19 = 0`.
  evt+0x784 is the stage manager, set by `FUN_00210050`. The doc's "camera op" label for 7a is wrong.
- Stage state 9 is `FUN_0023c2c0` (called from `FUN_00239a50`). Its substates:
  - 0: pause flag `*(0x1d0d70c)+9 = 1`, BGM and SE fades, then `FUN_004125b0(stage+0x190a8, mode=0, between=0)` (open),
    and a 30-frame fade-out.
  - 1: saves the player's position, rotation and yaw (stage+0x19250..0x1925c), and writes the player to the save
    (player vfunc +0x144 = `FUN_0025b740` into `*(stage+0xc)+0x104`). Then it destroys the player and his weapons.
  - 2: waits for the release, then loads the shop.
  - 6: runs `FUN_00412600` every frame until `FUN_00412df0` (CCustomize+4 == 0x10).
  - 7: **rebuilds the player** from the save: `FUN_00204210(char)`, then `FUN_00205b30(player, save+0x104)`, which calls
    the player's load vfunc +0x160 = `FUN_0025b420`. It puts him back at the saved position and reloads the room.
    This rebuild is how purchases take effect: new HP and DT, skill bits, weapons and style.
  - 8: back to state 1 (play), with a fade-in.
- In Room 5 the script runs op 7a after the first-visit message 10010 (EVENT_SCRIPTS.md).

### Between missions: `CSceneMisStart` [decoded, the menu wording is inferred]
- `FUN_002e5d20` is the `CSceneMisStart` tick (vtable 604fe0, called by `FUN_002e5750`). Its menu is built by
  `FUN_0035ba60`. When the menu returns **2**, it calls `FUN_004125b0(this+0x68, 0, 1)`, with between = 1, and a 60-frame
  fade. Return 1 starts the mission. The briefing's "Divinity Statue" option is return 2 [inferred from the flow].
- between = 1 (CCustomize+0x17) makes the shop call `FUN_00414ac0` right away, to preload the page's resources.
  The page change at state 6 then checks `FUN_00414ae0`.
- The mission-end screen doesn't open the shop in the code read here. Only those two callers of `FUN_004125b0` exist
  (calls.csv).

### `FUN_004125b0(this, mode, between)` [decoded]
It sets +4 = 0 (state), +8 = 0 (substate), +0x14 = +0x15 = mode (the current page), +0x178 = 1 (the index cursor)
and +0x17 = between.

## 2. The tables (all in `shop_tables.json`)

### 2a. Items: `CUIDCustomItem` [decoded]
- The list has 7 slots: `shop_item = *(s8*)(0x5db628 + mode_row*7 + slot)`, where `mode_row` is the byte `0x1d0d740`.
  Row 0 is `0,1,2,3,4,5,7` and row 1 is `0,1,2,3,4,5,6`. Inferred: `0x1d0d740` is the SE orb mode, 0 = Yellow Orb mode
  and 1 = Gold Orb mode, so only one of the Gold or Yellow Orb is sold.
- `shop item -> item id` comes from pairs at 0x5be700 (`FUN_004172c0`). The item ids come from `FUN_00300290`, which
  maps the # GAME names: 0 RED_ORB, 5 GOLD_ORB, 7 BLUE_ORB, 8 PURPLE_ORB, 9 BLUE_ORB_FRAGMENT, 10-12 GREEN_ORB0-2,
  13-15 WHITE_ORB0-2, 16 VITAL_STAR_L, 17 VITAL_STAR_S, 18 DEVIL_STAR, 19 HOLY_WATER. Item 6 is the Yellow Orb [inferred].

| # | Item | item id | Price by times bought (0x5db680, index capped at 7) | Max held (0x61a868) | Max buys (0x61a870) |
|---|---|---|---|---|---|
| 0 | Vital Star S | 17 | 500, 750, 1200, 1800, 2500, 3500, 5000, 5000 | 30 | - |
| 1 | Vital Star L | 16 | 2000, 3000, 4500, 6000, 7500, 10000, 10000, 10000 | 30 | - |
| 2 | Devil Star | 18 | 3000, 5000, 7000, 9000, 10000, 10000, 10000, 10000 | 10 | - |
| 3 | Holy Water | 19 | 10000, 15000, 20000, 25000, 30000, 30000, 30000, 30000 | 30 | - |
| 4 | Blue Orb | 7 | 5000, 10000, 15000, 20000, 30000, 50000, 50000, 50000 | 6 | 6 |
| 5 | Purple Orb | 8 | 3000, 5000, 7000, 9000, 10000, 20000, 30000, 30000 | 7 | 7 |
| 6 | Gold Orb | 5 | 10000, 15000, 20000, 20000, ... | 3 | - |
| 7 | Yellow Orb | 6 | 1000, 1500, 2000, 3000, 3000, ... | 99 | - |

- The price rises with the **times bought** counter (`save+0x5e+shop_item`, a u8 that saturates at 0xff). It never resets.
  The **held count** is a separate limit (`item count >= max held`).
- `FUN_004c83d0` (can buy?) returns:
  - 4: Devil Star or Purple Orb while Devil Trigger isn't unlocked (`customize+0xbf == 0`, save+0xbb). Message 0x4e88
    "You cannot purchase this item yet."
  - 1: bought the maximum number of times. 0x4e87 "You don't need anymore of this item."
  - 2: holding the maximum (`FUN_004c8500`). Message 0x2b33, in another bank [text not found].
  - 3: not enough orbs. 0x4e86.
  - 0: OK. Confirm prompt 0x4e85 "Will you use Red Orbs to buy this item?"
  There is no difficulty check, only the mode row and the Devil Trigger flag.
- The names are message ids 0x5db640 (id822.pac: 0x4e20 Blue Orb ... 0x4e3c Yellow Orb). The descriptions are 0x5db660
  (0x2afd..0x2b0b, a global message bank).

### 2b. Skills: `CUIDCustomSkill` [decoded; names inferred from the descriptions]
- A row is a weapon tab. The row base is 0 for Dante (chars 0, 1, 2) and 5 for Vergil (char 3) (`FUN_0045a8e0`).
  The number of tabs comes from 0x5be6f0[char*4 .. +1]. Each row is 0x14 bytes with 5 entries:
  - 0x5cc5d0: `s16 skill_bit`, then `u8 link` (high nibble = prerequisite entry, low nibble = the entry that replaces
    it once bought, 0xf = none), then `s8 display column`.
  - 0x5cc670: `s16 slot, s16 min`. It passes when `customize+0xc9+slot >= min`. That's save+0xc5+slot, the style level of
    style slot 2: Vergil's Dark Slayer [inferred]. Levels are 0-based (2 = Lv3).
  - 0x5cc710: the price. 0x5cc470: the description (id700.pac 20010..). 0x5cc530: the preview.

| Tab | Skill (bit) : price [prereq; style level] |
|---|---|
| Rebellion | Stinger Lv1 (7) 2500 · Stinger Lv2 (8) 10000 [Lv1] · Drive (13) 10000 · Air Hike (210) 20000 |
| Cerberus | Revolver Lv2 (39) 15000 · Windmill (37) 7500 |
| Agni & Rudra | Jet-Stream Lv2 (59) 10000 · Jet-Stream Lv3 (60) 15000 [Lv2] · Whirlwind (62) 7500 · Air Hike (211) 20000 |
| Nevan | Reverb Shock Lv1 (86) 7500 · Reverb Shock Lv2 (87) 15000 [Lv1] · Bat Rift Lv2 (85) 10000 · Air Raid (98) 20000 · Volume Up (97) 20000 |
| Beowulf | Straight Lv2 (122) 10000 · Beast Uppercut (117) 7500 · Rising Dragon (118) 15000 [Beast Uppercut] · Air Hike (212) 20000 |
| Yamato (V) | Rapid Slash Lv1 (4) 5000 · Rapid Slash Lv2 (5) 15000 [Lv1; DS 2] · Judgement Cut Lv1 (9) 10000 · Judgement Cut Lv2 (10) 20000 [Lv1; DS 1] |
| Beowulf (V) | Starfall Lv2 (24) 7500 [DS 1] · Rising Sun (25) 5000 · Lunar Phase Lv2 (27) 15000 [DS 2] |
| Force Edge (V) | Helm Breaker Lv2 (35) 13000 [DS 2] · Stinger Lv1 (38) 5000 · Stinger Lv2 (39) 10000 [Lv1; DS 1] · Round Trip (40) 10000 |

- Each Air Hike is a separate bit, so it's bought per weapon. Vergil's bits share the numbers (39) with Dante's, but
  each character has his own save.
- `FUN_0045bed0` (buy):
  - Already owned: 0x4e22 "You already know this skill."
  - Style level too low: 0x4e24 "Your Level is not high enough to learn this skill."
  - Orbs < price: 0x4e21 "You don't have enough Orbs."
  - Otherwise it asks 20000 "Learn this skill?". On yes: `orbs -= price`, `bits[bit>>5] |= 1<<(bit&31)` at customize+0x64.
    Then `FUN_0045b730` recomputes the masks. If a new entry is now visible: 0x4e23 "A new skill is now available."
- `FUN_0045b730` sorts the entries into masks: +0x2521 = owned; +0x2522 = hidden (prerequisite not owned, or replaced);
  +0x2523 = style level too low (drawn in blue). The columns go to +0x2525[column].
- Row 0's tab icon (Rebellion) 0x2da becomes 0x2d0 when Devil Trigger is unlocked (`FUN_0045c780`).

### 2c. Guns: `CUIDCustomGun` [decoded]
- A gun's level is `customize+0xad+slot` (save+0xa9+slot), from 0 to 2. Slot and item id come from the weapon map at
  0x5be6b0, triples (level slot, item id, weapon id) (`FUN_00417110` / `FUN_004171a0`). The tab only appears if
  the item is held. The price is `*(int*)(0x5d6880 + gun*12 + level*4)`; -1 means maxed.

| Gun (weapon id) | Lv1 to 2 | Lv2 to 3 |
|---|---|---|
| Ebony & Ivory (5) | 5000 | 10000 |
| Shotgun (6) | 10000 | 20000 |
| Artemis (7) | 10000 | 20000 |
| Spiral (8) | 7500 | 15000 |
| Kalina Ann (9) | 5000 | 10000 |

- `FUN_0048fc00` (buy):
  - Already level 2, or price < 0: 0x4e22 "can't enhance anymore".
  - Not enough orbs: 0x4e21.
  - Otherwise it asks 20000 "Enhance this gun?". Then `orbs -= price`, level++ (capped at 2), and the message is
    0x4e24 (new level) or 0x4e23 (max).
- `FUN_0048f840` writes the levels back. It also updates the equipped-gun level bytes (customize+0x57/0x58, when that gun
  is equipped at +0x52/0x53).

### 2d. Vergil's Summoned Swords: `CUIDCustomGunV` [decoded; names inferred]
0x5e5c80 holds the bits, 0x5e5ca0 the prices and 0x5e5c50 the descriptions (id3000.pac).
- Row 0: bit 51 for 7500 ("rate of fire increases"), then bit 52 for 15000 ("more swords in the circle").
- Row 1: bit 53 for 20000 ("barrier of countless swords").

### 2e. Styles and equipment
The style page (`CUIDStyleSelectV`) and the equipment page (`CUIDCustomWeapon`) cost nothing. Style levels come from
style EXP, not from the shop. Changing weapons needs at least 3 melee weapons (id620.pac 20001).

## 3. What purchases change

### Save / session struct (`*(u32*)0x1d0d710`) [decoded from `FUN_004165d0` (load), `FUN_00416b50` (store) and `FUN_0025b2d0` (sync)]

| Save offset | Customize offset | Field |
|---|---|---|
| +0x1c (int) | +4 | Red orbs (item 0) |
| +0x20+id (u8), id 1..0x3d | +8+id | Item counts (Vital Stars, Devil Star, Holy Water, Blue/Purple/Gold/Yellow Orbs, weapons 22..33) |
| +0x5e..0x65 | +0x46.. | Times bought, per shop item 0..7 |
| +0xa4..0xa8 / +0x104..0x108 | +0xa8 / +0x50 | Weapon ids: the per-character copy / the session copy (2 melee, 2 guns, 1 more) |
| +0xa9..0xb7 | +0xad | Level per weapon slot (gun levels 0..2) |
| +0x109..0x10d | +0x55 | Levels of the equipped weapons |
| +0xb8..0xbb | +0xbc..0xbf | +0xbb = **Devil Trigger unlocked** (session copy at +0x115) |
| +0xbc / +0xc0 | +0xc0 / +0xc4 | Max HP / max DT (per-character copy) |
| +0xc5+style (u8 x6) | +0xc9 | **Style levels** (session: style id +0x116, level +0x117) |
| +0xcc+style*4 | +0xd0 | Style EXP (session +0x138) |
| +0xe4..0x103 / +0x118..0x137 | - / +0x64 | **Skill bits**, 256 bits: the per-character copy / the session copy |
| +0x13c, +0x140 | +0x88, +0x8c | Current and **max HP** (floats) |
| +0x144, +0x148 | +0x90, +0x94 | Current and **max DT** (floats) |

- A Blue Orb (shop 4) does `max HP += 1000` (cap 20000.0, `FUN_00416ff0`), then `HP += max HP` clamped, so a full heal
  (`FUN_00416fc0`). A Purple Orb (5) does `max DT += 1000` (cap 10000.0, `FUN_00417050`), then fills DT (`FUN_00417020`).
  Both also add 1 to the item count. Dante's base max HP in his param block (0x5a6bb0+0x2c) is 6000 [decoded].
- Other items add 1 to `count[item id]`. `FUN_004c7370` takes off the price and adds 1 to the times-bought counter.
- Closing the shop: state 0xe, `FUN_00412c90`. If the weapons changed (customize+0xac.. against save+0x104), or the style
  did (+0xbe against save+0x116), it reloads the weapon resources (`FUN_00203640`, `FUN_00203f20`, `FUN_001b0110`) and
  waits in state 0xf. Then `FUN_00416b50` writes everything back to the save.

### The player and the moveset check [decoded]
`FUN_0025b420` (CPlDante vfunc +0x160) loads the 0x58-byte session block (save+0x104) into the player:

| Player offset | From |
|---|---|
| +0x3d4f..53, levels +0x3d6d..71 | Equipped weapons |
| +0x3c6c / +0x3c74 / +0x3c7c | Style id / level / EXP |
| **+0x29d8..0x29f4**, copy at +0x29f8..0x2a14 | Skill bits |
| Param block from `*0x618bf4 + char*0x54` copied to +0x2a7c, with max HP `+0x2aa8` from block+0x3c | Max HP |
| +0x2ad4 | Current HP |
| +0x28c4 / +0x28c8 | Current DT / max DT |
| +0x28af | Devil Trigger unlocked |

- `FUN_002458f0`, the command matcher, walks the command table (`player+0x2838` = 0x5a8e90). A command only fires if
  `player+0x29d8[act>>5] & (1 << (act&31))`, where `act` = record byte [2], the action id. **Shop bits are action ids.**
  For example, Stinger Lv2 (8) is listed before Lv1 (7), so the first unlocked one wins.
- To gate the port's moveset, keep a 256-bit mask and test it before matching a command.
  Bits 85, 97, 98 and 210-212 aren't in Dante's command table. Air Hike, Air Raid and Volume Up are checked elsewhere [inferred].
- Every command needs its bit, so a new game must set the free moves. No static default mask was found in the ELF
  (searched for 8 words with the 128 non-shop action bits set). They are probably set in code [inferred].

## 4. The menu state machine [decoded]

### `CCustomize::Tick` = `FUN_00412600` (+4 = state, +8 = page substate, +0x15 = page, +0x16 = next page)
- 0: load the customize copy from the save (`FUN_004165d0`), then 1.
- 1: wait for resources (`FUN_00414b90`).
- 2-4: run the page, by +0x15:
  - 0 Index `FUN_00412e00`
  - 1 Style `FUN_004143c0`
  - 2 Equipment: `FUN_00413d50` for Dante, `FUN_00414060` for Vergil (char byte `0x1d0d741` != 0)
  - 3 Item `FUN_004131a0`
  - 4 Skill `FUN_00413590`
  - 5 Gun `FUN_00413930` (Dante's `CUIDCustomGun`, or `CUIDCustomGunV`)
- 5 / 8: choose the next page (+0x16). 0 from the index means exit. 9 flips the double-buffer bit +0x54. 10-13 free the
  pages and UI objects. 0xe-0xf write back, reload and wait. **0x10 = done.**
- Each page handler builds its UI object, waits for its fade (`*0x618ab8` vfuncs), then polls the page's result:
  - Item `FUN_004c6820`: 1 or 2 = back to the index; 3 = previous page; 4 = next page (`FUN_00414af0` cycles pages 1..5).
  - Then it fades, frees and calls `FUN_00414ad0`.
  - The page messages are loaded by `FUN_00317ea0(0x1e585d0, ...)` and freed by `FUN_00318230(.., 20000, ..)`.

### Index: `CUIDCustomIndex` tick `FUN_00415290`
- The cursor +0x2001 runs 1..6 and wraps. Its value **is** the page number: 1 Style, 2 Equipment, 3 Item, 4 Skill, 5 Gun,
  6 Exit (6 becomes page 0).
- Moving makes SE 2. Confirm (pad bit 0x40) gives result 1 with SE 4. Cancel (0x10) gives result 2 with SE 3.
- The texts are id600.pac 20000-20007.

### Item page: `FUN_004c6130` (+4 = state, +5 = substate, +0x3375 = cursor 0..6, wraps)
- Input `FUN_004c6850`:
  - 0x4000 / 0x1000: cursor +1 / -1, SE 2.
  - 0x40: buy, state 4.
  - 0x80: description box, state 3.
  - 0x10: leave (result 2).
  - 0x1 / 0x2: previous / next page (results 3 / 4), SE 3.
- The pad is `FUN_001ae4b0` (pressed) or `FUN_001ae500` (repeat) on 0x6c3480. The logical bits are decoded, but which
  physical button each one is, is inferred.
- State 3, **description**: `FUN_003183b0(0x1e58600, desc msg)` in a box (`FUN_001b6cb0` at 0x40, 0x84, size 0x180 x 0x34).
  Any of 0xd0 closes it, with SE 3.
- State 4, **buy** `FUN_004c6c80`, by substate:
  - 0: check. The result is 4, 3, 2 or 1 (error SE **0x13**, message, then substate 10-12 shows it until a press), or 0.
    For 0: SE 4, message 0x4e85, substate 0x14.
  - 0x14: open the **yes/no dialog** `FUN_001ed550(0x1d0cab0, x, y, msg)`.
  - 0x15: poll `FUN_001ed670`. -1 or 2 means no, SE 3.
  - On yes, items 0-3 and 6-7: buy and SE **0x19**.
  - On yes, Blue or Purple Orb: a white flash (`*0x618ab8` vfunc +0x18, colour 0x808080, 4 frames) and SE 0x19. Then
    substates 0x16-0x17 fade back and buy.
  - 0x80 in the dialog shows the description (0x1e-0x1f).
  - 0x18: back to the list.
- The "**not enough orbs**" state is substate 10 with msg 0x4e86 and SE 0x13.

### Skill page: `FUN_0045ac80` / `FUN_0045bed0` / `FUN_0045c5b0`
- The tab is +0x251b and the column cursor is +0x251c. `FUN_0045c5b0` is the description box: it shows the 0x5cc470
  description, or 0x4e5c if there isn't one.
- Buying works like the items: the check, then the yes/no dialog at (0x1c0 - width, 0x90). After a purchase there's a
  24-frame highlight (+0x252c = 0x18, `FUN_0045d390`).

### Gun page: `FUN_0048fc00`
It works the same way. The dialog's position is per gun (0x5d6830). After an upgrade, a 30-frame level-up flash
(+0x3115 = 0x1e).

### Sounds [decoded ids, inferred meaning]
`FUN_0033e630(0x1e58e80, 0, id, 0, 0)`: 2 = cursor, 3 = back or close, 4 = decide, 0x13 = error buzzer,
0x19 = purchase or red-orb spend.

## 5. Red orbs

### Enemies [decoded]
- At death, `FUN_00257780` (enemy vfunc +0x1f4, shared by every CEm) calls vfunc +0x1e4 = `FUN_00257a60` with
  `(uint)param+0x2c`. param is the enemy's copied param block (`enemy+0x11c`), and +0x2c is the **max HP** field: Dante's
  block holds 6000 there.
- The count is `vfunc +0x1e0` = `FUN_002578c0` (it wasn't a Ghidra function, so it was read from the disassembly):
  `orbs = (uint)( maxHP * rankMult * diffMult * 0.01 )`
  - `rankMult` by `enemy+0x100` (jump table 0x5f3160): 0→5, 1→7.5, 2→10, 3→12.5, 4→15, 5→20, 6→22.5, 7→25.
    Inferred: +0x100 is the style rank recorded on the kill (0 = none ... 7 = SSS).
  - `diffMult` by the byte `0x1d0d73c`: 0→0.5, 1→1.0, 2→1.2, 3→1.5, 4→2.0, otherwise 1.0. If the byte `0x1d0d73d` != 0
    it is 0.1 instead. Inferred: 0x1d0d73c is the difficulty (Easy, Normal, Hard, Very Hard, DMD); 0x1d0d73d is unknown,
    maybe Heaven or Hell or Bloody Palace.
- No drop if `*(0x1d0d70c)+10 != 0` or `enemy+0x120 == 0xff`.
- Spawning goes `FUN_002f44b0` → `FUN_002f4510`. It splits the count into orbs worth **1000/100/20/5/1**. When fewer than
  7 orbs would come out, it breaks big ones into small ones.
- The spawn record word +0x24 (`enemy+0x5d0`):
  - `(w/10000)%100 == 1`: no red orbs.
  - `w%100` = 2/3/4: a Green Orb 10/11/12 instead of the random roll.
  - 0: a roll against 0x5953b0..0x595450, chance in percent by rank, then by `(HP/maxHP)*4`. Those are green (10-12)
    or white (13-15) orbs.

| Enemy | maxHP (+0x2c) | Orbs, Normal, rank 0 | Orbs, Normal, rank 7 |
|---|---|---|---|
| Pride CEm000 (0x5a78f0) | 250 | 12 | 62 |
| Gluttony CEm001 (0x5aace0; 0x5aac30 = 50 when spawn word [0xf6] set) | 700 | 35 | 175 |
| Lust = CEm002 [name inferred] (0x5ac100) | 750 | 37 | 187 |
| Sloth CEm003 (0x5b05c0) | 1200 | 60 | 300 |
| Wrath = CEm004 [name inferred] (0x5b10f0) | 500 | 25 | 125 |
| Greed [class inferred, block 0x5b0080, table *0x6191ec] | 1000 | 50 | 250 |
| Enigma CEm013 (0x5b56b0 + type*0xfc + rank*0x54) | 600 | 30 | 150 |
| Gigapede CEm023 (0x5baf40, copied to +0x1c99*4) | 2800 | 140 | 700 |

- The Pride and Gluttony "b" blocks replace +0x2c with `min(spawn word [0xf6] * 10, 20000)` (`FUN_002b6450`). So the HP,
  and with it the orbs, can come from the EST.

### BREAK objects and placed items [decoded]
- `FUN_002f50d0` parses the # GAME block. `item <NAME> <count> <chance>` adds `{id = FUN_00300290(NAME), count, chance}`,
  12 bytes each, copied to obj+0x3e4 (`FUN_002f5750`).
- On break, `FUN_002f70f0` rolls `r = rand%100 + 1`. It walks the entries doing `r -= chance`, and the first that takes r
  under 1 spawns (`FUN_002f71f0`):
  - id 0: `FUN_002f44b0` with **count** red orbs.
  - 5-9, 16-19: `FUN_002f2f00` (count items).
  - 10-15: count green or white orbs.
- So `item RED_ORB 100 100` is **100 red orbs, 100 % of the time**. There's no difficulty or rank scaling: that is only on
  the enemy path.
- The red-orb crystal `CStageSetORBreak` (`FUN_00366f10`) adds damage to +0x7e4. Every 30 frames (+0x7ec) it gives
  `(int)(dmgTotal * 0.1 * mult[min(dmgTotal/750, 6)])` orbs, with mult (0x5b1800) = 1, 1.5, 2, 3, 5, 5, 8.
  Whether +0x7e4 is reset between payouts wasn't checked [inferred: it's the hit total for the burst].

## Open points
- What 0x1d0d740 (mode) and 0x1d0d73c / 0x1d0d73d mean, beyond the code paths above.
- The physical button behind each logical pad bit.
- Where a new game sets the default skill bits.
- The class of the 1000-HP block 0x5b0080, and the Lust/Wrath names for CEm002/CEm004.
- Text 0x2b33.
- `FUN_00307850` also loads and stores CCustomizeData, for an unidentified scene.
