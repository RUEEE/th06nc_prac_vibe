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
stage result is overwritten with the persistent selection because arrow keys
belong to the focused ImGui row.

The top-level “Warp to” values are:

```text
None / Stage portion / Midboss / Boss / Nonspell / Spell / Frame
```

Boss choices are filtered from `BossJumps()` by stage, type, and the already
selected difficulty. There is no separate rank or difficulty widget. Stage 7
temporarily forces difficulty 4 (Extra); `g_originalDifficulty` preserves the
last main-game difficulty so returning from Extra does not poison later masks.

Up/down changes the focused row. Left/right changes its selection with repeat
delays of 300 ms initially and 75 ms thereafter. Mouse interaction remains
available. UI values live in persistent statics/atomics and are not reset when
F10 or the Practice screen is closed and reopened.

Both physical Z and the ImGui Start button produce the native confirmation
edge. Original and enhanced Practice selections bypass the redundant second
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
Replay playback is never modified. V toggles active auto-fire; a real Z, X, or
V transition cancels it as documented by the UI. `Shift+D` enables the option.
An `A` indicator is drawn at the top left while automatic firing is active.

“Disable Bomb” removes only logical bit `0x2` during live play. Physical X is
still available to menus, and replay playback retains Bombs already present in
the replay.

Auto-Bomb does not synthesize logical input. Its relay is installed at RVA
`+0x689ED`, where native PlayerUpdate begins comparing current and previous
Bomb bits. If enabled, not suppressed, not replay playback, and player state is
DIE, execution goes directly to native Bomb-success RVA `+0x68A11`. All earlier
native gates remain authoritative, including modes in which the game itself
does not permit deathbombing.

