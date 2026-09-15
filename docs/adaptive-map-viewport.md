# 1080p map camera and centered status bar

The renderer's internal size controls the visible world area. Window scaling
then scales that finished image. Increasing the internal height does not make
old map canvases taller or give the map new terrain.

## Map camera

`CMapLoadable::RestoreViewRange` reads each map's VR rectangle, with the native
foothold/link fallback. The new rule applies at that common entry, without a
map-ID list or WZ edits. Above 720p, if the map is shorter than the viewport,
both vertical camera limits use `VRBottom - renderHeight/2` instead of their
average. Horizontal centering and tall-map scrolling retain native behavior.
The native 600p/720p presentation is preserved.

For the reported flight map 200090510, VRTop=-748 and VRBottom=179. At 1080p
the old camera center was -284 and the screen bottom reached world Y=256.
The last foreground cloud ends at Y=196, explaining the roughly 60-pixel
strip of exposed sky. The new center is -361 and the visible bottom is 179,
within the cloud coverage. All world layers move together; character size,
physics, object placement and parallax are unchanged.

This is a shared **camera-boundary correction**, not automatic creation of
missing artwork. A map shorter than the viewport now exposes the extra area
above its top edge; a narrow map can still expose areas on its sides. Special
maps with finite background art need visual review. Arbitrarily repeating
foreground clouds vertically would cover playable space, so this change does
not change background types or stretch terrain to hide missing assets.

The old cave at 0x00642105 was in nullable COM-pointer cleanup, not camera
calculation. It could call Release through an integer derived from the render
height. It has been removed and the original null branch is preserved.

## Status bar

The existing 26-key status bar occupies 1280 logical pixels. Wider internal
resolutions center that group with `(renderWidth - 1280)/2`. Its native CWnd
and canvases retain a 1280-pixel local extent. The native window-origin result
is translated for status-bar creation and HP/MP flash effects. Chat input,
backgrounds and quickslot animation share the same origin. No global COM
objects are retained; each translated vector belongs to its native layer.

Quickslot hit testing consumes coordinates relative to the moved CWnd.
Its Y displacement is always -507: both the CWnd and quickslot are anchored
to the bottom, so render height cancels out. The previous expression was
correct at 720p but drifted at 1080p. The loop now compares its table pointer
with the end of all 26 entries; the old byte write changed `cmp esi` into
`cmp [esi]`, allowing reads past the array on a miss.

## Validation and limitations

- All 305 resolution writes have complete-instruction/operand signatures for
  the supported v83 EXE. Startup uses the existing transactional validation.
- The real EXE fixture executes camera clamps and all 26 hotkey rectangles,
  including gaps/misses, at 800x600, 1280x720, 1920x1080 and 2560x1440.
- All 64 resolution/layout combinations, repeat configuration, rollback and
  the earlier crash-site regressions are tested.
- Origin ownership and failure paths use COM fixtures. A separate integration
  mode uses the actual client PCOM.dll and Shape2D.dll and advances the vector
  clock to verify inherited parent motion between render frames.
- The read-only map inventory parsed all 5,364 installed research maps:
  4,126 explicit vertical ranges (2,550 shorter than 1080p), 1,238 using native
  fallback/link data, zero malformed explicit ranges or parse failures.
  This inventory is not a claim that every map was visually tested.

Run `tools/build-patch-integrity.ps1` and
`tools/build-window-scaling.ps1 -ToolchainRoot <MSVC-root>`. For the real vector
integration test, run `out/window-scaling/tests/StatusBarLayoutTests.exe
<client-directory>`. `tools/audit-map-viewports.ps1` writes its report under
`out/` and never saves client WZ files.

Research gameplay acceptance is required before formal deployment. Check a
flight map, town, small indoor map and a vertically scrolling map; verify
chat/buttons/hotkey drag-and-drop and quickslot collapse/expand. The login
frame's separate 800x600 artwork/black-border issue is outside this change.
