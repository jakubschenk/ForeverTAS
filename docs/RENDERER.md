# Textured map renderer

ForeverTAS reads ForeverValidator's immutable experimental render scene after a
replay load. That scene contains shared visual meshes, final material
references, lightweight instances, composed transforms, LOD/visibility data,
authored block provenance and structured diagnostics. Collision geometry
remains available through the pre-existing scene API.

## Material selection

Some exported stadium materials have no source paths, so
`src/viewer/material_classifier.*` primarily combines the surface-material ID
with instance provenance: block name, purpose, component identity, component
index, and descriptor path. Explicit semantic overrides cover turbo,
checkpoint, start/finish, road, dirt road, grass, pool/water, signage, and
emissive components. Path keywords remain a secondary hint for scenes that
provide them.

The rules never load game texture pixels. They select photo-scanned CC0 PBR
materials and purpose-built ImageGen textures under `assets/materials`; every
source is listed in `assets/materials/PROVENANCE.md`. Asphalt, dirt, metal,
painted metal, and rubber include independently sourced tangent-space normal
and scalar roughness maps. Other classes deliberately use scalar PBR values
instead of fabricated detail maps. Turbo uses right-facing cyan and yellow
directional arrows, checkpoints use colored bands, and start/finish components
use a checker pattern. Concrete is a deliberately flat gray. Broad, horizontal
surface-0 meshes attached to authored blocks are recognized as grass ground
cover instead of inheriting the generic concrete fallback. This makes gameplay
surfaces recognizable even when every source material path is empty. Dense
grass-blade and grass-overlay meshes are removed before batching; only opaque
ground surfaces remain. Asphalt, grass, dirt, and concrete also ignore source
vertex-color tint because those channels encode game-specific data that can
turn replacement textures pale or orange.

Unknown materials use a conspicuous magenta replacement. Missing UV0 receives
a deterministic X/Z projection. Asphalt and ground materials instead use a
dominant-axis world-space projection with one texture tile every four meters.
This replaces the tiny authored UV spans that previously reduced a 32-meter
block to only a few sampled pixels, and keeps road centers, standalone grass,
block borders, and grass clips at the same density. Missing normals are
accumulated from indexed triangles; missing tangents use a stable orthogonal
basis. Water, cube maps, render targets and complex shaders are reduced to
deterministic replacement material parameters.

## Qt Quick 3D

`src/viewer/visual_scene_pipeline.*` transforms static LOD0 geometry once on
load and batches compatible instances by replacement material, purpose, and
vertex-color mode. Each batch becomes one indexed `QQuick3DGeometry` with
preserved normals, tangents, UV0, UV1, colors, and material boundaries. Exact
duplicate mesh/material/purpose/transform tuples are suppressed.

The QML scene creates one `Model` per batch rather than one per source
instance. Material classes share a `PrincipledMaterial` and the exact albedo,
normal, and roughness assets sampled by the ray tracer. Both paths use the UVs
baked by the visual pipeline without a second scale and share vertex-color,
roughness, metalness, normal strength, specular, clearcoat, transmission,
refraction, and emissive parameters. Static geometry is rebuilt only after a
successful replay reload; playback updates car transforms without touching map
resources. Raster and ray-traced lighting remain renderer-specific, but both
consume the same material contract.

The textured viewport uses the project-owned 2:1 equirectangular panorama in
`assets/environment/day_sky.png` as both a true Qt Quick 3D skybox and an
image-based light probe. ACES tone mapping keeps the sunlit concrete and
emissive surfaces below clipping. Very-high MSAA, a depth pre-pass, screen-space
ambient occlusion, and a high-resolution warm key-light shadow map improve
surface separation and depth, while a restrained cool fill keeps dark stadium
structures readable. Glass and water receive but do not cast raster shadows.
The replay-car proxy now uses the same lit clear-coated PBR treatment and casts
shadows instead of rendering as an unlit overlay. These environment and light
properties are covered by the QML smoke test.

