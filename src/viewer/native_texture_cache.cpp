#include "viewer/native_texture_cache.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace forevertas::viewer {
namespace {

constexpr std::array<char, 12> KtxIdentifier{
        static_cast<char>(0xab), 'K', 'T', 'X', ' ', '1', '1',
        static_cast<char>(0xbb), '\r', '\n', static_cast<char>(0x1a), '\n'};
constexpr std::uint32_t KtxEndianness = 0x04030201u;
constexpr std::uint32_t GlRed = 0x1903u;
constexpr std::uint32_t GlRg = 0x8227u;
constexpr std::uint32_t GlRgb = 0x1907u;
constexpr std::uint32_t GlRgba = 0x1908u;
constexpr std::uint32_t GlCompressedRgbDxt1 = 0x83f0u;
constexpr std::uint32_t GlCompressedRgbaDxt1 = 0x83f1u;
constexpr std::uint32_t GlCompressedRgbaDxt3 = 0x83f2u;
constexpr std::uint32_t GlCompressedRgbaDxt5 = 0x83f3u;
constexpr std::uint32_t GlCompressedRedRgtc1 = 0x8dbbu;
constexpr std::uint32_t GlCompressedRgRgtc2 = 0x8dbdu;
constexpr char CacheVersion[] = "forevertas-native-texture-v3";
constexpr int MaxTextureDimension = 8192;
constexpr qint64 MaxDecodedTexturePixels = 16ll * 1024ll * 1024ll;
constexpr qsizetype MaxEncodedTextureBytes = 256ll * 1024ll * 1024ll;
constexpr qint64 MaxTextureCacheBytes = 1024ll * 1024ll * 1024ll;

enum class DdsFormat {
    Unknown,
    Dxt1,
    Dxt3,
    Dxt5,
    Bc4,
    Bc5,
    RgbaMasks,
    Luminance,
};

struct DdsDescription {
    int width = 0;
    int height = 0;
    int mipLevelCount = 0;
    int dataOffset = 0;
    int bitsPerPixel = 0;
    std::uint32_t redMask = 0u;
    std::uint32_t greenMask = 0u;
    std::uint32_t blueMask = 0u;
    std::uint32_t alphaMask = 0u;
    DdsFormat format = DdsFormat::Unknown;
};

struct DdsMipSlice {
    int width = 0;
    int height = 0;
    int offset = 0;
    int size = 0;
};

bool ValidTextureDimensions(int width, int height) {
    return width > 0 && height > 0 && width <= MaxTextureDimension &&
            height <= MaxTextureDimension &&
            static_cast<qint64>(width) * height <= MaxDecodedTexturePixels;
}

std::uint16_t ReadU16(const char *data) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadU32(const char *data) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

void AppendU32(QByteArray &bytes, std::uint32_t value) {
    bytes.append(static_cast<char>(value & 0xffu));
    bytes.append(static_cast<char>((value >> 8u) & 0xffu));
    bytes.append(static_cast<char>((value >> 16u) & 0xffu));
    bytes.append(static_cast<char>((value >> 24u) & 0xffu));
}

constexpr std::uint32_t FourCc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

bool ParseDds(const QByteArray &encoded, DdsDescription *description,
              QString *error) {
    if (encoded.size() < 128 || std::memcmp(encoded.constData(), "DDS ", 4) != 0) {
        if (error != nullptr) *error = QStringLiteral("not a DDS image");
        return false;
    }
    if (ReadU32(encoded.constData() + 4) != 124u ||
        ReadU32(encoded.constData() + 76) != 32u) {
        if (error != nullptr) *error = QStringLiteral("invalid DDS header size");
        return false;
    }

    DdsDescription result;
    result.height = static_cast<int>(ReadU32(encoded.constData() + 12));
    result.width = static_cast<int>(ReadU32(encoded.constData() + 16));
    result.mipLevelCount = std::max(
            1, static_cast<int>(ReadU32(encoded.constData() + 28)));
    result.bitsPerPixel =
            static_cast<int>(ReadU32(encoded.constData() + 88));
    result.redMask = ReadU32(encoded.constData() + 92);
    result.greenMask = ReadU32(encoded.constData() + 96);
    result.blueMask = ReadU32(encoded.constData() + 100);
    result.alphaMask = ReadU32(encoded.constData() + 104);
    result.dataOffset = 128;

    if (!ValidTextureDimensions(result.width, result.height) ||
        result.mipLevelCount > 32) {
        if (error != nullptr) *error = QStringLiteral("invalid DDS dimensions");
        return false;
    }

    constexpr std::uint32_t PixelFormatFourCc = 0x4u;
    constexpr std::uint32_t PixelFormatRgb = 0x40u;
    constexpr std::uint32_t PixelFormatLuminance = 0x20000u;
    const std::uint32_t flags = ReadU32(encoded.constData() + 80);
    const std::uint32_t fourCc = ReadU32(encoded.constData() + 84);
    if ((flags & PixelFormatFourCc) != 0u) {
        switch (fourCc) {
        case FourCc('D', 'X', 'T', '1'): result.format = DdsFormat::Dxt1; break;
        case FourCc('D', 'X', 'T', '3'): result.format = DdsFormat::Dxt3; break;
        case FourCc('D', 'X', 'T', '5'): result.format = DdsFormat::Dxt5; break;
        case FourCc('A', 'T', 'I', '1'):
        case FourCc('B', 'C', '4', 'U'): result.format = DdsFormat::Bc4; break;
        case FourCc('A', 'T', 'I', '2'):
        case FourCc('B', 'C', '5', 'U'): result.format = DdsFormat::Bc5; break;
        case FourCc('D', 'X', '1', '0'): {
            if (encoded.size() < 148) {
                if (error != nullptr) {
                    *error = QStringLiteral("truncated DDS DX10 header");
                }
                return false;
            }
            result.dataOffset = 148;
            const std::uint32_t dxgi = ReadU32(encoded.constData() + 128);
            switch (dxgi) {
            case 28u:
                result.format = DdsFormat::RgbaMasks;
                result.bitsPerPixel = 32;
                result.redMask = 0x000000ffu;
                result.greenMask = 0x0000ff00u;
                result.blueMask = 0x00ff0000u;
                result.alphaMask = 0xff000000u;
                break;
            case 71u:
            case 72u: result.format = DdsFormat::Dxt1; break;
            case 74u:
            case 75u: result.format = DdsFormat::Dxt3; break;
            case 77u:
            case 78u: result.format = DdsFormat::Dxt5; break;
            case 80u:
            case 81u: result.format = DdsFormat::Bc4; break;
            case 83u:
            case 84u: result.format = DdsFormat::Bc5; break;
            case 87u:
                result.format = DdsFormat::RgbaMasks;
                result.bitsPerPixel = 32;
                result.redMask = 0x00ff0000u;
                result.greenMask = 0x0000ff00u;
                result.blueMask = 0x000000ffu;
                result.alphaMask = 0xff000000u;
                break;
            default:
                if (error != nullptr) {
                    *error = QStringLiteral("unsupported DDS DXGI format %1")
                                     .arg(dxgi);
                }
                return false;
            }
            break;
        }
        default:
            if (error != nullptr) {
                *error = QStringLiteral("unsupported DDS FourCC 0x%1")
                                 .arg(fourCc, 8, 16, QLatin1Char('0'));
            }
            return false;
        }
    } else if ((flags & PixelFormatRgb) != 0u) {
        result.format = DdsFormat::RgbaMasks;
    } else if ((flags & PixelFormatLuminance) != 0u) {
        result.format = DdsFormat::Luminance;
    } else {
        if (error != nullptr) {
            *error = QStringLiteral("unsupported DDS pixel format");
        }
        return false;
    }

    *description = result;
    return true;
}

int BlockBytes(DdsFormat format) {
    switch (format) {
    case DdsFormat::Dxt1:
    case DdsFormat::Bc4: return 8;
    case DdsFormat::Dxt3:
    case DdsFormat::Dxt5:
    case DdsFormat::Bc5: return 16;
    default: return 0;
    }
}

QVector<DdsMipSlice> DdsMipSlices(const DdsDescription &description,
                                  int encodedSize, QString *error) {
    QVector<DdsMipSlice> result;
    result.reserve(description.mipLevelCount);
    int offset = description.dataOffset;
    int width = description.width;
    int height = description.height;
    for (int level = 0; level < description.mipLevelCount; ++level) {
        std::int64_t size = 0;
        const int blockBytes = BlockBytes(description.format);
        if (blockBytes != 0) {
            size = static_cast<std::int64_t>(std::max(1, (width + 3) / 4)) *
                   std::max(1, (height + 3) / 4) * blockBytes;
        } else if (description.bitsPerPixel > 0) {
            size = (static_cast<std::int64_t>(width) *
                    description.bitsPerPixel + 7) /
                   8 * height;
        }
        if (size <= 0 || size > std::numeric_limits<int>::max() ||
            offset < 0 || offset > encodedSize - static_cast<int>(size)) {
            if (error != nullptr) {
                *error = QStringLiteral("truncated DDS mip level %1").arg(level);
            }
            return {};
        }
        result.push_back({width, height, offset, static_cast<int>(size)});
        offset += static_cast<int>(size);
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
    }
    return result;
}

std::array<unsigned char, 4> Decode565(std::uint16_t value) {
    return {static_cast<unsigned char>(((value >> 11u) & 31u) * 255u / 31u),
            static_cast<unsigned char>(((value >> 5u) & 63u) * 255u / 63u),
            static_cast<unsigned char>((value & 31u) * 255u / 31u), 255u};
}

std::array<std::array<unsigned char, 4>, 4> DecodeColorPalette(
        const unsigned char *block, bool forceFourColors) {
    const std::uint16_t color0 = ReadU16(reinterpret_cast<const char *>(block));
    const std::uint16_t color1 =
            ReadU16(reinterpret_cast<const char *>(block + 2));
    std::array<std::array<unsigned char, 4>, 4> colors{};
    colors[0] = Decode565(color0);
    colors[1] = Decode565(color1);
    if (color0 > color1 || forceFourColors) {
        for (int channel = 0; channel < 3; ++channel) {
            colors[2][channel] = static_cast<unsigned char>(
                    (2u * colors[0][channel] + colors[1][channel]) / 3u);
            colors[3][channel] = static_cast<unsigned char>(
                    (colors[0][channel] + 2u * colors[1][channel]) / 3u);
        }
        colors[2][3] = 255u;
        colors[3][3] = 255u;
    } else {
        for (int channel = 0; channel < 3; ++channel) {
            colors[2][channel] = static_cast<unsigned char>(
                    (colors[0][channel] + colors[1][channel]) / 2u);
        }
        colors[2][3] = 255u;
        colors[3] = {0u, 0u, 0u, 0u};
    }
    return colors;
}

std::array<unsigned char, 8> DecodeAlphaPalette(const unsigned char *block) {
    std::array<unsigned char, 8> alpha{};
    alpha[0] = block[0];
    alpha[1] = block[1];
    if (alpha[0] > alpha[1]) {
        for (int index = 1; index <= 6; ++index) {
            alpha[index + 1] = static_cast<unsigned char>(
                    ((7 - index) * alpha[0] + index * alpha[1]) / 7);
        }
    } else {
        for (int index = 1; index <= 4; ++index) {
            alpha[index + 1] = static_cast<unsigned char>(
                    ((5 - index) * alpha[0] + index * alpha[1]) / 5);
        }
        alpha[6] = 0u;
        alpha[7] = 255u;
    }
    return alpha;
}

std::array<unsigned char, 16> DecodeAlphaBlock(const unsigned char *block) {
    const auto palette = DecodeAlphaPalette(block);
    std::uint64_t indices = 0u;
    for (int index = 0; index < 6; ++index) {
        indices |= static_cast<std::uint64_t>(block[index + 2]) <<
                   (8u * static_cast<unsigned>(index));
    }
    std::array<unsigned char, 16> result{};
    for (int index = 0; index < 16; ++index) {
        result[index] = palette[(indices >> (3u * index)) & 7u];
    }
    return result;
}

void StorePixel(QImage &image, int x, int y,
                const std::array<unsigned char, 4> &rgba,
                bool *hasTransparency) {
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) return;
    unsigned char *pixel = image.scanLine(y) + x * 4;
    std::memcpy(pixel, rgba.data(), 4u);
    *hasTransparency |= rgba[3] != 255u;
}

