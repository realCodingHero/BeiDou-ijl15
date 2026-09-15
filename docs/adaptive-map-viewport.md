# Adaptive map viewport, login presentation and status bar

The configured render size defines the framebuffer. The bounded world view
can be smaller on finite maps, with independent HUD coordinates; window
scaling then scales the finished frame. Increasing the resolution does not
add terrain or make old map canvases taller.

## Bounded world viewport and backgrounds

At render heights above 720, WorldViewport bounds the visible world to the VR
rectangle resolved by native CMapLoadable::RestoreViewRange. That retains the
engine's linked-map and foothold fallback; missing explicit VR is not treated
as an infinite scene. An eight-pixel inset hides candidate cut edges identified
by the asset audit. Inner extents are rounded down to even integers so an
integer camera center cannot escape a half-pixel edge on odd-sized maps.

The uniform scale is max(1, renderWidth/innerWidth, renderHeight/innerHeight).
The visible world dimensions are render dimensions divided by that scale;
camera limits keep this whole rectangle inside the bounds. Large maps retain
their configured world view, with only the edge inset. Small/narrow maps show
less world and larger actors, while the framebuffer and HUD remain at the
configured resolution. There is no map-ID patch list or runtime WZ mutation.

For flight map 200090500, the tree canvas ends exactly at VRTop=-633. Its
authored VR is (-809,-633)-(2765,179). A 1920x1080 render now shows about
1415.11x796 world pixels at scale 1.3568; camera Y=-227 gives a top of -625,
placing the cut eight world pixels outside the viewport. Moving the tree
independently would break its platform/portal/foothold alignment.

The guarded Gr2D_DX8 RenderLayer detour scales clip-space projection for world
layers only. It follows retained overlay ancestry before checking the root Z,
so relatively positive face/equipment sublayers still follow their world
parent. Negative children of HUD roots retain HUD coordinates. Native avatar
assembly, physics, server positions, skills and portals are unchanged.
Projection and the native layer's filter field are restored after each draw;
linear filtering is requested through Gr2D's own state cache. No additional
render target, neural pass, CPU pixel readback or cached COM/device reference
is added to production rendering. Shrinking the visible world keeps native
render-sized culling conservative rather than shrinking an actor's cull box.

World mouse events are inversely transformed before stage dispatch. Native
CWndMan::GetCursorPos(bWorld=1) is also transformed before adding the camera
origin, covering handlers that query the cursor rather than use event X/Y.
HUD events and the displayed cursor keep screen coordinates. Leaving a field
immediately disables the scale via current-stage identity; configuration
changes clear it until the next RestoreViewRange.

The upper-parallax LoadBack compensation remains restricted to front=0,
y<0, -100<ry<=0, type 0/1/4, but uses the effective scene height when the
bounded viewport is active. Foreground, world-attached back layers, moving
grids and login retain their existing rules. Weather generation range/center
now covers the three remaining old 390/590/300 constants. LimitedView's dark
canvas DrawRectangle now receives height instead of width.

Gr2D PE identity and the draw/overlay/Z/filter instructions must match before
the detour is installed. If unavailable, previous bottom-camera anchoring
remains the fallback. At 600p/720p the world projection stays native. The old
crashing 0x00642105 COM-release cave remains removed.

This is a viewport repair for existing artwork. VR is not proof that every
pixel inside it has matching art. Missing resources, authored seams deeper
inside VR, and unusual scripted layouts remain separate cases; the scan does
not claim that every map or animation has been visually played.

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

- All 316 resolution sites have operand/instruction signatures against the
  supported v83 EXE and retain transactional startup validation/rollback.
- The real EXE fixture covers 64 configurations, camera clamps and all 26
  hotkeys including gaps at 800x600, 1280x720, 1920x1080 and 2560x1440.
- Production background/login/camera/world-cursor caves are executed with native stack/register
  conventions, including original local writes and the frame MOVSD.
- Stage transitions, compatibility fallback, upper-layer exclusions and mouse
  round trips are tested. COM integration uses actual PCOM/Shape2D libraries.
- GPU tests verify cropped output pixels, window and in-place fullscreen
  composition, bounds rejection, state restoration and resource release.
- The real D3D8 proxy exercises the versioned bridge, field/login transitions,
  partial-present fallback, resets, managed textures and final Release.
- Independent image references, frame pacing, all four help actions, window
  resize/maximize and placement persistence regressions pass.
- The scene inventory covers 5,363 numeric maps (AreaCode.img is an index),
  167,251 objects, 431,960 tiles and 44,528 back/front layers. All 4,126
  explicit VR rectangles pass the production viewport bounds/input checks.
- Real PCOM/Gr2D/Canvas integration with the deployed D3D8-to-9 module verifies
  GPU pixels: world and equipment scale together, HUD stays 60x40 pixels,
  and stage exit restores a 60x40 world layer. It also checks 800-layer timing;
  that synthetic workload is not a game-wide performance guarantee.

Run tools/build-patch-integrity.ps1, tools/build-window-scaling.ps1
-ToolchainRoot <MSVC-root>, and tools/build-upscaling.ps1. GPU tests need desktop
GPU access. Run out/window-scaling/tests/StatusBarLayoutTests.exe <client-dir>
for real vector integration, and tools/audit-map-viewports.ps1 for inventory.

Research acceptance precedes formal deployment: check login, world/character
selection and mouse placement; the flight map's top/bottom; popup alignment;
and a town, small indoor map and vertically scrolling map. Exclusive fullscreen
mode switching and actual game visuals still require the user's game session.

Run tools/audit-scene-layers.ps1 to regenerate the complete scene/resource
inventory and out/scene-audit/bounds.txt. Pass that file to WorldViewportTests.exe
for data-driven camera checks. tools/test-world-viewport-native.ps1 takes
Client, ToolchainRoot and WzInclude paths; the last points to external WzLib
headers. It reads installed graphics libraries and runs in out/world-native,
without launching the game. See [the Chinese test matrix](map-viewport-test-matrix.md)
for representative map IDs and explicit acceptance limits.
