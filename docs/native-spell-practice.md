# Native spell-practice implementation

Native spell practice is not implemented by advancing the stage timeline to a
later frame. It launches one ECL `sub` directly, while separately advancing the
background/camera controller to approximate the scene that normally surrounds
that spell.

## Selection flow

The native menu stores its spell list index at menu object `+0x168E0` near
preferred VA `0x140052D31`, and its difficulty at `+0x168E8` near
`0x1400527BB`. Confirmation calls the lookup at RVA `+0x46630` and writes:

| Preferred VA | Effect |
| --- | --- |
| `0x1400529B8` | Set native spell-practice flag `+0x4F27B5` |
| `0x1400529BF` | Store selected spell ID at `+0x4F27B8` |
| `0x1400529CA` | Store the spell's stage group at `+0x4F1E84` |
| `0x1400529D4` | Store difficulty at `+0x4F27C0` |
| `0x1400529E1` | Start the menu-to-game transition |

## Recovered SpellMeta layout

The metadata table pointer is stored at RVA `+0xC221C0`, the entry count at
`+0xC2208C`, and each entry is `0x28` bytes:

```cpp
struct SpellMeta {
    uint8_t id;          // +0x00: global spell ID
    uint8_t stageGroup;  // +0x01: stage/group index
    int16_t subIndex;    // +0x02: ECL sub index
    uint8_t difficulty;  // +0x04
    uint8_t slot;        // +0x05: list position in this stage/difficulty
    uint8_t flags;       // +0x06: presentation/fast-forward special handling
    std::byte unknown[0x21];
};
static_assert(sizeof(SpellMeta) == 0x28);
```

Only the listed fields are established by current use sites. The remainder
contains display/statistics data and is intentionally unnamed.

## ECL loading and direct sub launch

After loading the stage ECL, initialization builds a main timeline pointer at
runtime global `0x140A6EB70` and a sub-pointer array at `0x140A6EB80`. The
relevant initialization range is preferred VA
`0x14003B3CB..0x14003B47C`. The file header supplies the timeline offset and a
table of sub offsets relative to the loaded ECL base.

Enemy timeline update at RVA `+0x36260` checks the native spell-practice flag.
In practice mode it finds the selected `SpellMeta`, allocates an enemy from a
256-entry pool with stride `0x10B0`, and assigns:

```cpp
enemy->eclInstruction = subPointers[meta->subIndex]; // enemy +0x88
enemy->subIndex = meta->subIndex;
```

It then sets the one-shot spawn flag at preferred VA `0x140AA1E7F`. The normal
timeline interpreter begins later at preferred VA `0x1400363C2` and is not
used to create this practice enemy.

## Background fast-forward

RVA `+0x77550` scans timeline records whose observed header is:

```cpp
struct TimelineRecordHeader {
    int16_t time;       // +0x00
    uint16_t opcode;    // +0x04
    int16_t size;       // +0x06
};
```

It derives a target time, then advances the independent stage presentation
controller rooted at preferred VA `0x140509B60` by repeatedly calling RVA
`+0x77BB0`. The event array is controller `+0x1F0`; the current event index is
at preferred VA `0x140509B84`.

This distinction is central to the enhanced implementation:

```text
ECL sub selection -> creates and runs the Boss enemy
background fast-forward -> aligns stage camera/effects only
```