void AccumulateAlphaCoverage(const QImage &image,
                             bool *hasTransparency,
                             bool *hasPartialTransparency) {
    for (int y = 0; y < image.height(); ++y) {
        const unsigned char *row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const unsigned char alpha = row[x * 4 + 3];
            *hasTransparency |= alpha != 255u;
            *hasPartialTransparency |= alpha != 0u && alpha != 255u;
            if (*hasTransparency && *hasPartialTransparency) return;
        }
    }
}

QImage DecodeBlockCompressed(const QByteArray &encoded,
                             const DdsMipSlice &slice, DdsFormat format,
                             NativeTextureSemantic semantic,
                             bool *hasTransparency) {
    QImage image(slice.width, slice.height, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    const auto *data = reinterpret_cast<const unsigned char *>(
            encoded.constData() + slice.offset);
    const int blockBytes = BlockBytes(format);
    const int blocksWide = std::max(1, (slice.width + 3) / 4);
    const int blocksHigh = std::max(1, (slice.height + 3) / 4);
    for (int blockY = 0; blockY < blocksHigh; ++blockY) {
        for (int blockX = 0; blockX < blocksWide; ++blockX) {
            const unsigned char *block =
                    data + (blockY * blocksWide + blockX) * blockBytes;
            std::array<unsigned char, 16> alpha{};
            alpha.fill(255u);
            std::array<unsigned char, 16> secondChannel{};
            const unsigned char *colorBlock = block;
            bool forceFourColors =
                    semantic == NativeTextureSemantic::OpaqueAlbedoSrgb &&
                    format == DdsFormat::Dxt1;
            if (format == DdsFormat::Dxt3) {
                for (int pixel = 0; pixel < 16; ++pixel) {
                    const unsigned char nibble = static_cast<unsigned char>(
                            (block[pixel / 2] >> ((pixel & 1) * 4)) & 0x0fu);
                    alpha[pixel] = static_cast<unsigned char>(nibble * 17u);
                }
                colorBlock += 8;
                forceFourColors = true;
            } else if (format == DdsFormat::Dxt5) {
                alpha = DecodeAlphaBlock(block);
                colorBlock += 8;
                forceFourColors = true;
            } else if (format == DdsFormat::Bc4) {
                alpha = DecodeAlphaBlock(block);
            } else if (format == DdsFormat::Bc5) {
                alpha = DecodeAlphaBlock(block);
                secondChannel = DecodeAlphaBlock(block + 8);
            }

            std::array<std::array<unsigned char, 4>, 4> colors{};
            std::uint32_t colorIndices = 0u;
            if (format == DdsFormat::Dxt1 || format == DdsFormat::Dxt3 ||
                format == DdsFormat::Dxt5) {
                colors = DecodeColorPalette(colorBlock, forceFourColors);
                colorIndices = ReadU32(
                        reinterpret_cast<const char *>(colorBlock + 4));
            }

            for (int local = 0; local < 16; ++local) {
                std::array<unsigned char, 4> pixel{0u, 0u, 0u, 255u};
                if (format == DdsFormat::Bc4) {
                    pixel = {alpha[local], alpha[local], alpha[local], 255u};
                } else if (format == DdsFormat::Bc5) {
                    if (semantic == NativeTextureSemantic::NormalMap) {
                        const float nx = alpha[local] / 127.5f - 1.0f;
                        const float ny = secondChannel[local] / 127.5f - 1.0f;
                        const float nz = std::sqrt(
                                std::max(0.0f, 1.0f - nx * nx - ny * ny));
                        pixel = {alpha[local], secondChannel[local],
                                 static_cast<unsigned char>(
                                         std::clamp(nz * 127.5f + 127.5f,
                                                    0.0f, 255.0f)),
                                 255u};
                    } else {
                        pixel = {alpha[local], secondChannel[local], 0u, 255u};
                    }
                } else {
                    pixel = colors[(colorIndices >> (2u * local)) & 3u];
                    if (format == DdsFormat::Dxt3 ||
                        format == DdsFormat::Dxt5) {
                        if (semantic == NativeTextureSemantic::NormalMap &&
                            format == DdsFormat::Dxt5) {
                            const float nx = alpha[local] / 127.5f - 1.0f;
                            const float ny = pixel[1] / 127.5f - 1.0f;
                            const float nz = std::sqrt(std::max(
                                    0.0f, 1.0f - nx * nx - ny * ny));
                            pixel = {alpha[local], pixel[1],
                                     static_cast<unsigned char>(std::clamp(
                                             nz * 127.5f + 127.5f,
                                             0.0f, 255.0f)),
                                     255u};
                        } else {
                            pixel[3] = alpha[local];
                        }
                    }
                }
                if (semantic == NativeTextureSemantic::OpaqueAlbedoSrgb) {
                    pixel[3] = 255u;
                }
                StorePixel(image, blockX * 4 + local % 4,
                           blockY * 4 + local / 4, pixel,
                           hasTransparency);
            }
        }
    }
    return image;
}

unsigned char ExtractMasked(std::uint32_t value, std::uint32_t mask,
                            unsigned char fallback) {
    if (mask == 0u) return fallback;
    unsigned shift = 0u;
    while (((mask >> shift) & 1u) == 0u && shift < 32u) ++shift;
    const std::uint32_t componentMask = mask >> shift;
    const std::uint32_t component = (value & mask) >> shift;
    return static_cast<unsigned char>(
            (component * 255u + componentMask / 2u) / componentMask);
}

QImage DecodeUncompressed(const QByteArray &encoded,
                          const DdsDescription &description,
                          const DdsMipSlice &slice,
                          NativeTextureSemantic semantic,
                          bool *hasTransparency) {
    QImage image(slice.width, slice.height, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    const int bytesPerPixel = (description.bitsPerPixel + 7) / 8;
    const int rowBytes = (slice.width * description.bitsPerPixel + 7) / 8;
    const auto *data = reinterpret_cast<const unsigned char *>(
            encoded.constData() + slice.offset);
    for (int y = 0; y < slice.height; ++y) {
        for (int x = 0; x < slice.width; ++x) {
            const unsigned char *source = data + y * rowBytes + x * bytesPerPixel;
            std::uint32_t value = 0u;
            for (int byte = 0; byte < bytesPerPixel && byte < 4; ++byte) {
                value |= static_cast<std::uint32_t>(source[byte]) << (8u * byte);
            }
            std::array<unsigned char, 4> pixel{};
            if (description.format == DdsFormat::Luminance) {
                const unsigned char luminance = ExtractMasked(
                        value, description.redMask != 0u
                                       ? description.redMask
                                       : ((1u << std::min(31,
                                                         description.bitsPerPixel)) -
                                          1u),
                        source[0]);
                pixel = {luminance, luminance, luminance,
                         ExtractMasked(value, description.alphaMask, 255u)};
            } else {
                pixel = {ExtractMasked(value, description.redMask, 0u),
                         ExtractMasked(value, description.greenMask, 0u),
                         ExtractMasked(value, description.blueMask, 0u),
                         ExtractMasked(value, description.alphaMask, 255u)};
            }
            if (semantic == NativeTextureSemantic::OpaqueAlbedoSrgb) {
                pixel[3] = 255u;
            }
            StorePixel(image, x, y, pixel, hasTransparency);
        }
    }
    return image;
}

DecodedNativeTexture DecodeDds(const QByteArray &encoded,
                               NativeTextureSemantic semantic,
                               QString *error) {
    DdsDescription description;
    if (!ParseDds(encoded, &description, error)) return {};
    const QVector<DdsMipSlice> slices =
            DdsMipSlices(description, encoded.size(), error);
    if (slices.isEmpty()) return {};

    DecodedNativeTexture result;
    result.sourceFormat = QStringLiteral("DDS");
    result.mipmaps.reserve(slices.size());
    for (const DdsMipSlice &slice : slices) {
        QImage image;
        if (BlockBytes(description.format) != 0) {
            image = DecodeBlockCompressed(encoded, slice, description.format,
                                           semantic,
                                           &result.hasTransparency);
        } else {
            image = DecodeUncompressed(encoded, description, slice, semantic,
                                       &result.hasTransparency);
        }
        if (image.isNull()) {
            if (error != nullptr) {
                *error = QStringLiteral("DDS mip decode allocation failed");
            }
            return {};
        }
        AccumulateAlphaCoverage(image, &result.hasTransparency,
                                &result.hasPartialTransparency);
        result.mipmaps.push_back(std::move(image));
    }
    return result;
}

QByteArray EncodeKtxHeader(std::uint32_t glType, std::uint32_t glFormat,
                           std::uint32_t glInternalFormat,
                           std::uint32_t glBaseInternalFormat,
                           int width, int height, int levels) {
    QByteArray result;
    result.reserve(64);
    result.append(KtxIdentifier.data(),
                  static_cast<qsizetype>(KtxIdentifier.size()));
    AppendU32(result, KtxEndianness);
    AppendU32(result, glType);
    AppendU32(result, 1u);
    AppendU32(result, glFormat);
    AppendU32(result, glInternalFormat);
    AppendU32(result, glBaseInternalFormat);
    AppendU32(result, static_cast<std::uint32_t>(width));
    AppendU32(result, static_cast<std::uint32_t>(height));
    AppendU32(result, 0u);
    AppendU32(result, 0u);
    AppendU32(result, 1u);
    AppendU32(result, static_cast<std::uint32_t>(levels));
    AppendU32(result, 0u);
    return result;
}

void AppendKtxLevel(QByteArray &ktx, const QByteArray &level) {
    AppendU32(ktx, static_cast<std::uint32_t>(level.size()));
    ktx.append(level);
    while ((ktx.size() & 3) != 0) ktx.append('\0');
}

PreparedNativeTexture WrapCompressedDds(
        const QByteArray &encoded, const DdsDescription &description,
        const QVector<DdsMipSlice> &slices,
        NativeTextureSemantic semantic, QString *error) {
    std::uint32_t internalFormat = 0u;
    std::uint32_t baseFormat = GlRgba;
    Q_UNUSED(semantic)
    switch (description.format) {
    case DdsFormat::Dxt1:
        internalFormat = GlCompressedRgbaDxt1;
        break;
    case DdsFormat::Dxt3:
        internalFormat = GlCompressedRgbaDxt3;
        break;
    case DdsFormat::Dxt5:
        internalFormat = GlCompressedRgbaDxt5;
        break;
    case DdsFormat::Bc4:
        internalFormat = GlCompressedRedRgtc1;
        baseFormat = GlRed;
        break;
    case DdsFormat::Bc5:
        internalFormat = GlCompressedRgRgtc2;
        baseFormat = GlRg;
        break;
    default:
        if (error != nullptr) {
            *error = QStringLiteral("DDS format cannot be wrapped as compressed KTX");
        }
        return {};
    }

    PreparedNativeTexture result;
    result.fileBytes = EncodeKtxHeader(
            0u, 0u, internalFormat, baseFormat,
            description.width, description.height, slices.size());
    for (const DdsMipSlice &slice : slices) {
        AppendKtxLevel(result.fileBytes,
                       encoded.mid(slice.offset, slice.size));
        result.estimatedGpuBytes += slice.size;
    }
    result.fileSuffix = QStringLiteral("ktx");
    result.size = QSize(description.width, description.height);
    result.sourceFormat = QStringLiteral("DDS block-compressed");
    result.mipLevelCount = slices.size();
    if (description.format == DdsFormat::Dxt1 ||
        description.format == DdsFormat::Dxt3 ||
        description.format == DdsFormat::Dxt5) {
        QString ignored;
        const DecodedNativeTexture decoded = DecodeDds(
                encoded, NativeTextureSemantic::LinearData, &ignored);
        result.hasTransparency = decoded.hasTransparency;
        result.hasPartialTransparency = decoded.hasPartialTransparency;
    }
    return result;
}

QByteArray EncodePng(const QImage &source, NativeTextureSemantic semantic) {
    QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    if (semantic == NativeTextureSemantic::OpaqueAlbedoSrgb) {
        for (int y = 0; y < image.height(); ++y) {
            unsigned char *row = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x) {
                row[x * 4 + 3] = 255u;
            }
        }
    }
    image.setColorSpace((semantic == NativeTextureSemantic::AlbedoSrgb ||
                         semantic == NativeTextureSemantic::OpaqueAlbedoSrgb)
                                ? QColorSpace(QColorSpace::SRgb)
                                : QColorSpace(QColorSpace::SRgbLinear));
    QByteArray result;
    QBuffer buffer(&result);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return result;
}

const QImage *MipFor(const DecodedNativeTexture *texture, int level) {
    if (texture == nullptr || texture->mipmaps.isEmpty()) return nullptr;
    const int last = static_cast<int>(texture->mipmaps.size()) - 1;
    return &texture->mipmaps[std::min(level, last)];
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
    return static_cast<unsigned char>(
            std::clamp(encoded * 255.0f + 0.5f, 0.0f, 255.0f));
}

QImage SizedRgba(const QImage *source, const QSize &size,
                 const QColor &fallback) {
    if (source == nullptr || source->isNull()) {
        QImage result(size, QImage::Format_RGBA8888);
        result.fill(fallback);
        return result;
    }
    QImage result = source->convertToFormat(QImage::Format_RGBA8888);
    if (result.size() != size) {
        result = result.scaled(size, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    }
    return result;
}

QString HashHex(const QByteArray &bytes) {
    return QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                    .toHex());
}

QString TextureCacheRoot(const QString &cacheRoot) {
    return cacheRoot.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                    QStringLiteral("/native-textures/v1")
            : cacheRoot;
}

void PruneTextureCache(const QString &root, const QString &keepPath) {
    QFileInfoList entries;
    QDirIterator iterator(
            root, {QStringLiteral("*.png"), QStringLiteral("*.ktx")},
            QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        entries.push_back(iterator.fileInfo());
    }
    qint64 totalBytes = 0;
    for (const QFileInfo &entry : entries) totalBytes += entry.size();
    if (totalBytes <= MaxTextureCacheBytes) return;

    std::sort(entries.begin(), entries.end(),
              [](const QFileInfo &left, const QFileInfo &right) {
                  return left.lastModified() < right.lastModified();
              });
    for (const QFileInfo &entry : entries) {
        if (totalBytes <= MaxTextureCacheBytes) break;
        if (entry.absoluteFilePath() == keepPath) continue;
        if (QFile::remove(entry.absoluteFilePath())) {
            totalBytes -= entry.size();
        }
    }
}

}  // namespace

