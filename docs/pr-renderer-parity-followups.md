# PR: Complete raster environment, vehicle, and skidmark parity

## Summary

This follow-up fixes four renderer gaps found after restoring TrackMania's
authored raster materials:

- restore the bundled neutral sky environment even before a map is loaded;
- keep diffuse alpha that carries specular data opaque, preventing distant
  transparent-sort seams and shimmer;
- render the installed vehicle's local-space visual mesh and material assets
  instead of using collision ellipsoids as the normal car body;
- reconstruct skidmarks from exact per-wheel contact, sliding, surface, and
  ground-point samples already produced by the simulator.

The static map remains authored/unlit. The moving vehicle may use the neutral
sky probe, so this does not reintroduce runtime map relighting or ray tracing.

## Vehicle path

The companion ForeverValidator change exposes a cached, immutable vehicle
render scene. ForeverTAS converts it once into local-space GPU buffers and
shares those buffers and materials across every visible run. Only each run's
car root pose changes during playback. If vehicle visuals cannot be decoded,
the existing collision ellipsoids remain an explicit non-fatal fallback.

This milestone uses the installed vehicle model and its native material/skin
assets. Replay-referenced third-party skin archives are not downloaded; doing
so would require a separate trusted local ZIP resolution and checksum policy.
ForeverTAS recognizes the vehicle's legacy `Diffuse_Gloss` sampler as its
albedo source while leaving that texture's alpha available as gloss data rather
than incorrectly treating the body as transparent.

## Skidmarks

Wheel ground points are copied only while producing viewer/replay samples, not
during CUDA candidate evaluation. A selected run receives one prebuilt ribbon
mesh whose vertex timestamps reveal marks as playback advances. Airborne,
non-sliding, unsupported-surface, and respawn discontinuities break the trail.
The effect is unlit and alpha blended so it respects baked map lighting.

## Validation

- native material alpha-semantics unit tests;
- graphics-settings persistence tests;
- deterministic skidmark geometry tests;
- D3D11 custom skidmark shader render smoke test;
- ForeverValidator render-scene and wheel-state tests;
- real installed-game replay gate: four shared vehicle batches, four material
  bindings, and all three referenced vehicle textures resolved with native
  albedo;
- renderer/QML smoke tests, including the pre-map skybox environment;
- portable NVIDIA package smoke test.
