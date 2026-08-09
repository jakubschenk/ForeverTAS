#include "viewer/skidmark_geometry.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QQuick3DGeometry>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using forevertas::viewer::BuildSkidmarkMesh;
using forevertas::viewer::SkidmarkGeometry;
using forevertas::viewer::SkidmarkMeshData;
using forevertas::viewer::SkidmarkSample;
using forevertas::viewer::SkidmarkVertexData;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool Close(float first, float second, float epsilon = 0.0001f) {
    return std::fabs(first - second) <= epsilon;
}

SkidmarkSample MakeSample(std::int64_t timeMs, float travel,
                          std::uint16_t surface = 0u) {
    SkidmarkSample sample;
    sample.timeMs = timeMs;
    sample.carRotation = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    sample.wheels[0u].contactPoint = {-0.75f, 0.0f, travel + 1.2f};
    sample.wheels[1u].contactPoint = {0.75f, 0.0f, travel + 1.2f};
    sample.wheels[2u].contactPoint = {0.75f, 0.0f, travel - 1.2f};
    sample.wheels[3u].contactPoint = {-0.75f, 0.0f, travel - 1.2f};
    for (auto &wheel : sample.wheels) {
        wheel.surface = surface;
    }
    return sample;
}

void ActivateRearWheels(SkidmarkSample &sample) {
    for (std::size_t wheel : {2u, 3u}) {
        sample.wheels[wheel].contact = true;
        sample.wheels[wheel].sliding = true;
    }
}

void ActivateOneWheel(SkidmarkSample &sample) {
    sample.wheels[2u].contact = true;
    sample.wheels[2u].sliding = true;
}

std::vector<SkidmarkVertexData> DecodeVertices(const QByteArray &bytes) {
    if (bytes.size() % static_cast<qsizetype>(sizeof(SkidmarkVertexData)) !=
        0) {
        return {};
    }
    const qsizetype count =
            bytes.size() / static_cast<qsizetype>(sizeof(SkidmarkVertexData));
    std::vector<SkidmarkVertexData> result(static_cast<std::size_t>(count));
    if (!result.empty()) {
        std::memcpy(result.data(), bytes.constData(),
                    static_cast<std::size_t>(bytes.size()));
    }
    return result;
}

QVector3D Position(const SkidmarkVertexData &vertex) {
    return {vertex.positionX, vertex.positionY, vertex.positionZ};
}

bool TestContinuousRearWheelRibbons() {
    SkidmarkSample first = MakeSample(100, 0.0f);
    SkidmarkSample second = MakeSample(110, 0.5f);
    ActivateRearWheels(first);
    ActivateRearWheels(second);

    const SkidmarkMeshData mesh = BuildSkidmarkMesh({first, second});
    bool okay = Check(mesh.stampCount == 2u,
                      "slip starts did not create two wheel stamps");
    okay &= Check(mesh.ribbonSegmentCount == 2u,
                  "continuous slip did not create two wheel ribbons");
    okay &= Check(mesh.vertices.size() == 16 * static_cast<qsizetype>(sizeof(
                                                       SkidmarkVertexData)),
                  "continuous slip emitted an unexpected vertex count");
    okay &= Check(mesh.indices.size() ==
                          24 * static_cast<qsizetype>(sizeof(std::uint32_t)),
                  "continuous slip emitted an unexpected index count");
    okay &= Check(mesh.boundsMin.y() > 0.0f && mesh.boundsMax.y() < 0.02f,
                  "skidmarks were not offset just above the road");

    const std::vector<SkidmarkVertexData> vertices =
            DecodeVertices(mesh.vertices);
    okay &= Check(vertices.size() == 16u,
                  "skidmark vertices could not be decoded");
    if (vertices.size() == 16u) {
        okay &= Check(Close(vertices[0u].birthTimeSeconds, 0.1f) &&
                              Close(vertices[8u].birthTimeSeconds, 0.1f) &&
                              Close(vertices[10u].birthTimeSeconds, 0.11f),
                      "ribbon reveal times did not follow simulation time");
        okay &= Check(Close(vertices[0u].surfaceClass, 0.0f) &&
                              Close(vertices[0u].colorA, 0.48f),
                      "hard-surface skidmark style was incorrect");
        okay &= Check(vertices[10u].uvY > vertices[8u].uvY,
                      "ribbon tread UVs did not advance with distance");
    }
    return okay;
}

