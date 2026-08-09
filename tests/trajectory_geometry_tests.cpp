#include "viewer/trajectory_geometry.h"

#include "viewer/race_geometry.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QQuick3DGeometry>
#include <QVector3D>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using forevertas::viewer::BuildTrajectoryTubeMesh;
using forevertas::viewer::RaceGeometry;
using forevertas::viewer::SetTrajectoryMesh;
using forevertas::viewer::TrajectoryMeshData;
using forevertas::viewer::TrajectoryTubeSideCount;
using forevertas::viewer::TrajectoryVertexData;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

std::vector<TrajectoryVertexData> DecodeVertices(const QByteArray &bytes) {
    if (bytes.size() %
                static_cast<qsizetype>(sizeof(TrajectoryVertexData)) !=
        0) {
        return {};
    }
    const std::size_t count = static_cast<std::size_t>(
            bytes.size() /
            static_cast<qsizetype>(sizeof(TrajectoryVertexData)));
    std::vector<TrajectoryVertexData> result(count);
    if (!result.empty()) {
        std::memcpy(result.data(), bytes.constData(),
                    static_cast<std::size_t>(bytes.size()));
    }
    return result;
}

std::vector<std::uint32_t> DecodeIndices(const QByteArray &bytes) {
    if (bytes.size() % static_cast<qsizetype>(sizeof(std::uint32_t)) != 0) {
        return {};
    }
    const std::size_t count = static_cast<std::size_t>(
            bytes.size() /
            static_cast<qsizetype>(sizeof(std::uint32_t)));
    std::vector<std::uint32_t> result(count);
    if (!result.empty()) {
        std::memcpy(result.data(), bytes.constData(),
                    static_cast<std::size_t>(bytes.size()));
    }
    return result;
}

QVector3D Position(const TrajectoryVertexData &vertex) {
    return {vertex.positionX, vertex.positionY, vertex.positionZ};
}

QVector3D RingCenter(const std::vector<TrajectoryVertexData> &vertices,
                     std::size_t ring) {
    QVector3D center;
    const std::size_t first = ring * TrajectoryTubeSideCount;
    for (std::size_t side = 0u; side < TrajectoryTubeSideCount; ++side) {
        center += Position(vertices[first + side]);
    }
    return center / static_cast<float>(TrajectoryTubeSideCount);
}

