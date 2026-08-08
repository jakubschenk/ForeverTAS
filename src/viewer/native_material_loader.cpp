#include "viewer/native_material_loader.h"

#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>

namespace forevertas::viewer {
namespace {

using forevervalidator::experimental::PhysicsSandboxMaterialBitmap;
using forevervalidator::experimental::PhysicsSandboxRenderLayer;
using forevervalidator::experimental::PhysicsSandboxRenderMaterial;
using forevervalidator::experimental::PhysicsSandboxRenderScene;
using forevervalidator::experimental::PhysicsSandboxTextureAssetHandle;
using forevervalidator::experimental::PhysicsSandboxTextureAssetId;

constexpr std::uint64_t MaxTextureGpuBytesPerScene =
        512ull * 1024ull * 1024ull;

struct TextureKey {
    PhysicsSandboxTextureAssetId id = 0u;
    NativeTextureSemantic semantic = NativeTextureSemantic::LinearData;

    auto Tie() const { return std::tie(id, semantic); }
    bool operator<(const TextureKey &other) const {
        return Tie() < other.Tie();
    }
};

struct CompositeKey {
    std::array<PhysicsSandboxTextureAssetId, 4> ids{};

    bool operator<(const CompositeKey &other) const {
        return ids < other.ids;
    }
};

PhysicsSandboxTextureAssetId AssetId(
        const PhysicsSandboxTextureAssetHandle &asset) {
    return asset ? asset->metadata.id : 0u;
}

QByteArray AssetBytes(const PhysicsSandboxTextureAssetHandle &asset) {
    if (!asset || asset->encodedBytes.empty()) return {};
    return QByteArray(
            reinterpret_cast<const char *>(asset->encodedBytes.data()),
            static_cast<qsizetype>(asset->encodedBytes.size()));
}

QString AssetReadError(
        const forevervalidator::experimental::
                PhysicsSandboxTextureAssetError &error) {
    QString result = QString::fromUtf8(error.diagnostic);
    if (result.isEmpty()) {
        result = QStringLiteral("texture asset %1 could not be read")
                         .arg(error.id);
    }
    if (!error.sourcePath.empty()) {
        result += QStringLiteral(" [%1]").arg(
                QString::fromUtf8(error.sourcePath));
    }
    return result;
}

void AppendDiagnostic(QString *target, const QString &message) {
    if (message.isEmpty()) return;
    if (!target->isEmpty()) *target += QStringLiteral("; ");
    *target += message;
}

class MaterialTextureLoader {
public:
    MaterialTextureLoader(const PhysicsSandboxRenderScene &scene,
                          QString packIdentity, QString cacheRoot,
                          NativeMaterialLoadTelemetry *telemetry)
        : scene_(scene),
          packIdentity_(std::move(packIdentity)),
          cacheRoot_(std::move(cacheRoot)),
          telemetry_(telemetry) {}

    std::optional<PhysicsSandboxTextureAssetHandle> Read(
            const PhysicsSandboxRenderMaterial &material, int bitmapIndex,
            QString *diagnostic) {
        if (bitmapIndex < 0 ||
            bitmapIndex >= static_cast<int>(material.bitmaps.size())) {
            return std::nullopt;
        }
        const PhysicsSandboxMaterialBitmap &bitmap =
                material.bitmaps[static_cast<std::size_t>(bitmapIndex)];
        ++telemetry_->referencedTextureCount;
        if (bitmap.textureAssetId == 0u) {
            ++telemetry_->failedTextureCount;
            AppendDiagnostic(
                    diagnostic,
                    !bitmap.textureDiagnostic.empty()
                            ? QString::fromUtf8(bitmap.textureDiagnostic)
                            : QStringLiteral("%1 has no resolved image asset")
                                      .arg(QString::fromUtf8(
                                              bitmap.samplerName)));
            return std::nullopt;
        }
        const auto read = scene_.textureAssets.Read(bitmap.textureAssetId);
        if (!read) {
            ++telemetry_->failedTextureCount;
            AppendDiagnostic(diagnostic, AssetReadError(read.Error()));
            return std::nullopt;
        }
        ++telemetry_->resolvedTextureCount;
        return read.Value();
    }