bool TestBreaksAndStationarySlip() {
    SkidmarkSample stationaryFirst = MakeSample(0, 0.0f);
    SkidmarkSample stationarySecond = MakeSample(10, 0.0f);
    ActivateOneWheel(stationaryFirst);
    ActivateOneWheel(stationarySecond);
    const SkidmarkMeshData stationary =
            BuildSkidmarkMesh({stationaryFirst, stationarySecond});
    bool okay = Check(stationary.stampCount == 1u &&
                              stationary.ribbonSegmentCount == 0u,
                      "stationary burnout did not retain exactly one stamp");

    SkidmarkSample jumpFirst = MakeSample(0, 0.0f);
    SkidmarkSample jumpSecond = MakeSample(10, 20.0f);
    ActivateOneWheel(jumpFirst);
    ActivateOneWheel(jumpSecond);
    const SkidmarkMeshData jump = BuildSkidmarkMesh({jumpFirst, jumpSecond});
    okay &= Check(jump.stampCount == 2u && jump.ribbonSegmentCount == 0u,
                  "teleport discontinuity produced a connecting ribbon");

    SkidmarkSample gapFirst = MakeSample(0, 0.0f);
    SkidmarkSample gapSecond = MakeSample(110, 0.5f);
    ActivateOneWheel(gapFirst);
    ActivateOneWheel(gapSecond);
    const SkidmarkMeshData gap = BuildSkidmarkMesh({gapFirst, gapSecond});
    okay &= Check(gap.stampCount == 2u && gap.ribbonSegmentCount == 0u,
                  "long sampling gap produced a connecting ribbon");

    SkidmarkSample turnFirst = MakeSample(0, 0.0f);
    SkidmarkSample turnSecond = MakeSample(10, 0.5f);
    ActivateOneWheel(turnFirst);
    ActivateOneWheel(turnSecond);
    turnSecond.carRotation =
            QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, 180.0f);
    const SkidmarkMeshData turn = BuildSkidmarkMesh({turnFirst, turnSecond});
    okay &= Check(turn.stampCount == 2u && turn.ribbonSegmentCount == 0u,
                  "rotation discontinuity produced a connecting ribbon");

    SkidmarkSample respawnFirst = MakeSample(0, 0.0f);
    SkidmarkSample respawnSecond = MakeSample(10, 0.5f);
    ActivateOneWheel(respawnFirst);
    ActivateOneWheel(respawnSecond);
    respawnSecond.respawnCount = 1u;
    const SkidmarkMeshData respawn =
            BuildSkidmarkMesh({respawnFirst, respawnSecond});
    okay &= Check(respawn.stampCount == 2u &&
                          respawn.ribbonSegmentCount == 0u,
                  "respawn discontinuity produced a connecting ribbon");
    return okay;
}

bool TestSurfacePolicyAndInvalidSamples() {
    constexpr std::array<std::uint16_t, 6u> filtered{
            {3u, 13u, 23u, 24u, 28u, 0xffffu}};
    bool okay = true;
    for (const std::uint16_t surface : filtered) {
        SkidmarkSample sample = MakeSample(0, 0.0f, surface);
        ActivateOneWheel(sample);
        okay &= Check(BuildSkidmarkMesh({sample}).empty(),
                      "filtered surface retained a skidmark");
    }

    SkidmarkSample grass = MakeSample(0, 0.0f, 2u);
    ActivateOneWheel(grass);
    const std::vector<SkidmarkVertexData> terrainVertices =
            DecodeVertices(BuildSkidmarkMesh({grass}).vertices);
    okay &= Check(!terrainVertices.empty() &&
                          Close(terrainVertices.front().surfaceClass, 1.0f) &&
                          Close(terrainVertices.front().colorA, 0.20f),
                  "terrain skidmark style was not selected");

    SkidmarkSample invalid = MakeSample(0, 0.0f);
    ActivateOneWheel(invalid);
    invalid.wheels[2u].contactPoint.setX(
            std::numeric_limits<float>::quiet_NaN());
    okay &= Check(BuildSkidmarkMesh({invalid}).empty(),
                  "non-finite contact position retained a skidmark");
    return okay;
}

