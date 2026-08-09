#include "viewer/native_material.h"
#include "viewer/native_material_loader.h"
#include "viewer/native_texture_cache.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using forevertas::viewer::ComposeNativeAlbedo;
using forevertas::viewer::DecodeNativeTexture;
using forevertas::viewer::ApplyNativeAlbedoTransparency;
using forevertas::viewer::NativeAlbedoAlphaUsage;
using forevertas::viewer::NativeMaterialProfile;
using forevertas::viewer::NativeTextureSemantic;
using forevertas::viewer::PrepareNativeTexture;
using forevertas::viewer::ResolveNativeMaterialProfile;
using forevertas::viewer::ResolveNativeMaterialProfiles;
using forevertas::viewer::SelectDefaultNativeMaterialLoadMask;
using forevertas::viewer::StaticVisualAlphaMode;
using forevertas::viewer::StoreNativeTexture;
using forevertas::viewer::LoadNativeMaterials;
using forevertas::viewer::CachedNativeTexture;
using forevertas::viewer::DecodedNativeTexture;
using forevertas::viewer::PreparedNativeTexture;
using forevervalidator::experimental::PhysicsSandboxMaterialBitmap;
using forevervalidator::experimental::PhysicsSandboxRenderInstance;
using forevervalidator::experimental::PhysicsSandboxRenderLayer;
using forevervalidator::experimental::PhysicsSandboxRenderMaterial;
using forevervalidator::experimental::PhysicsSandboxRenderMesh;
using forevervalidator::experimental::PhysicsSandboxRenderScene;
using forevervalidator::experimental::PhysicsSandboxScenePurpose;

bool Check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

void WriteLe32(std::uint32_t value, char *destination) {
    destination[0] = static_cast<char>(value & 0xffu);
    destination[1] = static_cast<char>((value >> 8u) & 0xffu);
    destination[2] = static_cast<char>((value >> 16u) & 0xffu);
    destination[3] = static_cast<char>((value >> 24u) & 0xffu);
}

void WriteLe16(std::uint16_t value, char *destination) {
    destination[0] = static_cast<char>(value & 0xffu);
    destination[1] = static_cast<char>((value >> 8u) & 0xffu);
}

std::uint32_t FourCc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

QByteArray CreateBlockCompressedDds(
        std::uint32_t fourCc, const QByteArray &levelData,
        std::uint32_t width = 4u, std::uint32_t height = 4u,
        std::uint32_t dxgiFormat = 0u) {
    const bool hasDx10Header = fourCc == FourCc('D', 'X', '1', '0');
    const int dataOffset = hasDx10Header ? 148 : 128;
    QByteArray encoded(dataOffset + levelData.size(), '\0');
    std::copy_n("DDS ", static_cast<std::size_t>(4), encoded.begin());
    WriteLe32(124u, encoded.data() + 4);
    WriteLe32(0x000a1007u, encoded.data() + 8);
    WriteLe32(height, encoded.data() + 12);
    WriteLe32(width, encoded.data() + 16);
    WriteLe32(static_cast<std::uint32_t>(levelData.size()),
              encoded.data() + 20);
    WriteLe32(1u, encoded.data() + 28);
    WriteLe32(32u, encoded.data() + 76);
    WriteLe32(4u, encoded.data() + 80);
    WriteLe32(fourCc, encoded.data() + 84);
    WriteLe32(0x1000u, encoded.data() + 108);
    if (hasDx10Header) {
        WriteLe32(dxgiFormat, encoded.data() + 128);
        WriteLe32(3u, encoded.data() + 132);  // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        WriteLe32(1u, encoded.data() + 140);  // array size
    }
    std::copy(levelData.begin(), levelData.end(),
              encoded.begin() + dataOffset);
    return encoded;
}

QByteArray CreateColorBlock(std::uint16_t color0, std::uint16_t color1,
                            std::uint32_t indices = 0u) {
    QByteArray block(8, '\0');
    WriteLe16(color0, block.data());
    WriteLe16(color1, block.data() + 2);
    WriteLe32(indices, block.data() + 4);
    return block;
}

QByteArray CreateAlphaBlock(
        unsigned char alpha0, unsigned char alpha1,
        const std::array<unsigned char, 16> &alphaIndices = {}) {
    QByteArray block(8, '\0');
    block[0] = static_cast<char>(alpha0);
    block[1] = static_cast<char>(alpha1);
    std::uint64_t packedIndices = 0u;
    for (std::size_t index = 0; index < alphaIndices.size(); ++index) {
        packedIndices |= static_cast<std::uint64_t>(alphaIndices[index] & 7u)
                         << (3u * index);
    }
    for (int byte = 0; byte < 6; ++byte) {
        block[byte + 2] = static_cast<char>(
                (packedIndices >> (8u * static_cast<unsigned>(byte))) & 0xffu);
    }
    return block;
}

