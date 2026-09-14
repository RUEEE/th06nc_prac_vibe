# Backspace helper options

Backspace toggles a compact always-auto-sized ImGui window. F1-F8 hotkeys work
while the game is foreground; their state persists while the game process is
running.

The native HUD also displays the game's accumulated miss and Bomb-use counters
beside the life and Bomb rows. This is hooked after the stock HUD draw at RVA
`+0x40670` and uses the game's ASCII renderer rather than ImGui. The native
infinite-lives flag at `+0x4F27C4` already makes the stock HUD draw misses, so
the hook adds only Bomb usage in that mode; finite-lives mode gets both added
counters. F2 Lock Lives is independent and continues displaying misses.
Replay playback is identified separately by `+0x4F278C` and does not itself
decide which counters are added.

## F1: Invincible

Collision normally writes player state `DIE` (`2`) at two confirmed sites:

| Source | RVA | Original operation |
| --- | --- | --- |
| Bullet collision | `+0x6AAF1` | `mov byte ptr [player+0x7898],2` |
| Laser collision | `+0x6AD3E` | `mov byte ptr [CurrentPlayerState],2` |

F1 replaces both seven-byte stores with NOPs. It does not freeze the entire
player state machine and does not patch unrelated transitions.

## F2: Lock lives at zero

This option intentionally preserves stock deaths while lives remain. Only
when the counter is already zero does it patch:

- RVA `+0x68E6F`, changing the game-over branch so the ordinary death path is
  retained;
- RVA `+0x68E86`, removing the byte decrement so zero does not wrap.

The patches are removed again when no longer needed. This matches thprac's
zero-life lock behavior and preserves normal play before the last life.

## F3: Lock Bombs

RVA `+0x68A70` contains `dec cl` immediately before the native current-Bomb
store. NOPing this instruction retains the count while allowing the complete
native Bomb callback and effects to execute.

## F4: Lock Power

Death power loss has multiple store paths. The option patches all confirmed
stores rather than continually rewriting the global:

| RVA | Role |
| --- | --- |
| `+0x68BAF` | First death-path power store. |
| `+0x68BBE` | Minimum-power clamp store. |
| `+0x68D16` | Alternate death-path power store. |

## F5: Lock Time

The enemy-update callback at RVA `+0x373B0` snapshots the global timeline at
RVA `+0xBADF4C` and the local timer of each of the 256 enemy slots. After the
native callback, the timeline and timers belonging to active slots are
restored. Stages 1, 2, 4, and 5 retain the zxxsmart midboss-introduction
exception that advances the timeline to the required interrupt point;
otherwise an introduction could remain locked forever.

Time locking is scoped to an active enhanced-Practice run.

## F6: Auto-Bomb

The hook at RVA `+0x689ED` preserves all native checks that precede the X-edge
test, then conditionally jumps to RVA `+0x68A11`. It is disabled during replay
playback. No current/previous input bits are fabricated, so generated input
cannot enter a replay.

The game has a deathbomb countdown at `player+0x773C`, initialized to 8 on
collision. Modes that reject deathbombing before `+0x689ED` remain rejected;
the helper intentionally does not override their game rules.

A simplified decompilation of the relevant native sequence is:

```cpp
if (!bombCallbackActive && nativeBombGateAllows() &&
    player->deathbombTimer != 0 && currentBombs > 0) {
    const bool xNow = (actionsCurrent & 2) != 0;
    const bool xBefore = (actionsPrevious & 2) != 0;
    if (xNow && xNow != xBefore && player->bombCallback != nullptr) {
        // preferred VA 0x140068A11
        --currentBombs;
        player->bombCallbackActive = 1;
        player->bombCallback(player);
    }
}
```

The project's relay replaces only the two input-edge tests. It does not move
the branch after `nativeBombGateAllows()`.

F1 and F6 are naturally mutually limiting: if F1 prevents the DIE store, F6
never observes a death state.

## F7: Everlasting BGM

RVA `+0xAA1E8C` is the game's native `KeepBgm` byte. As in the zxxsmart
implementation, an explicit enhanced-retry ownership flag survives the native
Practice flag's temporary reset. `KeepBgm` is set when state 12 is requested
and reaffirmed before the common initializer; it is explicitly cleared on
first entry, normal/native-practice initialization, save/exit, and enhanced-
Practice exit. `BgmLoad` caches the active filename at offset `+0x29C` in its
audio-state object. The enhanced-practice entry filename is captured there and
compared again when retry is requested: persistence is used only while the
active filename still matches the practice entry. Thus retrying a stage portion
after its music has naturally changed to the Boss theme reloads the original
stage theme instead of preserving the Boss theme. The custom pause menu follows
the zxxsmart implementation and
freezes gameplay by temporarily setting the native `Paused` byte around the
game update. It does not call the audio-object stop routine directly, so closing
the menu lets the existing stream continue instead of leaving it stopped.

## F8: Disable Bomb

F8 removes logical Bomb bit `0x2` only from live input after native action
construction. It does not disable the physical key in menus and does not alter
Bomb actions already stored in replay playback. Auto-Bomb also checks this
flag before entering the native deathbomb-success branch.

## Comparison with zxxsmart/thprac-th06nc

The visible Backspace overlay now follows the fork's compact presentation: a
top-right, input-transparent list with enabled entries colored green. The
underlying implementations intentionally remain different:

| Shared option | This project | zxxsmart reference |
| --- | --- | --- |
| Invincible | Byte-checks and NOPs the two confirmed bullet/laser DIE-state stores | On every player callback, forces state 0/3 to state 3 with timer 2 |
| Lock lives | Only at zero lives, patches the game-over branch and decrement so the ordinary miss/respawn path remains | Snapshots the value when enabled, temporarily supplies one life at zero, then restores the snapshot after PlayerUpdate |
| Lock Bombs | NOPs the native Bomb decrement instruction | Snapshots the counter and restores it around player updates |
| Lock Power | NOPs three confirmed death-path Power stores | Snapshots Power and restores it around player updates |
| Auto Bomb | Diverts the native input check directly to the deathbomb-success branch; disabled in replay playback | Injects a logical Bomb edge while state is DIE; its timestamped option state is replayed by the fork |
| Time Lock | Restores the timeline and active enemy-local timers around EnemyUpdate, including the midboss exception | Same algorithm |
| Persistent BGM | Sets and clears the native KeepBgm byte at enhanced-practice initialization boundaries | Sets the native KeepBgm byte for compatible practice restarts |

F1-F7 match the reference ordering; this project retains Disable Bomb as F8.
Runtime Backspace states remain excluded from replay metadata.

## Patch-group invariants

Each option uses `PatchSite<Size>` records containing original and replacement
bytes. A group is applied only if every site is either completely original or
completely patched. Unknown mixtures set the overlay's patch-error state rather
than guessing, which helps detect executable revisions or conflicts with other
mods.
