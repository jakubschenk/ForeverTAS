# Renderer visual-regression fixes

## Summary

This follow-up fixes renderer regressions found while comparing the native
TrackMania scene against real Stadium replays:

- restore the readable material/model/shader identity carried by
  ForeverValidator so hashed archive selections do not erase renderer
  semantics;
- use exact world-XZ mapping for the three legacy Stadium grass material
  identities;
- hide only the unsupported `VDep Fence PC3` grass fade helper, which otherwise
  renders as dense opaque CRT-like stripes;
- replace the overly dark authored/unlit path with a low-energy static sun,
  fill, and sky contribution while leaving normal/specular relighting and
  dynamic shadows opt-in;
- keep bilinear filtering on the mip chain instead of disabling mip selection;
- apply a clip-space depth bias to trajectory overlays and raise skidmarks
  slightly above visual road geometry to prevent coplanar clipping and flicker;
- expose renderer telemetry in deterministic capture metadata.

The readable path contract and vehicle relative-material fix are reviewed in
the stacked ForeverValidator draft PRs #9 and #10. Validator PR #11 is the
runnable integration branch based on CUDA-complete `experimental`; CMake and the
release manifest pin its immutable commit so these renderer fixes cannot drop
the existing CUDA search stack.

## Why the grass failed

The installed archive selects hashed model and shader paths. The readable
model identity still says `PDiff PDiff PA TOcc PX2 Grass`, but it was discarded,
so the renderer used the mesh's narrow authored UV strip instead of TrackMania's
world-XZ projection. A separate `VDep Fence PC3` helper uses `FenceA`/`FadeXZ`
shader behavior that the generic material path cannot reproduce; treating those
crossed helper planes as opaque grass caused the bright diagonal stripe field.

The fix deliberately does not apply the full legacy heuristic table to every
plain path. That broader approach changes unrelated Stadium road and start-block
blend modes. Exact whitelists recover only the two proven semantics.

## Lighting model

`DefaultPreLightGen.Texture.Gbx` is a shared runtime shader resource rather than
an extractable per-material lightmap. Native mode therefore keeps authored
diffuse textures and adds a restrained static daylight rig. Dynamic mode remains
available for normal/specular maps and optional world shadows.

## Validation

- native material/texture tests cover hashed-path grass recovery, the exact
  VDep helper visibility rule, near-miss isolation, opaque alpha behavior, and
  texture cache semantics;
- a real D3D11 trajectory shader smoke test verifies the clip-space depth
  offset, unshaded color/opacity path, and packaged shader resources without
  changing simulation vertices or bounds;
- skidmark geometry and shader smoke tests cover the surface/clip offsets;
- QML smoke tests exercise the static lighting rig and runtime bilinear versus
  trilinear mip-filter bindings;
- the automated D3D11 capture test and inspected StadiumA1/Seeding Track 2
  captures resolve every referenced map and vehicle texture with zero failures.