std::array<unsigned char, 4> PixelRgba(const QImage &image, int x, int y) {
    const unsigned char *pixel = image.constScanLine(y) + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

unsigned char ReconstructedNormalZ(unsigned char x, unsigned char y) {
    const float nx = x / 127.5f - 1.0f;
    const float ny = y / 127.5f - 1.0f;
    const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
    return static_cast<unsigned char>(
            std::clamp(nz * 127.5f + 127.5f, 0.0f, 255.0f));
}

QByteArray CreateOrientedTga(bool topOrigin) {
    constexpr int Width = 2;
    constexpr int Height = 2;
    const std::array<std::array<unsigned char, 4>, Width * Height> pixels{{
            {255u, 0u, 0u, 255u}, {0u, 255u, 0u, 255u},
            {0u, 0u, 255u, 255u}, {255u, 255u, 0u, 128u},
    }};

    constexpr int TgaFooterSize = 26;
    QByteArray encoded(18 + Width * Height * 4 + TgaFooterSize, '\0');
    encoded[2] = 2;  // uncompressed true-color image
    WriteLe16(Width, encoded.data() + 12);
    WriteLe16(Height, encoded.data() + 14);
    encoded[16] = 32;
    encoded[17] = static_cast<char>(8u | (topOrigin ? 0x20u : 0u));
    int offset = 18;
    for (int storedY = 0; storedY < Height; ++storedY) {
        const int logicalY = topOrigin ? storedY : Height - storedY - 1;
        for (int x = 0; x < Width; ++x) {
            const auto &rgba = pixels[logicalY * Width + x];
            encoded[offset++] = static_cast<char>(rgba[2]);
            encoded[offset++] = static_cast<char>(rgba[1]);
            encoded[offset++] = static_cast<char>(rgba[0]);
            encoded[offset++] = static_cast<char>(rgba[3]);
        }
    }
    constexpr char Tga20Signature[] = "TRUEVISION-XFILE.";
    std::copy_n(Tga20Signature, sizeof(Tga20Signature),
                encoded.begin() + encoded.size() - 18);
    return encoded;
}

QByteArray CreateMinimalDxt1Dds() {
    QByteArray encoded;
    encoded.resize(136);
    std::fill(encoded.begin(), encoded.end(), 0);
    std::copy_n("DDS ", static_cast<std::size_t>(4), encoded.begin());
    WriteLe32(124u, encoded.data() + 4);
    WriteLe32(4u, encoded.data() + 12);    // width
    WriteLe32(4u, encoded.data() + 16);    // height
    WriteLe32(1u, encoded.data() + 28);      // mip levels
    WriteLe32(32u, encoded.data() + 76);     // pixel format size
    WriteLe32(4u, encoded.data() + 80);      // flags with DDPF_FOURCC
    WriteLe32(FourCc('D', 'X', 'T', '1'), encoded.data() + 84);
    WriteLe32(8u, encoded.data() + 12 + 76); // pitch/size placeholder

    char *dxt1Block = encoded.data() + 128;
    const std::uint16_t color0 = 0xffffu;
    const std::uint16_t color1 = 0x0000u;
    dxt1Block[0] = static_cast<char>(color0 & 0xffu);
    dxt1Block[1] = static_cast<char>(color0 >> 8u);
    dxt1Block[2] = static_cast<char>(color1 & 0xffu);
    dxt1Block[3] = static_cast<char>(color1 >> 8u);
    dxt1Block[4] = 0x00;
    dxt1Block[5] = 0x00;
    dxt1Block[6] = 0x00;
    dxt1Block[7] = 0x00;
    return encoded;
}

QByteArray EncodePng(const QImage &image, QString *error = nullptr) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (error != nullptr) error->clear();
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        if (error != nullptr) *error = QStringLiteral("PNG encoding failed");
        return {};
    }
    return bytes;
}

DecodedNativeTexture DecodedFromRgba(QImage image) {
    DecodedNativeTexture decoded;
    decoded.sourceFormat = QStringLiteral("test");
    image = image.convertToFormat(QImage::Format_RGBA8888);
    decoded.mipmaps.push_back(std::move(image));
    return decoded;
}

float SrgbToLinear(unsigned char value) {
    const float encoded = value / 255.0f;
    return encoded <= 0.04045f ? encoded / 12.92f
                               : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

unsigned char LinearToSrgb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const float encoded = value <= 0.0031308f
                                  ? value * 12.92f
                                  : 1.055f * std::pow(value, 1.0f / 2.4f) -
                                            0.055f;
    return static_cast<unsigned char>(std::lround(std::clamp(encoded * 255.0f,
                                                           0.0f, 255.0f)));
}

std::array<unsigned char, 4> ExpectedCompositedPixel(
        const std::array<unsigned char, 4> &diffuse,
        const std::array<unsigned char, 4> *blend2,
        const std::array<unsigned char, 4> *blendMask,
        const std::array<unsigned char, 4> *blend3) {
    const float blend2Amount =
            blend2 != nullptr && blendMask != nullptr
                    ? blendMask->at(0) / 255.0f
                    : 0.0f;
    const float blend3Amount =
            blend3 != nullptr ? blend3->at(3) / 255.0f : 0.0f;

    std::array<unsigned char, 4> result;
    for (int channel = 0; channel < 3; ++channel) {
        const float diffuseLinear = SrgbToLinear(diffuse[channel]);
        const float blend2Linear =
                blend2 != nullptr ? SrgbToLinear((*blend2)[channel]) : 0.0f;
        const float blend3Linear =
                blend3 != nullptr ? SrgbToLinear((*blend3)[channel]) : 0.0f;
        const float firstMix =
                diffuseLinear + (blend2Linear - diffuseLinear) * blend2Amount;
        result[channel] = LinearToSrgb(
                firstMix + (blend3Linear - firstMix) * blend3Amount);
    }

    const float blendFirstAlpha =
            (diffuse[3] + (blend2 != nullptr
                                    ? ((*blend2)[3] - diffuse[3]) *
                                              blend2Amount
                                    : 0.0f)) /
            255.0f;
    const float blend3Alpha =
            blend3 != nullptr ? (*blend3)[3] / 255.0f : blendFirstAlpha;
    result[3] = static_cast<unsigned char>(std::lround(
            std::clamp((blendFirstAlpha + (blend3Alpha - blendFirstAlpha) *
                                        blend3Amount) *
                               255.0f,
                       0.0f, 255.0f)));
    return result;
}