    CachedNativeTexture Cache(
            const PhysicsSandboxTextureAssetHandle &asset,
            NativeTextureSemantic semantic, QString *diagnostic) {
        if (!asset) return {};
        const TextureKey key{asset->metadata.id, semantic};
        const auto existing = cached_.find(key);
        if (existing != cached_.end()) {
            ++telemetry_->cacheHitCount;
            ++telemetry_->memoryCacheHitCount;
            return existing->second;
        }

        const QByteArray bytes = AssetBytes(asset);
        telemetry_->encodedByteCount +=
                static_cast<std::uint64_t>(bytes.size());
        QString error;
        const PreparedNativeTexture prepared = PrepareNativeTexture(
                bytes, QString::fromUtf8(asset->metadata.sourcePath),
                semantic, &error);
        CachedNativeTexture cached;
        if (error.isEmpty() && prepared.estimatedGpuBytes > 0 &&
            static_cast<std::uint64_t>(prepared.estimatedGpuBytes) >
                    MaxTextureGpuBytesPerScene -
                            std::min(MaxTextureGpuBytesPerScene,
                                     telemetry_->estimatedGpuByteCount)) {
            error = QStringLiteral(
                    "native texture scene budget exceeds 512 MiB");
        }
        if (error.isEmpty()) {
            cached = StoreNativeTexture(
                    prepared, bytes, packIdentity_,
                    QString::fromUtf8(asset->metadata.sourcePath), semantic,
                    cacheRoot_);
            error = cached.error;
        }
        if (!error.isEmpty()) {
            ++telemetry_->failedTextureCount;
            AppendDiagnostic(
                    diagnostic,
                    QStringLiteral("%1: %2")
                            .arg(QString::fromUtf8(asset->metadata.sourcePath),
                                 error));
        } else {
            telemetry_->estimatedGpuByteCount +=
                    static_cast<std::uint64_t>(
                            std::max<qint64>(0, cached.estimatedGpuBytes));
            if (cached.cacheHit) {
                ++telemetry_->cacheHitCount;
                ++telemetry_->diskCacheHitCount;
            }
        }
        cached_.emplace(key, cached);
        return cached;
    }

