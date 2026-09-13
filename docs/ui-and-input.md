# UI, localization, and input

## F9-F12 base UI

`DrawPracticeBaseUi()` creates a display-sized ImGui window at `(0,0)`. Any key
from F9 through F12 toggles it independently of the replacement Practice window, so it may be
opened while practice configuration is visible. The window supports language
selection, automatic shooting, practice-only hitbox display, game-only
horizontal stretch, and FPS/timer-period adjustment.

ImGui font/style scale is computed in the renderer bootstrap from display
height, using 2.5x at 1440 pixels as the reference. ImGui is submitted after
the game stretch pass, so stretch mode does not distort UI geometry.

F9-F12 are polled as one edge-triggered group in the Present hook before
renderer visibility is evaluated, so they can request the first ImGui frame without depending on an already
subclassed game window. Backspace/F1-F8 helper state is updated at the same
point. `SC_KEYMENU` and keyboard-generated context-menu messages remain
suppressed after window initialization to prevent Alt/system-menu focus loss.

## Replacement Practice UI

The game-thread hook replaces the stage selector call at RVA `+0x4BFB2` but
continues calling the native selector for timing and sound bookkeeping. Its
stage result is overwritten with the persistent selection because logical menu
directions belong to the focused ImGui row.

The top-level “Warp to” values are:

```text
None / Stage portion / Midboss / Boss / Nonspell / Spell / Frame
```

Boss choices are filtered from `BossJumps()` by stage, type, and the already
selected difficulty. There is no separate rank or difficulty widget. Stage 7
temporarily forces difficulty 4 (Extra); `PracticeParam::difficulty` preserves
the last main-game difficulty so returning from Extra does not poison later
masks.

Up/down changes the focused row. Left/right changes its selection. The UI reads
the game's current/previous logical action words and native repeat flag, so
configured keyboard bindings and controllers work exactly like the stock menu.
Mouse interaction remains available. UI values live in the single persistent,
non-atomic `PracticeParam` and are not reset when the full-screen menu or the Practice screen is
closed and reopened. The same X-macro field list generates the named replay
map, so replay persistence and UI state cannot silently use different scalar
field sets.

Any input mapped to the game's logical Confirm action, plus the ImGui Start
button, produces the native confirmation edge. Original and enhanced Practice
selections bypass the redundant second
confirmation screen by reproducing its sound, 30-frame fade, state transition,
and bookkeeping directly.

## Localization

`LocaleText` is the stable enum for ordinary UI strings. `LocalizedText` holds
Chinese, English, and Japanese variants, and macro `S(Name)` performs lookup.
Boss jump names use `JumpEnum` integer keys and difficulty/fake-shot variants;
`BossJump` itself contains no string.

The default language is selected from the system code page. Adding a regular
label requires one enum member and one map entry. Adding a jump translation
requires updating the jump map, not the jump control-flow data.

Stage 4 Patchouli names are handled as shot-dependent/random elemental spells
instead of assuming the normal fixed spell-card ordering. With Fake Shot set
to None, the displayed name follows the currently selected Reimu/Marisa A/B
shot; an explicit Fake Shot selection overrides only the displayed/routed
variant as before.

## Logical input and replay compatibility

Opening gameplay pause with Escape cancels the active auto-shoot latch before
the paused frame is drawn. It does not disable the auto-shoot option itself.

### Keyboard remapping

The full-screen key-binding panel covers Up, Down, Left, Right, Focus, Shoot, Bomb, and
the auto-shoot toggle. Each row shows the current key, a complete virtual-key
Combo, and a capture button that waits for the next keyboard press. The two
preset buttons set the seven native gameplay actions to arrow keys or WASD
(both retain Shift/Z/X for Focus/Shoot/Bomb); the separately configured
auto-shoot toggle is not overwritten. Bindings and the auto-shoot enabled state
are loaded at hook installation and
saved immediately after changes in
`%APPDATA%\\shanghaialice\\th06nc\\input.ini`. Missing or invalid key entries
fall back independently to their built-in defaults.

Direct key capture owns a global input-suppression state. While it is waiting,
gameplay mapping, ImGui keyboard messages, F9-F12, Backspace/F1-F8, Retry,
Exit, and automatic-shoot shortcuts do not react. Capture accepts only a new
physical rising edge and releases the global suppression state immediately
after that edge has been assigned.

The SOCD Combo is applied independently to the vertical and horizontal
keyboard axes before their logical direction bits are passed to the game.
`None` preserves both opposing bits, `Last input wins` selects the most recent
edge, `First input wins` retains the direction held first, and `Neutral`
suppresses both. Simultaneous same-frame presses have no knowable ordering and
therefore resolve to neutral in either priority mode. Replay playback bypasses
the entire keyboard transform. The selected mode is persisted as `SOCD` in the
same INI `[Options]` section.

The same panel also binds Retry, direct Exit, and Confirm (R/Q/Enter by
default). Retry and Exit are Enhanced-Practice Pause shortcuts. Confirm is
inserted into the game's logical `0x100` menu-confirm bit, so it behaves like Z
in native menus and in the enhanced Pause menu without also firing a shot.
The key list is generated only from `keyBindDefine`. Left/right Shift normalize
to `VK_SHIFT`, and left/right Ctrl normalize to `VK_CONTROL`, consistently for
Combo selection, key capture, and INI loading. A saved VK absent from the map
is replaced with that action's built-in arrow-layout default and the repaired
configuration is written back. Retry and Exit detect rising edges from the
physical high-bit key state; they do not use `GetAsyncKeyState`'s shared
low-order event bit, which the game's own keyboard polling may consume first.

RVA `+0x12BE0` first constructs a keyboard-only action word, then tail-calls
RVA `+0x127D0` to merge controller input. The remapping detour is installed at
that second function's entry: it removes only the seven native keyboard action
groups, inserts the configured keys, and then calls the original function.
Controller input therefore remains native. Replay playback bypasses remapping;
recorded logical input remains authoritative.

RVA `+0xAE180` is the physical keyboard polling routine. When the game is not
foreground, the hook publishes an empty 256-byte keyboard state and clears its
nine bytes of cached metadata to avoid background input and stuck keys.

RVA `+0x12BE0` builds logical actions. Known bits include:

| Bit | Action |
| --- | --- |
| `0x1` | Shoot/Z |
| `0x2` | Bomb/X |
| `0x100` | Menu confirm/Z |

Auto-shoot adds bit `0x1` to the returned logical word. Because replay
recording sees an ordinary shoot action, the resulting replay remains portable.
Replay playback is never modified. V toggles active auto-fire; a real press of
the configured Shoot/Bomb key, or V, cancels it. `Shift+D` enables the option.
An `A` indicator is drawn at the top left while automatic firing is active.

Auto-Bomb does not synthesize logical input. Its relay is installed at RVA
`+0x689ED`, where native PlayerUpdate begins comparing current and previous
Bomb bits. If enabled, not replay playback, and player state is DIE, execution
goes directly to native Bomb-success RVA `+0x68A11`. All earlier native gates
remain authoritative, including modes in which the game itself does not permit
deathbombing.
