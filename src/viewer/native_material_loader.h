#ifndef FOREVERTAS_VIEWER_NATIVE_MATERIAL_LOADER_H
#define FOREVERTAS_VIEWER_NATIVE_MATERIAL_LOADER_H

#include "viewer/native_material.h"
#include "viewer/native_texture_cache.h"

#include <forevervalidator/experimental/physics_sandbox.h>

#include <QUrl>

#include <cstdint>
#include <vector>

namespace forevertas::viewer {

struct NativeMaterialRuntime {
    NativeMaterialProfile profile{};
    QUrl albedoTexture;
    QUrl normalTexture;
    QUrl specularTexture;
    QString albedoSourcePath;
    QString diagnostic;
    bool albedoGenerateMipmaps = false;
    bool normalGenerateMipmaps = false;
    bool specularGenerateMipmaps = false;
    bool nativeAlbedo = false;
    bool nativeNormal = false;
    bool nativeSpecular = false;
};

struct NativeMaterialLoadTelemetry {
    std::uint64_t referencedTextureCount = 0u;
    std::uint64_t resolvedTextureCount = 0u;
    std::uint64_t failedTextureCount = 0u;
    std::uint64_t cacheHitCount = 0u;
    std::uint64_t memoryCacheHitCount = 0u;
    std::uint64_t diskCacheHitCount = 0u;
    std::uint64_t nativeMaterialCount = 0u;
    std::uint64_t fallbackMaterialCount = 0u;
    std::uint64_t worldProjectedMaterialCount = 0u;
    std::uint64_t maskedMaterialCount = 0u;
    std::uint64_t blendedMaterialCount = 0u;
    std::uint64_t additiveMaterialCount = 0u;
    std::uint64_t subtractiveMaterialCount = 0u;
    std::uint64_t doubleSidedMaterialCount = 0u;
    std::uint64_t unlitMaterialCount = 0u;
    std::uint64_t encodedByteCount = 0u;
    std::uint64_t estimatedGpuByteCount = 0u;
    std::uint64_t loadNanoseconds = 0u;
};

struct NativeMaterialLoadResult {
    std::vector<NativeMaterialRuntime> materials;
    std::vector<StaticVisualMaterialState> renderStates;
    NativeMaterialLoadTelemetry telemetry;
};

std::vector<NativeMaterialProfile> ResolveNativeMaterialProfiles(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene);

std::vector<bool> SelectDefaultNativeMaterialLoadMask(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene,
        const std::vector<NativeMaterialProfile> &profiles);

NativeMaterialLoadResult LoadNativeMaterials(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene,
        const QString &packIdentity,
        const QString &cacheRoot = {});

NativeMaterialLoadResult LoadNativeMaterials(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene,
        const std::vector<NativeMaterialProfile> &profiles,
        const std::vector<bool> &loadMask,
        const QString &packIdentity,
        const QString &cacheRoot = {});

}  // namespace forevertas::viewer

#endif
