# Research help menu

The bottom Help button opens a two-entry popup using the native v83 ShortCut
(Interface) window: Maple Helper and Auction. The original Interface button
continues to open its original menu.

## Compatibility and configuration

- Client base: `03f11037eb7ce1fcd0c8cb46497760b9e7189cef` on `BeiDou`, including
  the completed window sizing/input/persistence fixes from PR #5.
- Supported BeiDou.exe/MoonKidsMS.exe SHA-256:
  `1198fa57ca5a7c489bae43ec13c69681d9cabe0f96762f3dc0357facf2e7d4df`.
- Enable `[optional] native_help_menu=true`. The source config defaults to false.
- Install `Data/UI/HelpMenu.img` and `EN/UI/HelpMenu.img` before enabling.
- The hook transaction checks the native function signatures and either installs
  all hooks or none. Normal ShortCut behavior is forwarded outside help mode.

## Native lifecycle and protocol

`CUIStatusBar::OnButtonClicked` (Help ID 1001) invokes the existing ShortCut path
(ID 1007) inside a scoped help mode. The native constructor, eight button ZRefs,
fade animation, modal loop and destructor remain responsible for ownership.
Two buttons receive new resource paths; the unused controls are initialized
below the screen and excluded from keyboard selection. The compact popup stays
anchored above Help and uses the current resolution's native Y position.

Selection closes the native modal with Cancel, preventing its normal inventory
or equipment dispatch. Only after the popup is destroyed does the wrapper send
one of the following packets:

| Entry | Full packet bytes |
| --- | --- |
| Maple Helper | `02 10 01 01` |
| Auction | `02 10 01 02` |

Opcode `0x1002`, version 1, action 1/2. Esc/cancel sends no packet. Up, Down and
Tab cycle between the two entries; Enter or Space selects the highlighted one.
The server opens NPC 9900001 independently of `use_mts`, while Auction respects
the existing MTS switch and gameplay restrictions. It returns normal NPC/MTS or
notice packets; there is no new server response opcode.

Backend PR: https://github.com/realCodingHero/MoonkidsMS/pull/147.
Research image commit: `4c18391180bdb1e1b8a3418d86ec17c61bb085fd`.
Deployment: https://github.com/realCodingHero/MoonkidsMS/actions/runs/34759089884.

## Resources

The resource builder derives both locales from the existing Chinese ShortCut
skin, retaining the frame, marker, gradient and all four button states. The
background is 93 x 84 pixels; each button is 81 x 25 pixels. Labels are authored
as pixels, so they do not depend on the Windows text code page.

All canvases explicitly use **Format2 (BGRA8888)**, matching the original
ShortCut assets. Do not use MapleLib's automatic `PNG` setter: it selected
Format257 for the buttons, which previews correctly in modern MapleLib but
renders as corrupted pixels in this v83 client. Verification must check the
serialized format, not just whether MapleLib can render the resulting image.

## Build and Research installation

Run in PowerShell 7 with the existing WzBridge MapleLib and x86 MSVC toolchain:

```powershell
./tools/build-help-menu-resources.ps1 -SourceClientPath C:\Game\BeiDou-Client-research
./tools/build-window-scaling.ps1 -ToolchainRoot C:\Game\BeiDou-Server\tools\msvc
./tools/install-help-menu-research.ps1
```

The installer is restricted to the Research directory and requires it to be
closed. It backs up the DLL, INI and any existing help images in
`.deployment-backups/help-menu-<timestamp>`, with a manifest recording which
files existed. Other INI bytes and the existing UIWindow.img are preserved.
Restart the client after updating images to clear the native resource cache.

For rollback, close Research, restore the files listed as existing in the
backup manifest, and remove only help images marked as newly created. The
pre-feature backup also restores `native_help_menu` to its previous state.

## Verification

- Backend CI: 49 tests passed, including 17 help-menu handler cases; exact-SHA
  Research image deployment completed successfully.
- Client fixture: helper/auction wire bytes, modal cancellation and cleanup
  before send, keyboard wrapping/keyup, original-menu isolation, disconnected
  socket and exception cleanup.
- Existing window geometry, resizing, mouse coordinates, maximize/restore and
  placement persistence regression suite passes with the new module compiled.
- Resource builder reparses both generated images, checks all button states,
  dimensions and Format2, then exports previews from the serialized images.
- The user verified in Research that Help opens two choices and both choices
  reach the correct feature. The first visual test identified Format257 button
  corruption; it is corrected by the explicit Format2 encoding above.

Remaining runtime check: restart Research and confirm that both corrected
labels are clear, including their hover/pressed appearance.
