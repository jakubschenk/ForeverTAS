# ForeverTAS replacement textures

The physical materials are resized from photo-scanned CC0 Poly Haven sets and
an ambientCG foliage atlas. Asphalt, dirt, metal, painted metal, and rubber
include matching OpenGL tangent-space normal and scalar roughness maps from
their Poly Haven sets. The game-specific materials are built-in ImageGen
outputs created expressly for this renderer. No texture pixels are extracted
from TrackMania.

Every shipped texture is a 512 x 512 PNG. The same replacement-material
contract (base texture, optional normal and roughness textures, PBR scalars,
baked UV mapping, and vertex-color rule) is used by the raster and ray-traced
renderers. Their lighting implementations remain intentionally independent.

Generated and deliberately flat classes do not carry fabricated detail maps.
Glass and water instead use explicit transmission and index-of-refraction
scalars; selected coated manufactured surfaces use clearcoat scalars.

Concrete is the single deliberate exception to the detailed material set: its
base color is uniform by art direction, without white aggregate spots. See
`PROVENANCE.md` for the source and prompt used for every material.
