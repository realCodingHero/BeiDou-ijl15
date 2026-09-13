# ItemEff lifetime fix

This directory contains the existing local cape-effect compatibility module,
previously built from `BeiDou-Server/tools/beidou-itemeff`. That build's SHA256
`b2a6f5660652ec6022b45c2f347c7c4fc0ca1fc1af5e47f866910d3882e271a8`
matches both clients' module from before this fix.

The module uses the existing [kaentake source](https://github.com/iw2d/kaentake)
at `2a43a01e7f4cdd75cda08ab533049230760d7cfa`, including its WzLib and Detours
dependencies. These dependencies are not copied into this directory. Pass the
checkout path as `KAENTAKE_ROOT` to CMake, or `-KaentakeRoot` to
`tools/build-itemeff.ps1`. Preserve the upstream notices and terms when obtaining
and distributing those dependencies.

The crash at `BeiDouItemEff.dll+0x1427` is a COM Release during destruction of the
global cape-property map. The dump's frame chain goes through the global map
destructor at `+0x17bc0` and CRT teardown. Its process differs from the separate
resolution instruction crash.

The catalog now retains only integer item IDs. Properties are resolved in the
active game callback, after the unchanged equipment/action/flip fast path, and
released before that callback returns. There are no WZ COM references in the
global catalog for DLL teardown to release. The avatar storage and native
rendering hooks keep their existing behavior.

`ItemEffLifetimeFixture.dll` compiles the actual catalog with game hooks disabled
only for the test build. `ItemEffLifetimeTests.exe` supplies a fake COM resource,
registers 407 items, checks that the catalog retains zero references, destroys
the resource, then unloads the DLL. Twenty cycles pass. The previous map would
retain 407 references and fail the test before teardown.

Build with `tools/build-itemeff.ps1`. The deployable module is
`out/itemeff/BeiDouItemEff.dll`; the separate fixture DLL must never be deployed.
