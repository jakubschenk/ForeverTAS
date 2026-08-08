# Textured map renderer

ForeverTAS reads ForeverValidator's immutable experimental render scene after a
replay load. That scene contains shared visual meshes, final material
references, lightweight instances, composed transforms, LOD/visibility data,
authored block provenance and structured diagnostics. Collision geometry
remains available through the pre-existing scene API.

## Material selection

ForeverValidator exposes material bitmap references and a scene-owned lazy
texture resolver. Before reading any bytes, ForeverTAS selects only materials
referenced by visible LOD0/default raster instances; hidden, background,
non-default, and non-renderable instances never enter the texture load set.
It then decodes the TMNF DDS/TGA variants into Qt-compatible PNG or KTX cache
entries. Successful resolver reads and converted files are content-addressed
and reused. Game assets are never bundled or redistributed.

The rules prefer photo-scanned CC0 PBR materials and purpose-built ImageGen
textures under `assets/materials`, and they use native texture paths only for
default-authored textured surfaces. Every source is listed in
`assets/materials/PROVENANCE.md`. Asphalt, dirt, metal, painted metal, and
rubber include independently sourced tangent-space normal and scalar roughness
maps. Other classes deliberately use scalar PBR values instead of fabricated
detail maps. Turbo uses right-facing cyan and yellow directional arrows,
checkpoints use colored bands, and start/finish components use a checker
pattern. Concrete is a deliberately flat gray. Broad, horizontal surface-0
meshes attached to authored blocks are recognized as grass ground cover instead
of inheriting the generic concrete fallback. This makes gameplay surfaces
recognizable even when every source material path is empty. Dense grass-blade
and grass-overlay meshes are removed before batching; only opaque ground
surfaces remain. Asphalt, grass, dirt, and concrete also ignore source
vertex-color tint because those channels encode game-specific data that can turn
replacement textures pale or orange.

`src/viewer/native_material.*` selects diffuse, blend, normal, and specular
slots by the legacy sampler names. Multi-layer albedo follows the original
blend equation, and a small explicit shader table supplies transparency,
two-sided, unlit, clamp/flip, water, and projection behavior. The eleven known
PDiff-family shaders sample exact world X/Z coordinates at one repeat per 16
metres. Other meshes retain authored UV0; the old randomized terrain remeshing
is not used.

If an asset is absent, inline/generated, unsupported, or fails validation, the
existing semantic replacement material remains available and the failure is
included in renderer telemetry. `src/viewer/material_classifier.*` still maps
those fallbacks from surface IDs and instance provenance, so a partial texture
archive never makes the scene unloadable.

## Qt Quick 3D

`src/viewer/visual_scene_pipeline.*` transforms static LOD0 geometry once on
load and batches compatible instances by source material, purpose,
vertex-color mode, alpha behavior, and bounded world-space cell. Opaque and
masked geometry is partitioned into 128-metre cells for view culling; blended,
additive, subtractive, and unknown ordering stays conservative. Each batch
becomes one indexed
`QQuick3DGeometry` with preserved normals, tangents, UV0, UV1, colors, and
material boundaries. Exact duplicate mesh/material/purpose/transform tuples
are suppressed.

The QML scene creates one `Model` per batch and one shared
`PrincipledMaterial` per source-material/vertex-color binding. Base, normal,
and specular maps use explicit sampler orientation, wrapping, filtering, mip,
alpha, and culling state. Static geometry is rebuilt only after a successful
replay reload; playback updates car transforms without touching map resources.

Authored/baked lighting is the default: materials use unlit texture color, no
light probe, no synthetic sun, and no tone mapping that would relight or wash
out the map. A compatibility setting enables dynamic fragment lighting,
normal/specular maps, ACES tone mapping, and optional world shadows. MSAA and
texture filtering are persistent graphics settings.
The replay-car proxy uses a clear-coated car treatment that remains compatible
with the terrain material contract.

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

The viewer provides Textured, Neutral, Collision, Wireframe, and High Contrast
modes. Collision and Wireframe continue to use the legacy collision buffers.
The experimental compute ray tracer is deliberately excluded from the build:
it duplicated material behavior, ignored authored baked lighting, and consumed
the GPU continuously even though the raster path already provides the desired
interactive view.
With no runtime ray-tracing mode, the raster renderer is the default path.

## Verification

The runtime data smoke test loads a native replay through the installed pack,
then audits the immutable public render scene rather than internal builders. It
checks indexed visual meshes, authored UVs and normals, material ranges,
instance transforms, provenance, shared mesh reuse, purpose bounds, overlap
conflicts, and separation from the collision triangle stream. Renderer and
viewer tests additionally verify exact world-XZ projection, spatial batch
telemetry, texture decoding/cache reuse, graphics-setting repair/persistence,
fallback behavior, shared QML material and texture objects, every render mode,
and transactional repeated reloads.

`forevertas-renderer-capture` is a Windows-only deterministic comparison tool.
It validates raster fixture renders, captures the camera pose and timeline state,
and writes both image and JSON evidence for review.

Runtime smoke validation covers a native replay without publishing that local
debug fixture. It verifies native game textures are actually resolved into
local cache files before exercising playback, camera updates, and repeated
loads.