DecodedNativeTexture DecodeNativeTexture(
        const QByteArray &encoded, const QString &sourcePath,
        NativeTextureSemantic semantic, QString *error) {
    if (error != nullptr) error->clear();
    if (encoded.size() > MaxEncodedTextureBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("encoded texture exceeds the 256 MiB limit");
        }
        return {};
    }
    if (encoded.size() >= 4 &&
        std::memcmp(encoded.constData(), "DDS ", 4) == 0) {
        return DecodeDds(encoded, semantic, error);
    }

    QByteArray format;
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    if (suffix == QStringLiteral("tga")) format = QByteArrayLiteral("TGA");
    QBuffer buffer;
    buffer.setData(encoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, format);
    reader.setAutoTransform(false);
    const QSize declaredSize = reader.size();
    if (declaredSize.isValid() &&
        !ValidTextureDimensions(declaredSize.width(), declaredSize.height())) {
        if (error != nullptr) {
            *error = QStringLiteral("texture exceeds the decoded pixel budget: %1")
                             .arg(sourcePath);
        }
        return {};
    }
    QImage image = reader.read();
    if (image.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("unsupported or malformed texture image: %1")
                             .arg(sourcePath);
        }
        return {};
    }
    if (!ValidTextureDimensions(image.width(), image.height())) {
        if (error != nullptr) {
            *error = QStringLiteral("texture exceeds the decoded pixel budget: %1")
                             .arg(sourcePath);
        }
        return {};
    }
    DecodedNativeTexture result;
    result.sourceFormat = suffix.isEmpty() ? QStringLiteral("image")
                                            : suffix.toUpper();
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (semantic == NativeTextureSemantic::OpaqueAlbedoSrgb) {
        for (int y = 0; y < image.height(); ++y) {
            unsigned char *row = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x) {
                row[x * 4 + 3] = 255u;
            }
        }
    }
    AccumulateAlphaCoverage(image, &result.hasTransparency,
                            &result.hasPartialTransparency);
    result.mipmaps.push_back(std::move(image));
    return result;
}