bool TestConnectedRingsAndEndpointCaps() {
    const std::vector<QVector3D> positions{
            {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 3.0f},
            {2.0f, 0.2f, 5.0f},
            {4.0f, 1.0f, 6.0f}};
    const TrajectoryMeshData mesh =
            BuildTrajectoryTubeMesh(positions, 0.2f);
    const std::vector<TrajectoryVertexData> vertices =
            DecodeVertices(mesh.vertices);
    const std::vector<std::uint32_t> indices =
            DecodeIndices(mesh.indices);
    const std::size_t ringVertexCount =
            positions.size() * TrajectoryTubeSideCount;
    const std::size_t expectedIndexCount =
            ((positions.size() - 1u) * TrajectoryTubeSideCount * 6u) +
            (TrajectoryTubeSideCount * 6u);

    bool okay = Check(mesh.ringCount == positions.size(),
                      "trajectory did not retain one ring per path point");
    okay &= Check(vertices.size() == ringVertexCount + 2u,
                  "trajectory rings did not share indexed vertices");
    okay &= Check(indices.size() == expectedIndexCount,
                  "trajectory emitted an unexpected triangle count");
    if (vertices.size() != ringVertexCount + 2u ||
        indices.size() != expectedIndexCount) {
        return false;
    }

    const std::uint32_t startCenter =
            static_cast<std::uint32_t>(ringVertexCount);
    const std::uint32_t endCenter = startCenter + 1u;
    std::size_t startCapTriangles = 0u;
    std::size_t endCapTriangles = 0u;
    bool internalCap = false;
    std::vector<std::array<bool, 2u>> internalConnectivity(
            ringVertexCount, {false, false});
    for (std::size_t triangle = 0u; triangle < indices.size(); triangle += 3u) {
        const std::array<std::uint32_t, 3u> corners{
                indices[triangle], indices[triangle + 1u],
                indices[triangle + 2u]};
        bool hasStartCenter = false;
        bool hasEndCenter = false;
        for (const std::uint32_t corner : corners) {
            okay &= Check(corner < vertices.size(),
                          "trajectory index exceeded the vertex buffer");
            hasStartCenter |= corner == startCenter;
            hasEndCenter |= corner == endCenter;
        }
        startCapTriangles += hasStartCenter ? 1u : 0u;
        endCapTriangles += hasEndCenter ? 1u : 0u;

        if (!hasStartCenter && !hasEndCenter) {
            const std::array<std::size_t, 3u> rings{
                    static_cast<std::size_t>(corners[0]) /
                            TrajectoryTubeSideCount,
                    static_cast<std::size_t>(corners[1]) /
                            TrajectoryTubeSideCount,
                    static_cast<std::size_t>(corners[2]) /
                            TrajectoryTubeSideCount};
            internalCap |= rings[0] == rings[1] && rings[1] == rings[2] &&
                    rings[0] > 0u && rings[0] + 1u < positions.size();
            for (const std::uint32_t corner : corners) {
                const std::size_t vertex = static_cast<std::size_t>(corner);
                const std::size_t ring = vertex / TrajectoryTubeSideCount;
                if (ring == 0u || ring + 1u >= positions.size()) {
                    continue;
                }
                for (const std::size_t otherRing : rings) {
                    internalConnectivity[vertex][0] |= otherRing + 1u == ring;
                    internalConnectivity[vertex][1] |= otherRing == ring + 1u;
                }
            }
        }
    }
    okay &= Check(startCapTriangles == TrajectoryTubeSideCount &&
                          endCapTriangles == TrajectoryTubeSideCount,
                  "trajectory caps were not limited to the two endpoints");
    okay &= Check(!internalCap,
                  "trajectory retained a coplanar cap at an internal join");
    for (std::size_t ring = 1u; ring + 1u < positions.size(); ++ring) {
        for (std::size_t side = 0u; side < TrajectoryTubeSideCount; ++side) {
            const std::array<bool, 2u> connected =
                    internalConnectivity[ring * TrajectoryTubeSideCount + side];
            okay &= Check(connected[0] && connected[1],
                          "an internal ring vertex was not shared by both joins");
        }
    }
    return okay;
}

QVector3D DirectionWithVerticalDot(float vertical, float azimuth) {
    const float horizontal = std::sqrt(1.0f - vertical * vertical);
    return {horizontal * std::cos(azimuth), vertical,
            horizontal * std::sin(azimuth)};
}

bool TestStableFrameAcrossOldAxisThreshold() {
    constexpr float Radius = 0.15f;
    const QVector3D below = DirectionWithVerticalDot(0.899f, 0.43f);
    const QVector3D above = DirectionWithVerticalDot(0.901f, 0.43f);
    const TrajectoryMeshData belowMesh =
            BuildTrajectoryTubeMesh({QVector3D(), below * 5.0f}, Radius);
    const TrajectoryMeshData aboveMesh =
            BuildTrajectoryTubeMesh({QVector3D(), above * 5.0f}, Radius);
    const std::vector<TrajectoryVertexData> belowVertices =
            DecodeVertices(belowMesh.vertices);
    const std::vector<TrajectoryVertexData> aboveVertices =
            DecodeVertices(aboveMesh.vertices);
    bool okay = Check(belowVertices.size() ==
                                  2u * TrajectoryTubeSideCount + 2u &&
                              aboveVertices.size() ==
                                  2u * TrajectoryTubeSideCount + 2u,
                      "threshold trajectory vertices could not be decoded");
    if (!okay) {
        return false;
    }
    for (std::size_t side = 0u; side < TrajectoryTubeSideCount; ++side) {
        const QVector3D belowOffset = Position(belowVertices[side]).normalized();
        const QVector3D aboveOffset = Position(aboveVertices[side]).normalized();
        okay &= Check(QVector3D::dotProduct(belowOffset, aboveOffset) > 0.999f,
                      "trajectory frame jumped at the former 0.9 axis cutoff");
    }

    const QVector3D turn = DirectionWithVerticalDot(0.92f, 0.47f);
    const std::vector<QVector3D> crossingPath{
            QVector3D(), below * 5.0f, below * 5.0f + turn * 5.0f};
    const TrajectoryMeshData crossingMesh =
            BuildTrajectoryTubeMesh(crossingPath, Radius);
    const std::vector<TrajectoryVertexData> crossingVertices =
            DecodeVertices(crossingMesh.vertices);
    okay &= Check(crossingMesh.ringCount == 3u &&
                          crossingVertices.size() ==
                                  3u * TrajectoryTubeSideCount + 2u,
                  "transported trajectory rings were incomplete");
    if (crossingVertices.size() ==
        3u * TrajectoryTubeSideCount + 2u) {
        const QVector3D firstCenter = RingCenter(crossingVertices, 0u);
        const QVector3D joinCenter = RingCenter(crossingVertices, 1u);
        const QVector3D firstSide =
                (Position(crossingVertices[0u]) - firstCenter).normalized();
        const QVector3D joinSide =
                (Position(crossingVertices[TrajectoryTubeSideCount]) -
                 joinCenter)
                        .normalized();
        okay &= Check(QVector3D::dotProduct(firstSide, joinSide) > 0.995f,
                      "parallel transport flipped the frame at a join");
    }
    return okay;
}

