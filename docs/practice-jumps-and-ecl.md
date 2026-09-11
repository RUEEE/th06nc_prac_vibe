# Practice jumps and writable ECL patching

## Request model

The UI does not patch ECL directly. It queues one request which is consumed by
the gameplay thread after the selected stage ECL has loaded:

```cpp
enum class RequestKind { None, Stage, Stage4Books, Boss };
```

This prevents the renderer thread from racing the loader or writing into an
ECL buffer belonging to the previous stage.

`HookedTimelineUpdate` is installed at RVA `+0x36260`. On the first suitable
update it validates the loaded buffer, applies the request, clears it, and then
continues through the native timeline update trampoline.

## Stage-portion and frame warps

The active timeline time is stored at enemy manager `+0x10C0BC`. A stage warp
sets this value to the selected chapter time. `StageChapterTimes()` is a
`map<int, vector<int>>`; its vector length drives the UI chapter slider.

The custom “Frame” destination uses the same mechanism with a user-entered
timeline frame. It is therefore a timeline warp, not arbitrary CPU-frame
rewinding.

Chapter times were seeded from the established thprac stage sections because
the classic and Steam stage content is substantially aligned. They remain
data, not hard-coded UI rows, so they can be corrected independently.

## BossJump catalog

```cpp
enum Bosstype {
    MID_BOSS_NONSPELL,
    MID_BOSS_SPELL,
    BOSS_NONSPELL,
    BOSS_SPELL,
};

struct BossJump {
    Bosstype type;
    JumpEnum jumpname;
    int stage; // one-based
    int diff;  // bit mask: E=1, N=2, H=4, L=8, Extra=16
};
```

The structure deliberately contains no display string. `JumpEnum` is the
stable key and `Locale::GetJump()` supplies Chinese, English, or Japanese text.
Filters combine entries for “Midboss”, “Boss”, “Nonspell”, and “Spell” without
duplicating catalog records.

## ECL identity validation

Each supported stage has a `StageEclIdentity` containing expected size and two
64-bit hashes. Before writing, the patcher checks that the loaded region is
readable/writable and matches the expected stage identity. This is essential:
all following offsets refer to the packed Steam ECL layout and a wrong buffer
would turn a script edit into memory corruption.

## EclWriter and patch primitives

`EclWriter` bounds-checks every write against the validated file size. Higher
level helpers encode recurring instruction edits:

- `ECLSetHealth`: replace an instruction's time/health operands.
- `ECLSetTime`: change script timing operands.
- `ECLStall`: disable or indefinitely delay an unwanted instruction.
- `PatchPair`: associate old/reference and Steam offsets during porting.

The large `JumpEnum` switch is intentionally explicit. Each case applies the
minimal operand/opcode edits needed to suppress earlier phases, select the
desired Boss sub-flow, or rewrite health/timing so execution begins at the
chosen nonspell/spell. This is not a raw copy of classic offsets: classic patch
sites were matched to equivalent instructions in the packed Steam `.ecl`, and
then rewritten using Steam offsets.

## Dialogue, fake shot, and Stage 4 books

The Boss queue records whether pre-Boss dialogue should be retained. The
fake-shot selector modifies Stage 4 Patchouli routing so the selected character
and shot type can use the corresponding elemental spell set. Spell display
names are resolved with the same fake-shot key.

Stage 4 chapter 4 exposes six book positions. The queue stores a fixed-position
bit mask and six X/Y pairs. At the first book instruction (ECL offset `0xF2F8`,
instruction size `0x1C`) the patcher rewrites selected operands. The UI supports
mirroring, rotation, random X values, default Y values, and validated clipboard
serialization:

```text
(x0,y0),(x1,y1),(x2,y2),(x3,y3),(x4,y4),(x5,y5)
```

Accepted ranges are X `[-192,192]` and Y `[-50,448]`.

## Why Boss practice is not native spell practice

Enhanced Boss jumps keep the ordinary stage/Boss ECL environment and patch its
control flow. This supports nonspells, midbosses, dialogue selection, and
multi-phase behavior that native spell practice cannot express. Native
presentation functions are reused separately where they are useful.

