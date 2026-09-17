# Town bottom edges and short-scene backdrops

Follow-up to `cave-parallax.md`, 2026-09-17. Actor scale remains 1.

## Evidence and scope

* Henesys Park `100000200`: recorded native fallback bounds
  `(-118,-56,4708,1010)`. The bottom woodMarble/enH1 row is at y=900,
  with a deliberate 180px gap. Actual canvases have opaque prefixes of
  12/9/11/11 rows. The previous continuous-row constraint rejected the gap;
  furthermore its 1050px effective scene had no vertical camera travel.
  For these audited bounds only, join gaps up to 180px with at least 95%
  overall coverage, retaining the existing side/bottom proximity checks.
  If camera travel is insufficient, reduce the effective scene height at
  native scale: 956px, camera center 430, vertical clip 62..1018 at 1080p.
  The original upper content edge remains visible. Do not generalize this
  ravine exception to arbitrary disconnected platforms.

* Leafre `240000000`: explicit bounds `(-2385,-960,2834,666)`.
  Foreground 22 uses minar/back/27 (416x63, origin 208,31), position
  (-45,577), rx=5, ry=-100, type=4. Its geometric bottom is world y=609;
  the previous visible bottom was 658. With the audited bounds and exact
  placement signature, constrain the camera to 68 instead of 118, keeping
  the first row below the water outside the viewport. The foreground keeps
  its original horizontal animation, dimensions, and world attachment.
  This is an intentionally scoped asset policy, not a general rule for water
  or scrolling effects. If resource placement changes, this rule declines.

* Volcano heart `280020000` / `280020001`: 650px native map height produces
  a 634px effective scene. Backdrop projection still used 1080/600=1.8,
  although the world was clipped to 634px. Size decorations against the
  effective scene height: max(1,634/600). Preserve native camera parallax
  compensation and foreground/world scale. Full-height scenes still use
  their previous 1.8 multiplier. This applies to other letterboxed scenes
  too, as their backgrounds must use their own effective scene height.

## Validation

Build and unit tests cover the actual park bounds/gap, unchanged neighboring
bounds, Leafre camera limit, and rejection of altered water placement.
Actual resource metadata and opacity were read from installed IMG files;
no IMG files were saved or changed.

Native PCOM/Gr2D GPU readback checks the short-scene backdrop: a 60x40 patch
now occupies 63x42 pixels instead of 108x72, with actor/HUD dimensions
unchanged. Existing native parallax, cave side cap, AquaRoad joins, ground
camera, clipping and stage-transition tests remain applicable.

These are component regressions, not in-game acceptance of the three maps.
In research, visit `100000200`, `240000000`, `280020000`, `280020001` and
move to both sides / vertically where possible; verify the floor/water
bottom, lava overlap, Buff, actor size and map transitions. Recheck
`211042000` and `230010400` for side anchoring and joined backgrounds.
