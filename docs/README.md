# Reverse-engineering documentation

This directory describes the reverse engineering behind `th06nc_prac_vibe`.
It is intended both as implementation documentation and as a starting point
for re-auditing a future executable revision.

## Documents

- [architecture.md](architecture.md): launcher, DLL bootstrap, renderer hooks,
  thread ownership, and patching conventions.
- [native-spell-practice.md](native-spell-practice.md): recovered native spell
  practice flow, `SpellMeta`, ECL sub launch, and background fast-forwarding.
- [addresses-and-structures.md](addresses-and-structures.md): known RVAs,
  global variables, object fields, and recovered layouts.
- [practice-jumps-and-ecl.md](practice-jumps-and-ecl.md): stage timeline warps,
  Boss jump catalog, writable ECL validation, and patch dispatch.
- [practice-presentation.md](practice-presentation.md): Stage 6/Extra Boss
  backgrounds, title suppression, entry clear removal, and BGM selection.
- [ui-and-input.md](ui-and-input.md): F10 UI, replacement Practice UI,
  localization, navigation, clipboard support, auto-shoot, and replay rules.
- [auxiliary-options.md](auxiliary-options.md): Backspace overlay options and
  their instruction-level behavior.
- [hitboxes-and-rendering.md](hitboxes-and-rendering.md): bullet/laser capture,
  recovered collision data, coordinate conversion, black background, and
  stretch post-processing.
- [replay-and-reference-comparison.md](replay-and-reference-comparison.md):
  Enhanced-Practice ESC flow, embedded replay trailer, loading rules, and a
  comparison with `zxxsmart/thprac-th06nc`.

## Address notation and confidence

The supported executable uses preferred image base `0x140000000`. Documents
primarily use RVAs, written as `+0x...`; the corresponding preferred VA is
`0x140000000 + RVA`. Runtime code must always use the actual module base.

Labels such as “recovered” mean the behavior is supported by instruction-level
use sites or by the working hook. Names are descriptive names assigned by this
project, not original symbols. Statements explicitly marked “inference” should
be rechecked when more call sites become available.

## Primary evidence

- `overlay/game_addresses.h`: canonical RVA and field registry.
- `overlay/practice_menu.cpp`: native Practice-flow and presentation hooks.
- `overlay/practice_jump.cpp`: timeline and ECL patch implementation.
- `overlay/game_overlay.cpp`: Backspace helper patches.
- `overlay/keyboard_input.cpp`: logical-input and replay-aware transforms.
- `overlay/hitbox_capture.cpp`: collision and rendering observations.
- `overlay/replay_support.cpp`: custom pause state, native save routing, and
  embedded replay-trailer serialization/restoration.
- `analysis/th06nc_spell_practice_report.md` in the parent analysis workspace:
  original native spell-practice disassembly report.
