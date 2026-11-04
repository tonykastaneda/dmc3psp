# DMC3 SE event scripts (EventTblNN.bin), decoded

Source: `SLUS_213.61` (PS2 US SE). Files: GDATA.AFS slot 238+N = EventTbl N. Slot 239 starts in room 0,
240 in room 1, 241 in room 2, 242 in room 100, so **EventTbl N is Mission N** (inferred from the start rooms;
EventTbl00 is a chain of demos from a room 900, probably the theatre/ending, not a mission).

Legend: **[code]** = read from the decompiled or disassembled interpreter. **[ctx]** = the handler calls a
function I haven't decoded, so the meaning comes from how the scripts use it, plus the walkthrough.
**[guess]** = weaker than that.

Files written:
- `build/hdref/evtdecode.py`: the decoder (uses `evtdis.py`). It writes `evt_m3.json`/`evt_m4.json`, with
  every op as {off, op, name, args, depth, section, block}, and the listings `evt_m3.txt`/`evt_m4.txt`.
- `build/hdref/estgroups.py` → `estgroups.json`: each room's EST groups × 5 difficulties, with spawn class counts.
- `build/hdref/mdis.py`: a small R5900 disassembler for the ELF, and `doortext.py`, which dumps a room's
  stage text to `sttext_NNN.txt`.
- Ghidra: about 150 more functions exported to `analysis/ghidra/export/decompiled/` (0x209050–0x210f50,
  0x2fae40…, 0x239b60, 0x2e4250, 0x2e5d20 and others).

## 1. The interpreter

| Function | Role |
|---|---|
| `FUN_0020ad90` | **Load-time fixup.** It walks the script and renumbers each `0d [type,-1]`: type 1 from a counter reset at every `07` (per room), type 0 from one counter for the whole script. That's why `-1` works. [code] |
| `FUN_0020b580(evt, mode, value)` | Finds a section: mode 0 looks for `07 value` (the room script, called on room load with the room id), mode 1 for `09 value` (called by the demo scene `FUN_002e4250` when demo `value` ends), mode 2 for `0b value` (called by `FUN_002e5d20`, the mission-start screen). The terminators are 08/0a/0c and the script ends at `01`. It searches script +4, then +8. [code] |
| `FUN_0020b410`/`b450`/`b500` | Mission start: runs the **first op of the file** (or of a 09/0b section): 02/03/04/05 fill a "next scene" record {kind, a, b} at evt+0xf0. kind 0 = room change (`FUN_0028ee50`), kind 1 = demo (`FUN_0028ef00`), kind 2 = mission-start screen, kind 3 = mission clear. [code] |
| `FUN_0020b7a0` | On room load: runs the **first block** of the room section once (the on-enter block), then moves the section start past it, so it's never rescanned. [code] |
| `FUN_0020b8d0` | Each frame: handles cutscene skip, ticks 2 helper threads (`e240`), then `bb60` (pick a block) and `bd50` (run it). [code] |
| `FUN_0020bb60` | Round-robin scan for the next `0d` block whose done-bit is clear. It runs the block's leading switch/case (`e510`), and if that doesn't reach `0e`, the block is live. [code] |
| `FUN_0020bd50` | Runs a block's actions through `be20` until `0e`; ops `be20` doesn't know (returns -1) go to `e510`. A return of 1 means wait (resume next frame) and 2 means the block is finished. [code] |
| `FUN_0020be20` | The action ops (0x02–0x8c). [code] |
| `FUN_0020e510` | Switch/case flow ops 0x0f–0x31 plus `3d 0`. [code] |
| `FUN_0020e460` | Skips to the next case or the end of the switch (nesting-aware). [code] |
| `FUN_0020f880` | **Cutscene-skip replay.** Between `49` and `4c` it re-runs only the state-changing ops (door, gimmick and flag ops, plus 50/53/54/56/59/5e/65). [code] |
| `FUN_0020e240`/`e370` | Helper threads started by `50`; only `57` (wait until time ≥ n) is special-cased. [code] |

