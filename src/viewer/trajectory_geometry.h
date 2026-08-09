#ifndef FOREVERTAS_VIEWER_TRAJECTORY_GEOMETRY_H
#define FOREVERTAS_VIEWER_TRAJECTORY_GEOMETRY_H

#include <QByteArray>
#include <QVector3D>

#include <cstddef>
#include <vector>

namespace forevertas::viewer {

class RaceGeometry;

constexpr std::size_t TrajectoryTubeSideCount = 6u;

struct TrajectoryVertexData {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
};

struct TrajectoryMeshData {
    QByteArray vertices;
    QByteArray indices;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
    std::size_t ringCount = 0u;

    bool empty() const noexcept {
        return vertices.isEmpty() || indices.isEmpty();
    }
};

TrajectoryMeshData BuildTrajectoryTubeMesh(
        const std::vector<QVector3D> &positions,
        float radius);
void SetTrajectoryMesh(RaceGeometry &geometry, TrajectoryMeshData mesh);

}  // namespace forevertas::viewer

#endif
