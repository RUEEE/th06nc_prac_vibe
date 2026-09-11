# Backspace helper options

Backspace toggles a compact always-auto-sized ImGui window. F1-F7 hotkeys work
while the game is foreground; their state persists while the game process is
running.

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

## F5: Auto-Bomb

The hook at RVA `+0x689ED` preserves all native checks that precede the X-edge
test, then conditionally jumps to RVA `+0x68A11`. It is disabled during replay
playback and when F7 is active. No current/previous input bits are fabricated,
so generated input cannot enter a replay.

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

F1 and F5 are naturally mutually limiting: if F1 prevents the DIE store, F5
never observes a death state.

## F6: Everlasting BGM

RVA `+0x3A313` calls the native BGM stop routine when Escape opens pause. F6
NOPs only this call, and only during enhanced Practice. Restart loading is left
native; the BGM path and playback position are tracked and restored after the
new stream opens. On leaving practice the code patch and tracking state are
cleared, restoring normal title/menu behavior.

## F7: Disable Bomb

F7 is implemented in logical input generation, not by disabling the physical X
key. Live Bomb bit `0x2` is removed after native action construction. Menus can
still use X, no prohibited Bomb is recorded, and replay playback can display
Bombs recorded elsewhere.

## Patch-group invariants

Each option uses `PatchSite<Size>` records containing original and replacement
bytes. A group is applied only if every site is either completely original or
completely patched. Unknown mixtures set the overlay's patch-error state rather
than guessing, which helps detect executable revisions or conflicts with other
mods.