ForeverValidator labels enclosing backdrop geometry with a generic
`PhysicsSandboxRenderLayer::Background` value. Classification is based on
purpose, shared provenance, relative bounds, and enclosure of ordinary world
geometry; it contains no map, environment, or block-name special cases. The
viewer omits that backdrop from normal map batches so it cannot occlude the
real skybox. Authored blocks, ordinary environment scenery, intentional
generated stadium objects, and `StadiumGrassClip` meshes remain world
geometry. Dense blade layers within those clips are still omitted. Other
clips, checkpoint/start helpers, triggers, and initial-collision geometry stay
hidden.
The overlap audit checks for exact duplicates, cross-purpose coincident
transforms, and coincident conflicting materials.

Camera near/far planes are recalculated from camera position, orbit distance,
and the default visible-scene bounds. There is no fixed 5000-unit minimum. The
near plane also tracks the far plane to maintain a useful depth ratio.
Background-layer bounds do not inflate the camera range.

The viewer provides Textured, Textured (RT), Neutral, Collision, Wireframe, and
High Contrast modes. Textured (RT) is available when the build supports GPU ray
tracing. Collision and Wireframe continue to use the legacy collision buffers.

## Real-time GPU ray tracing

The Textured (RT) mode replaces the Qt Quick 3D raster viewport with a
`QQuickRhiItem` renderer. On Qt 6.7 or newer, it uploads the final
default-visible textured triangles with tangents, a balanced
four-triangle-leaf BVH, the shared PBR material table, albedo/normal/roughness
texture arrays, and the mipmapped daytime environment map to QRhi resources.
The compute shader casts deterministic primary rays, ray-traced sunlight
visibility rays, one-bounce reflection rays, and bounded refraction rays for
transmissive materials. Direct lighting uses a GGX microfacet BRDF with Smith
geometry and Schlick Fresnel terms. Roughness-aware environment mip selection,
clearcoat, emissive response, and tangent-space normal mapping follow the same
material values as raster mode. The compute output uses RGBA16F; a fullscreen
pass applies ACES tone mapping, gamma conversion, and edge-aware antialiasing.

This is deliberately a real-time video-game renderer rather than a progressive
offline path tracer. It has no stochastic diffuse bounce loop and no noisy
image that must converge. Camera position and vertical field of view come from
the Qt Quick 3D camera, while the selected replay car is the explicit look
target. The present triangle maps the complete render texture to the viewport,
so the selected car remains centered during orbiting and playback.

QRhi does not expose hardware ray-tracing acceleration structures, so traversal
runs in a compute shader over the project-owned BVH instead of using vendor RT
cores. It is nevertheless real GPU ray tracing and intentionally consumes the
GPU continuously while enabled. The feature is compiled when Qt GuiPrivate and
ShaderTools are available with Qt 6.7 or newer. Qt 6.5 and 6.6 retain the
complete raster renderer and omit only the Textured (RT) mode rather than
failing configuration or startup.

## Verification

The runtime data smoke test loads a native replay through the installed pack,
then audits the immutable public render scene rather than internal builders. It
checks indexed visual meshes, authored UVs and normals, material ranges,
instance transforms, provenance, shared mesh reuse, purpose bounds, overlap
conflicts, and separation from the collision triangle stream. Renderer and
viewer tests additionally verify semantic overrides, clip-plane calculation,
default visibility, transformed batching, exact vertex attributes, packaged
replacement images and their provenance, the expanded CPU/GPU material ABI,
shared QML material and texture objects, every render mode, and transactional
repeated reloads.

`forevertas-renderer-capture` is a Windows-only deterministic comparison tool.
It loads the normal `Main.qml` scene on a real Direct3D 11 QRhi, isolates
settings, applies an explicit replay tick and orbital camera, waits for settled
frames, and writes both a PNG and JSON evidence record. RT capture additionally
discards warm-up grabs and requires two identical consecutive frames so a
previous-camera QQuickRhiItem texture cannot be accepted. The record contains
the camera, timeline, scene counts, sampled image statistics, output SHA-256, Qt
version, and proof that a real RHI/device was present. Raster modes and, when
compiled, `textured-rt` can therefore be captured from the same fixture and
camera rather than compared by eye from unrelated interactive views.

Runtime smoke validation covers a native replay without publishing that local
debug fixture. It verifies a clean first frame, centered car framing throughout
playback, responsive camera updates, and sustained GPU execution while ray
tracing is enabled.