bool TestResolveNativeMaterialProfile() {
    PhysicsSandboxRenderMaterial material;
    PhysicsSandboxMaterialBitmap basecolor;
    basecolor.samplerName = "basecolor";
    basecolor.textureAssetId = 31u;
    PhysicsSandboxMaterialBitmap diffuse;
    diffuse.samplerName = "diffuse";
    diffuse.textureAssetId = 11u;
    PhysicsSandboxMaterialBitmap gdiffuse;
    gdiffuse.samplerName = "gdiffuse";
    gdiffuse.textureAssetId = 17u;
    PhysicsSandboxMaterialBitmap occlusion;
    occlusion.samplerName = "Occlusion";
    occlusion.textureAssetId = 23u;
    material.bitmaps = {basecolor, diffuse, gdiffuse, occlusion};
    material.shaderPath = "LEVEL/TECHNO/MEDIA/MATERIAL/PDIFF pdiff pa px2";
    const NativeMaterialProfile selected =
            ResolveNativeMaterialProfile(material);
    bool okay = Check(selected.albedoBitmap == 1,
                      "albedo sampler selection did not honor diffuse-first "
                      "priority");
    okay &= Check(selected.occlusionBitmap == -1,
                  "generic occlusion sampler bypassed the narrow TOcc gate");
    okay &= Check(selected.renderState.worldXz,
                   "world-XZ rule was not applied for exact matching");
    okay &= Check(selected.albedoAlphaUsage ==
                          NativeAlbedoAlphaUsage::Ignore,
                  "opaque world-XZ material retained storage alpha as opacity");
    NativeMaterialProfile worldXzAlpha = selected;
    ApplyNativeAlbedoTransparency(true, true, &worldXzAlpha);
    okay &= Check(worldXzAlpha.renderState.alphaMode ==
                          StaticVisualAlphaMode::Opaque,
                  "opaque world-XZ material was promoted to transparency");

    PhysicsSandboxRenderMaterial archivedGrass = material;
    archivedGrass.modelPlainPath =
            "Techno2\\Media\\Material\\PDiff PDiff PA TOcc PX2 "
            "Grass.Material.Gbx";
    archivedGrass.modelSelectedPath =
            "Techno2\\Media\\Material\\A2A953B0330AA8444517528D3B749082FB";
    archivedGrass.modelPath = archivedGrass.modelSelectedPath;
    archivedGrass.shaderPlainPath =
            "Techno2\\Media\\Shader\\Grass TOcc PC3.Shader.Gbx";
    archivedGrass.shaderSelectedPath =
            "Techno2\\Media\\Shader\\9155F0DF77F757DE83272751ABBBB11812";
    archivedGrass.shaderPath = archivedGrass.shaderSelectedPath;
    const NativeMaterialProfile realGrass =
            ResolveNativeMaterialProfile(archivedGrass);
    okay &= Check(realGrass.renderState.worldXz,
                  "plain archived grass model identity did not enable "
                  "world-XZ mapping");
    archivedGrass.materialPlainPath =
            "Stadium\\Media\\Material\\StadiumGrassOcc.Material.Gbx";
    const NativeMaterialProfile grassOcclusion =
            ResolveNativeMaterialProfile(archivedGrass);
    okay &= Check(grassOcclusion.occlusionBitmap == 3 &&
                          grassOcclusion.occlusionUvSet == 1,
                  "exact StadiumGrassOcc TOcc identity did not select baked "
                  "AO on UV1");
    PhysicsSandboxRenderMaterial stadiumWarp = archivedGrass;
    stadiumWarp.materialPlainPath =
            "Stadium\\Media\\Material\\StadiumWarpGrassPreLightGen."
            "Material.Gbx";
    const NativeMaterialProfile rejectedWarpOcclusion =
            ResolveNativeMaterialProfile(stadiumWarp);
    okay &= Check(rejectedWarpOcclusion.occlusionBitmap == -1 &&
                          rejectedWarpOcclusion.occlusionUvSet == 0,
                  "environment grass bypassed the block-ground AO gate");

    PhysicsSandboxRenderMaterial archivedDirt = material;
    archivedDirt.materialPlainPath =
            "Stadium\\Media\\Material\\StadiumDirt.Material.Gbx";
    archivedDirt.modelPlainPath =
            "Techno2\\Media\\Material\\SoilGen21.Material.Gbx";
    archivedDirt.modelSelectedPath =
            "Techno2\\Media\\Material\\0123456789ABCDEF0123456789ABCDEF";
    archivedDirt.modelPath = archivedDirt.modelSelectedPath;
    archivedDirt.shaderPlainPath =
            "Techno2\\Media\\Shader\\SoilGen21 PC3.Shader.Gbx";
    archivedDirt.shaderSelectedPath =
            "Techno2\\Media\\Shader\\FEDCBA9876543210FEDCBA9876543210";
    archivedDirt.shaderPath = archivedDirt.shaderSelectedPath;
    const NativeMaterialProfile realDirt =
            ResolveNativeMaterialProfile(archivedDirt);
    okay &= Check(realDirt.renderState.worldXz,
                  "plain archived SoilGen21 model identity did not enable "
                  "world-XZ mapping");
    PhysicsSandboxRenderMaterial dirtTransition = archivedDirt;
    dirtTransition.modelPlainPath =
            "Techno2\\Media\\Material\\SoilFixToGen21.Material.Gbx";
    okay &= Check(!ResolveNativeMaterialProfile(dirtTransition)
                           .renderState.worldXz,
                  "SoilGen21 plain-path recovery matched a dirt transition "
                  "material");

    PhysicsSandboxRenderMaterial grassFadeHelper;
    grassFadeHelper.materialPlainPath =
            "Stadium\\Media\\Material\\StadiumGrassFence.Material.Gbx";
    grassFadeHelper.modelPlainPath =
            "Techno2\\Media\\Material\\VDep Fence.Material.Gbx";
    grassFadeHelper.shaderPlainPath =
            "Techno2\\Media\\Shader\\VDep Fence PC3.Shader.Gbx";
    grassFadeHelper.shaderSelectedPath =
            "Techno2\\Media\\Shader\\BDF6CF0A30FB109B132F014C979D1BE0";
    grassFadeHelper.shaderPath = grassFadeHelper.shaderSelectedPath;
    PhysicsSandboxMaterialBitmap fence;
    fence.samplerName = "FenceA";
    fence.textureSourcePath =
            "Stadium\\Media\\Texture\\Image\\StadiumGrassFenceD.dds";
    PhysicsSandboxMaterialBitmap fade;
    fade.samplerName = "FadeXZ";
    grassFadeHelper.bitmaps = {fence, fade};
    const NativeMaterialProfile grassFadeProfile =
            ResolveNativeMaterialProfile(grassFadeHelper);
    okay &= Check(!grassFadeProfile.visible,
                  "plain VDep Fence helper shader was rendered as opaque "
                  "grass geometry");
    okay &= Check(grassFadeProfile.renderState.alphaMode ==
                          StaticVisualAlphaMode::Opaque &&
                          !grassFadeProfile.renderState.worldXz,
                  "VDep Fence visibility bridge unexpectedly changed other "
                  "material semantics");
    grassFadeHelper.bitmaps.pop_back();
    okay &= Check(ResolveNativeMaterialProfile(grassFadeHelper).visible,
                  "incomplete grass-fence identity was hidden by the exact "
                  "helper rule");

    PhysicsSandboxRenderMaterial opaqueRoad = material;
    opaqueRoad.modelPlainPath =
            "Techno2\\Media\\Material\\TDiff_Spec_Nrm TOcc "
            "CSpecSoft.Material.Gbx";
    opaqueRoad.modelPath =
            "Techno2\\Media\\Material\\337419240E374ECD7069E5ED7C7F028E12";
    opaqueRoad.shaderPath =
            "Techno2\\Media\\Shader\\C293327207F73142F711AB47E911684D30";
    const NativeMaterialProfile nativeRoad =
            ResolveNativeMaterialProfile(opaqueRoad);
    okay &= Check(nativeRoad.renderState.alphaMode ==
                          StaticVisualAlphaMode::Opaque,
                  "plain native road model incorrectly enabled broad "
                  "transparency heuristics");

    PhysicsSandboxRenderMaterial vehicleMaterial;
    PhysicsSandboxMaterialBitmap diffuseGloss;
    diffuseGloss.samplerName = "Diffuse_Gloss";
    diffuseGloss.textureAssetId = 41u;
    vehicleMaterial.bitmaps = {diffuseGloss};
    const NativeMaterialProfile vehicleProfile =
            ResolveNativeMaterialProfile(vehicleMaterial);
    okay &= Check(vehicleProfile.albedoBitmap == 0,
                  "vehicle Diffuse_Gloss sampler was not selected as albedo");
    okay &= Check(vehicleProfile.albedoAlphaUsage ==
                          NativeAlbedoAlphaUsage::Specular,
                  "vehicle Diffuse_Gloss alpha was not retained as gloss");

    material.shaderPath =
            "LEVEL/TECHNO/MEDIA/MATERIAL/PDIFF pdiff pa px2x";
    const NativeMaterialProfile nearMatch =
            ResolveNativeMaterialProfile(material);
    okay &= Check(!nearMatch.renderState.worldXz,
                  "near-miss shader suffix was incorrectly accepted");

    material.shaderPath = "techno/media/material/sky";
    const NativeMaterialProfile unlit =
            ResolveNativeMaterialProfile(material);
    okay &= Check(unlit.unlit, "sky material should be marked as unlit");

    material.shaderPath = "TECHNO/MEDIA/MATERIAL/TDIFF PX2 TRANS 2SIDED";
    const NativeMaterialProfile transparent =
            ResolveNativeMaterialProfile(material);
    okay &= Check(
            transparent.renderState.alphaMode == StaticVisualAlphaMode::Blended,
            "transparent rule did not enable blended alpha");
    okay &= Check(transparent.renderState.doubleSided,
                  "transparent rule did not mark material as double sided");
    okay &= Check(transparent.albedoAlphaUsage ==
                          NativeAlbedoAlphaUsage::Opacity,
                  "transparent rule did not mark diffuse alpha as opacity");

    material.shaderPath = "LEVEL/TECHNO/MEDIA/MATERIAL/TDIFFG PX2 CSpecL_Pixel";
    NativeMaterialProfile specularAlpha =
            ResolveNativeMaterialProfile(material);
    okay &= Check(specularAlpha.albedoAlphaUsage ==
                          NativeAlbedoAlphaUsage::Specular,
                  "CSpecL_Pixel did not classify diffuse alpha as specular");
    ApplyNativeAlbedoTransparency(true, true, &specularAlpha);
    okay &= Check(specularAlpha.renderState.alphaMode ==
                          StaticVisualAlphaMode::Opaque,
                  "CSpecL_Pixel diffuse alpha incorrectly enabled blending");

    material.shaderPath =
            "LEVEL/TECHNO/MEDIA/MATERIAL/TDIFFG PX2 CSpecL_PixelExtra";
    const NativeMaterialProfile specularNearMatch =
            ResolveNativeMaterialProfile(material);
    okay &= Check(specularNearMatch.albedoAlphaUsage ==
                          NativeAlbedoAlphaUsage::Opacity,
                  "near-miss CSpecL_Pixel shader suffix was accepted");

    material.water = true;
    const NativeMaterialProfile water = ResolveNativeMaterialProfile(material);
    okay &= Check(water.renderState.alphaMode == StaticVisualAlphaMode::Blended,
                  "water should force blended alpha mode");
    okay &= Check(water.renderState.doubleSided,
                  "water should force double-sided rendering");

    NativeMaterialProfile binaryAlpha;
    binaryAlpha.renderState.alphaMode = StaticVisualAlphaMode::Opaque;
    ApplyNativeAlbedoTransparency(true, false, &binaryAlpha);
    okay &= Check(binaryAlpha.renderState.alphaMode ==
                          StaticVisualAlphaMode::Masked,
                  "binary albedo alpha should select masked rendering");
    NativeMaterialProfile partialAlpha;
    partialAlpha.renderState.alphaMode = StaticVisualAlphaMode::Opaque;
    ApplyNativeAlbedoTransparency(true, true, &partialAlpha);
    okay &= Check(partialAlpha.renderState.alphaMode ==
                          StaticVisualAlphaMode::Blended,
                  "fractional albedo alpha should select blended rendering");
    NativeMaterialProfile explicitBlend = transparent;
    ApplyNativeAlbedoTransparency(true, false, &explicitBlend);
    okay &= Check(explicitBlend.renderState.alphaMode ==
                          StaticVisualAlphaMode::Blended,
                  "texture alpha should not override an explicit blend mode");

    NativeMaterialProfile ignoredAlpha = selected;
    ignoredAlpha.albedoAlphaUsage = NativeAlbedoAlphaUsage::Ignore;
    ApplyNativeAlbedoTransparency(true, true, &ignoredAlpha);
    okay &= Check(ignoredAlpha.renderState.alphaMode ==
                          StaticVisualAlphaMode::Opaque,
                  "explicitly ignored diffuse alpha enabled blending");
    return okay;
}