### Encoding
u32 word: low byte = opcode, byte 1 = argc, then argc s32 args. Header: `"EVT\0"`, u16 1, u16 nScripts,
u32 offset of an nScripts×u32 table of script offsets (script 0 = 0x20; script 1, if present, is a helper
thread body for op `50`).

### Script structure
```
<first op>            02 demo | 03 room pos | 04 -1   (mission start)
09 D { op }  0a       after demo D ends: 02 next demo | 03 room pos | 04 -1 (start screen) | 05 (mission clear)
0b -1 { 03 R P } 0c   after the mission-start screen: first room R, entry position P
07 R                  room R script
  0d t i ... 0e       block. The first block is the on-enter block, run once per room load.
  ...
08
01                    end
```
**Block types** (`0d type idx`). The done-bit is set only by `3d 1` [code]:
- type 0: once per **mission** (bits at evt+0x1c…, never cleared on room load).
- type 1: once per **room visit** (bits at evt+0x48…, which `FUN_0020b730` clears on every room load).
- type 2: done-bit = **event flag** idx (bit idx+0x70 of the type-0 set). It's the same bit `12 idx` tests and `69 idx` sets.

Blocks usually open with guards: `sw_X; case_eq v; end_block 0; end_switch` means "abort this block (without
marking it done) unless X == v". So `0f 0 / 2c 0 / 3d 0` reads as "only while the player is in area 0".

### State used by the conditions [code]
- **Flags** (`10 n` test, `65 n v` set/clear): n < 32 are bits at evt+0x18 and persist through the mission.
  n ≥ 32 are bits at evt+0x44…, cleared on every room load (room-local). They're also the **door lock flags**:
  the door code `FUN_0020a210` won't let you use a door whose flag is set. `44`/`45` set/clear the door's flag.
- **Counters** 0–4 (`66 i v`, `67 i` ++, `68 i` --, `11 i` test).
- **Areas** (`0f n`): bit n is set while the player is inside event area n of the room (`FUN_00209c00`). The
  area list comes from the stage data, not from the script. `64 n 0/1` disables or enables area n.
- **Trigger byte** (`13 b`): bit b of evt+0x5c, cleared when tested. Bit 0 is set by `FUN_00389a10`, in the
  code for enemy id 41 (0x29), which is the Gigapede [ctx].
- **Enemy manager** (evt+0x788): `3e g b` starts EST group g; `17` = 1 when that encounter is fully cleared:
  no enemies alive, killed == total, and the hold flag (`43 1`) not set.

## 2. Opcode semantics (every op used in EventTbl03/04, plus neighbours)