PreparedNativeTexture PrepareNativeTexture(
        const QByteArray &encoded, const QString &sourcePath,
        NativeTextureSemantic semantic, QString *error) {
    if (error != nullptr) error->clear();
    if (encoded.size() > MaxEncodedTextureBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("encoded texture exceeds the 256 MiB limit");
        }
        return {};
    }
    DdsDescription description;
    QString parseError;
    if (ParseDds(encoded, &description, &parseError) &&
        BlockBytes(description.format) != 0 &&
        semantic == NativeTextureSemantic::LinearData &&
        (description.format == DdsFormat::Dxt1 ||
         description.format == DdsFormat::Dxt3 ||
         description.format == DdsFormat::Dxt5)) {
        const QVector<DdsMipSlice> slices =
                DdsMipSlices(description, encoded.size(), &parseError);
        if (!slices.isEmpty()) {
            PreparedNativeTexture wrapped = WrapCompressedDds(
                    encoded, description, slices, semantic, error);
            if (description.format != DdsFormat::Dxt1 ||
                !wrapped.hasTransparency) {
                if (description.format == DdsFormat::Dxt1) {
                    // Qt 6.8 has no QRhi mapping for RGBA_DXT1. Opaque BC1 is
                    // represented as RGB_DXT1 and remains compressed.
                    const std::uint32_t oldFormat = GlCompressedRgbaDxt1;
                    const std::uint32_t newFormat = GlCompressedRgbDxt1;
                    const std::uint32_t oldBase = GlRgba;
                    const std::uint32_t newBase = GlRgb;
                    for (int offset : {28, 32}) {
                        const std::uint32_t oldValue =
                                offset == 28 ? oldFormat : oldBase;
                        const std::uint32_t newValue =
                                offset == 28 ? newFormat : newBase;
                        if (ReadU32(wrapped.fileBytes.constData() + offset) ==
                            oldValue) {
                            for (int byte = 0; byte < 4; ++byte) {
                                wrapped.fileBytes[offset + byte] =
                                        static_cast<char>((newValue >>
                                                           (8u * byte)) &
                                                          0xffu);
                            }
                        }
                    }
                }
                return wrapped;
            }
        }
    }

    DecodedNativeTexture decoded =
            DecodeNativeTexture(encoded, sourcePath, semantic, error);
    if (decoded.mipmaps.isEmpty()) return {};
    return PrepareNativeTexture(decoded, semantic, error);
}

