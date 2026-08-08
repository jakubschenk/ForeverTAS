# PR: Fix Stadium material, texture, and skidmark fidelity

## Summary

This renderer follow-up fixes the remaining Stadium fidelity regressions:

- prevent Qt's PBR model from adding new reflections over textures that
  already contain TrackMania's authored lighting;
- recover the exact `SoilGen21` world-XZ mapping from its readable plain model
  identity when the installed archive selects hashed model and shader paths;
- add an explicit hard-surface sharpness option without changing stable
  trilinear filtering for existing installations; and
- consume live Validator wheel-ground state so real sliding contacts produce
  skidmark geometry.

The change keeps native TrackMania albedo textures and blend composition. It
does not replace or post-process the game-owned assets.

## Authored-lighting gloss

Both the outer `StadiumGrass` terrain and grass portions of road blocks are
classified as Grass, but the broad green start surface in the reported image
is `StadiumRoadRaceD`, classified as Asphalt. Lowering only the Grass profile
therefore cannot remove the rectangular highlight.

In authored-lighting mode, the raster delegate now sets track metalness and
specular amount to zero while keeping the restrained diffuse sun/fill that
makes the baked textures readable. Dynamic-lit mode still restores every
classified material scalar and native normal/specular map. Vehicle materials
remain on their independent delegate.

## Dirt resolution

The reported outer dirt was not a DDS parsing or mip failure. The real
`StadiumDirt` material resolves three valid 1024x1024 DXT1 textures with eleven
mips (`Blend1`, `Blend2`, and `BlendI`). Its selected model and shader paths are
opaque hashes, while the readable model remains
`Techno2/Media/Material/SoilGen21.Material.Gbx`.

Without plain-path recovery, a 736x448 metre terrain used an authored UV span
of only about `0.0139`, stretching roughly fourteen source texels across the
entire width and exposing giant block seams. The exact `SoilGen21` model is now
included in the narrow world-projection whitelist, producing the legacy
`world.xz / 16` mapping. The scene consequently uses 46x28 continuous
world-space repeats without broadening heuristics to unrelated materials.

## Distant texture filtering

Qt Quick 3D 6.8 cannot request anisotropic filtering from its D3D11 sampler;
the old `anisotropic` setting was therefore identical to trilinear. Existing
stored `anisotropic` values now migrate to stable trilinear instead of silently
changing appearance.

The new opt-in `Sharp hard surfaces (may shimmer)` mode uses full-resolution,
nearest minification only for opaque non-Grass, non-Dirt, non-Asphalt track
albedo. It visibly improves distant structure and sign detail while leaving
the repeated terrain classes that produced CRT-like moire on the stable path.
Trilinear remains the default. Driver-forced 16x anisotropic filtering remains
the best full-scene option because Qt has no application-level AF control.

## Skidmarks

The renderer and shader path were already present, but the headless Validator
runtime exported zero/default presentation snapshots. The paired Validator PR
now publishes live wheel contact and wheel-bottom coordinates. The deterministic
capture harness can extract recorded replay inputs and reports skidmark paths,
ribbon segments, and stamps in its JSON evidence.

Paired dependency:
[ForeverValidator PR #12](https://github.com/jakubschenk/ForeverValidator/pull/12).

## Validation

- native material/texture test for hashed `SoilGen21` selected paths with a
  readable plain model identity;
- renderer unit suite, including exact `world.xz / 16` vertex mapping;
- QML renderer-only smoke test on a real Stadium replay;
- graphics-settings persistence, legacy filtering migration, authored-to-lit
  scalar transitions, and class-aware sharp-filter bindings;
- deterministic D3D11 before/after capture of Stadium A02: all 1007 referenced
  textures still resolve, while the coarse dirt tiles become continuous native
  detail and world-projected materials increase from 29 to 60;
- same-camera `Abuk.Replay.Gbx` capture verifies the broad green start surface
  loses its pale PBR highlight without darkening the authored albedo;
- a fixed 90-m grazing-angle A/B capture verifies hard-surface detail changes
  while Grass, Dirt, and Asphalt remain stable; and
- recorded-input renderer capture produces a real skidmark path, backed by the
  Validator fixture's 440/440 valid sliding-contact ground samples.