| op | args | meaning |
|---|---|---|
| 01 | – | end of script [code] |
| 02 | D | play demo (cutscene) D. Afterwards section `09 D` decides what follows [code] |
| 03 | R,P | change room to R, entry position P (NextPosId) [code] |
| 04 | -1 | go to the mission-start screen. After it, section `0b -1` runs [code] |
| 05 | – | **mission clear** [code] |
| 06 | D | preload/queue demo D (`FUN_0028ef00` directly) [code; "preload" is ctx] |
| 07 / 08 | R | room R section / end [code] |
| 09 / 0a | D | "after demo D" section / end [code] |
| 0b / 0c | -1 | "after mission-start screen" section / end [code] |
| 0d | type,idx | block start (see types above) [code] |
| 0e | – | block end [code] |
| 0f | n | acc = player inside event area n [code] |
| 10 | n | acc = flag n [code] |
| 11 | i | acc = counter i [code] |
| 12 | n | acc = event flag n [code] |
| 13 | b | acc = trigger bit b, then clear it [code] |
| 14 | item | acc = count of item `item` in the save (item 0 = red orbs) [code] |
| 15 | item | acc = 0 if "obtained" bit (evt+0x768) is clear, else 1 if count > 0, 2 if count = 0 [code] |
| 17 | – | acc = encounter cleared (1/0) [code] |
| 1c | – | acc = player pressed the action/examine button (consumed) [code: the same call the BtnOn door code uses] |
| 1e | – | acc = **previous room** (the room the player came from) [code: `FUN_00210220` stores old room at +0xfa] |
| 1f | – | acc = room id stored at +0xfc (the room just transitioned to) [code] |
| 29 | – | acc = (player byte +0x46 == 3) [code]. The scripts use it to skip weapon gifts; probably "playing as Vergil" [guess] |
| 2c/2d/2e/2f | v | case acc == v / != v / >= v / <= v [code] |
| 30 / 31 | – | default / end of switch [code] |
| 32 | id,flag | bind gimmick `id` to flag `flag` (gimmick vfunc+0x28) [code: call; ctx: meaning] |
| 33 | gim,door,flag,x | attach seal/door gimmick `gim` (class 5) to door `door`, with lock flag `flag` (-1 = none). If the flag is already set, the door starts locked [code] |
| 36 | id,v | gimmick command A (vfunc+0x10) on every gimmick with that id (-1 = all of type 3) [code: call]. Used as "set to state A". The pairs with 37 look like on/off, but which way varies per gimmick [ctx] |
| 37 | id,v | gimmick command B (vfunc+0x0c) [code: call] |
| 38 | id,a,b | gimmick vfunc+0x2c(a,b), e.g. the lift in 110 [code: call] |
| 3d | 0/1 | end block. 1 = mark done, 0 = just stop/abort [code] |
| 3e | g,b | **start EST encounter group g** in this room (enemy manager +0xc = g). b=1 clears mgr+0x18, else sets it (meaning unknown). g=-1 picks a group from evt+0x7a0 [code] |
| 42 | – | `FUN_00210830` (not decompiled) |
| 43 | b | hold the "cleared" check while more waves are coming (enemy mgr +0x1a) [code] |
| 44 | door,anim | **lock door** (set its flag, seal-on animation) [code] |
| 45 | door,anim | **unlock door** (clear flag, seal-off animation) [code] |
| 46 / 47 | door | enable / disable a door (hide it or make it unusable) [code] |
| 48 | door,kind,room,pos | retarget a door (kind 0 = room R/pos P) [code] |
| 49 | cam,skip | begin an in-engine event (event camera `cam`). skip=1 lets the player skip it [code] |
| 4a | bits | event camera flags; 0x100/0x200 turn skip on/off [code] |
| 4c | – | end the event; restore the player [code] |
| 4d | n | play event-camera cut n [code: call; ctx] |
| 4f | – | wait for the camera cut to finish [code] |
| 50 | slot,s | start helper thread `slot` running script table entry s [code] |
| 53 | g | skip-only: if the cutscene is skipped, start group g instead (a no-op in normal flow) [code] |
| 54 | a,b,c,d | skip-only BGM change (same as 70) [code] |
| 56 | n | skip-only boss fix-up (n=0/1: Gigapede, enemy id 41) [code] |
| 57 | t | (thread) wait until the thread timer ≥ t [code] |
| 58 | n | wait n frames (game-speed scaled) [code] |
| 59 | n | event-camera op [code: call] |
| 5b | x,y,z,deg,? | warp the player to x,y,z facing deg [code] |
| 5c | item,n | **give n of item** (adds to the save and shows the get-item message) [code] |
| 5d | item,n | take item [code] |
| 64 | area,on | enable (1) / disable (0) event area [code] |
| 65 | n,v | set (v=1) / clear (v=0) flag n [code] |
| 66 / 67 / 68 | i[,v] | counter set / ++ / -- [code] |
| 69 | n,v | set/clear event flag n [code] |
| 6a | kind,id,x,y,z | spawn effect `id` at a position [code: call; ctx] |
| 6e | id | open message/text `id` (10003 and up) [ctx] |
| 6f | – | wait until the message is closed [code] |
| 70 | t0,t1,c0,c1 | BGM: two stream channels, track and command each [code] |
| 71 | kind,id,?,x,y,z | 3D sound effect [ctx] |
| 72 | kind,id,a,b | sound effect [ctx] |
| 75 | a,b | event-camera mode [code: call] |
| 78 | n | show tutorial tip n (only once per save; sets a save bit) [code] |
| 79 | a,b | `FUN_001acd70`: rumble or sound bank [guess] |
| 7a | 0 | camera op [code: call] |
| 7b | x,y,z | release the pending reward (orbs) at a position [ctx] |
| 7e | case | hard-coded special: 1 = Gigapede cleanup, 3/4 = camera, 5 = move gimmick 300 [code] |
| 89 | a,b,c | set a map-screen marker bit (save +0x14 obj) [code] |
| 8c | n | wait n raw frames [code] |