bool TestContactPlaneProjectionAndNormalFallback() {
    SkidmarkSample banked = MakeSample(0, 0.0f);
    ActivateOneWheel(banked);
    const QVector3D bankedNormal =
            QVector3D(0.0f, 1.0f, 1.0f).normalized();
    banked.wheels[2u].contactNormal = bankedNormal;
    const QVector3D bankedPoint = banked.wheels[2u].contactPoint;
    const std::vector<SkidmarkVertexData> bankedVertices =
            DecodeVertices(BuildSkidmarkMesh({banked}).vertices);
    bool okay = Check(bankedVertices.size() == 4u,
                      "banked skidmark stamp had an unexpected vertex count");
    if (bankedVertices.size() == 4u) {
        for (const SkidmarkVertexData &vertex : bankedVertices) {
            okay &= Check(
                    Close(QVector3D::dotProduct(Position(vertex) - bankedPoint,
                                               bankedNormal),
                          0.012f),
                    "banked skidmark was not offset along the contact normal");
        }
        okay &= Check(
                Close(QVector3D::dotProduct(
                              Position(bankedVertices[1u]) -
                                      Position(bankedVertices[0u]),
                              bankedNormal),
                      0.0f) &&
                        Close(QVector3D::dotProduct(
                                      Position(bankedVertices[3u]) -
                                              Position(bankedVertices[0u]),
                                      bankedNormal),
                              0.0f),
                "banked skidmark axes were not projected onto the contact "
                "plane");
    }

    SkidmarkSample bankedNext = banked;
    bankedNext.timeMs = 10;
    const QVector3D bankedTravel =
            QVector3D(0.0f, -1.0f, 1.0f).normalized() * 0.5f;
    bankedNext.wheels[2u].contactPoint += bankedTravel;
    const std::vector<SkidmarkVertexData> bankedRibbonVertices =
            DecodeVertices(BuildSkidmarkMesh({banked, bankedNext}).vertices);
    okay &= Check(bankedRibbonVertices.size() == 8u,
                  "banked skidmark ribbon had an unexpected vertex count");
    if (bankedRibbonVertices.size() == 8u) {
        for (std::size_t vertex = 4u; vertex < 6u; ++vertex) {
            okay &= Check(
                    Close(QVector3D::dotProduct(
                                  Position(bankedRibbonVertices[vertex]) -
                                          bankedPoint,
                                  bankedNormal),
                          0.012f),
                    "banked ribbon start was not offset along its contact "
                    "normal");
        }
        for (std::size_t vertex = 6u; vertex < 8u; ++vertex) {
            okay &= Check(
                    Close(QVector3D::dotProduct(
                                  Position(bankedRibbonVertices[vertex]) -
                                          bankedNext.wheels[2u].contactPoint,
                                  bankedNormal),
                          0.012f),
                    "banked ribbon end was not offset along its contact "
                    "normal");
        }
    }

    SkidmarkSample fallback = MakeSample(0, 0.0f);
    ActivateOneWheel(fallback);
    fallback.carRotation =
            QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, 90.0f);
    fallback.wheels[2u].contactNormal = {
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    const QVector3D fallbackPoint = fallback.wheels[2u].contactPoint;
    const QVector3D expectedCarUp = fallback.carRotation
                                            .rotatedVector(
                                                    QVector3D(0.0f, 1.0f,
                                                              0.0f))
                                            .normalized();
    const std::vector<SkidmarkVertexData> fallbackVertices =
            DecodeVertices(BuildSkidmarkMesh({fallback}).vertices);
    okay &= Check(fallbackVertices.size() == 4u,
                  "invalid-normal skidmark stamp was not retained");
    if (fallbackVertices.size() == 4u) {
        QVector3D center;
        for (const SkidmarkVertexData &vertex : fallbackVertices) {
            center += Position(vertex);
        }
        center /= static_cast<float>(fallbackVertices.size());
        const QVector3D offset = center - fallbackPoint;
        okay &= Check(
                Close(QVector3D::dotProduct(offset, expectedCarUp), 0.012f) &&
                        (offset - expectedCarUp * 0.012f).length() < 0.0001f,
                "invalid contact normal did not fall back to car up");
    }
    return okay;
}

bool TestGeometryMetadataAndCleanup() {
    SkidmarkSample first = MakeSample(0, 0.0f);
    SkidmarkSample second = MakeSample(10, 0.5f);
    ActivateOneWheel(first);
    ActivateOneWheel(second);

    SkidmarkGeometry geometry;
    geometry.setSkidmarkMesh(BuildSkidmarkMesh({first, second}));
    bool okay = Check(geometry.ribbonSegmentCount() == 1u &&
                              geometry.stampCount() == 1u,
                      "geometry did not retain skidmark counters");
    okay &= Check(geometry.stride() ==
                          static_cast<int>(sizeof(SkidmarkVertexData)),
                  "geometry stride was incorrect");
    okay &= Check(geometry.attributeCount() == 5,
                  "geometry did not expose all skidmark attributes");
    constexpr std::array<QQuick3DGeometry::Attribute::Semantic, 5u> semantics{
            {QQuick3DGeometry::Attribute::IndexSemantic,
             QQuick3DGeometry::Attribute::PositionSemantic,
             QQuick3DGeometry::Attribute::TexCoord0Semantic,
             QQuick3DGeometry::Attribute::TexCoord1Semantic,
             QQuick3DGeometry::Attribute::ColorSemantic}};
    for (int index = 0; index < geometry.attributeCount(); ++index) {
        okay &= Check(geometry.attribute(index).semantic ==
                              semantics[static_cast<std::size_t>(index)],
                      "geometry attribute semantic was incorrect");
    }
    okay &= Check(geometry.subsetCount() == 1 &&
                          geometry.subsetOffset(0) == 0 &&
                          geometry.subsetCount(0) == 12,
                  "geometry subset did not cover all skidmark indices");
    geometry.clearMesh();
    okay &= Check(geometry.vertexData().isEmpty() &&
                          geometry.indexData().isEmpty() &&
                          geometry.ribbonSegmentCount() == 0u &&
                          geometry.stampCount() == 0u,
                  "geometry cleanup retained stale skidmark data");
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
    expect(TestContinuousRearWheelRibbons(),
           "continuous rear-wheel ribbons were incorrect");
    expect(TestBreaksAndStationarySlip(),
           "skidmark continuity rules were incorrect");
    expect(TestSurfacePolicyAndInvalidSamples(),
           "skidmark surface policy was incorrect");
    expect(TestContactPlaneProjectionAndNormalFallback(),
           "skidmark contact-plane projection was incorrect");
    expect(TestGeometryMetadataAndCleanup(),
           "skidmark QQuick3D geometry was incorrect");

    if (failures == 0) {
        std::cout << "skidmark geometry tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
