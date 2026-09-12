# Hitboxes and rendering tools

## Capture sources

The visualization combines three sources:

1. Bullet manager update at RVA `+0x10870` captures the complete `0x280`-entry
   bullet pool each frame.
2. Collision routine `+0x6A980` supplements rectangle and special collision
   calls that are not represented as ordinary active bullet entries.
3. Rotated laser routine `+0x6ABA0` records laser center, half-extents, and
   angle so the overlay can draw the same oriented shape.

Captured records use fixed-capacity frame buffers. The game update and Present
path are driven on the game's single thread, so this state uses ordinary values
rather than unnecessary atomic variables or locks. The fixed capacity is 32768
records per frame to avoid allocation or unbounded growth in the collision hot
path.

## Recovered visualization record

```cpp
struct Float2 { float x, y; };

struct Hitbox {
    Float2 position;
    Float2 size;
    Float2 rotationPivot;
    float rotation;
    bool circular;
    bool rotated;
};
```

Names are project-level abstractions, not a claim that the native game stores
this exact packed structure. Bullet entries are decoded into this neutral form.

Bullet circles display only their own radius. Rectangle and laser records display
only their own half-extents; axis-aligned rectangles use square corners. The
player is drawn separately with the radius read from `player + 0x774C`. A second
transparent circle, with radius `player radius + 20`, marks the graze range with
a 2-pixel white outline.

## Coordinate conversion

Native positions are stage-space coordinates. The default reference is
`640x480` with playfield origin `(128,16)`. The output transform letterboxes
uniformly, then applies configurable stage origin, persistent pixel X/Y offsets,
optional Y inversion, and a persistent scale multiplier. Rotated laser corners
are rotated around their center before conversion. The displayed color is also
configurable and persistent.

The calibration controls are visualization-only and never change native
collision behavior.

## Optional square collision

The non-persistent **Use square collision** option changes the axis-aligned
player collision handled by `+0x6A980`:

- circle versus circle becomes AABB versus AABB; each former circle becomes a
  square whose side length equals its original diameter;
- player circle versus an axis-aligned rectangle becomes AABB versus AABB;
- ordinary bullet graze changes from a radius-squared Euclidean test to an
  axis-aligned square test with the same diameter;
- rotated laser/OBB collision and laser graze at `+0x6ABA0` remain native and
  unchanged.

The hook performs the requested AABB test first. For a hit that lies in a new
square corner, it temporarily supplies the native routine with the minimum
player radius needed to enter its original hit branch, then restores
`player+0x774C` immediately. This retains the native death, deathbomb, bullet
state, and return-value behavior. The visualization switches the affected
circles, including the player hitbox, to matching axis-aligned squares; the
ordinary bullet-graze range changes to a matching white square. Rotated lasers
remain OBBs because their gameplay tests are not changed by this option.

The bullet graze test is inlined in `BulletManagerUpdate`: `+0x1119B` normally
adds `dx² + dy²`, `+0x1119F` squares the combined radius, and `+0x111A3`
compares them. Square mode changes only the four-byte `addss xmm0,xmm1` at
`+0x1119B` to `maxss xmm0,xmm1`. The resulting comparison
`r² > max(dx²,dy²)` is exactly the desired AABB test. Disabling the option
restores the original instruction bytes.

## Practice-only visibility

The full-screen menu's hitbox checkbox is enabled only for an active Practice
run. Its enabled state and calibration are retained in the configuration, but
drawing and eager overlay initialization remain gated by the active Practice
run. This keeps the diagnostic overlay out of ordinary gameplay.

## Black stage background

Hooks at RVAs `+0x78290` and `+0x78390` can skip the high and low stage
background layers. Present then clears only the playfield rectangle
`(128,16)..(512,464)` to black. Player, enemies, bullets, and HUD render in
their normal order.

## Horizontal stretch mode

The D3D11 path copies the completed game back buffer into a separate texture,
runs a full-screen shader, and maps the X axis so the right edge remains fixed
while the left edge extends outside the window, producing the requested 4:3
horizontal expansion without resizing the window. ImGui is submitted after
this pass and is therefore not stretched. Hitbox coordinates and horizontal
radii receive the equivalent right-anchored X transform, so the overlay remains
aligned with the stretched game image. Stretch mode is persisted in the same
configuration file as the input and hitbox settings.

The stretch source is recreated when the back-buffer description changes and
released during renderer reset/shutdown. The current post-process is a D3D11
implementation; the D3D9 Present path does not apply this shader.
