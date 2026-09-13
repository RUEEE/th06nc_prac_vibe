# Enhanced-Practice pause and replay support

## User flow

The custom pause path is active only during an Enhanced Practice recording.
Escape opens an ImGui window with Resume, Restart, Exit-and-save, and direct
Exit-without-replay actions. `Esc`, then `Q`, selects direct exit and bypasses
the native replay-save screen.
The native gameplay update is still called for render preparation while the
game's `PausedFlag` is temporarily set, but its own pause-menu state is not
entered. This follows the same state-ownership approach as
[`zxxsmart/thprac-th06nc`](https://github.com/zxxsmart/thprac-th06nc).
The overlay therefore invokes the native BGM pause operation on entry and its
resume operation on every close path, except when Persistent BGM explicitly
requests uninterrupted playback.

Exit-and-save requests supervisor state 7. The hook at `ResultInitialize` temporarily
initializes result state 9 and then selects state 10, reproducing TH06NC's
native replay-save confirmation without adding a normal-run high score.
Restart requests native state 12, so the existing common initializer reapplies
the persistent Enhanced-Practice selection. Direct exit follows the original
non-replay Pause branch and requests supervisor state 1; it therefore returns
to the normal menu without entering `ResultInitialize` or the Replay menu.

## Replay file

The game remains responsible for producing and consuming its native `.rpy`.
For Enhanced Practice, the hook at `ReplayWrite` appends a compact trailer to
the complete serialized buffer and writes one file:

```text
example.rpy
```

Protocol 4 stores a UTF-8 `name=value` payload followed by a fixed footer. The
footer contains magic/version/length fields, the native replay length, and a
checksum-field-independent FNV-1a 64-bit digest of the native payload. Both
save and load first materialize a field map; unknown names are ignored and
missing names retain `PracticeParam` defaults. Each scalar macro row also
records the first protocol that understands a non-default value. The footer
uses the highest required version among values actually in use. Consequently,
a newly added option left at its default remains readable by an older build,
while a replay that actually relies on it is rejected by that build instead
of being played with silently incorrect defaults.

The payload is limited to values owned by the Practice selection UI:
destination/chapter/frame, Boss jump, dialogue, initial resources, Fake Shot,
the Extra Raging 495 switch, and Stage 4 book settings. Book arrays use stable
keys such as `bookX.0` and `bookY.0`. The scalar key list is generated from the
same X-macro that declares `PracticeParam`, preventing the struct and replay
field table from drifting apart.

Backspace F1-F8 assist flags are deliberately excluded. They are neither
saved nor restored during playback; the trailer is a Practice-selection
record, not a snapshot of runtime cheats.

RVA `+0x39610` can return before the queued native write is visible on disk.
The hook therefore snapshots its exact input buffer, waits on a small worker
until the file matches that buffer byte-for-byte, and only then appends the
payload/footer. After appending, it recalculates the TH06NC rolling-key
checksum over the complete file using the exact offsets, seed, and transform
recovered from the native reader at `+0x6AF60`. Consequently the native replay
list and loader accept the single extended `.rpy` directly. At common game initialization,
`ReplayModeFlag` and `ReplayPath` are already available. The loader reads and
validates the final trailer, restores stage/difficulty/practice state before
native initialization, then queues the timeline/ECL patch. A file without a
valid trailer is treated as a native replay.

Enhanced-Practice `.rpy` files intentionally require this practice tool; their
trailing private data is not promised to be compatible with an unmodified
game. The replay hook is scoped to Enhanced Practice. Normal runs, Original
Practice, and native Spell Practice are passed to the game's writer byte for
byte and retain native replay compatibility.

## Relevant recovered addresses

| RVA | Meaning |
| --- | --- |
| `+0x3A210` | Main gameplay update and pause/state-transition owner |
| `+0x73D50` | Result/replay-save menu initializer |
| `+0x39610` | Internal complete replay-buffer writer |
| `+0x4F278C` | Broad native replay-mode flag |
| `+0x4FF164` | Selected native replay path buffer |
| `+0xC21D98` / `+0xC21D9C` | Current and requested supervisor states |

All function detours verify complete instruction-aligned prologues
before installing absolute x64 jumps.

## Comparison with zxxsmart/thprac-th06nc

Both projects target the same 64-bit Steam executable and share the core
approach: intercept the gameplay update for a practice-only pause, route exit
through the native save confirmation, hook the replay writer, and restore
practice parameters from digest-bound metadata before initialization.

The implementations differ structurally:

| Area | This project | zxxsmart/thprac-th06nc |
| --- | --- | --- |
| Host/UI | Small standalone injector DLL; direct Dear ImGui windows | Full thprac fork using its launcher, GUI framework, locale and configuration layers |
| Hooking | Local byte-checked x64 trampolines and relays | MinHook after a full executable SHA-256 gate |
| Practice representation | `JumpEnum`, explicit timeline frame/chapter, ECL patch queue, book coordinates | Stable thprac section IDs and shared `Settings` transported through launcher/DLL IPC |
| Practice metadata | Protocol-4/5 named-field trailer embedded in the single `.rpy` | Separate `.rpy.thprac-nc` sidecar, replay protocol 7 |
| Runtime assists in replay | Explicitly excluded; only Practice-UI values are restored | Stores initial flags and timestamped flag changes |
| Compatibility | Enhanced replays require this tool; non-Enhanced modes remain byte-for-byte native | Native `.rpy` plus the fork's sidecar; older unpublished formats are rejected |
| Runtime controls | F9-F12, Backspace F1-F8, custom hitboxes/stretch, current project locale | Existing thprac quick/advanced menus, speed panel and upstream facilities |
| Packaging | Separate launcher EXE and DLL | Single-file package that extracts verified native modules |

The two metadata formats are intentionally not treated as interchangeable.
Although Boss IDs largely follow the same TH06 ordering, the configuration
layouts, portion representation, flags, and version contracts differ. This
project recognizes only its own trailer magic at the exact end of the file.
