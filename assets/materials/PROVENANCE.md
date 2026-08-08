# Replacement material provenance

All Poly Haven assets below are CC0 and were downloaded through the official
Poly Haven API at 1K, then resized to the committed 512 x 512 PNGs. The asset
library publishes photo-scanned, seamless PBR material sets. On 2026-08-08,
the added detail maps were resolved from each asset's
`https://api.polyhaven.com/files/{asset}` response at `nor_gl -> 1k -> png`
and `Rough -> 1k -> png`; no search mirror or third-party copy was used.

| ForeverTAS material | Source | Imported map |
| --- | --- | --- |
| Asphalt | [Asphalt Track](https://polyhaven.com/a/asphalt_track) | Diffuse, OpenGL normal, roughness |
| Dirt | [Dirt](https://polyhaven.com/a/dirt) | Diffuse, OpenGL normal, roughness |
| Metal | [Metal Plate 02](https://polyhaven.com/a/metal_plate_02) | Diffuse, OpenGL normal, roughness |
| Painted metal | [Blue Metal Plate](https://polyhaven.com/a/blue_metal_plate) | Diffuse, OpenGL normal, roughness |
| Rubber | [Rubber Tiles](https://polyhaven.com/a/rubber_tiles) | Diffuse, OpenGL normal, roughness |

License: [Poly Haven CC0](https://polyhaven.com/license).

## Poly Haven detail-map byte provenance

The URL behind every source filename below is the exact URL returned by the
official API. `API MD5` is Poly Haven's checksum from that response. Source and
committed SHA-256 values were computed locally with PowerShell
`Get-FileHash -Algorithm SHA256`.

| Committed file | Exact official 1K PNG | API MD5 | Source SHA-256 | Committed SHA-256 |
| --- | --- | --- | --- | --- |
| `asphalt_normal.png` | [asphalt_track_nor_gl_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/asphalt_track/asphalt_track_nor_gl_1k.png) | `af77c53cabdcccd619d85322f7af1535` | `1cd72b1a2ee50417d3e9cf3fff1e9873af978107b1c1bc9cbbef509b6a39604a` | `bebd3ccdfe4202833ff16a3fc918e68255d574804474ce001c28a00802f9f0d6` |
| `asphalt_roughness.png` | [asphalt_track_rough_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/asphalt_track/asphalt_track_rough_1k.png) | `c092a77d75b861a8b3be9f31e7ec5067` | `0b34dcd4da4726144ce47e1e5ce40ec361d6662916ce763d4bd96135c90fd81c` | `28a4ffcecf7b4b653de460156fe85fa194401ac75909775e0aab401dcb704be1` |
| `dirt_normal.png` | [dirt_nor_gl_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/dirt/dirt_nor_gl_1k.png) | `e1d855d9d6b1c9b39681ee4f4097303b` | `be315cdb7667d58df160a92be2b22423480912ac5668188f3c8360d464203f47` | `c63989d2503da30b8186ef407e911c34086e9a0fdbf2079193e8c09407454519` |
| `dirt_roughness.png` | [dirt_rough_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/dirt/dirt_rough_1k.png) | `293473a9600f0938205a05461a4396bb` | `29f68a8972f1e69cf47e67924efe469350f953a5f707f63633a9c36cd7c63f6a` | `c3806af6d73e663a03abfba54223833c2b19092c270e19ed4c14d0d7fe05bc07` |
| `metal_normal.png` | [metal_plate_02_nor_gl_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/metal_plate_02/metal_plate_02_nor_gl_1k.png) | `95504e9795834d3bd9b7818031d19f86` | `a39eee156a992afef4f1453010f2f2ff3c01cd6b5e0ce408259954ce25dd049f` | `341838ee357dc6e0a2f6c2ee2f33fab2709e1ef055965fae4daa286a45da7fe9` |
| `metal_roughness.png` | [metal_plate_02_rough_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/metal_plate_02/metal_plate_02_rough_1k.png) | `743fdbd128ed05ddb90af1d1c28c20a0` | `feb85c25248a91f45dceefad846df61f10e246c14009014bcf7be5ce68e32ddd` | `a774244935cc6d801590127692e22f09896e3280c0cd21d41811cb2f61092401` |
| `painted_metal_normal.png` | [blue_metal_plate_nor_gl_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/blue_metal_plate/blue_metal_plate_nor_gl_1k.png) | `6fcde97d9970fbccfa443c3efe9e53ae` | `9f03852e15eadef3b0e6610ed6f040f2e8f0fd818ca31b76c94711e43b4c4e7a` | `e12565fa16b4586589cae370ffc121ba65ddc0a3ca7663a67733554e346d53bd` |
| `painted_metal_roughness.png` | [blue_metal_plate_rough_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/blue_metal_plate/blue_metal_plate_rough_1k.png) | `4508681759a8f3ef926c86c6ee2b3ce8` | `db0cee2879a04f6449f8abd4dbe51b9ba4be939052dc27dd8f8c2bcee8d2c402` | `ce9e18a703596a1b6a143b3a96e686c24e6e8c63a2d0ff6a3ee1b1a4fbf45ff4` |
| `rubber_normal.png` | [rubber_tiles_nor_gl_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/rubber_tiles/rubber_tiles_nor_gl_1k.png) | `375d515ce497f48e95973235a88da950` | `7e3e3cbe43646da80bb4ac78e4078b874f3035daddc203752e500027dfe0a29f` | `9e7a6a9d4a6c954e66d9234afc09ca40fe897a2311c7c3674a06428b03369c9b` |
| `rubber_roughness.png` | [rubber_tiles_rough_1k.png](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/rubber_tiles/rubber_tiles_rough_1k.png) | `e01a061252bd3e6f4b6f6457b54a4d0c` | `b6916090b2a4b069792bbee710fe8eed18370b818461fac03ad3c4c0e6799598` | `83566650503a06058f56cfe80c9ab3d0c93fba3aa140b5f80e86fd69cb6ab997` |

All downloaded maps were 1024 x 1024 16-bit PNGs. They were converted with
FFmpeg 8.1.1 using Lanczos resampling, metadata stripping, and deterministic
PNG output. Normals use `rgb24`; roughness maps use `gray`:

```text
ffmpeg -hide_banner -loglevel error -y -i SOURCE -vf scale=512:512:flags=lanczos -frames:v 1 -c:v png -compression_level 9 -pred mixed -pix_fmt rgb24 -map_metadata -1 -update 1 DESTINATION
ffmpeg -hide_banner -loglevel error -y -i SOURCE -vf scale=512:512:flags=lanczos -frames:v 1 -c:v png -compression_level 9 -pred mixed -pix_fmt gray -map_metadata -1 -update 1 DESTINATION
```

Grass ground cover uses ambientCG's CC0
[Grass 001](https://ambientcg.com/view?id=Grass001) Color map. Grass blades are
intentionally not rendered; every grass appearance uses this opaque ground
material.

License: [ambientCG CC0](https://ambientcg.com/license).

## ImageGen prompt set

The remaining albedos were generated with built-in ImageGen in
`stylized-concept` mode. Every prompt requested a square, orthographic,
edge-to-edge, game-ready material texture at 1024 x 1024 or larger, without
perspective, objects, borders, isolated dots, polka dots, text, logos,
watermarks, baked directional light, or cast shadows.

| Material | Primary prompt | Local ImageGen source |
| --- | --- | --- |
| Plastic | Light-gray injection-molded engineering plastic with fine grain, manufacturing variation, and restrained scuffs; explicitly not concrete, paint, or metal. | `call_GJt9oj5qASrcHaHmy2PvpD9C.png` |
| Neutral | Medium-gray molded stadium mineral composite with subtle woven microstructure, broad manufacturing waves, and quiet wear. | `call_blG17INiFU6iKziuuRfRfeFh.png` |
| Unknown | Charcoal diagnostic technical composite with coherent, large-scale magenta angular circuit and hazard motifs. | `call_VXEV99VDz8hqNisySKX18mcy.png` |
| Signage | Weathered off-white stadium sign panel with abstract deep-red and cobalt racing graphics, printed-ink wear, and no readable brands. | `call_MWGaWCRd7WigyrlCSvo0gRRm.png` |
| Emissive | Graphite technical panel with recessed mint-cyan electroluminescent racing-circuit channels and no broad glow halo. | `call_d0GrXrX6HUIuB6IHoBkJkDh0.png` |
| Turbo | Dark-teal anti-slip turbo pad with exactly two unmistakable right-facing chevrons, cyan followed by yellow, and no competing directions. | `call_J1nNFuLNXUWDyhUTSLoBenii.png` |
| Checkpoint | Cobalt industrial checkpoint panel with broad horizontal off-white and vivid-red identification bands, inset seams, and racing wear. | `call_Uz8oulFbXTe63MrqSwQtpPrh.png` |
| Start/finish | Black and warm off-white motorsport checkerboard with a narrow horizontal green timing stripe, tire abrasion, and shallow joins. | `call_yfvvhGGi93KBe0uKLDDm3HwQ.png` |
| Glass | Pale cool-cyan tempered safety glass with broad manufacturing waviness, cleaning arcs, and restrained hairline wear. | `call_O9Lx9cMJ5gYd2tCKyPTwnkzN.png` |
| Water | Clear cyan-teal stadium-pool water viewed from above with layered wind ripples and coherent wave interference. | `call_ZzGO2kv0FwGXRl13BlPGmpsB.png` |

The listed source images remain under
`/home/mikael/.codex/generated_images/019f9782-1b58-71b1-a2ba-8ccd3c0aafe4/`.
The committed files are resized copies under `assets/materials`.

## Intentional flat concrete

`concrete_base.png` is a uniform `#a4a69f`. This preserves the requested clean,
spot-free gray concrete instead of reintroducing aggregate or painted noise.
