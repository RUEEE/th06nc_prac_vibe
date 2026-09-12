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

Before allocating or writing remote memory, the launcher reads the target's
full image path, confirms its filename and x64 architecture, then checks code
signatures at three independent RVAs used by the supported Steam build. This
prevents an unrelated executable merely named `th06nc.exe` from receiving the
DLL. Injection diagnostics include the PID, full target path, full DLL path,
failed stage, and Win32 error text/code where available.
Main-module enumeration is retried for up to one second to tolerate loader
startup races. While waiting for a newly launched game, transient candidate
failures are retained and shown only if no later candidate succeeds.

## Graphics bootstrap

The DLL creates a hidden temporary Direct3D 11 swap chain to discover the
system's actual `IDXGISwapChain::Present` and `ResizeBuffers` entry points, then
uses the statically linked MinHook source to detour those function entries.
Unlike replacing only the temporary object's virtual table, this also reaches
the game's swap chain when injection occurs after that object was created.
As in the known-good zxxsmart implementation, ImGui initialization is deferred
until an overlay is actually requested. This avoids touching an incomplete or
auxiliary presentation path during Windows 7 game startup. Once initialized,
the renderer stays bound to that exact swap chain. D3D9 devices observed from
Steam or compatibility overlays cannot become the ImGui renderer.

Renderer diagnostics are opt-in. The DLL creates
`%APPDATA%\shanghaialice\th06nc\input.ini` with `[Options] debug=0` when the
key is absent. Setting it to `1` enables the injected diagnostic console,
step-by-step Present/ImGui initialization messages, and Direct3D failure
dialogs. With `debug=0`, none of those diagnostic windows or dialogs appear.
System-font discovery tries both the Windows 7 `msyh.ttf` layout and newer
`msyh.ttc`, followed by other CJK fonts and ImGui's built-in fallback.

The Present hooks own the ImGui frame. Game-update hooks never call ImGui.
Persistent practice selections live in one ordinary `PracticeParam` because
TH06NC executes the relevant game/UI callbacks serially. Short-lived action
mailboxes and hook-lifetime state retain atomics so a future renderer/threading
change cannot lose an edge:

```text
game update thread                         rendering/Present thread
------------------                         ------------------------
native Practice selector  -- action -->    replacement Practice UI
player/timeline hooks      <-- struct --    persistent `PracticeParam`
                                             ImGui rendering
```

## Scaling

ImGui font/style scale is derived from display height. The reference is a
2.5x scale at 1440 pixels, so controls retain approximately the same fraction
of window height at other resolutions. The F9-F12 base window covers the complete
display; the compact Backspace window is independent.

The font atlas does not load ImGui's full Chinese range. At context creation,
`Locale::AppendAllGlyphText()` enumerates every Chinese, English, and Japanese
UI/jump string, including fake-shot spell variants. `ImFontGlyphRangesBuilder`
combines their code points with the default Latin range and the three language
selector labels. The resulting range vector persists for the lifetime of the
font atlas, allowing runtime language switching with a much smaller texture.

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

## Windows 7 import policy

Both binaries declare `WINVER` and `_WIN32_WINNT` as Windows 7 (`0x0601`) while
continuing to use the current `v143` toolset and Windows SDK. Release imports
must be audited after toolset updates because a single unavailable hard import
causes the Windows loader to reject the binary before any compatibility code
can run.

The overlay intentionally does not link or load a D3DCompiler DLL. The ImGui
and stretch-pipeline Shader Model 4 bytecode is compiled ahead of time and
embedded in `d3d11_shaders.h`; its editable sources are under
`overlay/shaders`. This avoids both a Windows 7 loader failure on
`D3DCompiler_47.dll` and an invisible Direct3D 11 UI when no fallback compiler
DLL is installed.

The current Release import table uses `GetSystemTimeAsFileTime`, not the
Windows 8-only `GetSystemTimePreciseAsFileTime`. Therefore the old thprac
`IATPatch` substitution has no target in this build and is deliberately not
run. This must be checked again whenever the MSVC runtime is updated.
