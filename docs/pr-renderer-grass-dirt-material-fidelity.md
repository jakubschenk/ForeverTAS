# PR: Fix Stadium grass gloss and dirt projection

## Summary

This renderer follow-up fixes two independent Stadium material regressions:

- bind each classified track material's specular amount into Qt Quick 3D so
  grass uses the intended low-specular response instead of Qt's fully specular
  default;
- recover the exact `SoilGen21` world-XZ mapping from its readable plain model
  identity when the installed archive selects hashed model and shader paths.

The change keeps native TrackMania albedo textures, blend composition, and mip
generation intact. It does not replace or blur the game-owned assets.

## Grass gloss

Both the outer `StadiumGrass` terrain and grass portions of road blocks are
classified as Grass. Their material definition already specifies roughness
`0.90`, metalness `0`, and specular amount `0.28`, but the raster QML delegate
previously bound only roughness and metalness. Qt 6.8 therefore used
`PrincipledMaterial`'s default specular amount of `1.0`.

The delegate now binds the classified specular scalar. The QML renderer smoke
test compares instantiated roughness, metalness, and specular values with every
published track material definition, preventing another silent default.

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
`world.xz / 16` mapping. The same scene consequently uses 46x28 continuous
world-space repeats without broadening heuristics to unrelated materials.

## Validation

- native material/texture test for hashed `SoilGen21` selected paths with a
  readable plain model identity;
- renderer unit suite, including exact `world.xz / 16` vertex mapping;
- QML renderer-only smoke test on a real Stadium replay;
- deterministic D3D11 before/after capture of Stadium A02: all 1007 referenced
  textures still resolve, while the coarse dirt tiles become continuous native
  detail and world-projected materials increase from 29 to 60;
- same-camera Stadium A1 capture verifies grass uses the classified material
  response with all 942 referenced textures resolved.