    CachedNativeTexture Composite(
            const PhysicsSandboxTextureAssetHandle &diffuse,
            const PhysicsSandboxTextureAssetHandle &blend2,
            const PhysicsSandboxTextureAssetHandle &blendMask,
            const PhysicsSandboxTextureAssetHandle &blend3,
            QString *diagnostic) {
        const CompositeKey key{{AssetId(diffuse), AssetId(blend2),
                                AssetId(blendMask), AssetId(blend3)}};
        const auto existing = cachedComposites_.find(key);
        if (existing != cachedComposites_.end()) {
            ++telemetry_->cacheHitCount;
            ++telemetry_->memoryCacheHitCount;
            return existing->second;
        }

        QString error;
        const QByteArray diffuseBytes = AssetBytes(diffuse);
        DecodedNativeTexture diffuseImage = DecodeNativeTexture(
                diffuseBytes, QString::fromUtf8(diffuse->metadata.sourcePath),
                NativeTextureSemantic::AlbedoSrgb, &error);
        if (!error.isEmpty()) {
            AppendDiagnostic(diagnostic, error);
            ++telemetry_->failedTextureCount;
            return {};
        }
        const auto decodeOptional = [&](
                                            const PhysicsSandboxTextureAssetHandle
                                                    &asset,
                                            NativeTextureSemantic semantic,
                                            DecodedNativeTexture *output) {
            if (!asset) return true;
            const QByteArray bytes = AssetBytes(asset);
            *output = DecodeNativeTexture(
                    bytes, QString::fromUtf8(asset->metadata.sourcePath),
                    semantic, &error);
            if (!error.isEmpty()) {
                AppendDiagnostic(diagnostic, error);
                ++telemetry_->failedTextureCount;
                return false;
            }
            return true;
        };

        DecodedNativeTexture blend2Image;
        DecodedNativeTexture maskImage;
        DecodedNativeTexture blend3Image;
        if (!decodeOptional(blend2, NativeTextureSemantic::AlbedoSrgb,
                            &blend2Image) ||
            !decodeOptional(blendMask, NativeTextureSemantic::LinearData,
                            &maskImage) ||
            !decodeOptional(blend3, NativeTextureSemantic::AlbedoSrgb,
                            &blend3Image)) {
            return {};
        }
        const DecodedNativeTexture composited = ComposeNativeAlbedo(
                diffuseImage, blend2 ? &blend2Image : nullptr,
                blendMask ? &maskImage : nullptr,
                blend3 ? &blend3Image : nullptr);
        const PreparedNativeTexture prepared = PrepareNativeTexture(
                composited, NativeTextureSemantic::AlbedoSrgb, &error);
        if (error.isEmpty() && prepared.estimatedGpuBytes > 0 &&
            static_cast<std::uint64_t>(prepared.estimatedGpuBytes) >
                    MaxTextureGpuBytesPerScene -
                            std::min(MaxTextureGpuBytesPerScene,
                                     telemetry_->estimatedGpuByteCount)) {
            error = QStringLiteral(
                    "native texture scene budget exceeds 512 MiB");
        }
        if (!error.isEmpty()) {
            AppendDiagnostic(diagnostic, error);
            ++telemetry_->failedTextureCount;
            return {};
        }

        QByteArray identity;
        for (const auto &asset : {diffuse, blend2, blendMask, blend3}) {
            if (!asset) {
                identity.append("0:0:", 4);
                continue;
            }
            const QByteArray bytes = AssetBytes(asset);
            identity.append(QByteArray::number(asset->metadata.id));
            identity.append(':');
            identity.append(QByteArray::number(bytes.size()));
            identity.append(':');
            identity.append(bytes);
        }
        CachedNativeTexture cached = StoreNativeTexture(
                prepared, identity, packIdentity_,
                QStringLiteral("composited material %1")
                        .arg(diffuse->metadata.id),
                NativeTextureSemantic::AlbedoSrgb, cacheRoot_);
        if (!cached) {
            ++telemetry_->failedTextureCount;
            AppendDiagnostic(diagnostic, cached.error);
        } else {
            if (cached.cacheHit) {
                ++telemetry_->cacheHitCount;
                ++telemetry_->diskCacheHitCount;
            }
            telemetry_->estimatedGpuByteCount +=
                    static_cast<std::uint64_t>(
                            std::max<qint64>(0, cached.estimatedGpuBytes));
            cachedComposites_.emplace(key, cached);
        }
        return cached;
    }

private:
    const PhysicsSandboxRenderScene &scene_;
    QString packIdentity_;
    QString cacheRoot_;
    NativeMaterialLoadTelemetry *telemetry_ = nullptr;
    std::map<TextureKey, CachedNativeTexture> cached_;
    std::map<CompositeKey, CachedNativeTexture> cachedComposites_;
};

}  // namespace

std::vector<NativeMaterialProfile> ResolveNativeMaterialProfiles(
        const PhysicsSandboxRenderScene &scene) {
    std::vector<NativeMaterialProfile> profiles;
    profiles.reserve(scene.materials.size());
    for (const PhysicsSandboxRenderMaterial &material : scene.materials) {
        profiles.push_back(ResolveNativeMaterialProfile(material));
    }
    return profiles;
}

std::vector<bool> SelectDefaultNativeMaterialLoadMask(
        const PhysicsSandboxRenderScene &scene,
        const std::vector<NativeMaterialProfile> &profiles) {
    std::vector<bool> loadMask(scene.materials.size(), false);
    for (const auto &instance : scene.instances) {
        if (!instance.visible || instance.lodLevel != 0u ||
            instance.renderLayer == PhysicsSandboxRenderLayer::Background ||
            instance.meshIndex >= scene.meshes.size() ||
            instance.materialIndex >= scene.materials.size() ||
            !IsDefaultVisualInstance(instance.purpose,
                                     instance.provenance.blockName) ||
            (instance.materialIndex < profiles.size() &&
             !profiles[instance.materialIndex].visible)) {
            continue;
        }
        loadMask[instance.materialIndex] = true;
    }
    return loadMask;
}

NativeMaterialLoadResult LoadNativeMaterials(
        const PhysicsSandboxRenderScene &scene, const QString &packIdentity,
        const QString &cacheRoot) {
    const std::vector<NativeMaterialProfile> profiles =
            ResolveNativeMaterialProfiles(scene);
    const std::vector<bool> loadMask(scene.materials.size(), true);
    return LoadNativeMaterials(scene, profiles, loadMask, packIdentity,
                               cacheRoot);
}

NativeMaterialLoadResult LoadNativeMaterials(
        const PhysicsSandboxRenderScene &scene,
        const std::vector<NativeMaterialProfile> &profiles,
        const std::vector<bool> &loadMask, const QString &packIdentity,
        const QString &cacheRoot) {
    NativeMaterialLoadResult result;
    result.materials.resize(scene.materials.size());
    result.renderStates.reserve(scene.materials.size());
    QElapsedTimer timer;
    timer.start();
    MaterialTextureLoader loader(scene, packIdentity, cacheRoot,
                                 &result.telemetry);

    bool loadedAny = false;
    for (std::size_t materialIndex = 0u;
         materialIndex < scene.materials.size(); ++materialIndex) {
        const PhysicsSandboxRenderMaterial &material =
                scene.materials[materialIndex];
        NativeMaterialRuntime runtime;
        runtime.profile = materialIndex < profiles.size()
                                  ? profiles[materialIndex]
                                  : ResolveNativeMaterialProfile(material);
        const bool loadTextures = materialIndex < loadMask.size() &&
                loadMask[materialIndex] && runtime.profile.visible;
        if (!loadTextures) {
            result.materials[materialIndex] = std::move(runtime);
            continue;
        }
        loadedAny = true;

        auto albedo = loader.Read(material, runtime.profile.albedoBitmap,
                                  &runtime.diagnostic);
        auto blend2 = loader.Read(material, runtime.profile.blend2Bitmap,
                                  &runtime.diagnostic);
        auto blendMask = loader.Read(material, runtime.profile.blendMaskBitmap,
                                     &runtime.diagnostic);
        auto blend3 = loader.Read(material, runtime.profile.blend3Bitmap,
                                  &runtime.diagnostic);
        CachedNativeTexture cachedAlbedo;
        if (albedo) {
            runtime.albedoSourcePath =
                    QString::fromUtf8((*albedo)->metadata.sourcePath);
            if ((blend2 && blendMask) || blend3) {
                cachedAlbedo = loader.Composite(
                        *albedo, blend2.value_or(nullptr),
                        blendMask.value_or(nullptr), blend3.value_or(nullptr),
                        &runtime.diagnostic);
            } else {
                cachedAlbedo = loader.Cache(
                        *albedo, NativeTextureSemantic::AlbedoSrgb,
                        &runtime.diagnostic);
            }
        }
        if (cachedAlbedo) {
            runtime.albedoTexture = cachedAlbedo.source;
            runtime.albedoGenerateMipmaps = cachedAlbedo.generateMipmaps;
            runtime.nativeAlbedo = true;
            ApplyNativeAlbedoTransparency(
                    cachedAlbedo.hasTransparency,
                    cachedAlbedo.hasPartialTransparency, &runtime.profile);
            ++result.telemetry.nativeMaterialCount;
        } else {
            ++result.telemetry.fallbackMaterialCount;
        }

        const auto normal = loader.Read(material, runtime.profile.normalBitmap,
                                        &runtime.diagnostic);
        if (normal) {
            const CachedNativeTexture cachedNormal = loader.Cache(
                    *normal, NativeTextureSemantic::NormalMap,
                    &runtime.diagnostic);
            if (cachedNormal) {
                runtime.normalTexture = cachedNormal.source;
                runtime.normalGenerateMipmaps = cachedNormal.generateMipmaps;
                runtime.nativeNormal = true;
            }
        }
        const auto specular = loader.Read(
                material, runtime.profile.specularBitmap,
                &runtime.diagnostic);
        if (specular) {
            const CachedNativeTexture cachedSpecular = loader.Cache(
                    *specular, NativeTextureSemantic::LinearData,
                    &runtime.diagnostic);
            if (cachedSpecular) {
                runtime.specularTexture = cachedSpecular.source;
                runtime.specularGenerateMipmaps =
                        cachedSpecular.generateMipmaps;
                runtime.nativeSpecular = true;
            }
        }

        result.telemetry.worldProjectedMaterialCount +=
                runtime.profile.renderState.worldXz ? 1u : 0u;
        result.telemetry.doubleSidedMaterialCount +=
                runtime.profile.renderState.doubleSided ? 1u : 0u;
        result.telemetry.unlitMaterialCount += runtime.profile.unlit ? 1u : 0u;
        switch (runtime.profile.renderState.alphaMode) {
        case StaticVisualAlphaMode::Masked:
            ++result.telemetry.maskedMaterialCount;
            break;
        case StaticVisualAlphaMode::Blended:
            ++result.telemetry.blendedMaterialCount;
            break;
        case StaticVisualAlphaMode::Additive:
            ++result.telemetry.additiveMaterialCount;
            break;
        case StaticVisualAlphaMode::Subtractive:
            ++result.telemetry.subtractiveMaterialCount;
            break;
        case StaticVisualAlphaMode::Opaque:
        case StaticVisualAlphaMode::Unknown:
        default:
            break;
        }

        result.materials[materialIndex] = std::move(runtime);
    }
    for (const NativeMaterialRuntime &runtime : result.materials) {
        result.renderStates.push_back(runtime.profile.renderState);
    }
    if (loadedAny) {
        PruneNativeTextureCache(cacheRoot);
    }
    result.telemetry.loadNanoseconds =
            static_cast<std::uint64_t>(timer.nsecsElapsed());
    return result;
}

}  // namespace forevertas::viewer
