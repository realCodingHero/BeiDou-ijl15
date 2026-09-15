# Adaptive map viewport, login presentation and status bar

The internal render size controls the visible world area; window scaling then
scales that finished image. Increasing the height does not add terrain or make
old map canvases taller.

## Map camera and backgrounds

CMapLoadable::RestoreViewRange reads each map's VR rectangle and keeps native
foothold/link fallback. Above 720p, a map shorter than the viewport uses
VRBottom - renderHeight/2 for both vertical camera limits. Horizontal centering
and tall-map scrolling remain native. 600p/720p behavior is preserved.

For flight map 200090510, VRTop=-748 and VRBottom=179. At 1080p the previous
center was -284, exposing sky down to world Y=256 below clouds ending at Y=196.
The bottom-aligned center is -361, so the clouds cover the visible bottom=179.

The same map's crescent background has a deliberately flat-cut top. It is a
460x279 canvas with origin Y=139, placed at Y=-226 and parallax ry=-13. With
1080p's larger viewport, its top becomes visible around screen Y=222. Moving
the camera alone cannot fix both this top and the lower foreground boundary.

The common LoadBack hook at 0x0063D2F2 now adjusts upper parallax decorations:
front=0, y<0, -100<ry<=0, and type 0/1/4. It shifts Y up by
MulDiv(renderHeight-600, 100-ry, 200), retaining the 600p upper composition and
accounting for the bottom camera's parallax displacement. Both static and
animated layers consume the adjusted position. CLogin, foreground, world-
attached layers (ry=-100), vertical grids/motion and lower decorations retain
their authored coordinates. Layer dimensions, speeds, terrain and physics are
unchanged; there is no map-ID list or WZ mutation.

This is a layout rule for existing artwork. Finite artwork or unusual map
compositions still need visual review; the inventory is not a claim that every
map has been played or that missing scenery can be generated automatically.

The old cave at 0x00642105 was in nullable COM cleanup, not camera calculation.
It was removed; the original null branch remains intact.

## Centered status bar and popups

The 26-key status bar occupies 1280 logical pixels. Wider modes center the group
at (renderWidth-1280)/2 while keeping a 1280px local CWnd/canvas extent. The
native window-origin result is translated for creation and HP/MP flash effects.
No global COM objects are retained.

System and shortcut menu constructors have separate slide coordinates. The X
immediates at 0x00849E40 and 0x0084A5BE now receive the same horizontal offset;
each constructor uses this for all three slide positions. The four-action help
menu already derives its placement from the shortcut menu, so it follows the
new anchor without changing its actions or resources.

Quickslot hit testing remains relative to the moved CWnd. Its Y displacement
is always -507 because both components share the bottom anchor. The loop ends
after all 26 entries; the former cmp-[esi] corruption no longer reads past the
table on misses.

## Login viewport

The installed frame1920 contains an 800x600 book surrounded by opaque black.
Scaling the whole 1920x1080 buffer also scales that black area. Instead, use the
existing frame1280 and the corresponding centered 1280x720 login composition.
The existing lightweight renderer crops that area on GPU and linearly scales
it to the output. The game buffer stays at the configured resolution; no live
patch rewriting, device-mode switch, CPU readback or CuNNy pass is added.

The native frame RelMove pushes -640/-360, preserving its interleaved MOVSD.
At 1920x1080 the source rectangle is (320,180)-(1600,900). Existing centered
login controls remain in native render coordinates. Physical mouse points map
into that rectangle; cursor warps use the inverse mapping.

A versioned handshake between ijl15 and BeiDouUpscale enables this only after
the DX9 translator is created, with upscaling enabled, built-in custom login
art, and a 16:9 render mode larger than 1280x720. Missing/older DLLs and custom
user frames keep their former behavior. Native get_stage at BEDED4 and the
CLogin vtable AF6B24 restrict cropping to the login stage. The window input flag
is set after successful presentation and cleared on fallback, reset or field
entry; input also checks the current stage to reject a stale flag.

Windowed login uses the existing synchronized output chain. Fullscreen login
copies the crop to a separate texture before drawing back into the game
backbuffer, then lets native Present submit it. Ordinary fullscreen gameplay
keeps the native fast path; windowed gameplay keeps the accepted lightweight
linear/vsync path and full internal resolution.

## Validation

- All 307 resolution sites have operand/instruction signatures against the
  supported v83 EXE and retain transactional startup validation/rollback.
- The real EXE fixture covers 64 configurations, camera clamps and all 26
  hotkeys including gaps at 800x600, 1280x720, 1920x1080 and 2560x1440.
- Production background/login caves are executed with native stack/register
  conventions, including original local writes and the frame MOVSD.
- Stage transitions, compatibility fallback, upper-layer exclusions and mouse
  round trips are tested. COM integration uses actual PCOM/Shape2D libraries.
- GPU tests verify cropped output pixels, window and in-place fullscreen
  composition, bounds rejection, state restoration and resource release.
- The real D3D8 proxy exercises the versioned bridge, field/login transitions,
  partial-present fallback, resets, managed textures and final Release.
- Independent image references, frame pacing, all four help actions, window
  resize/maximize and placement persistence regressions pass.
- The read-only map inventory covers all 5,364 installed maps, including
  native VR fallback/link data; 1,952 maps contain 14,605 upper parallax
  layers matching the rule. There were no parse failures or invalid ranges.

Run tools/build-patch-integrity.ps1, tools/build-window-scaling.ps1
-ToolchainRoot <MSVC-root>, and tools/build-upscaling.ps1. GPU tests need desktop
GPU access. Run out/window-scaling/tests/StatusBarLayoutTests.exe <client-dir>
for real vector integration, and tools/audit-map-viewports.ps1 for inventory.

Research acceptance precedes formal deployment: check login, world/character
selection and mouse placement; the flight map's top/bottom; popup alignment;
and a town, small indoor map and vertically scrolling map. Exclusive fullscreen
mode switching and actual game visuals still require the user's game session.
