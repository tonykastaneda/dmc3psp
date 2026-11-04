# Divinity Statue shop ("Customize") UI: assets, layout and text from the disc

The statue shop is the game's **Customize** screen (SLUS RTTI: `CCustomize`, `CCustomizeData`,
`CUIDCustomIndex`, `CUIDCustomItem`, `CUIDCustomSkill`, `CUIDCustomGun`, `CUIDCustomWeapon`,
`CUIDStyleSelect`; Vergil: `CUIDCustomGunV`, `CUIDVergilWeapon`, `CUIDStyleSelectV`). The Japanese
message labels call it 時空神像 (time-space god statue). Every screen is a GDATA PAC built the same way as
the menus `tools/prepare_menu.py` already reads: flat MODs in screen units (origin at the centre, Y up,
art filling +-256 x +-192), each **followed** by its texture set (so `after=True`), plus a
`# NotRegistered ... # Msg nnnnn "..." # End` text part that holds that screen's English strings.

Slot numbers are GDATA indices. The English PAC is the bare name (`id600.pac`). Other languages sit in
neighbouring slots: `J` (Japanese) just before it, then `F G I S K C`, and `V`/`VJ` for Vergil's copies.
Pictures are named `<slot>_<k>.png` = the k-th TIM2 picture in the slot, in file order
(`dump_slots.py`). Crops are pixel rectangles (x0,y0)-(x1,y1) in that picture, and come straight from the
MOD UVs (`crops.py`, full list in `crops.txt` / `crops_more.txt`). Screen positions are in MOD units.
`compose_<slot>.png` rebakes each screen from its own MODs through `prepare_menu.bake` as a check
(`sheets/compose_all.png`).

## 1. Slots

