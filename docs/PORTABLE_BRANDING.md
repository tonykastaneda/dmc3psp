# Portable logo

The local transparent master is `.tools/branding/dmc3_portable.png`. It is derived
from the disc's title logo and must stay gitignored. No branding image is shipped
in this public repository. With that file absent, both preparation tools retain
the original disc logo.

`tools/portable_branding.py` loads and scales the local master. `prepare_menu.py`
omits only the old title logo quad, then composites the replacement into the baked
title background. The options band and runtime menu code are unchanged.
`prepare_xmb.py` uses the same logo for ICON0 and overlays it on PIC1's left side.
The original PIC1 source crop was checked against the existing background and
matched exactly. The background pixels outside the logo remain unchanged.

The build tracks the local master's SHA-256, including its presence or removal,
and the preparation scripts' timestamps. Changing the branding regenerates both
MENU.BIN and the XMB pictures before packing the EBOOT. Explicit `--logo PATH`
arguments are also available on the preparation tools.

## Image workflow

Built-in image generation supplied the new cursive subtitle and a repair of the
small region beneath the old subtitle. The generated main lettering was discarded:
the final composite retains the original lettering pixels and the original Cry
descender. Only the former subtitle region receives the repair and new lettering.
The source atlas, intermediate generations, and final PNGs remain local.

Final subtitle prompt:

> Use case: text-localization. Supporting style reference only: the silver cursive
> 'Special Edition' subtitle in this logo. Generate ONLY the exact word 'Portable'
> in that same narrow slanted calligraphic italic script, metallic silver-white
> strokes with thin black edging and a small dark drop shadow. P-o-r-t-a-b-l-e,
> uppercase P, lowercase ortable. Single line, gently rising italic energy like
> the source. Match thin pen strokes and original subtitle weight, not heavy bold.
> Transparent RGBA background. No Devil May Cry lettering, no red 3, no silhouette,
> no glow, no other graphic or word. This is a replacement subtitle to composite
> under the unchanged original logo.

Repair prompt:

> Use case: precise-object-edit. Remove ONLY the silver-white cursive subtitle
> 'Special Edition' and its black shadow from the lower right of this exact
> transparent logo. Reconstruct the red glow or transparency behind that small
> subtitle. Do not add any new text. Preserve all the black gothic Devil May Cry
> lettering, exact large red 3 design, silhouette, trademark, original 512x256
> composition and proportions. No redesign, no rescaling, no new effects. Output
> clean RGBA transparency. The desired result is this exact original logo without
> its small cursive subtitle.

## Validation and captures

Build with `./tools/build_testroom.ps1`. The title image is baked into MENU.BIN;
no additional runtime texture or draw call is required. Verify in PPSSPP:

```powershell
python tools/ppsspp_scene.py portable_title --mission 0 --menu --frames 60 --line "mshot 45" --line "mkey cross 70" --line "mkey cross 110" --line "mkey cross 150"
```

The scene tool now exports menu BMP captures as PNGs, alongside game captures.
The title was visually checked at 480x272. ICON0 is 144x80 and PIC1 is 480x272;
the packed EBOOT image sections were compared byte-for-byte with their previews.

Local deliverables:

- `analysis/testroom/dmc3_portable_logo.png`: transparent master, 512x256.
- `analysis/testroom/dmc3_portable_logo_psp.png`: transparent 480x272 canvas.
- `analysis/testroom/portable_title_menu_45.png`: actual PPSSPP title capture.
- `analysis/testroom/dmc3_portable_pic1.png`: final PIC1 preview.
- `analysis/testroom/dmc3_portable_icon0.png`: final ICON0 preview.

All five paths and the local master were checked with `git check-ignore` before
committing the integration code.