bool TestTexturePrepareDecodePipeline() {
    QImage png(2, 2, QImage::Format_RGBA8888);
    png.fill(qRgba(64, 128, 192, 200));
    QString error;
    const QByteArray pngBytes = EncodePng(png, &error);
    if (!Check(!pngBytes.isEmpty(), "PNG encoding for test image failed")) {
        return false;
    }

    const DecodedNativeTexture decodedPng =
            DecodeNativeTexture(pngBytes, "tile.png",
                                NativeTextureSemantic::AlbedoSrgb, &error);
    bool okay = Check(decodedPng.sourceFormat == QStringLiteral("PNG"),
                     "PNG decode did not report PNG source format");
    okay &= Check(error.isEmpty(), "PNG decode returned an unexpected error");
    okay &= Check(decodedPng.mipmaps.size() == 1u &&
                          !decodedPng.mipmaps.front().isNull(),
                  "PNG decode should produce one decoded mip");
    okay &= Check(decodedPng.hasTransparency &&
                          decodedPng.hasPartialTransparency,
                  "fractional PNG alpha metadata was not detected");

    const PreparedNativeTexture preparedPng =
            PrepareNativeTexture(pngBytes, "tile.png",
                                 NativeTextureSemantic::AlbedoSrgb, &error);
    okay &= Check(error.isEmpty(), "PNG prepare returned an unexpected error");
    okay &= Check(preparedPng.fileSuffix == QStringLiteral("png"),
                  "PNG prepare should emit PNG output");
    okay &= Check(preparedPng.generateMipmaps,
                  "PNG prepare should request mipmap generation");
    okay &= Check(preparedPng.hasTransparency &&
                          preparedPng.hasPartialTransparency,
                  "PNG prepare should preserve fractional alpha metadata");

    const QByteArray ddsBytes = CreateMinimalDxt1Dds();
    const DecodedNativeTexture decodedDds =
            DecodeNativeTexture(ddsBytes, "compressed.dds",
                                NativeTextureSemantic::LinearData, &error);
    okay &= Check(decodedDds.sourceFormat == QStringLiteral("DDS"),
                 "DDS decode should report DDS source format");
    okay &= Check(!decodedDds.hasTransparency,
                  "Dxt1 test texture should not be transparent");
    okay &= Check(!decodedDds.hasPartialTransparency,
                  "opaque DXT1 should not report partial transparency");
    okay &= Check(decodedDds.mipmaps.size() == 1u,
                  "DDS decode should expose a single mip level");

    const PreparedNativeTexture preparedDds =
            PrepareNativeTexture(ddsBytes, "compressed.dds",
                                 NativeTextureSemantic::LinearData, &error);
    okay &= Check(error.isEmpty(),
                  "Linear-data compressed DDS prepare returned error");
    okay &= Check(preparedDds.fileSuffix == QStringLiteral("ktx"),
                  "Dxt1 prepare should wrap into KTX for native linear data");
    okay &= Check(!preparedDds.generateMipmaps,
                  "wrapped compressed textures should not enable mipmap "
                  "generation");
    return okay;
}