bool TestDuplicateSamplesProduceOneMarkerOrRing() {
    const QVector3D point(3.0f, 4.0f, 5.0f);
    const TrajectoryMeshData stationary =
            BuildTrajectoryTubeMesh({point, point, point}, 0.1f);
    const std::vector<TrajectoryVertexData> vertices =
            DecodeVertices(stationary.vertices);
    const std::vector<std::uint32_t> indices =
            DecodeIndices(stationary.indices);
    return Check(!stationary.empty() && stationary.ringCount == 0u &&
                         vertices.size() == 6u && indices.size() == 24u,
                 "stationary trajectory did not produce one compact marker");
}

bool TestQuick3DGeometryContract() {
    TrajectoryMeshData mesh = BuildTrajectoryTubeMesh(
            {QVector3D(), QVector3D(0.0f, 0.0f, 2.0f)}, 0.1f);
    const int indexCount = static_cast<int>(
            mesh.indices.size() /
            static_cast<qsizetype>(sizeof(std::uint32_t)));
    const qsizetype vertexBytes = mesh.vertices.size();
    const qsizetype indexBytes = mesh.indices.size();
    RaceGeometry geometry;
    SetTrajectoryMesh(geometry, std::move(mesh));
    bool okay = Check(
            geometry.primitiveType() ==
                    QQuick3DGeometry::PrimitiveType::Triangles &&
                    geometry.stride() ==
                            static_cast<int>(sizeof(TrajectoryVertexData)),
            "trajectory Quick3D primitive or stride was incorrect");
    okay &= Check(geometry.vertexData().size() == vertexBytes &&
                          geometry.indexData().size() == indexBytes,
                  "trajectory Quick3D buffers were incomplete");
    okay &= Check(
            geometry.attributeCount() == 2 &&
                    geometry.attribute(0).semantic ==
                            QQuick3DGeometry::Attribute::IndexSemantic &&
                    geometry.attribute(1).semantic ==
                            QQuick3DGeometry::Attribute::PositionSemantic,
            "trajectory Quick3D attributes were incorrect");
    okay &= Check(geometry.subsetCount() == 1 &&
                          geometry.subsetOffset(0) == 0 &&
                          geometry.subsetCount(0) == indexCount,
                  "trajectory Quick3D subset did not cover every index");
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);

    int failures = 0;
    const auto expect = [&failures](bool condition, const char *message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };
    expect(TestConnectedRingsAndEndpointCaps(),
           "connected trajectory topology was incorrect");
    expect(TestStableFrameAcrossOldAxisThreshold(),
           "trajectory frame transport was unstable");
    expect(TestDuplicateSamplesProduceOneMarkerOrRing(),
           "stationary trajectory marker was incorrect");
    expect(TestQuick3DGeometryContract(),
           "trajectory Quick3D geometry was incorrect");

    if (failures == 0) {
        std::cout << "trajectory geometry tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
