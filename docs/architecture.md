# Architecture and hook conventions

## Process layout

The project contains two binaries:

1. `th06nc_test_launcher.exe` locates or starts `th06nc.exe`, verifies that the
   target is 64-bit, and injects `th06nc_test.dll` with a remote
   `LoadLibraryW` call.
2. `th06nc_test.dll` installs game-code hooks and intercepts graphics API
   discovery so it can render ImGui after the game scene.

The launcher supports attaching to an existing process, attaching to a PID,
and starting an explicitly supplied executable suspended. In ordinary mode it
prefers a `th06nc.exe` beside the launcher and otherwise opens
`steam://rungameid/4659620`.

## Graphics bootstrap

The DLL patches the main module's `GetProcAddress` import. The replacement
observes requests for `D3D11CreateDevice`, `CreateDXGIFactory*`, and
`Direct3DCreate9*`, then patches the resulting DXGI or D3D9 virtual tables.
Already-resolved exports are also recovered after injection.

The Present hooks own the ImGui frame. Game-update hooks never call ImGui;
they exchange values through atomics and small request records. This is an
important thread boundary:

```text
game update thread                         rendering/Present thread
------------------                         ------------------------
native Practice selector  -- atomics -->   replacement Practice UI
player/timeline hooks      <-- request --   persistent UI settings
                                             ImGui rendering
```

## Scaling

ImGui font/style scale is derived from display height. The reference is a
2.5x scale at 1440 pixels, so controls retain approximately the same fraction
of window height at other resolutions. The F10 base window covers the complete
display; the compact Backspace window is independent.

## Safe x64 patching rules

- `GameAddress` values are RVAs, not absolute VAs.
- `ResolveGameAddress()` adds the RVA to the actual main-module base.
- Every fixed patch checks the original bytes before writing.
- Code pages are changed to `PAGE_EXECUTE_READWRITE` only for the write and the
  instruction cache is flushed afterward.
- Five-byte relative branches use relay blocks allocated within signed 32-bit
  displacement range. Relay blocks use an absolute indirect jump when the DLL
  target may be outside that range.
- A trampoline copies whole instructions before returning to the first
  unmodified instruction. Partial instruction overwrites are not allowed.

These checks deliberately fail closed. “Unsupported executable” is safer than
executing a hook against a shifted instruction stream.

## Installation order

`OverlayWorker` installs the practice jump, replacement Practice menu,
keyboard/action input, Backspace helper, and collision capture hooks before it
waits for a renderer. This allows game-flow hooks to work even if graphics API
discovery occurs later.

