# UI, localization, and input

## F10 base UI

`DrawPracticeBaseUi()` creates a display-sized ImGui window at `(0,0)`. F10
toggles it independently of the replacement Practice window, so it may be
opened while practice configuration is visible. The window supports language
selection, automatic shooting, practice-only hitbox display, game-only
horizontal stretch, and FPS/timer-period adjustment.

ImGui font/style scale is computed in the renderer bootstrap from display
height, using 2.5x at 1440 pixels as the reference. ImGui is submitted after
the game stretch pass, so stretch mode does not distort UI geometry.

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
non-atomic `PracticeParam` and are not reset when F10 or the Practice screen is
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
instead of assuming the normal fixed spell-card ordering.

## Logical input and replay compatibility

### Keyboard remapping

The F10 key-binding panel covers Up, Down, Left, Right, Focus, Shoot, Bomb, and
the auto-shoot toggle. Each row shows the current key, a complete virtual-key
Combo, and a capture button that waits for the next keyboard press. The two
preset buttons set the seven native gameplay actions to arrow keys or WASD
(both retain Shift/Z/X for Focus/Shoot/Bomb); the separately configured
auto-shoot toggle is not overwritten. Bindings and the auto-shoot enabled state
are loaded at hook installation and
saved immediately after changes in
`%APPDATA%\\shanghaialice\\th06nc\\input.ini`. Missing or invalid key entries
fall back independently to their built-in defaults.

The SOCD Combo is applied independently to the vertical and horizontal
keyboard axes before their logical direction bits are passed to the game.
`None` preserves both opposing bits, `Last input wins` selects the most recent
edge, `First input wins` retains the direction held first, and `Neutral`
suppresses both. Simultaneous same-frame presses have no knowable ordering and
therefore resolve to neutral in either priority mode. Replay playback bypasses
the entire keyboard transform. The selected mode is persisted as `SOCD` in the
same INI `[Options]` section.

The same panel also binds the Enhanced-Practice Pause shortcuts for Retry,
direct Exit, and Confirm (R/Q/Enter by default). Its key list is generated only
from `keyBindDefine`. Left/right Shift normalize to `VK_SHIFT`, and left/right
Ctrl normalize to `VK_CONTROL`, consistently for Combo selection, key capture,
and INI loading. A saved VK absent from the map is replaced with that action's
built-in arrow-layout default and the repaired configuration is written back.

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