PreparedNativeTexture PrepareNativeTexture(
        const DecodedNativeTexture &decoded,
        NativeTextureSemantic semantic, QString *error) {
    if (error != nullptr) error->clear();
    if (decoded.mipmaps.isEmpty() || decoded.mipmaps.front().isNull()) {
        if (error != nullptr) *error = QStringLiteral("texture has no pixels");
        return {};
    }
    const QSize size = decoded.mipmaps.front().size();
    if (!ValidTextureDimensions(size.width(), size.height())) {
        if (error != nullptr) {
            *error = QStringLiteral("texture exceeds the decoded pixel budget");
        }
        return {};
    }
    PreparedNativeTexture result;
    result.fileBytes = EncodePng(decoded.mipmaps.front(), semantic);
    if (result.fileBytes.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("PNG encoding failed");
        return {};
    }
    result.fileSuffix = QStringLiteral("png");
    result.size = size;
    result.sourceFormat = decoded.sourceFormat + QStringLiteral(" -> PNG");
    result.mipLevelCount = decoded.mipmaps.size();
    result.estimatedGpuBytes =
            static_cast<qint64>(size.width()) * size.height() * 4 * 4 / 3;
    result.generateMipmaps = true;
    result.hasTransparency =
            semantic == NativeTextureSemantic::OpaqueAlbedoSrgb
                    ? false
                    : decoded.hasTransparency;
    result.hasPartialTransparency =
            semantic == NativeTextureSemantic::OpaqueAlbedoSrgb
                    ? false
                    : decoded.hasPartialTransparency;
    return result;
}