## 3. Mission 3 (EventTbl03, slot 241)

Start: demo **5** → demo **123** → mission-start screen → **room 2, pos 0**.
Room names below come from the walkthrough and the door graph (002↔003↔004↔005→006→100) [ctx].

**Room 2: 66 Slum Avenue** (EST: 6 groups)
- On enter: BGM, map mark. If flag 0 is set (a later visit): start **group 4** (big mixed wave) and stop.
  If event flag 0 is set: start **group 5** (2 Enigma) and hold the clear check.
- Event-flag block 0, in area 3: start **group 2** (2 Enigma, Normal) and hold. Enigma intro cutscene
  (effects, cams), skip → group 5. Then tip 7 (Kick Jump).
- Once, in area 0: effect and sound, start **group 1** (1 Pride).
- Once, in area 1: set flag 0, effect and sound, start **group 0** (1 Pride), gimmick 100, wait 120 frames,
  start **group 3** (main wave: Normal 7 Pride, 1 Wrath, 2 Lust), release the hold.
- Once, if flag 1 isn't set yet and the encounter is cleared: a short camera event (cam 4).
- No door locks: the walkthrough says you may leave early.

**Room 9** (reached from 2 through its BtnOff door 0): on enter, start **group 2** (Normal 8 Pride, 2 Lust,
1 Wrath). That's all.

**Room 3: Bullseye** (no EST, no enemies)
- On enter: set flag 1. Gimmick 100 bound to flag 32 (the sword-reactive seal device). Blue seal gimmick 200
  on **door 1** (→ room 4) with lock flag 33. If you have no item 29 or the player check is true: gimmick 300
  and areas 2/5 are turned off. If flag 4 is set, door 0 is retargeted to room 11 pos 1. Counter 1:
  1 → door 1 locked; 2 → flag 32 set.
- Per visit, if counter 1 == 0: counter 1 = 1, cutscene that **locks door 1** (blue seal), tip 8.
- Once, in area 6: tip 9.
- Per visit, if counter 1 == 1 and flag 32 (the device is fully lit): cutscene that **unlocks door 1**, counter 1 = 2.
- Once, if you have no item 29: in area 0 (touch), or in area 3 and examine → **give item 29 ×1** (the Shotgun,
  identity from the walkthrough [ctx]), gimmick 300 off, areas 2/5 off, tip 10 (switch firearms).
- Examine texts: area 1 with flag 33 clear → message 10003; area 2 with flag 32 set → message 10004.

**Room 4: Love Planet** (EST: 3 groups)
- On enter: red seals 200/201 on doors 0 and 1 (no flags).
- Once, in area 2: tip 6 (camera).
- Per visit, if flag 2 isn't set and you're in area 1: hold, **lock doors 0 and 1**, event (warp the player to
  3107,79,2503, effect, helper thread 1 = a fire sound loop), start **group 1** (3 Sloth; skip-only path the
  same), cams, tip 11, then start **group 0** (Normal 6 Sloth + 4 Pride), release.
- Per visit, if flag 2 isn't set and the encounter is cleared: **unlock doors 0 and 1**, set flag 2.

