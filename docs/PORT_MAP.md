# DMC3 PS2 → PSP Technical Port Map

This map separates shipped-artifact observations, static-analysis inferences, and remaining unknowns. Decompiled pseudocode is a reconstruction aid, not original source or proof of runtime behavior.

## Target identity

- Disc volume: `DEVIL3SE`, NTSC, `SLUS_213.61`, version 1.00.
- ISO size: `4,410,736,640` bytes.
- ISO SHA-256: `78ad0f94c04f3632b7d8bb45a8db5ece90113c600c5e5e624a55c443745e41b0`.
- ELF SHA-256: `a538ac05f313f193fbf86d44aa6cf97764456ce1994a896cc6ef5aff17b11a2b`.
- ELF32 little-endian MIPS, `EF_MIPS_MACH_5900`, entry `0x00100008`.
- File-backed load range: `0x00100000` + `5,352,320` bytes; declared memory segment size `30,780,160` bytes.
- No dynamic exports, usable symbol table, relocations, or ELF notes.

REA Evidence is retained in `analysis/rea/evidence.json`. The available REA Windows native providers did not admit this MIPS ELF for deep decompilation, so the R5900 analysis uses Ghidra 12.2 with `r5900:LE:32:default`.

## Static program map

Ghidra's completed analysis currently reports:

| Item | Count/value |
|---|---:|
| Functions | 12,046 |
| Instructions | 1,106,277 |
| Direct call edges exported | 36,211 |
| Defined strings | 4,503 |

Exports live in `analysis/ghidra/export/`:

- `functions.csv` — address, size, signature, and flags.
- `calls.csv` — caller/callee graph.
- `string_xrefs.csv` — string address, referencing instruction/function, and value.
- `decompiled/` — selected R5900 pseudocode.

### Boot and platform chain

| Address | Observed role |
|---|---|
| `0x00100008` | Clears BSS, performs early syscalls, enters runtime initialization and the main loop. |
| `0x00116a48` | Early runtime setup called by the entry point. |
| `0x001ec410` | Repeated platform/frame processing loop. |
| `0x001edd60` | IOP/module initialization path. |
| `0x001c3f10` | Loads `MODULE.BIN`, transfers it to the IOP, relocates module pointers, and installs RPC interfaces. |
| `0x001ec090` | Builds a `cdrom0:` path, normalizes separators, opens it, and obtains file size/position information. |
| `0x004fd860` | CAPCOM memory-card subsystem and MCMAN/MCSERV module references. |

These PS2 SDK, SIF/DMA, IOP, GS, VU, IPU, pad, and memory-card boundaries require PSP platform implementations or build-time replacements.

## Disc and archive map

| File | ISO byte offset | Size | Members |
|---|---:|---:|---:|
| `MDATA1.AFS` | `0x0060c000` | 927,674,368 | 19 named SFD videos |
| `MDATA2.AFS` | `0x37abf000` | 1,335,275,520 | 13 named SFD videos |
| `SDATA.AFS` | `0x8742a000` | 874,715,136 | 154 named audio/data entries |
| `GDATA.AFS` | `0xbb65b800` | 1,245,739,008 | 3,863 numeric slots |

`0FLIST.DIR` lists those four archive filenames. The game executable resolves the same names in `FUN_001aa8d0`.

### AFS startup behavior

`FUN_001aa8d0` performs this sequence for SDATA, GDATA, MDATA1, and MDATA2:

1. Clear the archive-state region.
2. Call `FUN_00144dd0` with archive ID, filename, and state storage.
3. Submit progress through `FUN_001aabb0`.
4. Poll `FUN_00145258` until state `3`, pumping `FUN_001489e8` and a frame/yield helper.

Recovered state offsets relative to the initialization object's base:

| Archive | State offset | Cleared bytes |
|---|---:|---:|
| GDATA | `0x0000` | `0x3d78` |
| SDATA | `0x3d78` | 900 |
| MDATA1 | `0x40fc` | `0x168` |
| MDATA2 | `0x4264` | `0x150` |

`FUN_00145258` statically reveals asynchronous sector reads, format validation, entry-count parsing, 16/32-bit offset handling, offset conversion, and ready/error states (`3`/`4`). The full `0x144000–0x146fff` library range has been exported for implementation reference.

## GDATA and PAC map

The GDATA AFS has no central filename table. `tools/dmc3_pac.py` preserves numeric slot identity and classifies each member:

- 3,183 `PAC\0` containers.
- 680 raw/non-PAC members.
- 14,969 top-level chunks.
- 533 nested PACs.
- 2,547 zero/null chunk placeholders.
- Zero malformed containers under the validated offset rules.

