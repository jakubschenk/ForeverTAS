#include "viewer/trajectory_geometry.h"

#include "viewer/race_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace forevertas::viewer {

namespace {

constexpr float MinimumSegmentLengthSquared = 0.000001f;
constexpr float MinimumFrameLengthSquared = 0.00000001f;
constexpr float Pi = 3.14159265358979323846f;

bool IsFinite(const QVector3D &value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) &&
            std::isfinite(value.z());
}

void ExpandBounds(const QVector3D &point,
                  QVector3D &minimum,
                  QVector3D &maximum) {
    minimum.setX(std::min(minimum.x(), point.x()));
    minimum.setY(std::min(minimum.y(), point.y()));
    minimum.setZ(std::min(minimum.z(), point.z()));
    maximum.setX(std::max(maximum.x(), point.x()));
    maximum.setY(std::max(maximum.y(), point.y()));
    maximum.setZ(std::max(maximum.z(), point.z()));
}

QVector3D InitialSide(const QVector3D &tangent) {
    const QVector3D up(0.0f, 1.0f, 0.0f);
    QVector3D normal = up -
            tangent * QVector3D::dotProduct(up, tangent);
    if (normal.lengthSquared() < MinimumFrameLengthSquared) {
        const QVector3D fallback(1.0f, 0.0f, 0.0f);
        normal = fallback -
                tangent * QVector3D::dotProduct(fallback, tangent);
    }
    normal.normalize();
    return QVector3D::crossProduct(tangent, normal).normalized();
}

QVector3D RotateAroundAxis(const QVector3D &value,
                          const QVector3D &axis,
                          float cosine,
                          float sine) {
    return value * cosine +
            QVector3D::crossProduct(axis, value) * sine +
            axis * QVector3D::dotProduct(axis, value) * (1.0f - cosine);
}

QVector3D TransportSide(const QVector3D &previousSide,
                        const QVector3D &previousTangent,
                        const QVector3D &tangent) {
    const QVector3D rotationAxis =
            QVector3D::crossProduct(previousTangent, tangent);
    const float sine = rotationAxis.length();
    const float cosine = std::clamp(
            QVector3D::dotProduct(previousTangent, tangent), -1.0f, 1.0f);
    QVector3D side = previousSide;
    if (sine > 0.000001f) {
        side = RotateAroundAxis(
                previousSide, rotationAxis / sine, cosine, sine);
    }

    side -= tangent * QVector3D::dotProduct(side, tangent);
    if (side.lengthSquared() < MinimumFrameLengthSquared) {
        side = InitialSide(tangent);
    } else {
        side.normalize();
    }
    return side;
}

void StoreMesh(TrajectoryMeshData &mesh,
               const std::vector<TrajectoryVertexData> &vertices,
               const std::vector<std::uint32_t> &indices) {
    if (vertices.size() > static_cast<std::size_t>(
                std::numeric_limits<qsizetype>::max() /
                static_cast<qsizetype>(sizeof(TrajectoryVertexData))) ||
        indices.size() > static_cast<std::size_t>(
                std::numeric_limits<qsizetype>::max() /
                static_cast<qsizetype>(sizeof(std::uint32_t)))) {
        throw std::length_error("trajectory geometry is too large");
    }
    mesh.vertices.resize(static_cast<qsizetype>(
            vertices.size() * sizeof(TrajectoryVertexData)));
    mesh.indices.resize(static_cast<qsizetype>(
            indices.size() * sizeof(std::uint32_t)));
    std::memcpy(mesh.vertices.data(), vertices.data(),
                static_cast<std::size_t>(mesh.vertices.size()));
    std::memcpy(mesh.indices.data(), indices.data(),
                static_cast<std::size_t>(mesh.indices.size()));
}

TrajectoryMeshData BuildStationaryMarker(const QVector3D &center,
                                         float radius) {
    TrajectoryMeshData mesh;
    const std::array<QVector3D, 6u> points{
            center + QVector3D(radius, 0.0f, 0.0f),
            center + QVector3D(-radius, 0.0f, 0.0f),
            center + QVector3D(0.0f, radius, 0.0f),
            center + QVector3D(0.0f, -radius, 0.0f),
            center + QVector3D(0.0f, 0.0f, radius),
            center + QVector3D(0.0f, 0.0f, -radius)};
    std::vector<TrajectoryVertexData> vertices;
    vertices.reserve(points.size());
    mesh.boundsMin = points.front();
    mesh.boundsMax = points.front();
    for (const QVector3D &point : points) {
        vertices.push_back({point.x(), point.y(), point.z()});
        ExpandBounds(point, mesh.boundsMin, mesh.boundsMax);
    }
    const std::vector<std::uint32_t> indices{
            0u, 2u, 4u, 4u, 2u, 1u, 1u, 2u, 5u, 5u, 2u, 0u,
            0u, 4u, 3u, 4u, 1u, 3u, 1u, 5u, 3u, 5u, 0u, 3u};
    StoreMesh(mesh, vertices, indices);
    return mesh;
}

}  // namespace