bool TestDxtAlphaDecoding() {
    QByteArray explicitAlpha(8, '\0');
    std::array<unsigned char, 16> alphaNibbles{};
    alphaNibbles.fill(15u);
    alphaNibbles[0] = 0u;
    alphaNibbles[1] = 5u;
    alphaNibbles[2] = 10u;
    for (std::size_t pixel = 0; pixel < alphaNibbles.size(); ++pixel) {
        const int byte = static_cast<int>(pixel / 2u);
        const unsigned shift = static_cast<unsigned>((pixel & 1u) * 4u);
        const auto current = static_cast<unsigned char>(explicitAlpha[byte]);
        explicitAlpha[byte] = static_cast<char>(
                current | (alphaNibbles[pixel] << shift));
    }
    QByteArray dxt3Block = explicitAlpha;
    dxt3Block.append(CreateColorBlock(0xf800u, 0x0000u));
    const QByteArray dxt3 = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '3'), dxt3Block);

    QString error;
    const DecodedNativeTexture decodedDxt3 = DecodeNativeTexture(
            dxt3, QStringLiteral("explicit-alpha.dds"),
            NativeTextureSemantic::AlbedoSrgb, &error);
    bool okay = Check(error.isEmpty(), "DXT3 decode returned an error");
    if (!Check(decodedDxt3.mipmaps.size() == 1u,
               "DXT3 should decode one mip level")) {
        return false;
    }
    const QImage &dxt3Image = decodedDxt3.mipmaps.front();
    okay &= Check(PixelRgba(dxt3Image, 0, 0) ==
                          std::array<unsigned char, 4>{255u, 0u, 0u, 0u},
                  "DXT3 zero-alpha nibble decoded incorrectly");
    okay &= Check(PixelRgba(dxt3Image, 1, 0)[3] == 85u,
                  "DXT3 5/15 alpha nibble decoded incorrectly");
    okay &= Check(PixelRgba(dxt3Image, 2, 0)[3] == 170u,
                  "DXT3 10/15 alpha nibble decoded incorrectly");
    okay &= Check(PixelRgba(dxt3Image, 3, 0)[3] == 255u,
                  "DXT3 opaque alpha nibble decoded incorrectly");
    okay &= Check(decodedDxt3.hasTransparency,
                  "DXT3 non-opaque alpha should set transparency metadata");
    okay &= Check(decodedDxt3.hasPartialTransparency,
                  "DXT3 fractional alpha should be marked as partial");

    std::array<unsigned char, 16> dxt5Indices{};
    dxt5Indices[0] = 0u;
    dxt5Indices[1] = 1u;
    dxt5Indices[2] = 6u;
    dxt5Indices[3] = 7u;
    QByteArray dxt5Block = CreateAlphaBlock(10u, 240u, dxt5Indices);
    dxt5Block.append(CreateColorBlock(0x001fu, 0x0000u));
    const QByteArray dxt5 = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '5'), dxt5Block);
    const DecodedNativeTexture decodedDxt5 = DecodeNativeTexture(
            dxt5, QStringLiteral("interpolated-alpha.dds"),
            NativeTextureSemantic::AlbedoSrgb, &error);
    okay &= Check(error.isEmpty(), "DXT5 decode returned an error");
    if (!Check(decodedDxt5.mipmaps.size() == 1u,
               "DXT5 should decode one mip level")) {
        return false;
    }
    const QImage &dxt5Image = decodedDxt5.mipmaps.front();
    okay &= Check(PixelRgba(dxt5Image, 0, 0) ==
                          std::array<unsigned char, 4>{0u, 0u, 255u, 10u},
                  "DXT5 endpoint-zero alpha decoded incorrectly");
    okay &= Check(PixelRgba(dxt5Image, 1, 0)[3] == 240u,
                  "DXT5 endpoint-one alpha decoded incorrectly");
    okay &= Check(PixelRgba(dxt5Image, 2, 0)[3] == 0u,
                  "DXT5 six-step zero alpha decoded incorrectly");
    okay &= Check(PixelRgba(dxt5Image, 3, 0)[3] == 255u,
                  "DXT5 six-step opaque alpha decoded incorrectly");
    okay &= Check(decodedDxt5.hasTransparency,
                  "DXT5 non-opaque alpha should set transparency metadata");
    okay &= Check(decodedDxt5.hasPartialTransparency,
                  "DXT5 fractional alpha should be marked as partial");

    const QByteArray dxt1Binary = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '1'),
            CreateColorBlock(0x0000u, 0xffffu, 3u));
    const DecodedNativeTexture decodedDxt1Binary = DecodeNativeTexture(
            dxt1Binary, QStringLiteral("binary-alpha.dds"),
            NativeTextureSemantic::AlbedoSrgb, &error);
    okay &= Check(error.isEmpty(), "binary-alpha DXT1 decode returned an error");
    if (!Check(decodedDxt1Binary.mipmaps.size() == 1u,
               "binary-alpha DXT1 should decode one mip level")) {
        return false;
    }
    okay &= Check(PixelRgba(decodedDxt1Binary.mipmaps.front(), 0, 0)[3] == 0u,
                  "DXT1 transparent palette entry decoded incorrectly");
    okay &= Check(decodedDxt1Binary.hasTransparency,
                  "DXT1 binary alpha should set transparency metadata");
    okay &= Check(!decodedDxt1Binary.hasPartialTransparency,
                  "DXT1 binary alpha must not be classified as partial");

    const DecodedNativeTexture decodedDxt1Opaque = DecodeNativeTexture(
            dxt1Binary, QStringLiteral("opaque-color.dds"),
            NativeTextureSemantic::OpaqueAlbedoSrgb, &error);
    okay &= Check(error.isEmpty(), "opaque DXT1 decode returned an error");
    if (!Check(decodedDxt1Opaque.mipmaps.size() == 1u,
               "opaque DXT1 should decode one mip level")) {
        return false;
    }
    okay &= Check(PixelRgba(decodedDxt1Opaque.mipmaps.front(), 0, 0) ==
                          std::array<unsigned char, 4>{170u, 170u, 170u,
                                                       255u},
                  "opaque DXT1 did not reinterpret palette entry 3 as color");
    okay &= Check(!decodedDxt1Opaque.hasTransparency &&
                          !decodedDxt1Opaque.hasPartialTransparency,
                  "opaque DXT1 retained storage alpha metadata");
    const PreparedNativeTexture preparedDxt1Opaque = PrepareNativeTexture(
            dxt1Binary, QStringLiteral("opaque-color.dds"),
            NativeTextureSemantic::OpaqueAlbedoSrgb, &error);
    okay &= Check(error.isEmpty(), "opaque DXT1 prepare returned an error");
    okay &= Check(preparedDxt1Opaque.fileSuffix == QStringLiteral("png"),
                  "opaque DXT1 should preserve the remapped palette in PNG");
    okay &= Check(!preparedDxt1Opaque.hasTransparency &&
                          !preparedDxt1Opaque.hasPartialTransparency,
                  "prepared opaque DXT1 retained storage alpha metadata");
    return okay;
}

