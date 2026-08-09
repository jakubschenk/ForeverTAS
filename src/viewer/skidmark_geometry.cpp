#include "viewer/skidmark_geometry.h"

#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>

namespace forevertas::viewer {
namespace {

static_assert(sizeof(SkidmarkVertexData) == 11u * sizeof(float));

// Contact positions sit on the collision plane while authored road visuals can
// be a few millimetres above it. A 12 mm normal offset is still visually flush
// at driving scale but survives that collision/render height disagreement.
constexpr float kSurfaceOffset = 0.012f;
constexpr float kMinimumTireWidth = 0.075f;
constexpr float kMaximumTireWidth = 0.16f;
constexpr float kTreadRepeatMetres = 0.24f;
constexpr float kMinimumSegmentLengthSquared = 0.002f * 0.002f;
constexpr std::int64_t kMaximumContinuousGapMs = 100;

struct SurfaceStyle {
    float surfaceClass = 0.0f;
    std::array<float, 4u> color{};
};

std::optional<SurfaceStyle> StyleForSurface(std::uint16_t surface) {
    // Water, ice, ball/wall collision helpers, and non-collidable geometry do
    // not retain a rubber trail. Terrain gets a subdued disturbed-surface
    // tint while manufactured surfaces receive the darker tyre treatment.
    switch (surface) {
    case 3u:
    case 13u:
    case 23u:
    case 24u:
    case 28u:
    case 0xffffu:
        return std::nullopt;
    case 2u:
    case 20u:
        return SurfaceStyle{1.0f, {0.10f, 0.075f, 0.035f, 0.20f}};
    case 5u:
        return SurfaceStyle{1.0f, {0.22f, 0.15f, 0.070f, 0.18f}};
    case 6u:
    case 8u:
    case 17u:
        return SurfaceStyle{1.0f, {0.095f, 0.055f, 0.025f, 0.28f}};
    case 21u:
        return SurfaceStyle{1.0f, {0.35f, 0.34f, 0.32f, 0.18f}};
    default:
        return SurfaceStyle{0.0f, {0.028f, 0.024f, 0.022f, 0.48f}};
    }
}

bool Finite(const QVector3D &value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) &&
           std::isfinite(value.z());
}

QQuaternion NormalizedRotation(const QQuaternion &rotation) {
    if (!std::isfinite(rotation.scalar()) || !std::isfinite(rotation.x()) ||
        !std::isfinite(rotation.y()) || !std::isfinite(rotation.z()) ||
        rotation.isNull()) {
        return {};
    }
    return rotation.normalized();
}

QVector3D UnitAxis(const QQuaternion &rotation, const QVector3D &axis,
                   const QVector3D &fallback) {
    QVector3D result = NormalizedRotation(rotation).rotatedVector(axis);
    if (!Finite(result) || result.lengthSquared() < 0.000001f) {
        return fallback;
    }
    return result.normalized();
}

bool NormalizeFinite(QVector3D &value) {
    if (!Finite(value) || value.lengthSquared() < 0.000001f) {
        return false;
    }
    value.normalize();
    return Finite(value);
}

QVector3D ProjectOntoPlane(const QVector3D &value,
                           const QVector3D &normal) {
    return value - normal * QVector3D::dotProduct(value, normal);
}

QVector3D SurfaceNormal(const SkidmarkSample &sample, std::size_t wheel) {
    QVector3D normal = sample.wheels[wheel].contactNormal;
    if (NormalizeFinite(normal)) {
        return normal;
    }
    return UnitAxis(sample.carRotation, {0.0f, 1.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f});
}

struct ContactBasis {
    QVector3D normal;
    QVector3D lateral;
    QVector3D forward;
};

ContactBasis BuildContactBasis(const SkidmarkSample &sample,
                               std::size_t wheel) {
    ContactBasis basis;
    basis.normal = SurfaceNormal(sample, wheel);
    const QVector3D carLateral = UnitAxis(
            sample.carRotation, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    const QVector3D carForward = UnitAxis(
            sample.carRotation, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});

    basis.lateral = ProjectOntoPlane(carLateral, basis.normal);
    QVector3D projectedForward =
            ProjectOntoPlane(carForward, basis.normal);
    if (!NormalizeFinite(basis.lateral)) {
        basis.lateral =
                QVector3D::crossProduct(basis.normal, projectedForward);
    }
    if (!NormalizeFinite(basis.lateral)) {
        const QVector3D reference =
                std::fabs(QVector3D::dotProduct(
                                  basis.normal,
                                  QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f
                ? QVector3D(0.0f, 1.0f, 0.0f)
                : QVector3D(1.0f, 0.0f, 0.0f);
        basis.lateral = QVector3D::crossProduct(basis.normal, reference);
        NormalizeFinite(basis.lateral);
    }

    basis.forward = projectedForward -
            basis.lateral *
                    QVector3D::dotProduct(projectedForward, basis.lateral);
    if (!NormalizeFinite(basis.forward)) {
        basis.forward =
                QVector3D::crossProduct(basis.lateral, basis.normal);
        NormalizeFinite(basis.forward);
    }
    if (QVector3D::dotProduct(basis.forward, carForward) < 0.0f) {
        basis.forward = -basis.forward;
    }
    return basis;
}

float TireWidth(const SkidmarkSample &sample, std::size_t wheel) {
    const std::size_t peer = wheel == 0u   ? 1u
                             : wheel == 1u ? 0u
                             : wheel == 2u ? 3u
                                           : 2u;
    QVector3D separation;
    if (sample.wheels[peer].contact &&
        Finite(sample.wheels[peer].contactPoint) &&
        Finite(sample.wheels[wheel].contactPoint)) {
        separation = sample.wheels[peer].contactPoint -
                sample.wheels[wheel].contactPoint;
        separation = ProjectOntoPlane(separation,
                                      SurfaceNormal(sample, wheel));
    }
    const float candidate = Finite(separation) &&
                    separation.lengthSquared() > 0.000001f
            ? separation.length() * 0.07f
            : 0.11f;
    return std::clamp(candidate, kMinimumTireWidth, kMaximumTireWidth);
}

float TimeSeconds(std::int64_t timeMs) {
    return static_cast<float>(
            static_cast<double>(std::max<std::int64_t>(0, timeMs)) / 1000.0);
}

class MeshBuilder final {
public:
    void AddStamp(const SkidmarkSample &sample, std::size_t wheel,
                  const SurfaceStyle &style) {
        const float halfWidth = TireWidth(sample, wheel) * 0.5f;
        const float halfLength = std::max(0.045f, halfWidth * 0.75f);
        const ContactBasis basis = BuildContactBasis(sample, wheel);
        const QVector3D center =
                sample.wheels[wheel].contactPoint +
                basis.normal * kSurfaceOffset;
        const float time = TimeSeconds(sample.timeMs);
        AddQuad(center - basis.lateral * halfWidth -
                        basis.forward * halfLength,
                center + basis.lateral * halfWidth -
                        basis.forward * halfLength,
                center + basis.lateral * halfWidth +
                        basis.forward * halfLength,
                center - basis.lateral * halfWidth +
                        basis.forward * halfLength,
                0.0f, 0.5f, time, time, style, style);
        ++stampCount_;
    }

    void AddRibbon(const SkidmarkSample &from, const SkidmarkSample &to,
                   std::size_t wheel, float uvStart, float uvEnd,
                   const SurfaceStyle &style) {
        const ContactBasis fromBasis = BuildContactBasis(from, wheel);
        const ContactBasis toBasis = BuildContactBasis(to, wheel);
        QVector3D fromLateral = fromBasis.lateral;
        QVector3D toLateral = toBasis.lateral;
        if (QVector3D::dotProduct(fromLateral, toLateral) < 0.0f) {
            toLateral = -toLateral;
        }
        const QVector3D start =
                from.wheels[wheel].contactPoint +
                fromBasis.normal * kSurfaceOffset;
        const QVector3D end =
                to.wheels[wheel].contactPoint +
                toBasis.normal * kSurfaceOffset;
        const float fromHalfWidth = TireWidth(from, wheel) * 0.5f;
        const float toHalfWidth = TireWidth(to, wheel) * 0.5f;
        AddQuad(start - fromLateral * fromHalfWidth,
                start + fromLateral * fromHalfWidth,
                end + toLateral * toHalfWidth, end - toLateral * toHalfWidth,
                uvStart, uvEnd, TimeSeconds(from.timeMs),
                TimeSeconds(to.timeMs), style, style);
        ++ribbonSegmentCount_;
    }

    SkidmarkMeshData Finish() {
        SkidmarkMeshData result;
        if (vertices_.empty() || indices_.empty()) {
            return result;
        }
        if (vertices_.size() > static_cast<std::size_t>(
                                       std::numeric_limits<qsizetype>::max()) /
                                       sizeof(SkidmarkVertexData) ||
            indices_.size() > static_cast<std::size_t>(
                                      std::numeric_limits<qsizetype>::max()) /
                                      sizeof(std::uint32_t)) {
            throw std::length_error("skidmark geometry is too large");
        }
        result.vertices.resize(static_cast<qsizetype>(
                vertices_.size() * sizeof(SkidmarkVertexData)));
        result.indices.resize(static_cast<qsizetype>(indices_.size() *
                                                     sizeof(std::uint32_t)));
        std::memcpy(result.vertices.data(), vertices_.data(),
                    static_cast<std::size_t>(result.vertices.size()));
        std::memcpy(result.indices.data(), indices_.data(),
                    static_cast<std::size_t>(result.indices.size()));
        result.boundsMin = boundsMin_;
        result.boundsMax = boundsMax_;
        result.ribbonSegmentCount = ribbonSegmentCount_;
        result.stampCount = stampCount_;
        return result;
    }

private:
    void AddQuad(const QVector3D &a, const QVector3D &b, const QVector3D &c,
                 const QVector3D &d, float uvStart, float uvEnd,
                 float startTime, float endTime, const SurfaceStyle &startStyle,
                 const SurfaceStyle &endStyle) {
        if (vertices_.size() >
            static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()) -
                    4u) {
            throw std::length_error("skidmark vertex index overflow");
        }
        const std::uint32_t first =
                static_cast<std::uint32_t>(vertices_.size());
        AddVertex(a, 0.0f, uvStart, startTime, startStyle);
        AddVertex(b, 1.0f, uvStart, startTime, startStyle);
        AddVertex(c, 1.0f, uvEnd, endTime, endStyle);
        AddVertex(d, 0.0f, uvEnd, endTime, endStyle);
        indices_.insert(indices_.end(), {first, first + 1u, first + 2u, first,
                                         first + 2u, first + 3u});
    }

    void AddVertex(const QVector3D &position, float uvX, float uvY,
                   float birthTime, const SurfaceStyle &style) {
        vertices_.push_back({position.x(), position.y(), position.z(), uvX, uvY,
                             birthTime, style.surfaceClass, style.color[0],
                             style.color[1], style.color[2], style.color[3]});
        if (!boundsInitialized_) {
            boundsMin_ = position;
            boundsMax_ = position;
            boundsInitialized_ = true;
        } else {
            boundsMin_.setX(std::min(boundsMin_.x(), position.x()));
            boundsMin_.setY(std::min(boundsMin_.y(), position.y()));
            boundsMin_.setZ(std::min(boundsMin_.z(), position.z()));
            boundsMax_.setX(std::max(boundsMax_.x(), position.x()));
            boundsMax_.setY(std::max(boundsMax_.y(), position.y()));
            boundsMax_.setZ(std::max(boundsMax_.z(), position.z()));
        }
    }

    std::vector<SkidmarkVertexData> vertices_;
    std::vector<std::uint32_t> indices_;
    QVector3D boundsMin_{};
    QVector3D boundsMax_{};
    std::size_t ribbonSegmentCount_ = 0u;
    std::size_t stampCount_ = 0u;
    bool boundsInitialized_ = false;
};

bool Active(const SkidmarkSample &sample, std::size_t wheel) {
    const SkidmarkWheelSample &value = sample.wheels[wheel];
    return value.contact && value.sliding && Finite(value.contactPoint) &&
           StyleForSurface(value.surface).has_value();
}

bool Continuous(const SkidmarkSample &from, const SkidmarkSample &to,
                std::size_t wheel) {
    if (!Active(from, wheel) || !Active(to, wheel) ||
        from.respawnCount != to.respawnCount ||
        from.wheels[wheel].surface != to.wheels[wheel].surface) {
        return false;
    }
    const std::int64_t elapsedMs = to.timeMs - from.timeMs;
    if (elapsedMs <= 0 || elapsedMs > kMaximumContinuousGapMs) {
        return false;
    }
    const QVector3D delta =
            to.wheels[wheel].contactPoint - from.wheels[wheel].contactPoint;
    const float maximumDistance =
            std::max(10.0f, static_cast<float>(elapsedMs) * 1.0f);
    if (delta.lengthSquared() > maximumDistance * maximumDistance) {
        return false;
    }
    const QQuaternion first = NormalizedRotation(from.carRotation);
    const QQuaternion second = NormalizedRotation(to.carRotation);
    return std::fabs(QQuaternion::dotProduct(first, second)) >= 0.5f;
}

}  // namespace

SkidmarkMeshData BuildSkidmarkMesh(const std::vector<SkidmarkSample> &samples) {
    MeshBuilder builder;
    std::array<float, 4u> uvCursor{};
    for (std::size_t sampleIndex = 0u; sampleIndex < samples.size();
         ++sampleIndex) {
        const SkidmarkSample &current = samples[sampleIndex];
        for (std::size_t wheel = 0u; wheel < current.wheels.size(); ++wheel) {
            if (!Active(current, wheel)) {
                uvCursor[wheel] = 0.0f;
                continue;
            }
            const SurfaceStyle style =
                    *StyleForSurface(current.wheels[wheel].surface);
            const bool continuous =
                    sampleIndex > 0u &&
                    Continuous(samples[sampleIndex - 1u], current, wheel);
            if (!continuous) {
                uvCursor[wheel] = 0.0f;
                builder.AddStamp(current, wheel, style);
                continue;
            }
            const QVector3D delta =
                    current.wheels[wheel].contactPoint -
                    samples[sampleIndex - 1u].wheels[wheel].contactPoint;
            const float distanceSquared = delta.lengthSquared();
            if (distanceSquared < kMinimumSegmentLengthSquared) {
                continue;
            }
            const float nextUv = uvCursor[wheel] + std::sqrt(distanceSquared) /
                                                           kTreadRepeatMetres;
            builder.AddRibbon(samples[sampleIndex - 1u], current, wheel,
                              uvCursor[wheel], nextUv, style);
            uvCursor[wheel] = nextUv;
        }
    }
    return builder.Finish();
}

SkidmarkGeometry::SkidmarkGeometry() : QQuick3DGeometry(nullptr) {}

void SkidmarkGeometry::setSkidmarkMesh(SkidmarkMeshData mesh) {
    if (mesh.empty()) {
        clearMesh();
        return;
    }
    constexpr int PositionOffset = 0;
    constexpr int Uv0Offset = 3 * static_cast<int>(sizeof(float));
    constexpr int Uv1Offset = 5 * static_cast<int>(sizeof(float));
    constexpr int ColorOffset = 7 * static_cast<int>(sizeof(float));

    const qsizetype indexCount =
            mesh.indices.size() / static_cast<qsizetype>(sizeof(std::uint32_t));
    if (indexCount > static_cast<qsizetype>(std::numeric_limits<int>::max())) {
        throw std::length_error("skidmark index count is too large");
    }

    clear();
    setPrimitiveType(PrimitiveType::Triangles);
    setStride(static_cast<int>(sizeof(SkidmarkVertexData)));
    setVertexData(mesh.vertices);
    setIndexData(mesh.indices);
    setBounds(mesh.boundsMin, mesh.boundsMax);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
    addAttribute(Attribute::PositionSemantic, PositionOffset,
                 Attribute::F32Type);
    addAttribute(Attribute::TexCoord0Semantic, Uv0Offset, Attribute::F32Type);
    addAttribute(Attribute::TexCoord1Semantic, Uv1Offset, Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, ColorOffset, Attribute::F32Type);
    addSubset(0, static_cast<int>(indexCount), mesh.boundsMin, mesh.boundsMax,
              QStringLiteral("skidmarks"));
    ribbonSegmentCount_ = mesh.ribbonSegmentCount;
    stampCount_ = mesh.stampCount;
    update();
}

void SkidmarkGeometry::clearMesh() {
    clear();
    setVertexData({});
    setIndexData({});
    setBounds({}, {});
    ribbonSegmentCount_ = 0u;
    stampCount_ = 0u;
    update();
}

std::size_t SkidmarkGeometry::ribbonSegmentCount() const noexcept {
    return ribbonSegmentCount_;
}

std::size_t SkidmarkGeometry::stampCount() const noexcept {
    return stampCount_;
}

}  // namespace forevertas::viewer
