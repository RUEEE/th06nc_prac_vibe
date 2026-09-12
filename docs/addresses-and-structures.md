# Addresses and recovered structures

The authoritative list is `overlay/game_addresses.h`. This document explains
the most important entries; it does not replace byte verification in code.

## Core functions

| RVA | Project name | Recovered purpose |
| --- | --- | --- |
| `+0x10870` | `BulletManagerUpdate` | Updates the bullet manager; exposes the active bullet pool. |
| `+0x12BE0` | `ActionInputUpdate` | Builds the logical action word used by gameplay/replay. |
| `+0x127D0` | `KeyboardActionMerge` | Receives the keyboard word, then ORs native controller input into it. |
| `+0x35850` | `FinalSpellRage` | Selects QED 495's phase from the enemy-local age. |
| `+0x36260` | `EnemyTimelineUpdate` | Dispatches stage timeline records and native spell-practice spawning. |
| `+0x3A9C0` | `PlayerInitialize` | Final common player/resource initialization used on entry and restart. |
| `+0x68820` | `PlayerUpdate` | Player state machine, Bomb activation, death and respawn. |
| `+0x689ED` | `AutoBombInputCheck` | Start of native Bomb current/previous-edge test. |
| `+0x68A11` | `DeathBombBranch` | Bomb-success path after the input-edge checks. |
| `+0x6A980` | `CollisionTest` | Bullet/rectangle versus player collision path. |
| `+0x6ABA0` | `LaserCollisionTest` | Rotated laser versus player collision path. |
| `+0x77550` | `StageBackgroundFastForward` | Native spell-practice presentation pre-advance. |
| `+0x77BB0` | unnamed native routine | Advances the stage presentation controller by one step. |
| `+0x7BC80` | `BgmLoad` | Opens and starts an Opus BGM stream. |
| `+0xAE180` | `KeyboardUpdate` | Polls and publishes the 256-key physical keyboard state. |

## Global gameplay state

| RVA | Type | Meaning |
| --- | --- | --- |
| `+0x4F1E80` | `uint8_t` | Character: Reimu or Marisa. |
| `+0x4F1E81` | `uint8_t` | Shot type A/B. |
| `+0x4F1E84` | `int32_t` | Zero-based active stage/group. |
| `+0x4F1E88` | `uint16_t` | Power. |
| `+0x4F2798` | `int64_t` | Score. |
| `+0x4F27B4` | `uint8_t` | General Practice flag; initialized too late for some first-load presentation paths. |
| `+0x4F27B5` | `uint8_t` | Native spell-practice flag. |
| `+0x4F27B8` | `uint8_t` | Native spell-practice spell ID/presentation selector. |
| `+0x4F27BC` | `uint16_t` | Point items. |
| `+0x4F27C0` | `int32_t` | Difficulty; Extra is 4. |
| `+0x4F27C4` | `uint8_t` | Replay playback flag. |
| `+0x4FF0CC` | `int32_t` | Graze. |
| `+0x4FF0F0` | `uint8_t` | Lives. |
| `+0x4FF0F1` | `uint8_t` | Bombs. |
| `+0x506AD0` | two `float`s | Player stage-space position. |
| `+0x506AEC` | `float` | Player collision radius. |
| `+0x506C38` | `uint8_t` | Player state: 0 normal, 1 entry, 2 DIE, 3 respawn. |
| `+0xA6EC48` | `uint16_t` | Native held-direction repeat pulse used by menus. |
| `+0xA6EC60` | `uint32_t` | Current logical/menu action bits. |
| `+0xA6EC64` | `uint32_t` | Previous logical/menu action bits. |
| `+0xA6EB78` | pointer | Writable loaded ECL buffer used by the jump patcher. |

The initial lives and Bomb backups at `+0xC21DE0/+0xC21DE1` are updated with
the live counters so an in-game restart preserves the configured resources.

## Player object fields

The current global player object begins at preferred VA `0x1404FF3A0`, inferred
by subtracting known object offsets from the global aliases above.

| Offset | Meaning |
| --- | --- |
| `+0x7730` | Player X position. |
| `+0x7734` | Player Y position. |
| `+0x773C` | Deathbomb countdown; collision initializes it to 8. |
| `+0x774C` | Collision radius in collision context. |
| `+0x7858` | Player-state animation/state timer. |
| `+0x7898` | Player state byte. |
| `+0x9EC8` | Bomb callback/activity flag. |
| `+0x9ED0` | Bomb callback function pointer. |

At preferred VA `0x14006AAF1`, bullet collision writes state 2, clears the
state timer, and writes 8 to `player+0x773C`. The laser collision path performs
the equivalent global stores near `0x14006AD3E`. Player update first requires
the countdown to be nonzero, checks Bomb count, then tests current and previous
action bit `0x2`. A successful edge reaches `+0x68A11`.

## Practice menu fields

| Offset | Meaning |
| --- | --- |
| `+0x28` | Selected stage. |
| `+0x9638` | Transition frame. |
| `+0x168B0` | Menu sub-state. |
| `+0x168DC` | Native spell-practice stage/group. |
| `+0x168E0` | Native spell list index. |
| `+0x168E4` | Native list scroll start. |
| `+0x168E8` | Native spell-practice difficulty. |

## Bullet and hitbox observations

The bullet pool contains `0x280` entries beginning at `bulletManager+0x8`,
with stride `0x620`. The project treats only native active states as drawable;
the field at bullet `+0x618` is a graze-completion marker and is not a general
collision-enable flag.