Observed signatures include:

- `MOD ` model data and `MOT ` motion-related data.
- `TIM2` textures and `PNST` data.
- Nested `PAC\0` containers.
- `LIG2` lighting and `SCM ` scene/camera-related data.
- `CAM`, `EVE`, `POS`, `ITM`, `STE`, `DCA`, `EST`, and `SEF` records.
- `HITS` collision/hit data.
- `SHW ` and `EFM ` data.
- Text scripts for player (`pl`), enemy (`em`), stage (`st`), weapon (`wp`), cloth (`clt`), and event/configuration content.
- SFD movies, ADX audio, and `ipum`/`SPUM` data.

Representative slot 0 (`analysis/assets/gdata/slot_00000/`) contains 15 top-level chunks, including two `MOD ` chunks, three nested PACs, two `SHW ` chunks, binary animation/data, and a readable `pl000_02.clt` script. Recursive extraction validated 95 output files from this member.

## Engine/scene vocabulary recovered from RTTI strings

The executable contains names for `CScene`, `CSceneMgr`, `CSceneGame`, `CSceneBoot`, `CSceneOpening`, `CSceneStartMenu`, `CSceneGameMain`, `CSceneMgrGame`, `CSceneMgrRoot`, `CSceneFactoryApp`, `CSceneDemo`, `CSceneMisStart`, `CSceneMisSelect`, `CSceneResult`, and `CSceneEnding`, plus `CPl000Shl00` through additional player-shell variants. These provide subsystem names for the next function/vtable mapping pass; they do not by themselves prove method behavior.

## Implemented runtime milestones

- Converted all 87 indexed standalone TIM2 resources with zero failures; model texture containers also decode correctly.
- Recovered PS2 MOD object records and stream tables: command fields, float3 positions/normals, signed-16 UV/4096, bone indices, and weights.
- Exported the first model as OBJ and generated a freestanding PSP runtime containing 5,316 vertices, 5,282 triangles, and three original textures.
- Adapted initial difficulty/gauge routines from the CC0 decomp reference and verified them on PSP.
- Built a textured, lit, z-buffered software renderer. Precomputed vertex transforms and a 240×136 raster buffer with 2× upscale improved measured performance from 12.5 FPS to **37.4 FPS**.
- Excluded only MOD object 8 (`0x8002`) from the static runtime; its stretched geometry requires the separate deformation path. All `0x8001` objects remain, and the host preview is visually clean.
- `PERF.TXT`, `BOOT_OK.TXT`, and the host preview provide repeatable performance and visual evidence. Runtime target is 30 FPS on PSP-2000/3000/Go/Street and Adrenaline on Vita, with `MEMSIZE=1`.

## Port implementation order

1. **Archive compatibility** — finish naming AFS library routines; implement and test equivalent PSP/host readers against the original archives and numeric slots.
2. **Resource identity** — map GDATA numeric slots to scene/player/enemy/stage ownership using script names, RTTI references, and loader cross-references.
3. **Asset conversion** — recover MOD/PNST/TIM2/MOT/SHW layouts; generate indexed PSP geometry, textures, animations, and collision data with bounded memory use.
4. **Platform layer** — replace PS2 memory, DMA, SIF/IOP RPC, GS/VU/IPU, pad, memory-card, and CD access behind explicit interfaces.
5. **Scene runtime** — reconstruct scene transitions, script VM, entity lifecycle, camera, and resource streaming.
6. **Gameplay** — port player/enemy state machines, combat, physics/collision, items, progression, and AI; compare behavior against the PS2 build.
7. **Presentation** — PSP rendering, audio conversion/playback, video policy, UI, saves, and performance/memory tuning.

## Immediate reverse-engineering targets

- AFS `0x144000–0x146fff`: assign semantic names and document public APIs and state structures.
- Follow callers of AFS read/open APIs to identify GDATA slot request sites and scene ownership.
- Locate functions referencing `MOD `, `TIM2`, `PNST`, and `MOT ` parsing code; export their callers and data structures.
- Recover RTTI/vtable associations for scene and player classes to replace `FUN_xxxxxxxx` names with subsystem names.
- Inventory VU microcode and GS packet builders separately from portable gameplay logic.

## Remaining unknowns

- Complete numeric GDATA slot-to-resource naming map.
- Exact binary layouts and coordinate/unit conventions for models, animations, collision, lighting, and scene records.
- Which routines are portable game logic versus EE/GS/VU/IOP-specific implementation.
- Runtime timing, streaming, and memory-pressure behavior on PSP hardware.
- Full audio/video strategy within PSP memory and storage budgets.
