#ifndef FOREVERTAS_VIEWER_SKIDMARK_GEOMETRY_H
#define FOREVERTAS_VIEWER_SKIDMARK_GEOMETRY_H

#include <QtQuick3D/qquick3dgeometry.h>

#include <QByteArray>
#include <QQuaternion>
#include <QVector3D>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forevertas::viewer {

struct SkidmarkWheelSample {
    QVector3D contactPoint{};
    QVector3D contactNormal{0.0f, 1.0f, 0.0f};
    bool contact = false;
    bool sliding = false;
    std::uint16_t surface = 0xffffu;
};

struct SkidmarkSample {
    std::int64_t timeMs = 0;
    std::uint32_t respawnCount = 0u;
    QQuaternion carRotation{};
    std::array<SkidmarkWheelSample, 4u> wheels{};
};

struct SkidmarkVertexData {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float uvX = 0.0f;
    float uvY = 0.0f;
    float birthTimeSeconds = 0.0f;
    float surfaceClass = 0.0f;
    float colorR = 0.0f;
    float colorG = 0.0f;
    float colorB = 0.0f;
    float colorA = 0.0f;
};

struct SkidmarkMeshData {
    QByteArray vertices;
    QByteArray indices;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
    std::size_t ribbonSegmentCount = 0u;
    std::size_t stampCount = 0u;

    bool empty() const noexcept {
        return vertices.isEmpty() || indices.isEmpty();
    }
};

SkidmarkMeshData BuildSkidmarkMesh(const std::vector<SkidmarkSample> &samples);

class SkidmarkGeometry final : public QQuick3DGeometry {
    Q_OBJECT

public:
    SkidmarkGeometry();

    void setSkidmarkMesh(SkidmarkMeshData mesh);
    void clearMesh();
    std::size_t ribbonSegmentCount() const noexcept;
    std::size_t stampCount() const noexcept;

private:
    std::size_t ribbonSegmentCount_ = 0u;
    std::size_t stampCount_ = 0u;
};

}  // namespace forevertas::viewer

#endif