TrajectoryMeshData BuildTrajectoryTubeMesh(
        const std::vector<QVector3D> &positions,
        float radius) {
    TrajectoryMeshData mesh;
    if (positions.empty() || !std::isfinite(radius) || radius <= 0.0f) {
        return mesh;
    }

    std::vector<QVector3D> centers;
    centers.reserve(positions.size());
    for (const QVector3D &position : positions) {
        if (!IsFinite(position)) {
            throw std::invalid_argument(
                    "trajectory contains a non-finite position");
        }
        if (centers.empty() ||
            (position - centers.back()).lengthSquared() >=
                    MinimumSegmentLengthSquared) {
            centers.push_back(position);
        }
    }
    if (centers.empty()) {
        return mesh;
    }
    if (centers.size() == 1u) {
        return BuildStationaryMarker(centers.front(), radius);
    }

    constexpr std::size_t IndicesPerRing =
            TrajectoryTubeSideCount * 6u;
    if (centers.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max()) / IndicesPerRing ||
        centers.size() >
                (static_cast<std::size_t>(
                         std::numeric_limits<std::uint32_t>::max()) -
                 2u) /
                        TrajectoryTubeSideCount) {
        throw std::length_error("trajectory geometry is too large");
    }

    std::vector<QVector3D> segmentTangents;
    segmentTangents.reserve(centers.size() - 1u);
    for (std::size_t index = 1u; index < centers.size(); ++index) {
        segmentTangents.push_back(
                (centers[index] - centers[index - 1u]).normalized());
    }

    std::vector<QVector3D> ringTangents(centers.size());
    ringTangents.front() = segmentTangents.front();
    ringTangents.back() = segmentTangents.back();
    for (std::size_t index = 1u; index + 1u < centers.size(); ++index) {
        const QVector3D combined =
                segmentTangents[index - 1u] + segmentTangents[index];
        ringTangents[index] =
                combined.lengthSquared() >= MinimumFrameLengthSquared
                ? combined.normalized()
                : segmentTangents[index];
    }

    std::vector<QVector3D> sides(centers.size());
    sides.front() = InitialSide(ringTangents.front());
    for (std::size_t index = 1u; index < centers.size(); ++index) {
        sides[index] = TransportSide(
                sides[index - 1u], ringTangents[index - 1u],
                ringTangents[index]);
    }

    const std::size_t ringVertexCount =
            centers.size() * TrajectoryTubeSideCount;
    std::vector<TrajectoryVertexData> vertices;
    vertices.reserve(ringVertexCount + 2u);
    for (std::size_t ring = 0u; ring < centers.size(); ++ring) {
        const QVector3D normal = QVector3D::crossProduct(
                sides[ring], ringTangents[ring]).normalized();
        for (std::size_t side = 0u; side < TrajectoryTubeSideCount; ++side) {
            const float angle = 2.0f * Pi * static_cast<float>(side) /
                    static_cast<float>(TrajectoryTubeSideCount);
            const QVector3D point = centers[ring] +
                    radius * (std::cos(angle) * sides[ring] +
                              std::sin(angle) * normal);
            vertices.push_back({point.x(), point.y(), point.z()});
            if (vertices.size() == 1u) {
                mesh.boundsMin = point;
                mesh.boundsMax = point;
            } else {
                ExpandBounds(point, mesh.boundsMin, mesh.boundsMax);
            }
        }
    }
    const std::uint32_t startCenter =
            static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({centers.front().x(), centers.front().y(),
                        centers.front().z()});
    const std::uint32_t endCenter =
            static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({centers.back().x(), centers.back().y(),
                        centers.back().z()});

    std::vector<std::uint32_t> indices;
    indices.reserve(centers.size() * IndicesPerRing);
    for (std::size_t ring = 1u; ring < centers.size(); ++ring) {
        const std::uint32_t previousBase = static_cast<std::uint32_t>(
                (ring - 1u) * TrajectoryTubeSideCount);
        const std::uint32_t base = static_cast<std::uint32_t>(
                ring * TrajectoryTubeSideCount);
        for (std::size_t side = 0u; side < TrajectoryTubeSideCount; ++side) {
            const std::uint32_t current = static_cast<std::uint32_t>(side);
            const std::uint32_t next = static_cast<std::uint32_t>(
                    (side + 1u) % TrajectoryTubeSideCount);
            indices.insert(indices.end(),
                           {previousBase + current,
                            previousBase + next,
                            base + next,
                            previousBase + current,
                            base + next,
                            base + current});
        }
    }
    for (std::size_t side = 0u; side < TrajectoryTubeSideCount; ++side) {
        const std::uint32_t current = static_cast<std::uint32_t>(side);
        const std::uint32_t next = static_cast<std::uint32_t>(
                (side + 1u) % TrajectoryTubeSideCount);
        const std::uint32_t endBase = static_cast<std::uint32_t>(
                (centers.size() - 1u) * TrajectoryTubeSideCount);
        indices.insert(indices.end(),
                       {startCenter, current, next,
                        endCenter, endBase + next, endBase + current});
    }

    mesh.ringCount = centers.size();
    StoreMesh(mesh, vertices, indices);
    return mesh;
}

void SetTrajectoryMesh(RaceGeometry &geometry, TrajectoryMeshData mesh) {
    const std::size_t indexCount = static_cast<std::size_t>(
            mesh.indices.size()) / sizeof(std::uint32_t);
    if (indexCount > static_cast<std::size_t>(
                std::numeric_limits<int>::max())) {
        throw std::length_error("trajectory geometry is too large");
    }
    geometry.setIndexedMesh(
            std::move(mesh.vertices), std::move(mesh.indices),
            static_cast<int>(sizeof(TrajectoryVertexData)),
            false, false, false, false, false,
            mesh.boundsMin, mesh.boundsMax,
            {{0, static_cast<int>(indexCount)}});
}

}  // namespace forevertas::viewer
