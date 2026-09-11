# Hitboxes and rendering tools

## Capture sources

The visualization combines three sources:

1. Bullet manager update at RVA `+0x10870` captures the complete `0x280`-entry
   bullet pool each frame.
2. Collision routine `+0x6A980` supplements rectangle and special collision
   calls that are not represented as ordinary active bullet entries.
3. Rotated laser routine `+0x6ABA0` records laser center, half-extents, and
   angle so the overlay can draw the same oriented shape.

Captured records are double-buffered between update and Present. The fixed
capacity is 32768 records per frame to avoid allocation or unbounded growth in
the collision hot path.

## Recovered visualization record

```cpp
struct Float2 { float x, y; };

struct Hitbox {
    Float2 position;
    Float2 size;
    Float2 rotationPivot;
    float rotation;
    float playerRadius;
    bool circular;
    bool rotated;
};
```

Names are project-level abstractions, not a claim that the native game stores
this exact packed structure. Bullet entries are decoded into this neutral form.

The collision outline includes the player's collision radius because that
matches the game's overlap test. Optional size labels display the raw bullet
radius or half-extents, not the expanded outline.

## Coordinate conversion

Native positions are stage-space coordinates. The default reference is
`640x480` with playfield origin `(128,16)`. The output transform letterboxes
uniformly, then applies configurable stage origin, pixel X/Y offsets, optional
Y inversion, and scale multiplier. Rotated laser corners are rotated around
their center before conversion.

The calibration controls are visualization-only and never change native
collision behavior.

## Practice-only visibility

The F10 hitbox checkbox is enabled only for an active Practice run. The capture
hooks may remain installed, but drawing is gated. This keeps the diagnostic
overlay out of ordinary gameplay while allowing native and enhanced Practice.

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
this pass and is therefore not stretched.

The stretch source is recreated when the back-buffer description changes and
released during renderer reset/shutdown. The current post-process is a D3D11
implementation; the D3D9 Present path does not apply this shader.