**Room 5: 13th Avenue** (no EST)
- On enter: set flag 4 (it retargets Bullseye's door 0), counter 0 = 0.
- Once, in area 1: a camera event.
- Per visit, in area 0 and examine: the first time (flag 5 clear), message 10010 (the time-god statue, where
  the shop is), tip 12, then set flag 5. Then op 7a.

**Room 6: Ice Guardian's Chamber, Cerberus** (EST: 1 group = class 43 = Cerberus)
- On enter: gimmick setup, door 1 (→ 100) disabled. Then by counter 0:
  0 → BGM, preload demo 6. 1 → **start group 0 (Cerberus)**, preload demo 7, door 0 disabled. 2 → door 0 disabled.
- Counter 0 == 0, in area 0: **demo 6** (Cerberus appears), counter 0++. After demo 6 → back to room 6 pos 2.
- Counter 0 == 1 and cleared: **demo 7** (Cerberus beaten), counter 0++. After demo 7 → room 6 pos 2.
- Counter 0 == 2: if not (player check) and you have no item 23 → **give item 23 ×1** (Cerberus, the devil
  arm [ctx]), tip 17. Then `7b` reward at 2500,200,2500 (boss orbs [ctx]).
- Counter 0 == 2, in area 1 (the tower door): **demo 8** → **mission clear (05)**.

## 4. Mission 4 (EventTbl04, slot 242)

Start: demo **9** → mission-start screen → **room 100, pos 0**.

### Room ↔ name (from the event logic, door graph and walkthrough)
| room | name | confidence |
|---|---|---|
| 100 | Chamber of Echoes (2F = the same room, entered from 110 at pos 2) | high: start room, "came from 110" event, examine tablets |
| 101 | Entranceway | high from the doors (100↔101↔102). **No M4 section**, see open questions |
| 102 | Living Statue Room | high (Enigma group, red seal, cage-item text) |
| 103 | Silence Statuary | high (seal-print gimmick flag 32 → lift gimmick flag 33; BtnOff door up to 110) |
| 110 | Incandescent Space | high (red seal fight, Astronomical Board → mission end) |
| 112 | Endless Infernum | high (red seals unlocked by flag 6; stairs-break area → BtnOff fall into 104) |
| 104 | Chamber of Sins | high (one door, back to 112, enabled after the fight = teleport light) |
| 111 | Giantwalker Chamber | high (group 0 = enemy class 41 = Gigapede; trigger bit 0 on its death) |
| 113, 120–126, 105–109 | not in the M4 script | – |

Order the script supports: 100 → 101 → 102 → 103 → (lift) 110 → 100 (2F) → 112 → (fall) 104 → (teleport) 112
→ 111 → 110 → demo 10 → clear. It matches the walkthrough.

**Room 100: Chamber of Echoes**
- On enter: BGM, map mark, **door 4 (→ 101 pos 1) disabled**, camera mode, gimmicks 101/211/300/301
  command A. If flag 0 is set: gimmick 202 command A.
- Once per mission: the opening camera event, then tip 13.
- Once per mission, if the previous room is 110 (arriving on 2F): camera event, **set flag 0**, gimmick 202 command A.
- Examine texts (per visit, area + action button): area 0 → 10150, area 2 → 10151, area 3 → 10155,
  area 4 → 10158 (one of them is the tablet with the round indentation [ctx]).

**Room 102: Living Statue Room** (EST: 7 groups)
- On enter: BGM, door seals 200 (door 0) and 201 (door 1), gimmick 100 command A.
- Per visit, if flag 1 isn't set: cutscene: **lock doors 0 and 1**, **start group 1** (Normal 6 Enigma + 3 Lust),
  cams, BGM. Skip-only: BGM, and start **group 4** (the same make-up) if skipped. Then tip 14.
- Per visit, if flag 1 isn't set and cleared: **unlock doors 0 and 1**, **set flag 1**.
- Area 0 + examine: message 10153 (the cage: "soul of steel").

**Room 103: Silence Statuary** (no EST)
- On enter: BGM, clear flags 32/33/34, gimmick 100 bound to **flag 32** (the circular seal print), gimmick 101
  bound to **flag 33** (the lift), gimmick 102 command A.
- Per visit: if flag 34 isn't set and flag 32 is set (print fully lit): short event, **set flag 33** (raises the
  lift), set flag 34. If flag 34 is set but 33 isn't: clear 32 and 34 (reset).
- Door 1 is BtnOff → room 110 pos 2 (riding the lift up).

**Room 110: Incandescent Space** (EST: 3 groups)
- On enter: seal 200 on door 0. If not arriving from 103: lift gimmick 100 set to the up state (38 100 1 1).
- Per visit, arriving from 103: lift event (38 100 1 0), sound, cam 3. Then, if flag 2 isn't set: event,
  **start group 0** (Normal 6 Gluttony + 2 Sloth), **lock door 0**, wait 200. Skip-only: start group 2 (the same make-up).