DecodedNativeTexture ComposeNativeAlbedo(
        const DecodedNativeTexture &diffuse,
        const DecodedNativeTexture *blend2,
        const DecodedNativeTexture *blendMask,
        const DecodedNativeTexture *blend3) {
    DecodedNativeTexture result;
    result.sourceFormat = QStringLiteral("composited albedo");
    result.mipmaps.reserve(diffuse.mipmaps.size());
    for (int level = 0; level < diffuse.mipmaps.size(); ++level) {
        const QImage *baseMip = MipFor(&diffuse, level);
        if (baseMip == nullptr || baseMip->isNull()) continue;
        const QSize size = baseMip->size();
        QImage base = SizedRgba(baseMip, size, Qt::white);
        const QImage layer2 = SizedRgba(MipFor(blend2, level), size,
                                       Qt::transparent);
        const QImage mask = SizedRgba(MipFor(blendMask, level), size,
                                     Qt::transparent);
        const QImage layer3 = SizedRgba(MipFor(blend3, level), size,
                                       Qt::transparent);
        for (int y = 0; y < size.height(); ++y) {
            unsigned char *destination = base.scanLine(y);
            const unsigned char *second = layer2.constScanLine(y);
            const unsigned char *control = mask.constScanLine(y);
            const unsigned char *third = layer3.constScanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                const int offset = x * 4;
                const float blend2Amount = blend2 != nullptr &&
                                                   blendMask != nullptr
                                                   ? control[offset] / 255.0f
                                                   : 0.0f;
                const float blend3Amount = blend3 != nullptr
                                                   ? third[offset + 3] / 255.0f
                                                   : 0.0f;
                for (int channel = 0; channel < 3; ++channel) {
                    const float baseLinear =
                            SrgbToLinear(destination[offset + channel]);
                    const float secondLinear =
                            SrgbToLinear(second[offset + channel]);
                    const float thirdLinear =
                            SrgbToLinear(third[offset + channel]);
                    const float firstMix =
                            baseLinear + (secondLinear - baseLinear) *
                                                 blend2Amount;
                    destination[offset + channel] = LinearToSrgb(
                            firstMix + (thirdLinear - firstMix) *
                                               blend3Amount);
                }
                const float baseAlpha = destination[offset + 3] / 255.0f;
                const float secondAlpha = second[offset + 3] / 255.0f;
                const float firstAlpha =
                        baseAlpha + (secondAlpha - baseAlpha) * blend2Amount;
                const float finalAlpha =
                        firstAlpha + (third[offset + 3] / 255.0f - firstAlpha) *
                                             blend3Amount;
                destination[offset + 3] = static_cast<unsigned char>(
                        std::clamp(finalAlpha * 255.0f + 0.5f,
                                   0.0f, 255.0f));
                result.hasTransparency |= destination[offset + 3] != 255u;
                result.hasPartialTransparency |=
                        destination[offset + 3] != 0u &&
                        destination[offset + 3] != 255u;
            }
        }
        result.mipmaps.push_back(std::move(base));
    }
    return result;
}