bool TestDx10BcDecoding() {
    const QByteArray bc4 = CreateBlockCompressedDds(
            FourCc('D', 'X', '1', '0'), CreateAlphaBlock(64u, 0u),
            4u, 4u, 80u);
    QString error;
    const DecodedNativeTexture decodedBc4 = DecodeNativeTexture(
            bc4, QStringLiteral("dx10-bc4.dds"),
            NativeTextureSemantic::LinearData, &error);
    bool okay = Check(error.isEmpty(), "DX10 BC4 decode returned an error");
    if (!Check(decodedBc4.mipmaps.size() == 1u,
               "DX10 BC4 should decode one mip level")) {
        return false;
    }
    okay &= Check(PixelRgba(decodedBc4.mipmaps.front(), 0, 0) ==
                          std::array<unsigned char, 4>{64u, 64u, 64u, 255u},
                  "DX10 BC4 channel expansion is incorrect");
    okay &= Check(!decodedBc4.hasTransparency,
                  "BC4 data should decode as opaque RGBA");
    okay &= Check(!decodedBc4.hasPartialTransparency,
                  "BC4 data should not report partial transparency");

    QByteArray bc5Block = CreateAlphaBlock(64u, 0u);
    bc5Block.append(CreateAlphaBlock(192u, 0u));
    const QByteArray bc5 = CreateBlockCompressedDds(
            FourCc('D', 'X', '1', '0'), bc5Block, 4u, 4u, 83u);
    const DecodedNativeTexture decodedBc5 = DecodeNativeTexture(
            bc5, QStringLiteral("dx10-bc5.dds"),
            NativeTextureSemantic::LinearData, &error);
    okay &= Check(error.isEmpty(), "DX10 BC5 decode returned an error");
    if (!Check(decodedBc5.mipmaps.size() == 1u,
               "DX10 BC5 should decode one mip level")) {
        return false;
    }
    okay &= Check(PixelRgba(decodedBc5.mipmaps.front(), 0, 0) ==
                          std::array<unsigned char, 4>{64u, 192u, 0u, 255u},
                  "DX10 BC5 channel expansion is incorrect");
    okay &= Check(!decodedBc5.hasTransparency,
                  "BC5 data should decode as opaque RGBA");
    okay &= Check(!decodedBc5.hasPartialTransparency,
                  "BC5 data should not report partial transparency");
    return okay;
}

bool TestNormalMapReconstruction() {
    QByteArray dxt5nmBlock = CreateAlphaBlock(128u, 0u);
    dxt5nmBlock.append(CreateColorBlock(0x0400u, 0x0000u));
    const QByteArray dxt5nm = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '5'), dxt5nmBlock);
    QString error;
    const DecodedNativeTexture decodedDxt5nm = DecodeNativeTexture(
            dxt5nm, QStringLiteral("dxt5nm.dds"),
            NativeTextureSemantic::NormalMap, &error);
    bool okay = Check(error.isEmpty(), "DXT5nm decode returned an error");
    if (!Check(decodedDxt5nm.mipmaps.size() == 1u,
               "DXT5nm should decode one mip level")) {
        return false;
    }
    const unsigned char dxt5nmY = 129u;
    const std::array<unsigned char, 4> expectedDxt5nm{
            128u, dxt5nmY, ReconstructedNormalZ(128u, dxt5nmY), 255u};
    okay &= Check(PixelRgba(decodedDxt5nm.mipmaps.front(), 0, 0) ==
                          expectedDxt5nm,
                  "DXT5nm did not reconstruct Z from alpha and green");
    okay &= Check(!decodedDxt5nm.hasTransparency,
                  "DXT5nm output should replace source alpha with opacity");
    okay &= Check(!decodedDxt5nm.hasPartialTransparency,
                  "DXT5nm output should not retain partial source alpha");

    QByteArray bc5Block = CreateAlphaBlock(128u, 0u);
    bc5Block.append(CreateAlphaBlock(128u, 0u));
    const QByteArray bc5 = CreateBlockCompressedDds(
            FourCc('D', 'X', '1', '0'), bc5Block, 4u, 4u, 83u);
    const DecodedNativeTexture decodedBc5 = DecodeNativeTexture(
            bc5, QStringLiteral("bc5-normal.dds"),
            NativeTextureSemantic::NormalMap, &error);
    okay &= Check(error.isEmpty(), "BC5 normal-map decode returned an error");
    if (!Check(decodedBc5.mipmaps.size() == 1u,
               "BC5 normal map should decode one mip level")) {
        return false;
    }
    const std::array<unsigned char, 4> expectedBc5{
            128u, 128u, ReconstructedNormalZ(128u, 128u), 255u};
    okay &= Check(PixelRgba(decodedBc5.mipmaps.front(), 0, 0) == expectedBc5,
                  "BC5 normal map did not reconstruct its Z channel");
    okay &= Check(!decodedBc5.hasTransparency,
                  "BC5 normal-map output should be opaque");
    okay &= Check(!decodedBc5.hasPartialTransparency,
                  "BC5 normal-map output should not be partially transparent");
    return okay;
}

