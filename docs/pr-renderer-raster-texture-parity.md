# PR: Restore authored raster materials and bounded visual batching

## Summary

This change replaces the normal viewer's synthetic/re-lit material path with a
raster-first pipeline that uses the TrackMania textures already installed on
the user's machine. It also removes the experimental compute ray tracer from
the app build, preserves authored UV behavior, adds persistent graphics
settings, and bounds large opaque geometry into spatial batches.

No TrackMania texture is committed, packaged, or redistributed. The renderer
reads game-owned bytes lazily through the companion ForeverValidator API and
writes only content-addressed Qt-compatible cache files below the user's cache
directory.

## Material and texture path

- Selects legacy albedo, blend, normal, and specular sampler slots with explicit
  priority rules.
- Reproduces the observed three-layer albedo equation in linear color.
- Decodes legacy/DX10 BC1-BC5 DDS, DXT5nm normals, uncompressed DDS, TGA, and
  ordinary Qt image formats.
- Keeps compatible linear compressed mip chains as KTX; color-managed albedo,
  normal, and composite outputs use PNG plus GPU mip generation where Qt 6.8
  cannot represent both sRGB and authored compressed mips correctly.
- Uses exact world-XZ `/ 16` mapping only for the known PDiff-family shader set;
  all other materials keep authored UVs. Random terrain subdivision is no
  longer used.
- Carries alpha, two-sided, unlit, visibility, wrap, flip, and water behavior
  into shared QML materials. Qt's Screen and Multiply modes are used as the
  closest built-in approximations for additive and subtractive legacy shaders.
- Falls back per material to the existing semantic replacement textures when
  an asset is missing, generated inline, malformed, or unsupported.

The implementation is an independent behavior-level compatibility layer. It
does not copy gbx-tools-3d source or redistribute its AGPL implementation.

## Performance and safety

- Texture reads, decoded assets, composites, and on-disk conversions are reused
  through scene-local and content-addressed caches.
- Texture loading is restricted to materials referenced by visible LOD0,
  default-raster instances; hidden, background, non-default, and invalid
  instances do not trigger asset reads.
- Opaque/masked batches are partitioned into 128-metre world cells for view
  culling. Blended, additive, subtractive, and unknown ordering remains
  conservative.
- Renderer telemetry reports referenced/resolved/failed textures, cache hits,
  native/fallback materials, estimated texture memory, load/build time, batch
  count, and populated spatial cells.
- A texture is limited to 8,192 pixels per dimension, 16 million decoded base
  pixels, and 256 MiB encoded input. A scene may submit at most 512 MiB of
  estimated native texture memory, and the global converted-texture cache is
  pruned to 1 GiB.

## Graphics behavior

- Authored/baked lighting is the default: no synthetic light probe, dynamic
  sun, shadows, or extra tone mapping.
- Optional dynamic lighting enables normal/specular maps, ACES tone mapping,
  and user-controlled world shadows.
- Render mode, lighting mode, MSAA, filtering, and shadow settings persist with
  repaired defaults for stale values (including migration from `textured-rt`).
- The render-mode menu now contains Textured, Neutral, Collision, Wireframe,
  and High Contrast only.

## Companion dependency

This branch requires the ForeverValidator `expose-material-texture-assets`
branch. That API owns pack routing and lazy byte access so the public render
scene remains immutable and cheap to copy. The exact validator commit is pinned
in CMake and the release manifest.

## Validation

- Focused graphics-settings, native texture/material, and renderer tests.
- Synthetic coverage for BC1-BC5, DXT3/5 alpha, DXT5nm reconstruction, TGA
  orientation, malformed/oversized inputs, blend composition, and cache reuse.
- QML coverage for shared bindings, fallback mipmaps, alpha/blend/culling,
  authored-vs-dynamic native maps, shadow toggling, and raster-only modes.
- Real-replay render-scene, viewer, QML, application, and portable-package smoke
  gates are run before publishing the package.

## Known approximations

- Qt Quick 3D 6.8 has no true subtractive `PrincipledMaterial` equation;
  Multiply preserves the intended darkening but is not bit-identical.
- Inline/generated render textures are deliberately diagnostic fallbacks rather
  than files exposed as game assets.
- Rare shader archives that depend on file-global shared CMwId dictionary state
  may remain unresolved by the companion parser and fall back safely.
