# Renderer fidelity comparison

This report compares the renderer on the integrated `experimental` baseline
(`ea89cfc`) with the renderer-fidelity changes. The comparison uses the same
ForeverValidator revision, replay, timeline tick, camera, output size, Qt
version, and Direct3D 11 backend on both sides.

## Reference and target

The visual target was the public
[3d.gbx.tools replay viewer](https://3d.gbx.tools/view/replay?tmx=TMUF&id=7229782&mapid=3056)
and its [open-source implementation](https://github.com/BigBang1112/gbx-tools-3d).
It establishes the target qualities: readable silhouettes, authored-looking
surface variation, convincing sun and contact shadows, material separation,
sky/environment lighting, and a car that belongs in the scene. ForeverTAS does
not copy AGPL implementation code or assets; the implementation here is an
independent MIT-licensed renderer change using documented CC0 assets.

The reference viewer still has a fundamental asset advantage: it can display
game-derived texture data and richer vehicle/scene assets. ForeverTAS currently
receives geometry plus material identity/path metadata from ForeverValidator,
not authored texture pixels, and still represents the car with simulation
ellipsoids. This iteration targets lighting and material fidelity within that
data contract.

## Controlled fixture

- Replay: `Seeding Track 1_QwerTAS(00'17''84).Replay.Gbx`
- Replay SHA-256:
  `0A47BBBAD3D4AB94EEF8D36610392AFF03C4420E545000A6AE35198F06AFB287`
- Qt: 6.8.3
- API: Direct3D 11 through a real QRhi/device
- Resolution: 1280x720
- Camera: yaw 35, pitch -20, distance 38, vertical FOV 55
- Timeline fractions: 0.05, 0.50, and 0.90
- Scene at the middle capture: 1,054 visual meshes, 26 final batches,
  1,601,868 visible triangles, and 358 source materials

The local replay and TrackMania pack files are not committed or uploaded.
Capture metadata redacts absolute paths by default.

## Reproduction

Build `forevertas-renderer-capture`, then run:

```powershell
forevertas-renderer-capture.exe `
  --packs <Packs> `
  --replay <Replay.Gbx> `
  --output capture.png `
  --mode textured `
  --tick-fraction 0.50 `
  --size 1280x720 `
  --camera 35,-20,38,55
```

Use `--mode textured-rt` to exercise the actual QRhi compute renderer. Every
successful run writes a PNG and adjacent JSON evidence record with the camera,
timeline, scene counts, image statistics, Qt/API state, and PNG SHA-256.

## Raster results

The comparison images generated during validation are:

- `track1-050-before-left-after-right.png`
- `track1-detail-050-before-left-after-right.png`

They live under the local build's `comparisons` directory and intentionally are
not committed as binary test artifacts. In both images, the baseline is on the
left and the renderer-fidelity result is on the right.

| Tick | Mean luma before | Mean luma after | Sample colors before | Sample colors after | Luma range before | Luma range after |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 0.05 | 82.1529 | 60.7299 | 10,941 | 13,475 | 14..218 | 4..215 |
| 0.50 | 82.1575 | 60.7529 | 10,975 | 13,544 | 13..218 | 4..216 |
| 0.90 | 82.1524 | 60.7570 | 10,996 | 13,545 | 13..218 | 4..216 |

The lower average is deliberate rather than an exposure regression: the old
renderer filled most of the stadium with flat ambient light, while the new
renderer preserves bright sky/water highlights and adds physically located
shadowed regions. The expanded luminance range and roughly 23% increase in
sampled colors reflect materially greater local contrast and surface detail.

Visible improvements in the matched frames include:

- directional shadows from track structures and the replay-car proxy;
- screen-space ambient occlusion and stronger contact/depth cues;
- higher quality MSAA and a depth pre-pass;
- normal and roughness detail on asphalt, dirt, metal, painted metal, and
  rubber;
- material-specific specular, clearcoat, transmission, and refraction values;
- a lit, clear-coated replay-car proxy instead of a flat emissive overlay; and
- stronger separation between grass, road, metal, water, and structural parts.

The baseline and final middle frames measured SSIM 0.810539, confirming a
material visual change rather than a metadata-only renderer update. Two
independent final raster captures at the same inputs measured SSIM 0.999985.
The D3D11 raster result is therefore visually repeatable, although it is not
promised to be byte-identical across asynchronous GPU runs.

## Ray-tracing result

The baseline `textured-rt` path failed on D3D11 while creating the compute
pipeline because the generated HLSL saw a partially initialized `Hit` output
parameter. The renderer-fidelity branch replaces that cross-compiled struct
output with an explicitly initialized scalar distance result.

After the fix, the real D3D11 RT capture passed at tick 300/601 with:

- `gpuRayTracingView` selected and active;
- non-clear 1280x720 output;
- 17,950 distinct sampled colors and luminance range 7..251;
- deterministic PNG SHA-256 across repeated runs:
  `a2938ce875ab2ad74e40ff0f7e212a0077505fb30b55b7869684545b7bd6530a`.

The capture harness discards initial nonblank RT warm-up grabs and requires
two consecutive pixel-identical RT frames. This prevents a previous-camera
QQuickRhiItem texture from being accepted as current evidence.

The RT material path now consumes tangents plus albedo, normal, and roughness
arrays; uses GGX/Smith/Schlick direct lighting; selects environment mips from
roughness; supports clearcoat and bounded refraction; and writes RGBA16F rather
than RGBA32F.

## Remaining fidelity gap

This iteration closes much of the lighting and generic-material gap, but it
does not make replacement textures equivalent to authored TrackMania assets.
The two highest-value future steps are exposing decoded material texture pixels
through ForeverValidator's immutable render-scene API and replacing the
ellipsoid car proxy with an actual vehicle mesh. Those are cross-repository data
and asset changes rather than additional lighting tweaks.
