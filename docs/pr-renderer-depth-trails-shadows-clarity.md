# PR: Stabilize renderer depth, trails, skidmarks, and shadows

## Summary

This renderer pass removes several unrelated-looking artifacts that shared two
causes: imprecise depth placement and presentation data that was too coarse for
the visual being drawn.

- Camera clipping now projects visible batch bounds onto the current view
  direction. Geometry behind the camera cannot inflate the far plane, normal
  orbit views retain a useful near plane, and the far/near ratio stays bounded.
- Trajectories use one indexed, shared-ring tube with parallel-transport frames
  and endpoint-only caps instead of independent capped prisms per sample pair.
- Skidmarks use each wheel's accepted collision point and surface normal. Their
  tangent frame is projected onto the contact plane, and their shader retains a
  visible antialiased core when the ribbon becomes sub-pixel.
- Authored lighting keeps the baked map composition while adding a localized
  selected-car contact shadow. A separate lit mode enables bounded full-world
  shadows with explicit caster metadata, PCF, bias, cascade, and range settings.
  Contact shadows are effective only in authored mode, so enabling full-world
  shadows in lit mode cannot bypass imported caster metadata through the
  contact-only branch.
- Graphics settings now persist contact shadows and off/2x/4x/8x MSAA. The
  sharp texture profile keeps linear minification, and turbo/sign/checkpoint/
  start-finish graphics keep stable trilinear mip filtering.

The companion ForeverValidator branch exports the exact per-wheel contact point
and normalized contact normal used by the skidmark path. ForeverTAS pins that
immutable revision in both CMake and the release manifest.

## Root causes

The old camera solver used one scene-wide bound independent of view direction.
That needlessly pushed the far plane out and consumed depth precision. The old
trajectory builder then added coplanar caps at every sample and changed its
frame reference abruptly, making those precision problems visible as seams and
angle-dependent deformation.

Skidmarks previously reconstructed a wheel-bottom point and offset it along car
up. That is wrong on banks, walls, and inverted contacts. A second bug made
far-away marks disappear even when their geometry was valid: the fragment
shader allowed the derivative-based edge feather to grow wider than the whole
strip, multiplying all alpha to nearly zero.

Finally, the renderer had only an expensive all-world shadow switch. Applying
that pass to maps whose lighting is already baked both darkened the image and
submitted hundreds of unnecessary casters. The new default limits dynamic
shadow work to the selected vehicle while retaining the authored map.

## Validation

- renderer, graphics-settings, trajectory-geometry, skidmark-geometry,
  trajectory-shader, and skidmark-shader tests pass;
- real D3D11 authored/contact-shadow, non-surface collision, lit/full-world-
  shadow, and visible-trajectory capture tests pass serially;
- the authored capture requires an actual visible selected-car caster (four
  imported vehicle batches in the configured fixture), so the default contact
  shadow cannot silently degrade into a settings-only no-op;
- the D3D capture harness corrects fractional-DPI native window insets before
  validating projection aspect, preventing stretched evidence images;
- the real orbit capture resolves a 0.114 near plane and an approximately
  2.1-kilometre far plane without exceeding the 50,000:1 precision guard;
- the full-world capture resolves a 1,200-unit shadow range with the configured
  PCF, bias, factor, and cascade split; and
- the local `Abuk.Replay.Gbx` skidmark A/B contains 230 ribbon segments and 52
  stamps. At the normal gameplay camera, 179 of 224 changed pixels have a
  channel delta of at least eight, compared with the effectively invisible old
  minification path; and
- a clean FetchContent build against the pinned Validator revision passes the
  same six focused tests and four real-D3D captures, while the release cache
  policy test accepts the explicitly output-neutral CUDA transport coverage;
  and
- the SM120/RTX 5090 CUDA search parity test passes with wheel ground points,
  accepted contact points, and normalized contact normals included in the
  winner-state comparison.

The broad software-backend QML smoke still reports its established camera and
editor failures because Qt Quick 3D is unavailable under that backend. Its
renderer-state, filtering, graphics-settings, and daylight/shadow source checks
pass; the real D3D11 captures are the render authority.