bool TestTgaOrientation() {
    const std::array<std::array<unsigned char, 4>, 4> expected{{
            {255u, 0u, 0u, 255u}, {0u, 255u, 0u, 255u},
            {0u, 0u, 255u, 255u}, {255u, 255u, 0u, 128u},
    }};
    bool okay = true;
    for (const bool topOrigin : {false, true}) {
        QString error;
        const DecodedNativeTexture decoded = DecodeNativeTexture(
                CreateOrientedTga(topOrigin),
                topOrigin ? QStringLiteral("top-origin.tga")
                          : QStringLiteral("bottom-origin.tga"),
                NativeTextureSemantic::AlbedoSrgb, &error);
        okay &= Check(error.isEmpty(),
                      "oriented TGA decode returned an error");
        if (!Check(decoded.mipmaps.size() == 1u,
                   "oriented TGA should decode one image")) {
            return false;
        }
        const QImage &image = decoded.mipmaps.front();
        okay &= Check(image.size() == QSize(2, 2),
                      "oriented TGA decoded with the wrong dimensions");
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                okay &= Check(PixelRgba(image, x, y) == expected[y * 2 + x],
                              "TGA origin metadata was not respected");
            }
        }
        okay &= Check(decoded.hasTransparency,
                      "TGA alpha should set transparency metadata");
        okay &= Check(decoded.hasPartialTransparency,
                      "fractional TGA alpha should be marked as partial");
    }
    return okay;
}

bool TestMalformedTextureRejection() {
    QByteArray truncated = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '1'),
            CreateColorBlock(0xffffu, 0x0000u));
    truncated.chop(1);
    QString error;
    const DecodedNativeTexture decodedTruncated = DecodeNativeTexture(
            truncated, QStringLiteral("truncated.dds"),
            NativeTextureSemantic::LinearData, &error);
    bool okay = Check(decodedTruncated.mipmaps.isEmpty(),
                      "truncated DDS data should be rejected");
    okay &= Check(error.contains(QStringLiteral("truncated DDS mip level")),
                  "truncated DDS rejection should identify the bad mip");

    const QByteArray oversized = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '1'),
            CreateColorBlock(0xffffu, 0x0000u), 8193u, 4u);
    const DecodedNativeTexture decodedOversized = DecodeNativeTexture(
            oversized, QStringLiteral("oversized.dds"),
            NativeTextureSemantic::LinearData, &error);
    okay &= Check(decodedOversized.mipmaps.isEmpty(),
                  "oversized DDS dimensions should be rejected");
    okay &= Check(error == QStringLiteral("invalid DDS dimensions"),
                  "oversized DDS rejection should report invalid dimensions");

    const QByteArray excessivePixels = CreateBlockCompressedDds(
            FourCc('D', 'X', 'T', '1'),
            CreateColorBlock(0xffffu, 0x0000u), 4097u, 4096u);
    const DecodedNativeTexture decodedExcessivePixels = DecodeNativeTexture(
            excessivePixels, QStringLiteral("too-many-pixels.dds"),
            NativeTextureSemantic::LinearData, &error);
    okay &= Check(decodedExcessivePixels.mipmaps.isEmpty(),
                  "DDS base levels above the pixel budget should be rejected");
    okay &= Check(error == QStringLiteral("invalid DDS dimensions"),
                  "DDS pixel-budget rejection should report invalid dimensions");

    QByteArray malformedHeader = oversized;
    WriteLe32(123u, malformedHeader.data() + 4);
    const DecodedNativeTexture decodedMalformedHeader = DecodeNativeTexture(
            malformedHeader, QStringLiteral("bad-header.dds"),
            NativeTextureSemantic::LinearData, &error);
    okay &= Check(decodedMalformedHeader.mipmaps.isEmpty(),
                  "malformed DDS header should be rejected");
    okay &= Check(error == QStringLiteral("invalid DDS header size"),
                  "malformed DDS header should report its invalid size");

    QByteArray oversizedTga = CreateOrientedTga(true);
    WriteLe16(8193u, oversizedTga.data() + 12);
    const DecodedNativeTexture decodedOversizedTga = DecodeNativeTexture(
            oversizedTga, QStringLiteral("oversized.tga"),
            NativeTextureSemantic::AlbedoSrgb, &error);
    okay &= Check(decodedOversizedTga.mipmaps.isEmpty(),
                  "oversized TGA dimensions should be rejected before decode");
    okay &= Check(error.contains(QStringLiteral("decoded pixel budget")),
                  "oversized TGA rejection should report the pixel budget");

    const DecodedNativeTexture decodedMalformedTga = DecodeNativeTexture(
            QByteArrayLiteral("not a TGA image"),
            QStringLiteral("malformed.tga"),
            NativeTextureSemantic::AlbedoSrgb, &error);
    okay &= Check(decodedMalformedTga.mipmaps.isEmpty(),
                  "malformed TGA data should be rejected");
    okay &= Check(error.contains(QStringLiteral("unsupported or malformed")),
                  "malformed TGA rejection should provide a useful diagnostic");
    return okay;
}

bool TestComposeNativeAlbedo() {
    const QImage diffuseImage(1, 1, QImage::Format_RGBA8888);
    const QImage blend2Image(1, 1, QImage::Format_RGBA8888);
    const QImage maskImage(1, 1, QImage::Format_RGBA8888);
    const QImage blend3Image(1, 1, QImage::Format_RGBA8888);

    QImage diffuse = diffuseImage;
    QImage blend2 = blend2Image;
    QImage mask = maskImage;
    QImage blend3 = blend3Image;
    diffuse.setPixel(0, 0, qRgba(255, 96, 16, 255));
    blend2.setPixel(0, 0, qRgba(16, 128, 255, 64));
    mask.setPixel(0, 0, qRgba(128, 0, 0, 255));
    blend3.setPixel(0, 0, qRgba(64, 255, 0, 192));

    const DecodedNativeTexture diffuseDecoded = DecodedFromRgba(std::move(diffuse));
    const DecodedNativeTexture blend2Decoded = DecodedFromRgba(std::move(blend2));
    const DecodedNativeTexture maskDecoded = DecodedFromRgba(std::move(mask));
    const DecodedNativeTexture blend3Decoded = DecodedFromRgba(std::move(blend3));

    const DecodedNativeTexture output = ComposeNativeAlbedo(
            diffuseDecoded, &blend2Decoded, &maskDecoded, &blend3Decoded);
    bool okay = Check(output.mipmaps.size() == 1u,
                      "composited albedo should keep one mipmap level");
    const QImage &composited = output.mipmaps.front();
    okay &= Check(composited.format() == QImage::Format_RGBA8888,
                  "composited albedo image must stay in RGBA8888");
    const unsigned char *out = composited.constScanLine(0);
    const std::array<unsigned char, 4> diffusePixel{
            255u, 96u, 16u, 255u};
    const std::array<unsigned char, 4> blend2Pixel{16u, 128u, 255u, 64u};
    const std::array<unsigned char, 4> maskPixel{128u, 0u, 0u, 255u};
    const std::array<unsigned char, 4> blend3Pixel{64u, 255u, 0u, 192u};
    const std::array<unsigned char, 4> expected =
            ExpectedCompositedPixel(diffusePixel, &blend2Pixel, &maskPixel,
                                   &blend3Pixel);
    constexpr int Red = 0;
    constexpr int Green = 1;
    constexpr int Blue = 2;
    constexpr int Alpha = 3;
    okay &= Check(out[Red] == expected[Red], "composited red channel mismatch");
    okay &= Check(out[Green] == expected[Green],
                  "composited green channel mismatch");
    okay &= Check(out[Blue] == expected[Blue],
                  "composited blue channel mismatch");
    okay &= Check(out[Alpha] == expected[Alpha],
                  "composited alpha channel mismatch");
    okay &= Check(output.hasTransparency && output.hasPartialTransparency,
                  "composited fractional alpha metadata is incorrect");
    return okay;
}