- Per visit, if flag 2 isn't set and cleared: **unlock door 0**, **set flag 2**.
- Once per mission, when **item 36 is obtained** (`15 36`; the item pickup itself isn't in the script, so it's
  a stage item): disable doors 1 and 0, wait 30, **demo 10** → after demo 10 → **05 mission clear**.
  Item 36 = Astronomical Board [ctx].

**Room 112: Endless Infernum** (no EST)
- On enter: BGM, door seals 200 (door 0, flag 4) and 201 (door 1, flag 5), gimmicks 103/106/107 command A,
  door 2 disabled. Stairs: if flag 3 is clear, gimmick 101 A, else gimmicks 100/101/102 A.
- Per visit, arriving from 104: effect and sound (the teleport back).
- Per visit, if flag 6 isn't set: event, **lock doors 0 and 1** (the red seals).
- Once per mission, in area 0 (the narrow stairs): gimmick 100 A, gimmick 101 B, sound, wait 30, gimmick 102 A,
  **set flag 3** (the stairs break; you fall through door 3, BtnOff, into 104).
- Once per mission, if flag 6 is set: event, **unlock doors 0 and 1**.

**Room 104: Chamber of Sins** (EST: 1 group)
- On enter: BGM, gimmick 100 A, **door 0 disabled**, **start group 0** at once (Normal 3 Sloth + 4 Lust + 3 Wrath,
  by the user's class-id map; the walkthrough says Gluttony + Wrath, so check the 4/12 class ids).
- Once per mission, when cleared: **set flag 6** (this breaks 112's seals).
- Per visit, when cleared: event, gimmick 100 B (the column of light [ctx]), **door 0 enabled** (the teleport
  back to 112 pos 3).

**Room 111: Giantwalker Chamber, Gigapede** (EST: 5 groups; group 0 = class 41 = Gigapede)
- On enter: door seals 200/201 on doors 0/1 (no flags), gimmicks 300/400/202 A, door 4 (→ 422) disabled.
- Per visit, if flag 7 isn't set: BGM, event: **lock doors 0 and 1**, cam, **start group 0 (Gigapede)**, cams,
  BGM; skip-only: BGM and the boss fix-up. Then gimmick 400 B.
- Per visit, if flag 7 isn't set and trigger bit 0 is set (Gigapede died): BGM 18, event, **unlock doors 0 and 1**,
  cams, `7e 1` boss cleanup, **set flag 7**, gimmick 400 A, `7b` reward at 2500,0,2500 (the boss orbs [ctx]).
  Door 1 → room 110 pos 1: the small red door back to Incandescent Space.

### Mission 4 flags, summarized
| flag | set by | meaning |
|---|---|---|
| 0 | 100, arriving from 110 | CoE 2F reached (gimmick 202) |
| 1 | 102 cleared | Living Statue Room done |
| 2 | 110 cleared | Incandescent Space seal broken |
| 3 | 112 area 0 | stairs broken |
| 4, 5 | (only bound as 112's door lock flags) | – |
| 6 | 104 cleared | Endless Infernum seals off |
| 7 | 111 Gigapede dead | Giantwalker done |
| 32–34 | 103 (room-local) | seal print lit / lift raised / latch |

## 5. Open questions / not verified
- **Room 101 (Entranceway) has no section in EventTbl04**, yet the walkthrough has enemies there, and 101's EST
  has 4 groups. Encounters in sectionless rooms must start some other way (enemy manager defaults?), which I
  didn't trace. Rooms 105–109 and 113 are also absent.
- The meaning of `3e`'s second arg (enemy mgr +0x18) is unknown.
- Gimmick commands 36/37/38 are vfunc calls on stage gimmicks whose classes I didn't decode. On/off readings
  are taken from context.
- Item ids 23/29/36 = Cerberus / Shotgun / Astronomical Board are taken from the walkthrough, not from an item table.
- Message ids (10003, 10004, 10010, 10150–10158) and demo ids (5, 6, 7, 8, 9, 10, 123) are raw numbers. I didn't
  look up their text or demo files.
- Event areas (`0f n`) come from per-room stage data (evt+0x9d0 table) that I didn't parse, so area n's position
  is unknown.