CachedNativeTexture StoreNativeTexture(
        const PreparedNativeTexture &prepared,
        const QByteArray &sourceBytes,
        const QString &packIdentity,
        const QString &sourceIdentity,
        NativeTextureSemantic semantic,
        const QString &cacheRoot) {
    CachedNativeTexture result;
    result.size = prepared.size;
    result.sourceFormat = prepared.sourceFormat;
    result.mipLevelCount = prepared.mipLevelCount;
    result.sourceBytes = sourceBytes.size();
    result.estimatedGpuBytes = prepared.estimatedGpuBytes;
    result.generateMipmaps = prepared.generateMipmaps;
    result.hasTransparency = prepared.hasTransparency;
    result.hasPartialTransparency = prepared.hasPartialTransparency;
    if (prepared.fileBytes.isEmpty() || prepared.fileSuffix.isEmpty() ||
        prepared.size.isEmpty()) {
        result.error = QStringLiteral("prepared texture is empty");
        return result;
    }

    QByteArray identity(CacheVersion, static_cast<qsizetype>(sizeof(CacheVersion) - 1));
    identity.append(static_cast<char>(semantic));
    identity.append(sourceBytes.isEmpty() ? prepared.fileBytes : sourceBytes);
    result.cacheKey = HashHex(identity);
    const QString packKey = HashHex(packIdentity.toUtf8()).left(24);
    const QString root = TextureCacheRoot(cacheRoot);
    const QString directory = QDir(root).filePath(packKey);
    if (!QDir().mkpath(directory)) {
        result.error = QStringLiteral("creating texture cache directory failed");
        return result;
    }
    const QString path = QDir(directory).filePath(
            result.cacheKey + QLatin1Char('.') + prepared.fileSuffix);
    const QFileInfo existing(path);
    if (existing.isFile() && existing.size() == prepared.fileBytes.size()) {
        result.cacheHit = true;
    } else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(prepared.fileBytes) != prepared.fileBytes.size() ||
            !file.commit()) {
            result.error = QStringLiteral("writing texture cache entry failed for %1")
                                   .arg(sourceIdentity);
            return result;
        }
    }
    QFile touch(path);
    if (touch.open(QIODevice::ReadOnly)) {
        touch.setFileTime(QDateTime::currentDateTimeUtc(),
                          QFileDevice::FileModificationTime);
    }
    result.source = QUrl::fromLocalFile(path);
    return result;
}

void PruneNativeTextureCache(const QString &cacheRoot) {
    const QString root = TextureCacheRoot(cacheRoot);
    if (!QFileInfo(root).isDir()) return;
    PruneTextureCache(root, QString{});
}

}  // namespace forevertas::viewer