bool TestStoreNativeTexture() {
    QTemporaryDir cacheDirectory;
    if (!cacheDirectory.isValid()) {
        return false;
    }

    QImage png(2, 2, QImage::Format_RGBA8888);
    png.fill(qRgba(12, 34, 56, 200));
    const QString cacheRoot = cacheDirectory.path();
    QString error;
    const QByteArray pngBytes = EncodePng(png, &error);
    if (!Check(error.isEmpty() && !pngBytes.isEmpty(),
               "failed to produce PNG bytes for store-native-texture test")) {
        return false;
    }

    const PreparedNativeTexture prepared =
            PrepareNativeTexture(pngBytes, "store.png",
                                 NativeTextureSemantic::AlbedoSrgb, &error);
    if (!Check(error.isEmpty(), "prepare failed before cache storage test")) {
        return false;
    }

    const CachedNativeTexture first = StoreNativeTexture(
            prepared, pngBytes, QStringLiteral("pack"), QStringLiteral("store.png"),
            NativeTextureSemantic::AlbedoSrgb, cacheRoot);
    bool okay = Check(static_cast<bool>(first),
                      "first cache write should succeed");
    okay &= Check(!first.cacheHit, "first cache write should not be a hit");
    const QString cachedPath = first.source.toLocalFile();
    okay &= Check(QFileInfo(cachedPath).isFile(),
                  "cache should write the encoded texture file");
    okay &= Check(QFileInfo(cachedPath).size() == prepared.fileBytes.size(),
                  "cached texture size should match prepared output");
    okay &= Check(first.hasTransparency && first.hasPartialTransparency,
                  "cache write should preserve fractional alpha metadata");

    const CachedNativeTexture second = StoreNativeTexture(
            prepared, pngBytes, QStringLiteral("pack"), QStringLiteral("store.png"),
            NativeTextureSemantic::AlbedoSrgb, cacheRoot);
    okay &= Check(static_cast<bool>(second),
                  "second cache access should succeed");
    okay &= Check(second.cacheHit, "second cache access should be a hit");
    okay &= Check(second.source == first.source,
                  "cache hits should reuse the same generated path");
    okay &= Check(second.hasTransparency && second.hasPartialTransparency,
                  "cache hit should preserve fractional alpha metadata");
    return okay;
}

bool TestDefaultMaterialLoadSelection() {
    PhysicsSandboxRenderScene scene;
    scene.meshes.resize(1u);
    scene.materials.resize(8u);
    for (PhysicsSandboxRenderMaterial &material : scene.materials) {
        PhysicsSandboxMaterialBitmap bitmap;
        bitmap.samplerName = "Diffuse";
        material.bitmaps.push_back(std::move(bitmap));
    }
    scene.materials[6].shaderPath = "test/:fakeshad";

    const auto appendInstance = [&](std::uint32_t materialIndex) ->
            PhysicsSandboxRenderInstance & {
        PhysicsSandboxRenderInstance instance;
        instance.materialIndex = materialIndex;
        scene.instances.push_back(std::move(instance));
        return scene.instances.back();
    };
    appendInstance(0u);
    appendInstance(1u).visible = false;
    appendInstance(2u).lodLevel = 1u;
    appendInstance(3u).renderLayer = PhysicsSandboxRenderLayer::Background;
    appendInstance(4u).purpose = PhysicsSandboxScenePurpose::Helper;
    appendInstance(5u).meshIndex = 99u;
    appendInstance(6u);
    PhysicsSandboxRenderInstance &grassClip = appendInstance(7u);
    grassClip.purpose = PhysicsSandboxScenePurpose::Clip;
    grassClip.provenance.blockName = "StadiumGrassClip";

    const std::vector<NativeMaterialProfile> profiles =
            ResolveNativeMaterialProfiles(scene);
    const std::vector<bool> loadMask =
            SelectDefaultNativeMaterialLoadMask(scene, profiles);
    bool okay = Check(loadMask.size() == scene.materials.size(),
                      "material load mask size mismatch");
    for (std::size_t index = 0u; index < loadMask.size(); ++index) {
        const bool expected = index == 0u || index == 7u;
        okay &= Check(loadMask[index] == expected,
                      "material load mask selected an unused material");
    }

    QTemporaryDir cacheDirectory;
    if (!Check(cacheDirectory.isValid(),
               "material selection cache directory is invalid")) {
        return false;
    }
    const auto loaded = LoadNativeMaterials(
            scene, profiles, loadMask, QStringLiteral("selection-test"),
            cacheDirectory.path());
    okay &= Check(loaded.materials.size() == scene.materials.size(),
                  "material runtime table must preserve source indices");
    okay &= Check(loaded.renderStates.size() == loaded.materials.size(),
                  "material render-state table size mismatch");
    for (std::size_t index = 0u; index < loaded.renderStates.size(); ++index) {
        okay &= Check(loaded.renderStates[index].alphaMode ==
                              loaded.materials[index]
                                      .profile.renderState.alphaMode,
                      "material render state was captured before loading");
    }
    okay &= Check(loaded.telemetry.referencedTextureCount == 2u,
                  "unused material textures should not be referenced");
    okay &= Check(loaded.telemetry.failedTextureCount == 2u,
                  "only selected unresolved textures should fail");
    okay &= Check(loaded.telemetry.fallbackMaterialCount == 2u,
                  "only selected materials should count as fallbacks");
    return okay;
}

}  // namespace

int main() {
    bool okay = true;
    okay &= TestResolveNativeMaterialProfile();
    okay &= TestTexturePrepareDecodePipeline();
    okay &= TestDxtAlphaDecoding();
    okay &= TestDx10BcDecoding();
    okay &= TestNormalMapReconstruction();
    okay &= TestTgaOrientation();
    okay &= TestMalformedTextureRejection();
    okay &= TestComposeNativeAlbedo();
    okay &= TestStoreNativeTexture();
    okay &= TestDefaultMaterialLoadSelection();
    return okay ? 0 : 1;
}
