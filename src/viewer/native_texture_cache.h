#ifndef FOREVERTAS_VIEWER_NATIVE_TEXTURE_CACHE_H
#define FOREVERTAS_VIEWER_NATIVE_TEXTURE_CACHE_H

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVector>

namespace forevertas::viewer {

enum class NativeTextureSemantic {
    AlbedoSrgb,
    LinearData,
    NormalMap,
};

struct DecodedNativeTexture {
    QVector<QImage> mipmaps;
    QString sourceFormat;
    bool hasTransparency = false;
    bool hasPartialTransparency = false;
};

struct PreparedNativeTexture {
    QByteArray fileBytes;
    QString fileSuffix;
    QSize size;
    QString sourceFormat;
    int mipLevelCount = 0;
    qint64 estimatedGpuBytes = 0;
    bool generateMipmaps = false;
    bool hasTransparency = false;
    bool hasPartialTransparency = false;
};

struct CachedNativeTexture {
    QUrl source;
    QString cacheKey;
    QString error;
    QSize size;
    QString sourceFormat;
    int mipLevelCount = 0;
    qint64 sourceBytes = 0;
    qint64 estimatedGpuBytes = 0;
    bool cacheHit = false;
    bool generateMipmaps = false;
    bool hasTransparency = false;
    bool hasPartialTransparency = false;

    explicit operator bool() const noexcept {
        return error.isEmpty() && source.isValid();
    }
};

// Decodes the DDS/TGA formats used by the TMNF material archive. DDS supports
// legacy DXT1/3/5, BC4/5 through DX10 headers, common RGB(A) bit masks, and
// luminance images. Normal maps reconstruct DXT5nm/BC5 Z into conventional
// RGB normals so Qt Quick 3D can consume them directly.
DecodedNativeTexture DecodeNativeTexture(
        const QByteArray &encoded,
        const QString &sourcePath,
        NativeTextureSemantic semantic,
        QString *error = nullptr);

// Produces a file accepted by Qt Quick 3D. Compatible linear-data DXT textures
// retain their block-compressed KTX mip chain. Color, normal, composited, and
// unsupported compressed formats use color-managed PNG and GPU mip generation
// because Qt 6.8 cannot load uncompressed or sRGB S3TC KTX1 textures.
PreparedNativeTexture PrepareNativeTexture(
        const QByteArray &encoded,
        const QString &sourcePath,
        NativeTextureSemantic semantic,
        QString *error = nullptr);

PreparedNativeTexture PrepareNativeTexture(
        const DecodedNativeTexture &decoded,
        NativeTextureSemantic semantic,
        QString *error = nullptr);

// Applies gbx3d's blend equation to decoded albedo maps:
// mix(mix(diffuse, blend2, BlendI.r), blend3, blend3.a).
DecodedNativeTexture ComposeNativeAlbedo(
        const DecodedNativeTexture &diffuse,
        const DecodedNativeTexture *blend2,
        const DecodedNativeTexture *blendMask,
        const DecodedNativeTexture *blend3);

// Stores a prepared texture under a pack-local, content-addressed cache key.
// packIdentity should identify the installed Packs directory; sourceIdentity
// is diagnostic only and is never used as a filesystem path.
CachedNativeTexture StoreNativeTexture(
        const PreparedNativeTexture &prepared,
        const QByteArray &sourceBytes,
        const QString &packIdentity,
        const QString &sourceIdentity,
        NativeTextureSemantic semantic,
        const QString &cacheRoot = {});

// Applies the global on-disk cache budget once after a scene load. Keeping
// eviction out of StoreNativeTexture avoids an O(entry-count^2) directory scan
// while a map's many textures are being converted.
void PruneNativeTextureCache(const QString &cacheRoot = {});

}  // namespace forevertas::viewer

#endif