| Slot | File | Screen / role |
|---|---|---|
| **2294** | id600 | Customize index (the shop's top menu), with the **statue art** |
| **2649** | id822 | Item list: icons, price/count digits, name word-pieces. Used for both buying (Customize > Item) and using (Status > Item). **2671 id830** is an identical copy |
| **2322** | id700 | Action (skill) shop frame: weapon wheel, weapon list, 4 skill rows, description box |
| 2330 / 2338 / 2346 / 2354 / 2362 / 2370 | id720 / 730 / 731 / 732 / 733 / 734 | Per-weapon skill-name lists + weapon art: Rebellion (two versions), Cerberus, Agni & Rudra, Nevan, Beowulf |
| 2378..2546 step 8 | id740..id761 | One 512x512 preview per skill, with its **orb price painted in** (lower right) |
| **3017** | id3000 | Gun shop (gun list, Lv1-3 pips, price glyphs) |
| 2302 | id620 | Equip (choose devil arms / guns); weapon icons in 2309..2320 (id627..id642, 256x256 each, name lettered in) |
| 1716 | id300 | Style select (Trickster .. Doppelganger, Lv1-3); previews 1724..2068 (id320..id367, 512x512, text painted in) |
| 2637 | id818 | Status (pause) index, for comparison: STATUS / EQUIP ITEM MAP STYLE EXIT FILE |
| 3422 / 3424 / 3440 | id6200 / 6210 / 6400 | Vergil's Equip / Action / Summoned Swords shops (Yamato, Beowulf, Force Edge) |
| 2117..2175 | id500..id558 | Big item renders with name and frame (the Get Item / File page), picture _1 = item, _0 = frame: 2122 Gold Orb, 2123 Yellow Orb, 2124 Blue Orb, 2125 Purple Orb, 2126 Blue Orb Fragment, 2133 Vital Star L, 2134 Vital Star S, 2135 Devil Star, 2136 Holy Water (2117-2121 Red Orb, 2127-2132 Green/White Orb) |
| 1495 | id100 | In-mission HUD sheet: the red-orb icon and orb digits (1495_0), which the in-mission counter uses |

## 2. Customize index: slot 2294 (id600)

| Element | Picture | Crop | Screen (x, y) |
|---|---|---|---|
| Background + **Divinity Statue (woman holding an hourglass)** | 2294_0 (512x512) | (0,0)-(512,448) | full screen -256..256, -192..192 |
| Description box (text 20000-20007) | 2294_1 | (0,2)-(256,130) | 12..235, -165..-50 |
| Red bar / L1 R1 / red-and-black arrow cursors | 2294_1 | bar (0,138)-(256,182); L1 ~(4,196)-(42,218); R1 ~(52,196)-(90,218); arrows ~(4,226)-(60,256) (measured from the art) | not used by this MOD |
| "CUSTOMIZE" title | 2294_2 | (1,0)-(255,63) | -198..-75, 137..170 |
| STYLE | 2294_2 | (0,65)-(127,111) | -176..-90, 0..30 |
| EQUIP | 2294_2 | (35,160)-(160,207) | -196..-105, -30..0 |
| ITEM | 2294_2 | (147,112)-(248,159) | -206..-130, -60..-30 |
| ACTION | 2294_2 | (1,112)-(144,159) | -197..-94, -90..-60 |
| GUNS | 2294_2 | (132,64)-(256,111) | -161..-75, -120..-90 |
| EXIT | 2294_2 | (160,161)-(255,207) | -108..-40, -150..-120 |
| Swoosh arc behind the list | 2294_3 | (0,122)-(256,256) | -216..30, -155..35 |
| Vertical red brush stroke (rotated quad) | 2294_3 | (0,100)-(137,124) | x -208..-160, full height |
| Cursor/highlight bar, one per row (6) | 2294_4 | (1,1)-(255,46) | rows at y 0..30, -30..0, -60..-30, -90..-60, -120..-90, -150..-120; x roughly label x0-20 .. x1+20 |

The menu order, top to bottom, is STYLE, EQUIP, ITEM, ACTION, GUNS, EXIT, staggered along the arc.

## 3. Item shop: slot 2649 (id822)

| Element | Picture | Crop | Screen |
|---|---|---|---|
| Background (dark eye / smoke) | 2649_0 | (0,0)-(512,448) | full |
| Row bar x7 | 2649_1 | (1,1)-(255,62) | x -258..88; rows y 97..127, 62..92, 27..57, -8..22, -43..-13, -78..-48, -113..-83 (35-unit pitch) |
| Wide bottom bar | 2649_1 | (0,1)-(256,63) | -257..157, -145..-120 |
| Item icons (one per row, x -218..-182, 35x35) | 2649_2 | Vital Star S (192,1)-(256,64); Vital Star L (0,64)-(64,128); Devil Star (64,65)-(128,128); Holy Water (128,64)-(192,128); Blue Orb (0,0)-(64,64); Purple Orb (64,0)-(128,64); Gold Orb (128,0)-(192,64). Two grey masks at (0,192)-(128,256) | rows top to bottom in that order |
| Price digits, 6 per row (white/red font, 32x48 glyphs) | 2649_3 | glyph g at ((g%5)*32, (g//5)*48) for 0-9; × (160,0); / (192,0); red-orb mark (160,48); · (192,48); - (224,48); darker copy in the lower half (y+96) | x -175..-85 in 15-unit cells, row y as above |
| Count "00/00" (owned/max) | 2649_4 (same font) | / = (193,0)-(223,48) | 150..225, -124..-101 |
| Yellow Orb icon + 2 digits | 2649_5 | icon (2,81)-(70,126); small font 24x40: digit at (g*24, 0) for 0-4, ((g-5)*24, 40) for 5-9; ':' (120,0), '/' (141,0)-(159,40) | icon -182..-150, -156..-107; digits -152..-126 |
| Blue Orb Fragment icon + "n/4" | 2649_6 | icon (73,81)-(141,126) | icon -102..-70; digits -72..-33, y -148..-127 |
| Item-name word pieces (yellow, from row 128 down) | 2649_1 | ORB (165,134)-(219,152); L (234,134)-(246,152); WATER (4,164)-(88,185); BLUE (102,164)-(164,185); VITAL (181,164)-(249,185); PURPLE (7,197)-(105,218); YELLOW (118,197)-(215,218); S (231,197)-(247,218); DEVIL (10,228)-(86,250); STAR (102,228)-(169,250); HOLY (187,228)-(245,250) | placed by code, not by the MOD |

The red-orb balance has no glyph of its own here; the font has a red-orb mark at (160,48) in 2649_3/4.
SLUS has a glyph-UV table for this screen at **0x5db780** (the 32x48 font: 0,0 32,0 ... 160,48 ...) and
**0x5db850** (the 24x40 font), plus screen x,y pairs at 0x5db600 (192,82 / 192,102 / 192,123 / 192,143 /
192,164: probably 640x448 pixel positions for the price column).

**Prices (SLUS 0x5db680, u32, 8 per item, the n-th purchase costs entry n, staying at the last one).** The order comes from the message-id table at 0x5db640:

| Item (msg id) | 1st | 2nd | 3rd | 4th | 5th | 6th | 7th+ |
|---|---|---|---|---|---|---|---|
| Vital Star S (20003) | 500 | 750 | 1200 | 1800 | 2500 | 3500 | 5000 |
| Vital Star L (20004) | 2000 | 3000 | 4500 | 6000 | 7500 | 10000 | 10000 |
| Devil Star (20005) | 3000 | 5000 | 7000 | 9000 | 10000 | 10000 | 10000 |
| Holy Water (20006) | 10000 | 15000 | 20000 | 25000 | 30000 | 30000 | 30000 |
| Blue Orb (20000) | 5000 | 10000 | 15000 | 20000 | 30000 | 50000 | 50000 |
| Purple Orb (20001) | 3000 | 5000 | 7000 | 9000 | 10000 | 20000 | 30000 |
| Gold Orb (20002) | 10000 | 15000 | 20000 | 20000 | ... | | 20000 |
| Yellow Orb (20028) | 1000 | 1500 | 2000 | 3000 | ... | | 3000 |

The table at 0x5db660 (11017 11016 11018 11019 11007 11008 11005 11006) holds a second id per item,
probably the File/description message ids. SE has **no Vital Star M**: the disc only has S and L.

## 4. Action (skill) shop: 2322 (id700) + lists 2330-2370 + previews 2378-2546

| Element | Picture | Crop | Screen |
|---|---|---|---|
| "ACTION" background | 2322_0 | (0,0)-(512,448) | full |
| Weapon list Rebellion / Cerberus / Agni&Rudra / Nevan | 2322_1 | rows (0,0)-(256,32), (0,64)-(256,96), (0,128)-(256,160), (0,192)-(256,224); the other 32-px rows are the highlighted copies | -98..60 at y 93..121, 66..95, 40..68, 13..41 |
| Beowulf (and YAMATO for Vergil) | 2322_2 | (0,0)-(256,32) | -94..47, -13..15 |
| Weapon wheel (red ring) | 2322_4 | (0,0)-(256,256) | -250..-94, -21..132 |
| L1 / R1 pips, arrow | 2322_5 | arrow (208,16)-(248,48); R1 (48,16)-(96,64); L1 (0,16)-(48,64) | arrow -116..-97, 42..59 and -240..-219, 42..61; R1 -123..-92, 54..84; L1 -246..-214, 56..86 |
| Skill row frame x4 | 2322_3 | (0,0)-(256,128) | -247..8 at y -57..-19, -90..-53, -124..-86, -157..-119 |
| Description panel (slides in from below) | 2322_3 | (0,0)-(256,128) | 1..249, -299..-223 |
| Skill names per weapon | 2330_0 .. 2370_0 (256x128, 32-px rows) | row r = (0,32r)-(256,32r+32) | -239..-30, rows y -56..-26, -90..-60, -123..-93, -157..-127; an upgrade (e.g. Stinger then Stinger Level 2) shares a row |
| Weapon art | 2330_1 .. 2370_1 | (0,0)-(256,204) | about -234..-96, 0..111 |
| Skill preview + price | 2378_0 .. 2546_0 | (0,0)-(512,448) | full screen; price lettering about (380,360)-(480,400) |

**Skill prices (SLUS 0x5cc710, u32, 5 per weapon, -1 = none).** Order = description msg ids 20010.. in id700:

- Rebellion: Stinger 2500 (20010), Stinger Lv2 10000 (20011), Drive 10000 (20012), Air Hike 20000 (20013)
- Cerberus: Revolver Lv2 15000 (20020), Windmill 7500 (20021)
- Agni & Rudra: Jet-Stream Lv2 10000 (20030), Jet-Stream Lv3 15000 (20031), Whirlwind 7500 (20032), Air Hike 20000 (20033)
- Nevan: Reverb Shock 7500 (20040), Reverb Shock Lv2 15000 (20041), Bat Rift Lv2 10000 (20042), Air Raid 20000 (20043), Volume Up 20000 (20044)
- Beowulf: Straight Lv2 10000 (20050), Beast Uppercut 7500 (20051), Rising Dragon 15000 (20052), Air Hike 20000 (20053)
- Vergil (id6210): Yamato 5000 / 15000 / 10000 / 20000; Beowulf 7500 / 5000 / 15000; Force Edge 13000 / 5000 / 10000 / 10000; then 20000 / 7000 / 15000 (msgs 20070.. in 3424)

These match the painted prices: id740 2500, 741 10000, 742 10000, 743 20000, 745 15000, 746 7500,
748 10000, 749 15000, 750 7500, 751 20000, 753 7500, 754 15000, 755 10000, 756 20000, 757 20000,
758 10000, 759 7500, 760 15000, 761 20000. id744, 747 and 752 are unfinished Japanese placeholders
(飛行, 溜め押し技, 2段ジャンプ).

## 5. Gun shop: slot 3017 (id3000)

| Element | Picture | Crop | Screen |
|---|---|---|---|
| "GUNS" background | 3017_0 | (0,0)-(512,448) | full |
| E&I / Shotgun / Artemis / Spiral names | 3017_1 | rows (0,0)-(256,32), (0,64)-(256,96), (0,128)-(256,160), (0,192)-(256,224) | -219..-30, 84..114 / -209..-54, 34..64 / -219..-63, -15..15 / -205..-68, -65..-35 |
| Kalina Ann | 3017_3 | (0,0)-(256,32) | -218..-49, -117..-86 |
| Wheel (red ring) | 3017_2 | (0,0)-(256,256) | -9..241, -101..142 |
| Price box x5 + description box | 3017_4 | (0,0)-(256,128) | -142..-34 at y 61..87, 11..37, -39..-13, -89..-63, -141..-115; desc box -38..244, -150..-81 |
| Red brush highlight x5 | 3017_5 | (0,0)-(256,80) | behind each gun name (y 77..123 ... -125..-81) |
| Lv1 / Lv2 / Lv3 pips | 3017_6 | (0,0)-(32,32), (32,0)-(64,32), (64,0)-(96,32) | -76..-48 beside each gun |
| Price lettering 10000 / 20000 / 15000 / 7500 / 5000 | 3017_7 (256x128) | 0.25 x 0.5 UV cells (see crops.txt part 12) | -146..-48 inside each price box |
| Confirm-dialog window | 3017_8 (256x128, alpha stored as 0, so draw it opaque) | whole | code-placed |

**Gun prices (SLUS 0x5d6880, 3 per gun: Lv2, Lv3, -1):** E&I 5000/10000; Shotgun 10000/20000;
Artemis 10000/20000; Spiral 7500/15000; Kalina Ann 5000/10000. Description ids follow at 0x5d6860
(20010..20020). Vergil's Summoned Swords (0x5e5ca0): Lv2 7500, Lv3 15000, Spiral Swords 20000.

## 6. Equip (2302 id620) and Style (1716 id300)

- Equip: background 2302_0; four red rings 2302_1 (two per side, about -248..-65 / 72..248); gun names
  (2302_2/2302_3, x 104..314, rows y 29..61 down to -85..-53) and devil-arm names (2302_4/2302_5, x
  -235..-32); L1/R1, Lv pips and labels in 2302_6 (MOD part 8: L1 (0,16)-(48,64) at -182..-146, 94..129;
  R1 (48,16)-(96,64) at 137..174, 92..127; "DEVIL ARMS" (0,64)-(180,96) at 105..221, 68..93; "GUNS"
  (0,96)-(128,128) at -194..-107, 71..96; Lv1/2/3 (96,16)-(192,48) in 32-px cells; arrow (208,16)-(248,48)); bar 2302_7 at -133..65,
  -134..-117. Weapon icons are 2309..2320.
- Style: background 1716_0 ("STYLE"); style names 1716_3/1716_4 (two rows each: plain and highlighted);
  Lv1-3, the Japanese button legends 決定/詳細/戻る and the panel are in 1716_1/1716_2; the style emblems are
  1716_7..1716_12; weapon names are 1716_13/1716_14. This PAC keeps all its texture sets at the end
  (parts 18-20), so its MOD texture indices run across the combined list. Not decoded further.

## 7. Text (English)

All from the `# Msg` blocks inside each English PAC: `extract_msgs.py` -> `msgs_all.json` (every
language). `msgs_shop_eng.txt` has the shop's English strings. Markup: `<BR>` new line, `<HBR>` half
line, `<BTN n>` button glyph (0-18, see the file header in id600), `<N00>` number.

- Index descriptions (2294 id600): 20000 "Purchase items." 20001 "Learn new actions." 20002 "Increase the
  power of your guns." 20003 "Change equipment." 20004 "Change your style." 20005 "Return to game."
  20006 "View equipment." 20007 "View your style." The menu labels themselves are art (2294_2).
- Item names and prompts (2649 id822): 20000 Blue Orb, 20001 Purple Orb, 20002 Gold Orb, 20003 Vital Star S,
  20004 Vital Star L, 20005 Devil Star, 20006 Holy Water, 20007-20027 key items, 20028 Yellow Orb;
  20050 "Use items." 20051 "Use this item?" 20052 "You can't use this item now." 20053 "This item is used
  automatically."; 20100 "Purchase items." 20101 "Will you use Red Orbs to buy this item?" 20102 "You
  don't have enough Red Orbs to buy this item." 20103 "You don't need anymore of this item." 20104 "You
  cannot purchase this item yet."
- Item descriptions (2760 id880, Library): 20060 Red Orb .. 20067 Yellow Orb (20063 Blue Orb Fragment,
  20064 Blue Orb, 20065 Purple Orb, 20066 Gold Orb), 20070 Vital Star S, 20071 Vital Star L, 20072 Devil
  Star, 20073 Holy Water. Move descriptions from 20160 on (Rebellion: 20165 Stinger, 20166 Stinger 2,
  20169 Drive, 20170 Air Hike ...), with English labels in the comments.
- Action shop (2322 id700): 20000 "Learn this skill?" 20001 "You don't have enough Orbs." 20002 "You
  already know this skill." 20003 "A new skill is now available." 20004 "Your Level is not high enough to
  learn this skill." 20009 "<BTN 7><BTN 4>：Weapon Select  <BTN 16>：Confirm  <BTN 17>：Exit", then the skill
  descriptions 20010-20053 (Dante) and 20070-20093 (Vergil, unused here).
- Skill **names** are only art: the lists 2330..2370 (e.g. "Stinger / Stinger: Level 2 / Drive / Air
  Hike") and the 1716/1495 sheets. As text they appear only as the English comment labels in id880.
- Gun shop (3017 id3000): 20000 "Enhance this gun?" 20001 "You don't have enough Orbs." 20002/20009 "You
  can't enhance this gun anymore." 20003 "This skill has reached the max level." 20004 "This skill can now
  be powered up to a new level." 20008 "COST: <N00> orb"; 20010 E&I "Raises max firepower." 20012
  Shotgun "Raises max bullet output." 20014 Artemis "Raises max number of lock-on targets." 20016 Spiral
  "Raises max firepower." 20018 Kalina Ann "Firing speed increases."
- Equip (2302 id620): 20000 "Already equipped! ...", 20001 "You currently cannot change your equipped
  weapons. ..."
- SLUS_213.61 has no player-facing shop text, only internal names (RED_ORB, VITAL_STAR_S/L, HOLY_WATER,
  BLUE_ORB_FRAGMENT ... at 0x5aa900-0x5aa9d8 and 0x618f00) and the class names above.

Not found: a bitmap font for the message text. The PACs carry only the strings, and the port will need
its own font. Which of 2649 and 2671 `CUIDCustomItem` loads is inferred from content, not traced: the
two are byte-for-byte the same art and text.

## Files

- `dump_slots.py`: every TIM2 in slots 1495-3861 (English/base names) -> `png/`, `index.json`
- `sheets.py`: contact sheets -> `sheets/` (`s_*` overview, `z_*` per slot, `action_prices.png`, `compose_all.png`)
- `inspect_pac.py` -> `pac_layout.txt`; `crops.py` -> `crops.txt`, `crops_more.txt`
- `extract_msgs.py` -> `msgs_all.json`; `msgs_shop_eng.txt`
- `compose.py` -> `compose_<slot>.png`
