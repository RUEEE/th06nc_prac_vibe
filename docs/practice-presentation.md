# Practice presentation fixes

Boss ECL flow alone is insufficient for a clean practice start. The game also
initializes BGM, stage title, player entry state, and an independent background
controller. These hooks reproduce the relevant parts of native spell practice
without setting its global flag and confusing its enemy-spawn path.

## Enhanced-session lifetime

`g_enhancedSessionActive` is latched when the replacement menu confirms an
enhanced run. It is cleared before a later normal run or native spell-practice
run. This latch exists because general Practice flag `+0x4F27B4` is written too
late for several first-load decisions.

Every flow-sensitive helper also checks native spell flag `+0x4F27B5`. This
prevents enhanced hooks from leaking into the game's own spell-practice mode,
which previously caused crashes after leaving enhanced practice.

## Stage title suppression

Native spell practice skips title creation at the check near RVA `+0x3EA61`.
The replacement check calls `ShouldUseNativePracticePresentation()` and treats
an active enhanced run like native practice for presentation only. The title
object is therefore never created; hiding or clearing it later is unnecessary.

This applies to stage-portion practice as well, because a title shown after a
timeline warp no longer corresponds to the selected section.

## Opening clear and entry invulnerability

The stock entry path uses player state 1 and an approximately 120-frame entry
sequence. Its early portion includes the opening bullet-clear behavior and
entry invulnerability. Enhanced practice changes the native selection near RVA
`+0x6841C` to choose state 0/timer 0.

The new executable can restore the entry state during later initialization, so
the project also intercepts the first state read at RVA `+0x68AD2`. A one-shot
flag clears `player+0x7898` and `player+0x7858` only on the first enhanced-run
dispatch. It is then consumed, leaving genuine death and respawn transitions
untouched. This one-shot design avoids the old bug where continuously clearing
state also erased later normal player behavior.

## Stage 6 and Extra Boss backgrounds

Stage 6 and Extra use special Boss presentation resources (`eff06`/`eff07`)
that differ from their midboss backgrounds. Merely calling `read_msg2` or
changing the ECL target does not select them.

The working implementation mirrors the native spell-practice sequence:

1. The check at RVA `+0x3B4AE` calls `ShouldPreAdvanceStageBackground()`.
2. Main-Boss practice enables the native fast-forward call at `+0x3B4B7`.
3. Before that call, `HookedStageBackgroundFastForward()` selects a real
   `SpellMeta` from the active stage/difficulty whose special flag at metadata
   `+0x06` is zero, and temporarily stores its ID in `+0x4F27B8`.
4. The following native special setup at `+0x3B4DF` sees the same presentation
   ID and loads the correct Stage 6/Extra Boss effect.

Selecting from metadata is important. A guessed global spell ID can select a
midboss presentation or trigger special-flag behavior on another difficulty.

## BGM selection and persistence

Each stage asset record stores the stage BGM path and Boss BGM path `0x80`
bytes apart. Native spell practice selects the latter through its flag;
enhanced main-Boss practice instead adjusts the path in
`HookedStageBgmLoad()` before calling native `BgmLoad` at RVA `+0x7BC80`.
Midboss and stage-portion practice retain the stage BGM.

The current filename and stream handle are tracked for “Everlasting BGM”. On a
practice restart, the native loader is allowed to reopen the stream and the
saved playback position is restored with RVA `+0xC9930`. Leaving practice
restores native stop/disposal behavior, preventing Boss music from leaking into
a subsequent normal game.

## Resource initialization and restart

`HookedPlayerInitialize()` runs after the complete native initializer at RVA
`+0x3A9C0`, which is later than the game's score, power, graze, point, life and
Bomb writes. It then applies the persistent configured values and queues the
jump. The same common initializer is reached by in-game restart, so enhanced
mode and its values survive `Esc+R` without patching every earlier store site.

