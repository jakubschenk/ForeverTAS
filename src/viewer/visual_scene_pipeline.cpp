#include "viewer/visual_scene_pipeline.h"

#include <QVector2D>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace forevertas::viewer {
namespace {

using forevervalidator::experimental::PhysicsSandboxRenderInstance;
using forevervalidator::experimental::PhysicsSandboxRenderLayer;
using forevervalidator::experimental::PhysicsSandboxRenderMesh;
using forevervalidator::experimental::PhysicsSandboxRenderScene;
using forevervalidator::experimental::PhysicsSandboxScenePurpose;
using forevervalidator::experimental::PhysicsSandboxTransform;

struct VisualVertex {
    float position[3];
    float normal[3];
    float tangent[3];
    float uv0[2];
    float uv1[2];
    float color[4];
};

static_assert(sizeof(VisualVertex) == StaticVisualVertexStride);

QVector3D ToQt(const forevervalidator::Vector3 &value) {
    return {value.x, value.y, value.z};
}

void ExpandBounds(const QVector3D &point, QVector3D &minimum,
                  QVector3D &maximum) {
    minimum.setX(std::min(minimum.x(), point.x()));
    minimum.setY(std::min(minimum.y(), point.y()));
    minimum.setZ(std::min(minimum.z(), point.z()));
    maximum.setX(std::max(maximum.x(), point.x()));
    maximum.setY(std::max(maximum.y(), point.y()));
    maximum.setZ(std::max(maximum.z(), point.z()));
}

QVector3D TransformPoint(const PhysicsSandboxTransform &transform,
                         const QVector3D &point) {
    return ToQt(transform.translation) + ToQt(transform.basisX) * point.x() +
           ToQt(transform.basisY) * point.y() +
           ToQt(transform.basisZ) * point.z();
}

QVector3D TransformDirection(const PhysicsSandboxTransform &transform,
                             const QVector3D &direction) {
    return ToQt(transform.basisX) * direction.x() +
           ToQt(transform.basisY) * direction.y() +
           ToQt(transform.basisZ) * direction.z();
}

float TransformDeterminant(const PhysicsSandboxTransform &transform) {
    return QVector3D::dotProduct(
            ToQt(transform.basisX),
            QVector3D::crossProduct(ToQt(transform.basisY),
                                    ToQt(transform.basisZ)));
}

QVector3D TransformNormal(const PhysicsSandboxTransform &transform,
                          const QVector3D &normal, float determinant) {
    const QVector3D x = ToQt(transform.basisX);
    const QVector3D y = ToQt(transform.basisY);
    const QVector3D z = ToQt(transform.basisZ);
    QVector3D result;
    if (std::fabs(determinant) > 1.0e-12f) {
        result = (QVector3D::crossProduct(y, z) * normal.x() +
                  QVector3D::crossProduct(z, x) * normal.y() +
                  QVector3D::crossProduct(x, y) * normal.z()) /
                 determinant;
    } else {
        result = TransformDirection(transform, normal);
    }
    return result.lengthSquared() > 1.0e-12f ? result.normalized()
                                             : QVector3D(0.0f, 1.0f, 0.0f);
}

QVector3D OrthogonalTangent(const QVector3D &normal) {
    QVector3D tangent = QVector3D::crossProduct(
            std::fabs(normal.y()) < 0.9f ? QVector3D(0.0f, 1.0f, 0.0f)
                                         : QVector3D(1.0f, 0.0f, 0.0f),
            normal);
    return tangent.lengthSquared() > 1.0e-12f ? tangent.normalized()
                                              : QVector3D(1.0f, 0.0f, 0.0f);
}

void GenerateMissingAttributes(std::vector<VisualVertex> &vertices,
                               const std::vector<std::uint32_t> &indices,
                               bool generateNormals, bool generateTangents,
                               bool generateUv0) {
    if (generateUv0) {
        for (VisualVertex &vertex : vertices) {
            vertex.uv0[0] = vertex.position[0] * 0.1f;
            vertex.uv0[1] = vertex.position[2] * 0.1f;
        }
    }
    if (generateNormals) {
        for (VisualVertex &vertex : vertices) {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 0.0f;
        }
        for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u) {
            const std::uint32_t ia = indices[index];
            const std::uint32_t ib = indices[index + 1u];
            const std::uint32_t ic = indices[index + 2u];
            if (ia >= vertices.size() || ib >= vertices.size() ||
                ic >= vertices.size()) {
                continue;
            }
            const QVector3D a(vertices[ia].position[0],
                              vertices[ia].position[1],
                              vertices[ia].position[2]);
            const QVector3D b(vertices[ib].position[0],
                              vertices[ib].position[1],
                              vertices[ib].position[2]);
            const QVector3D c(vertices[ic].position[0],
                              vertices[ic].position[1],
                              vertices[ic].position[2]);
            const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
            for (std::uint32_t vertexIndex : {ia, ib, ic}) {
                vertices[vertexIndex].normal[0] += normal.x();
                vertices[vertexIndex].normal[1] += normal.y();
                vertices[vertexIndex].normal[2] += normal.z();
            }
        }
        for (VisualVertex &vertex : vertices) {
            QVector3D normal(vertex.normal[0], vertex.normal[1],
                             vertex.normal[2]);
            normal = normal.lengthSquared() > 1.0e-12f
                             ? normal.normalized()
                             : QVector3D(0.0f, 1.0f, 0.0f);
            vertex.normal[0] = normal.x();
            vertex.normal[1] = normal.y();
            vertex.normal[2] = normal.z();
        }
    }
    if (generateTangents) {
        for (VisualVertex &vertex : vertices) {
            const QVector3D tangent = OrthogonalTangent(QVector3D(
                    vertex.normal[0], vertex.normal[1], vertex.normal[2]));
            vertex.tangent[0] = tangent.x();
            vertex.tangent[1] = tangent.y();
            vertex.tangent[2] = tangent.z();
        }
    }
}

std::vector<VisualVertex> PrepareMesh(const PhysicsSandboxRenderMesh &mesh) {
    std::vector<VisualVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto &vertex : mesh.vertices) {
        vertices.push_back(
                {{vertex.position.x, vertex.position.y, vertex.position.z},
                 {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                 {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z},
                 {vertex.uv0.x, vertex.uv0.y},
                 {vertex.uv1.x, vertex.uv1.y},
                 {vertex.color.x, vertex.color.y, vertex.color.z,
                  vertex.color.w}});
    }
    GenerateMissingAttributes(vertices, mesh.indices, !mesh.hasNormals,
                              !mesh.hasTangents, !mesh.hasUv0);
    return vertices;
}

struct BatchKey {
    std::uint32_t sourceMaterialIndex = 0u;
    ReplacementMaterialClass materialClass = ReplacementMaterialClass::Unknown;
    StaticVisualAlphaMode alphaMode = StaticVisualAlphaMode::Unknown;
    PhysicsSandboxScenePurpose purpose =
            PhysicsSandboxScenePurpose::Environment;
    bool vertexColors = false;
    bool doubleSided = false;
    bool defaultVisible = false;
    bool spatiallyPartitioned = false;
    std::int64_t cellX = 0;
    std::int64_t cellZ = 0;

    auto asTuple() const {
        return std::tie(sourceMaterialIndex, materialClass, alphaMode, purpose,
                        vertexColors, doubleSided, defaultVisible,
                        spatiallyPartitioned, cellX, cellZ);
    }
};

bool operator<(const BatchKey &left, const BatchKey &right) {
    return left.asTuple() < right.asTuple();
}

struct BatchAccumulator {
    std::vector<VisualVertex> vertices;
    std::vector<std::uint32_t> indices;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
    bool hasBounds = false;
    std::uint64_t sourceInstanceCount = 0u;
};

BatchKey MakeBatchKey(std::uint32_t sourceMaterialIndex,
                      ReplacementMaterialClass materialClass,
                      StaticVisualMaterialState materialState,
                      PhysicsSandboxScenePurpose purpose, bool vertexColors,
                      bool defaultVisible, bool spatiallyPartitioned,
                      std::int64_t cellX, std::int64_t cellZ) {
    return {sourceMaterialIndex,
            materialClass,
            materialState.alphaMode,
            purpose,
            vertexColors,
            materialState.doubleSided,
            defaultVisible,
            spatiallyPartitioned,
            cellX,
            cellZ};
}

using BuildClock = std::chrono::steady_clock;

std::uint64_t ElapsedNanoseconds(BuildClock::time_point begin,
                                 BuildClock::time_point end) {
    const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
                    .count();
    return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0u;
}

struct WorldBounds {
    QVector3D minimum{};
    QVector3D maximum{};
};

WorldBounds TransformBounds(const PhysicsSandboxRenderMesh &mesh,
                            const PhysicsSandboxTransform &transform) {
    const QVector3D minimum = ToQt(mesh.boundsMin);
    const QVector3D maximum = ToQt(mesh.boundsMax);
    WorldBounds result;
    for (int corner = 0; corner < 8; ++corner) {
        const QVector3D local(
                (corner & 1) != 0 ? maximum.x() : minimum.x(),
                (corner & 2) != 0 ? maximum.y() : minimum.y(),
                (corner & 4) != 0 ? maximum.z() : minimum.z());
        const QVector3D world = TransformPoint(transform, local);
        if (corner == 0) {
            result.minimum = world;
            result.maximum = world;
        } else {
            ExpandBounds(world, result.minimum, result.maximum);
        }
    }
    return result;
}

bool ShouldSpatiallyPartition(StaticVisualAlphaMode alphaMode) {
    return alphaMode == StaticVisualAlphaMode::Opaque ||
           alphaMode == StaticVisualAlphaMode::Masked;
}

std::optional<std::int64_t> CellCoordinate(double worldCoordinate,
                                           double cellSize) {
    if (!std::isfinite(worldCoordinate) || !std::isfinite(cellSize) ||
        cellSize <= 0.0) {
        return std::nullopt;
    }
    const double coordinate = std::floor(worldCoordinate / cellSize);
    const double minimumCoordinate = static_cast<double>(
            std::numeric_limits<std::int64_t>::min());
    // INT64_MAX rounds to 2^63 as a double. Treat that rounded value as an
    // exclusive limit so the subsequent conversion is always representable.
    const double maximumCoordinateExclusive = -minimumCoordinate;
    if (coordinate < minimumCoordinate ||
        coordinate >= maximumCoordinateExclusive) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(coordinate);
}

struct SpatialCell {
    std::int64_t x = 0;
    std::int64_t z = 0;
};

std::optional<SpatialCell> SelectSpatialCell(const WorldBounds &bounds,
                                             float cellSize,
                                             StaticVisualAlphaMode alphaMode) {
    if (!ShouldSpatiallyPartition(alphaMode)) {
        return std::nullopt;
    }
    const double centerX =
            (static_cast<double>(bounds.minimum.x()) + bounds.maximum.x()) *
            0.5;
    const double centerZ =
            (static_cast<double>(bounds.minimum.z()) + bounds.maximum.z()) *
            0.5;
    const auto cellX = CellCoordinate(centerX, cellSize);
    const auto cellZ = CellCoordinate(centerZ, cellSize);
    if (!cellX.has_value() || !cellZ.has_value()) {
        return std::nullopt;
    }
    return SpatialCell{*cellX, *cellZ};
}

StaticVisualMaterialState MaterialStateFor(
        const StaticVisualBatchOptions &options,
        std::uint32_t materialIndex) {
    return materialIndex < options.materialStates.size()
                   ? options.materialStates[materialIndex]
                   : StaticVisualMaterialState{};
}

[[maybe_unused]] bool UsesRandomizedTerrainTiles(
        ReplacementMaterialClass materialClass,
        PhysicsSandboxScenePurpose purpose) {
    if (materialClass != ReplacementMaterialClass::Grass &&
        materialClass != ReplacementMaterialClass::Dirt) {
        return false;
    }
    // Random rotation requires splitting triangles at every crossed texture
    // tile. That is appropriate for track-sized ground surfaces, but scene
    // environment meshes can span tens of kilometres and must retain their
    // ordinary repeating world-space UVs.
    switch (purpose) {
    case PhysicsSandboxScenePurpose::PlacedBlock:
    case PhysicsSandboxScenePurpose::Clip:
    case PhysicsSandboxScenePurpose::Terrain:
    case PhysicsSandboxScenePurpose::Generated:
        return true;
    case PhysicsSandboxScenePurpose::Environment:
    case PhysicsSandboxScenePurpose::SubMobil:
    case PhysicsSandboxScenePurpose::Helper:
    case PhysicsSandboxScenePurpose::CheckpointTrigger:
    case PhysicsSandboxScenePurpose::DedicatedInitialCollision:
    case PhysicsSandboxScenePurpose::Pylon:
    case PhysicsSandboxScenePurpose::Decoration:
        return false;
    }
    return false;
}

struct DuplicateInstanceKey {
    std::uint32_t meshIndex = 0u;
    std::uint32_t materialIndex = 0u;
    PhysicsSandboxScenePurpose purpose =
            PhysicsSandboxScenePurpose::Environment;
    std::array<float, 12> transform{};

    auto asTuple() const {
        return std::tie(meshIndex, materialIndex, purpose, transform);
    }
};

bool operator<(const DuplicateInstanceKey &left,
               const DuplicateInstanceKey &right) {
    return left.asTuple() < right.asTuple();
}

DuplicateInstanceKey DuplicateKey(
        const PhysicsSandboxRenderInstance &instance) {
    return {
            instance.meshIndex,
            instance.materialIndex,
            instance.purpose,
            {instance.worldTransform.translation.x,
             instance.worldTransform.translation.y,
             instance.worldTransform.translation.z,
             instance.worldTransform.basisX.x,
             instance.worldTransform.basisX.y,
             instance.worldTransform.basisX.z,
             instance.worldTransform.basisY.x,
             instance.worldTransform.basisY.y,
             instance.worldTransform.basisY.z,
             instance.worldTransform.basisZ.x,
             instance.worldTransform.basisZ.y,
             instance.worldTransform.basisZ.z}};
}

bool IsGrassGroundCover(const PhysicsSandboxRenderMesh &mesh,
                        const PhysicsSandboxRenderInstance &instance) {
    if (!mesh.hasNormals || !mesh.hasUv0 || mesh.vertices.empty() ||
        instance.provenance.blockName.empty() ||
        (instance.purpose != PhysicsSandboxScenePurpose::PlacedBlock &&
         instance.purpose != PhysicsSandboxScenePurpose::Clip)) {
        return false;
    }
    const float width = mesh.boundsMax.x - mesh.boundsMin.x;
    const float height = mesh.boundsMax.y - mesh.boundsMin.y;
    const float depth = mesh.boundsMax.z - mesh.boundsMin.z;
    if (height > 1.0f || width < 16.0f || depth < 16.0f) {
        return false;
    }
    const std::size_t horizontalNormals = std::count_if(
            mesh.vertices.cbegin(), mesh.vertices.cend(),
            [](const auto &vertex) {
                return std::fabs(vertex.normal.y) >= 0.9f;
            });
    return horizontalNormals * 100u >= mesh.vertices.size() * 95u;
}

bool IsGrassBladeGeometry(const PhysicsSandboxRenderMesh &mesh,
                          const PhysicsSandboxRenderInstance &instance,
                          std::uint8_t surfaceMaterialId) {
    if (!mesh.hasNormals || mesh.vertices.empty() ||
        instance.provenance.blockName.empty() ||
        (instance.purpose != PhysicsSandboxScenePurpose::PlacedBlock &&
         instance.purpose != PhysicsSandboxScenePurpose::Clip) ||
        (surfaceMaterialId != 0u && surfaceMaterialId != 2u &&
         surfaceMaterialId != 20u && surfaceMaterialId != 25u) ||
        mesh.vertices.size() < 64u || mesh.indices.size() % 3u != 0u) {
        return false;
    }
    const float width = mesh.boundsMax.x - mesh.boundsMin.x;
    const float height = mesh.boundsMax.y - mesh.boundsMin.y;
    const float depth = mesh.boundsMax.z - mesh.boundsMin.z;
    if (height > 1.0f || std::max(width, depth) < 16.0f) {
        return false;
    }
    const std::size_t horizontalNormals = std::count_if(
            mesh.vertices.cbegin(), mesh.vertices.cend(),
            [](const auto &vertex) {
                return std::fabs(vertex.normal.y) >= 0.9f;
            });
    if (horizontalNormals * 100u < mesh.vertices.size() * 95u) {
        return false;
    }
    return (mesh.indices.size() / 3u) * 2u == mesh.vertices.size();
}

enum class ProjectionAxis {
    X,
    Y,
    Z,
};

struct CoverageTileKey {
    ProjectionAxis axis = ProjectionAxis::Y;
    std::int64_t tileU = 0;
    std::int64_t tileV = 0;

    auto asTuple() const {
        return std::tie(axis, tileU, tileV);
    }
};

bool operator<(const CoverageTileKey &left, const CoverageTileKey &right) {
    return left.asTuple() < right.asTuple();
}

struct CoveragePolygon {
    float planeCoordinate = 0.0f;
    std::vector<QVector2D> vertices;
};

using TerrainCoverage =
        std::map<CoverageTileKey, std::vector<CoveragePolygon>>;

ProjectionAxis DominantProjectionAxis(const QVector3D &normal) {
    const QVector3D absoluteNormal(std::fabs(normal.x()),
                                   std::fabs(normal.y()),
                                   std::fabs(normal.z()));
    if (absoluteNormal.y() >= absoluteNormal.x() &&
        absoluteNormal.y() >= absoluteNormal.z()) {
        return ProjectionAxis::Y;
    }
    return absoluteNormal.x() >= absoluteNormal.z() ? ProjectionAxis::X
                                                    : ProjectionAxis::Z;
}

QVector2D ProjectedUv(const QVector3D &position, ProjectionAxis axis,
                      float scale) {
    switch (axis) {
    case ProjectionAxis::X:
        return {position.z() * scale, position.y() * scale};
    case ProjectionAxis::Y:
        return {position.x() * scale, position.z() * scale};
    case ProjectionAxis::Z:
        return {position.x() * scale, position.y() * scale};
    }
    return {};
}

std::pair<QVector3D, QVector3D> ProjectionTangents(ProjectionAxis axis) {
    switch (axis) {
    case ProjectionAxis::X:
        return {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};
    case ProjectionAxis::Y:
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    case ProjectionAxis::Z:
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    }
    return {};
}

void ApplyUvRotation(VisualVertex &vertex, float u, float v,
                     ProjectionAxis axis, unsigned quarterTurns,
                     QVector3D &tangent) {
    const auto [tangentU, tangentV] = ProjectionTangents(axis);
    switch (quarterTurns % 4u) {
    case 1u:
        vertex.uv0[0] = 1.0f - v;
        vertex.uv0[1] = u;
        tangent = -tangentV;
        break;
    case 2u:
        vertex.uv0[0] = 1.0f - u;
        vertex.uv0[1] = 1.0f - v;
        tangent = -tangentU;
        break;
    case 3u:
        vertex.uv0[0] = v;
        vertex.uv0[1] = 1.0f - u;
        tangent = tangentV;
        break;
    case 0u:
    default:
        vertex.uv0[0] = u;
        vertex.uv0[1] = v;
        tangent = tangentU;
        break;
    }
}

[[maybe_unused]] void ApplyWorldUvProjection(VisualVertex &vertex,
                            const QVector3D &position,
                            const QVector3D &normal,
                            float scale,
                            QVector3D &tangent) {
    const ProjectionAxis axis = DominantProjectionAxis(normal);
    const QVector2D uv = ProjectedUv(position, axis, scale);
    ApplyUvRotation(vertex, uv.x(), uv.y(), axis, 0u, tangent);
}

unsigned TileQuarterTurns(std::int64_t tileU, std::int64_t tileV,
                          ProjectionAxis axis) {
    const auto coordinateHash = [](std::int64_t coordinate) {
        const std::uint64_t bits = static_cast<std::uint64_t>(coordinate);
        return static_cast<std::uint32_t>(bits ^ (bits >> 32u));
    };
    std::uint32_t hash = 0x9e3779b9u;
    const auto mix = [&hash](std::uint32_t value) {
        hash ^= value + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= hash >> 16u;
        hash *= 0x7feb352du;
        hash ^= hash >> 15u;
    };
    mix(coordinateHash(tileU));
    mix(coordinateHash(tileV));
    mix(static_cast<std::uint32_t>(axis));
    return hash & 3u;
}

VisualVertex LerpVertex(const VisualVertex &left, const VisualVertex &right,
                        float amount) {
    VisualVertex result;
    const auto interpolate = [amount](const float *a, const float *b,
                                      float *output, std::size_t count) {
        for (std::size_t index = 0u; index < count; ++index) {
            output[index] = a[index] + (b[index] - a[index]) * amount;
        }
    };
    interpolate(left.position, right.position, result.position, 3u);
    interpolate(left.normal, right.normal, result.normal, 3u);
    interpolate(left.tangent, right.tangent, result.tangent, 3u);
    interpolate(left.uv0, right.uv0, result.uv0, 2u);
    interpolate(left.uv1, right.uv1, result.uv1, 2u);
    interpolate(left.color, right.color, result.color, 4u);
    return result;
}

std::vector<VisualVertex> ClipProjectedPolygon(
        const std::vector<VisualVertex> &input, ProjectionAxis axis,
        float scale, bool clipU, float boundary, bool keepGreater) {
    std::vector<VisualVertex> output;
    if (input.empty()) {
        return output;
    }
    output.reserve(input.size() + 1u);
    const auto coordinate = [axis, scale, clipU](const VisualVertex &vertex) {
        const QVector2D uv = ProjectedUv(
                {vertex.position[0], vertex.position[1], vertex.position[2]},
                axis, scale);
        return clipU ? uv.x() : uv.y();
    };
    VisualVertex previous = input.back();
    float previousCoordinate = coordinate(previous);
    bool previousInside = keepGreater ? previousCoordinate >= boundary
                                      : previousCoordinate <= boundary;
    for (const VisualVertex &current : input) {
        const float currentCoordinate = coordinate(current);
        const bool currentInside = keepGreater
                                           ? currentCoordinate >= boundary
                                           : currentCoordinate <= boundary;
        if (currentInside != previousInside) {
            const float denominator = currentCoordinate - previousCoordinate;
            const float amount = std::fabs(denominator) > 1.0e-12f
                                         ? std::clamp(
                                                   (boundary -
                                                    previousCoordinate) /
                                                           denominator,
                                                   0.0f, 1.0f)
                                         : 0.0f;
            output.push_back(LerpVertex(previous, current, amount));
        }
        if (currentInside) {
            output.push_back(current);
        }
        previous = current;
        previousCoordinate = currentCoordinate;
        previousInside = currentInside;
    }
    return output;
}

float AxisCoordinate(const VisualVertex &vertex, ProjectionAxis axis) {
    switch (axis) {
    case ProjectionAxis::X:
        return vertex.position[0];
    case ProjectionAxis::Y:
        return vertex.position[1];
    case ProjectionAxis::Z:
        return vertex.position[2];
    }
    return vertex.position[1];
}

bool AxisAlignedPlane(const std::vector<VisualVertex> &polygon,
                      ProjectionAxis axis, float &planeCoordinate) {
    if (polygon.empty()) {
        return false;
    }
    float minimum = AxisCoordinate(polygon.front(), axis);
    float maximum = minimum;
    for (const VisualVertex &vertex : polygon) {
        const float coordinate = AxisCoordinate(vertex, axis);
        minimum = std::min(minimum, coordinate);
        maximum = std::max(maximum, coordinate);
    }
    constexpr float CoplanarEpsilon = 0.001f;
    planeCoordinate = (minimum + maximum) * 0.5f;
    return maximum - minimum <= CoplanarEpsilon;
}

float EdgeSide(const QVector2D &a, const QVector2D &b,
               const QVector2D &point) {
    return (b.x() - a.x()) * (point.y() - a.y()) -
           (b.y() - a.y()) * (point.x() - a.x());
}

float SignedArea(const std::vector<QVector2D> &polygon) {
    float twiceArea = 0.0f;
    for (std::size_t index = 0u; index < polygon.size(); ++index) {
        const QVector2D &a = polygon[index];
        const QVector2D &b = polygon[(index + 1u) % polygon.size()];
        twiceArea += a.x() * b.y() - a.y() * b.x();
    }
    return twiceArea * 0.5f;
}

std::vector<VisualVertex> ClipAgainstProjectedEdge(
        const std::vector<VisualVertex> &input, ProjectionAxis axis,
        float scale, const QVector2D &edgeStart, const QVector2D &edgeEnd,
        bool keepInside) {
    std::vector<VisualVertex> output;
    if (input.empty()) {
        return output;
    }
    output.reserve(input.size() + 1u);
    const auto side = [&](const VisualVertex &vertex) {
        return EdgeSide(
                edgeStart, edgeEnd,
                ProjectedUv({vertex.position[0], vertex.position[1],
                             vertex.position[2]},
                            axis, scale));
    };
    constexpr float HalfPlaneEpsilon = 1.0e-6f;
    VisualVertex previous = input.back();
    float previousSide = side(previous);
    bool previousInside = keepInside ? previousSide >= -HalfPlaneEpsilon
                                     : previousSide <= HalfPlaneEpsilon;
    for (const VisualVertex &current : input) {
        const float currentSide = side(current);
        const bool currentInside =
                keepInside ? currentSide >= -HalfPlaneEpsilon
                           : currentSide <= HalfPlaneEpsilon;
        if (currentInside != previousInside) {
            const float denominator = previousSide - currentSide;
            const float amount =
                    std::fabs(denominator) > 1.0e-12f
                            ? std::clamp(previousSide / denominator, 0.0f, 1.0f)
                            : 0.0f;
            output.push_back(LerpVertex(previous, current, amount));
        }
        if (currentInside) {
            output.push_back(current);
        }
        previous = current;
        previousSide = currentSide;
        previousInside = currentInside;
    }
    return output;
}

std::vector<std::vector<VisualVertex>> SubtractCoveredPolygon(
        const std::vector<VisualVertex> &subject,
        const std::vector<QVector2D> &cover, ProjectionAxis axis, float scale) {
    if (subject.size() < 3u || cover.size() < 3u) {
        return {subject};
    }
    std::vector<QVector2D> orientedCover = cover;
    if (SignedArea(orientedCover) < 0.0f) {
        std::reverse(orientedCover.begin(), orientedCover.end());
    }

    std::vector<std::vector<VisualVertex>> uncovered;
    std::vector<VisualVertex> intersection = subject;
    // Peeling the outside of each convex edge partitions subject - cover
    // without introducing another polygon dependency.
    for (std::size_t edge = 0u; edge < orientedCover.size(); ++edge) {
        const QVector2D &start = orientedCover[edge];
        const QVector2D &end =
                orientedCover[(edge + 1u) % orientedCover.size()];
        std::vector<VisualVertex> outside = ClipAgainstProjectedEdge(
                intersection, axis, scale, start, end, false);
        if (outside.size() >= 3u) {
            uncovered.push_back(std::move(outside));
        }
        intersection = ClipAgainstProjectedEdge(intersection, axis, scale,
                                                start, end, true);
        if (intersection.size() < 3u) {
            break;
        }
    }
    return uncovered;
}

void SimplifyProjectedPolygon(std::vector<VisualVertex> &polygon,
                              ProjectionAxis axis, float scale) {
    const auto projected = [axis, scale](const VisualVertex &vertex) {
        return ProjectedUv(
                {vertex.position[0], vertex.position[1], vertex.position[2]},
                axis, scale);
    };
    constexpr float PositionEpsilonSquared = 1.0e-10f;
    for (std::size_t index = 0u; polygon.size() >= 2u &&
                                index < polygon.size();) {
        const std::size_t next = (index + 1u) % polygon.size();
        if ((projected(polygon[index]) - projected(polygon[next]))
                    .lengthSquared() <= PositionEpsilonSquared) {
            polygon.erase(polygon.begin() + static_cast<std::ptrdiff_t>(next));
            if (next == 0u) {
                index = 0u;
            }
        } else {
            ++index;
        }
    }

    bool removed = true;
    while (removed && polygon.size() >= 3u) {
        removed = false;
        for (std::size_t index = 0u; index < polygon.size(); ++index) {
            const QVector2D previous =
                    projected(polygon[(index + polygon.size() - 1u) %
                                      polygon.size()]);
            const QVector2D current = projected(polygon[index]);
            const QVector2D next =
                    projected(polygon[(index + 1u) % polygon.size()]);
            if (std::fabs(EdgeSide(previous, next, current)) <= 1.0e-6f) {
                polygon.erase(
                        polygon.begin() + static_cast<std::ptrdiff_t>(index));
                removed = true;
                break;
            }
        }
    }
}

void FlattenAxisAlignedNormal(std::vector<VisualVertex> &polygon,
                              ProjectionAxis axis,
                              const QVector3D &authoredNormal) {
    QVector3D normal;
    switch (axis) {
    case ProjectionAxis::X:
        normal = {1.0f, 0.0f, 0.0f};
        break;
    case ProjectionAxis::Y:
        normal = {0.0f, 1.0f, 0.0f};
        break;
    case ProjectionAxis::Z:
        normal = {0.0f, 0.0f, 1.0f};
        break;
    }
    if (QVector3D::dotProduct(normal, authoredNormal) < 0.0f) {
        normal = -normal;
    }
    for (VisualVertex &vertex : polygon) {
        vertex.normal[0] = normal.x();
        vertex.normal[1] = normal.y();
        vertex.normal[2] = normal.z();
    }
}

VisualVertex TransformVertex(const VisualVertex &source,
                             const PhysicsSandboxTransform &transform,
                             float determinant) {
    VisualVertex vertex = source;
    const QVector3D position = TransformPoint(
            transform,
            {source.position[0], source.position[1], source.position[2]});
    const QVector3D normal =
            TransformNormal(transform,
                            {source.normal[0], source.normal[1],
                             source.normal[2]},
                            determinant);
    QVector3D tangent = TransformDirection(
            transform,
            {source.tangent[0], source.tangent[1], source.tangent[2]});
    tangent -= normal * QVector3D::dotProduct(normal, tangent);
    tangent = tangent.lengthSquared() > 1.0e-12f
                      ? tangent.normalized()
                      : OrthogonalTangent(normal);
    vertex.position[0] = position.x();
    vertex.position[1] = position.y();
    vertex.position[2] = position.z();
    vertex.normal[0] = normal.x();
    vertex.normal[1] = normal.y();
    vertex.normal[2] = normal.z();
    vertex.tangent[0] = tangent.x();
    vertex.tangent[1] = tangent.y();
    vertex.tangent[2] = tangent.z();
    return vertex;
}

void AppendVertex(BatchAccumulator &batch, const VisualVertex &vertex) {
    if (batch.vertices.size() ==
        std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("static visual batch exceeds U32 indices");
    }
    batch.vertices.push_back(vertex);
    const QVector3D position(vertex.position[0], vertex.position[1],
                             vertex.position[2]);
    if (!batch.hasBounds) {
        batch.boundsMin = position;
        batch.boundsMax = position;
        batch.hasBounds = true;
    } else {
        ExpandBounds(position, batch.boundsMin, batch.boundsMax);
    }
}

void AppendRandomizedFragment(BatchAccumulator &batch,
                              std::vector<VisualVertex> &fragment,
                              ProjectionAxis axis, float worldUvScale,
                              std::int64_t tileU, std::int64_t tileV,
                              unsigned quarterTurns) {
    const std::uint32_t baseVertex =
            static_cast<std::uint32_t>(batch.vertices.size());
    for (VisualVertex &vertex : fragment) {
        const QVector3D position(vertex.position[0], vertex.position[1],
                                 vertex.position[2]);
        QVector3D normal(vertex.normal[0], vertex.normal[1],
                         vertex.normal[2]);
        normal = normal.lengthSquared() > 1.0e-12f
                         ? normal.normalized()
                         : QVector3D(0.0f, 1.0f, 0.0f);
        const QVector2D projectedUv =
                ProjectedUv(position, axis, worldUvScale);
        QVector3D tangent;
        ApplyUvRotation(vertex,
                        projectedUv.x() - static_cast<float>(tileU),
                        projectedUv.y() - static_cast<float>(tileV), axis,
                        quarterTurns, tangent);
        tangent -= normal * QVector3D::dotProduct(normal, tangent);
        tangent = tangent.lengthSquared() > 1.0e-12f
                          ? tangent.normalized()
                          : OrthogonalTangent(normal);
        vertex.normal[0] = normal.x();
        vertex.normal[1] = normal.y();
        vertex.normal[2] = normal.z();
        vertex.tangent[0] = tangent.x();
        vertex.tangent[1] = tangent.y();
        vertex.tangent[2] = tangent.z();
        AppendVertex(batch, vertex);
    }
    for (std::uint32_t corner = 1u; corner + 1u < fragment.size(); ++corner) {
        batch.indices.insert(batch.indices.end(),
                             {baseVertex, baseVertex + corner,
                              baseVertex + corner + 1u});
    }
}

void AppendInstance(BatchAccumulator &batch,
                    const std::vector<VisualVertex> &sourceVertices,
                    const std::vector<std::uint32_t> &sourceIndices,
                    const PhysicsSandboxTransform &transform,
                    float worldUvScale) {
    if (batch.vertices.size() >
        std::numeric_limits<std::uint32_t>::max() - sourceVertices.size()) {
        throw std::runtime_error("static visual batch exceeds U32 indices");
    }
    const std::uint32_t baseVertex =
            static_cast<std::uint32_t>(batch.vertices.size());
    const float transformDeterminant = TransformDeterminant(transform);
    for (const VisualVertex &source : sourceVertices) {
        VisualVertex vertex =
                TransformVertex(source, transform, transformDeterminant);
        const QVector3D position(vertex.position[0], vertex.position[1],
                                 vertex.position[2]);
        const QVector3D normal(vertex.normal[0], vertex.normal[1],
                               vertex.normal[2]);
        QVector3D tangent(vertex.tangent[0], vertex.tangent[1],
                          vertex.tangent[2]);
        if (worldUvScale > 0.0f) {
            // This is the precise legacy PDiff mapping used by the game and
            // gbx3d: world.xz / 16. It is deliberately not a dominant-axis
            // triplanar projection and does not randomize individual tiles.
            vertex.uv0[0] = position.x() * worldUvScale;
            vertex.uv0[1] = position.z() * worldUvScale;
            tangent = QVector3D(1.0f, 0.0f, 0.0f);
        }
        tangent -= normal * QVector3D::dotProduct(normal, tangent);
        tangent = tangent.lengthSquared() > 1.0e-12f
                          ? tangent.normalized()
                          : OrthogonalTangent(normal);
        vertex.tangent[0] = tangent.x();
        vertex.tangent[1] = tangent.y();
        vertex.tangent[2] = tangent.z();
        AppendVertex(batch, vertex);
    }
    const bool reverseWinding = transformDeterminant < 0.0f;
    if (reverseWinding && sourceIndices.size() % 3u == 0u) {
        for (std::size_t index = 0u; index < sourceIndices.size(); index += 3u) {
            batch.indices.push_back(baseVertex + sourceIndices[index]);
            batch.indices.push_back(baseVertex + sourceIndices[index + 2u]);
            batch.indices.push_back(baseVertex + sourceIndices[index + 1u]);
        }
    } else {
        for (std::uint32_t index : sourceIndices) {
            batch.indices.push_back(baseVertex + index);
        }
    }
    ++batch.sourceInstanceCount;
}

[[maybe_unused]] void AppendRandomizedTiledInstance(
        BatchAccumulator &batch,
        const std::vector<VisualVertex> &sourceVertices,
        const std::vector<std::uint32_t> &sourceIndices,
        const PhysicsSandboxTransform &transform, float worldUvScale,
        TerrainCoverage *coverage) {
    std::vector<VisualVertex> vertices;
    vertices.reserve(sourceVertices.size());
    const float transformDeterminant = TransformDeterminant(transform);
    for (const VisualVertex &source : sourceVertices) {
        vertices.push_back(
                TransformVertex(source, transform, transformDeterminant));
    }
    // Only earlier instances cover this one. Triangles within one authored
    // mesh retain their original topology until the whole instance is done.
    TerrainCoverage pendingCoverage;

    constexpr float BoundaryEpsilon = 1.0e-5f;
    for (std::size_t index = 0u; index + 2u < sourceIndices.size();
         index += 3u) {
        const std::uint32_t ia = sourceIndices[index];
        const std::uint32_t ib = sourceIndices[index + 1u];
        const std::uint32_t ic = sourceIndices[index + 2u];
        if (ia >= vertices.size() || ib >= vertices.size() ||
            ic >= vertices.size()) {
            continue;
        }
        const QVector3D averageNormal(
                vertices[ia].normal[0] + vertices[ib].normal[0] +
                        vertices[ic].normal[0],
                vertices[ia].normal[1] + vertices[ib].normal[1] +
                        vertices[ic].normal[1],
                vertices[ia].normal[2] + vertices[ib].normal[2] +
                        vertices[ic].normal[2]);
        const ProjectionAxis axis = DominantProjectionAxis(averageNormal);
        const std::array<QVector2D, 3> projected{{
                ProjectedUv({vertices[ia].position[0],
                             vertices[ia].position[1],
                             vertices[ia].position[2]},
                            axis, worldUvScale),
                ProjectedUv({vertices[ib].position[0],
                             vertices[ib].position[1],
                             vertices[ib].position[2]},
                            axis, worldUvScale),
                ProjectedUv({vertices[ic].position[0],
                             vertices[ic].position[1],
                             vertices[ic].position[2]},
                            axis, worldUvScale),
        }};
        const auto [minimumU, maximumU] = std::minmax(
                {projected[0].x(), projected[1].x(), projected[2].x()});
        const auto [minimumV, maximumV] = std::minmax(
                {projected[0].y(), projected[1].y(), projected[2].y()});
        std::int64_t firstTileU = static_cast<std::int64_t>(
                std::floor(minimumU + BoundaryEpsilon));
        std::int64_t lastTileU = static_cast<std::int64_t>(
                std::floor(maximumU - BoundaryEpsilon));
        std::int64_t firstTileV = static_cast<std::int64_t>(
                std::floor(minimumV + BoundaryEpsilon));
        std::int64_t lastTileV = static_cast<std::int64_t>(
                std::floor(maximumV - BoundaryEpsilon));
        if (lastTileU < firstTileU) {
            firstTileU = lastTileU = static_cast<std::int64_t>(
                    std::floor((minimumU + maximumU) * 0.5f));
        }
        if (lastTileV < firstTileV) {
            firstTileV = lastTileV = static_cast<std::int64_t>(
                    std::floor((minimumV + maximumV) * 0.5f));
        }

        for (std::int64_t tileU = firstTileU; tileU <= lastTileU; ++tileU) {
            for (std::int64_t tileV = firstTileV; tileV <= lastTileV;
                 ++tileV) {
                std::vector<VisualVertex> polygon{
                        vertices[ia], vertices[ib], vertices[ic]};
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, true,
                        static_cast<float>(tileU), true);
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, true,
                        static_cast<float>(tileU + 1), false);
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, false,
                        static_cast<float>(tileV), true);
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, false,
                        static_cast<float>(tileV + 1), false);
                if (polygon.size() < 3u) {
                    continue;
                }

                const unsigned quarterTurns =
                        TileQuarterTurns(tileU, tileV, axis);
                float planeCoordinate = 0.0f;
                const bool axisAligned =
                        AxisAlignedPlane(polygon, axis, planeCoordinate);
                const CoverageTileKey coverageKey{axis, tileU, tileV};
                std::vector<std::vector<VisualVertex>> uncovered{polygon};
                if (axisAligned && coverage != nullptr) {
                    constexpr float CoplanarEpsilon = 0.001f;
                    const auto coveredTile = coverage->find(coverageKey);
                    if (coveredTile != coverage->cend()) {
                        for (const CoveragePolygon &covered :
                             coveredTile->second) {
                            if (std::fabs(covered.planeCoordinate -
                                          planeCoordinate) >
                                CoplanarEpsilon) {
                                continue;
                            }
                            std::vector<std::vector<VisualVertex>> next;
                            for (const auto &fragment : uncovered) {
                                auto pieces = SubtractCoveredPolygon(
                                        fragment, covered.vertices, axis,
                                        worldUvScale);
                                for (auto &piece : pieces) {
                                    next.push_back(std::move(piece));
                                }
                            }
                            uncovered = std::move(next);
                            if (uncovered.empty()) {
                                break;
                            }
                        }
                    }
                }

                for (std::vector<VisualVertex> &fragment : uncovered) {
                    SimplifyProjectedPolygon(fragment, axis, worldUvScale);
                    if (fragment.size() < 3u) {
                        continue;
                    }
                    std::vector<QVector2D> projectedFragment;
                    projectedFragment.reserve(fragment.size());
                    for (const VisualVertex &vertex : fragment) {
                        projectedFragment.push_back(ProjectedUv(
                                {vertex.position[0], vertex.position[1],
                                 vertex.position[2]},
                                axis, worldUvScale));
                    }
                    if (std::fabs(SignedArea(projectedFragment)) < 1.0e-5f) {
                        continue;
                    }
                    if (axisAligned && coverage != nullptr) {
                        FlattenAxisAlignedNormal(fragment, axis, averageNormal);
                        pendingCoverage[coverageKey].push_back(
                                {planeCoordinate, std::move(projectedFragment)});
                    }
                    AppendRandomizedFragment(batch, fragment, axis,
                                             worldUvScale, tileU, tileV,
                                             quarterTurns);
                }
            }
        }
    }
    if (coverage != nullptr) {
        for (auto &[key, polygons] : pendingCoverage) {
            auto &coveredTile = (*coverage)[key];
            for (auto &polygon : polygons) {
                coveredTile.push_back(std::move(polygon));
            }
        }
    }
    ++batch.sourceInstanceCount;
}

}  // namespace

CameraClipPlanes CalculateCameraClipPlanes(const QVector3D &cameraPosition,
                                           float cameraDistance,
                                           const QVector3D &boundsMin,
                                           const QVector3D &boundsMax) {
    float farthest = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const QVector3D point((corner & 1) != 0 ? boundsMax.x() : boundsMin.x(),
                              (corner & 2) != 0 ? boundsMax.y() : boundsMin.y(),
                              (corner & 4) != 0 ? boundsMax.z()
                                                : boundsMin.z());
        farthest = std::max(farthest, cameraPosition.distanceToPoint(point));
    }

    const QVector3D nearestPoint(
            std::clamp(cameraPosition.x(), boundsMin.x(), boundsMax.x()),
            std::clamp(cameraPosition.y(), boundsMin.y(), boundsMax.y()),
            std::clamp(cameraPosition.z(), boundsMin.z(), boundsMax.z()));
    const float nearest = cameraPosition.distanceToPoint(nearestPoint);
    const float distance = std::max(0.0f, cameraDistance);
    const float margin = std::max(2.0f, distance * 0.05f);
    const float farPlane = std::max(25.0f, farthest + margin);
    float nearPlane = std::max({0.1f, distance * 0.01f, farPlane / 50000.0f});
    if (nearest > 0.0f) {
        nearPlane = std::min(nearPlane, nearest * 0.5f);
        nearPlane = std::max(0.05f, nearPlane);
    }
    nearPlane = std::min(nearPlane, farPlane * 0.25f);
    return {nearPlane, farPlane};
}

bool IsDefaultVisualPurpose(PhysicsSandboxScenePurpose purpose) {
    switch (purpose) {
    case PhysicsSandboxScenePurpose::Environment:
    case PhysicsSandboxScenePurpose::PlacedBlock:
    case PhysicsSandboxScenePurpose::SubMobil:
    case PhysicsSandboxScenePurpose::Pylon:
    case PhysicsSandboxScenePurpose::Decoration:
    case PhysicsSandboxScenePurpose::Terrain:
    case PhysicsSandboxScenePurpose::Generated:
        return true;
    case PhysicsSandboxScenePurpose::Clip:
    case PhysicsSandboxScenePurpose::Helper:
    case PhysicsSandboxScenePurpose::CheckpointTrigger:
    case PhysicsSandboxScenePurpose::DedicatedInitialCollision:
        return false;
    }
    return false;
}

bool IsDefaultVisualInstance(PhysicsSandboxScenePurpose purpose,
                             std::string_view blockName) {
    if (IsDefaultVisualPurpose(purpose)) {
        return true;
    }
    return purpose == PhysicsSandboxScenePurpose::Clip &&
           blockName == "StadiumGrassClip";
}

StaticVisualBatchResult
BuildStaticVisualBatches(const PhysicsSandboxRenderScene &scene) {
    return BuildStaticVisualBatches(scene, StaticVisualBatchOptions{});
}

StaticVisualBatchResult BuildStaticVisualBatches(
        const PhysicsSandboxRenderScene &scene,
        const StaticVisualBatchOptions &options) {
    const BuildClock::time_point buildStarted = BuildClock::now();
    StaticVisualBatchResult result;
    result.sourceMeshCount = scene.meshes.size();
    std::vector<std::optional<std::vector<VisualVertex>>> preparedMeshes(
            scene.meshes.size());

    std::map<BatchKey, BatchAccumulator> accumulators;
    std::set<DuplicateInstanceKey> seenInstances;
    bool hasDefaultBounds = false;
    for (const PhysicsSandboxRenderInstance &instance : scene.instances) {
        if (!instance.visible || instance.lodLevel != 0u) {
            continue;
        }
        if (instance.meshIndex >= scene.meshes.size() ||
            instance.materialIndex >= scene.materials.size()) {
            ++result.invalidInstanceCount;
            continue;
        }
        ++result.visibleSourceInstanceCount;
        if (instance.renderLayer ==
            PhysicsSandboxRenderLayer::Background) {
            ++result.skippedBackgroundInstanceCount;
            result.skippedBackgroundTriangleCount +=
                    scene.meshes[instance.meshIndex].indices.size() / 3u;
            continue;
        }
        if (!seenInstances.insert(DuplicateKey(instance)).second) {
            ++result.duplicateInstanceCount;
            continue;
        }

        const PhysicsSandboxRenderMesh &mesh = scene.meshes[instance.meshIndex];
        const std::uint8_t surfaceMaterialId =
                scene.materials[instance.materialIndex].surfaceMaterialId;
        if (IsGrassBladeGeometry(mesh, instance, surfaceMaterialId)) {
            ++result.skippedGrassBladeInstanceCount;
            result.skippedGrassBladeTriangleCount +=
                    mesh.indices.size() / 3u;
            continue;
        }
        result.telemetry.acceptedSourceTriangleCount +=
                mesh.indices.size() / 3u;
        const WorldBounds worldBounds =
                TransformBounds(mesh, instance.worldTransform);
        const StaticVisualMaterialState materialState =
                MaterialStateFor(options, instance.materialIndex);
        const std::optional<SpatialCell> spatialCell = SelectSpatialCell(
                worldBounds, options.cellSize, materialState.alphaMode);
        if (spatialCell.has_value()) {
            ++result.telemetry.spatiallyPartitionedInstanceCount;
        }
        if (materialState.alphaMode == StaticVisualAlphaMode::Unknown) {
            ++result.telemetry.unknownAlphaInstanceCount;
        }

        const BuildClock::time_point classificationStarted = BuildClock::now();
        const MaterialSemanticContext context{
                instance.provenance.blockName,
                instance.provenance.descriptorPath,
                instance.provenance.sceneObjectId,
                instance.provenance.componentIndex, instance.purpose,
                IsGrassGroundCover(mesh, instance)};
        const ReplacementMaterialClass materialClass = ClassifyMaterial(
                scene.materials[instance.materialIndex], context);
        result.telemetry.materialClassificationNanoseconds +=
                ElapsedNanoseconds(classificationStarted, BuildClock::now());
        const bool defaultVisible = IsDefaultVisualInstance(
                instance.purpose, instance.provenance.blockName);
        const BatchKey key = MakeBatchKey(
                instance.materialIndex, materialClass, materialState,
                instance.purpose, mesh.hasVertexColors, defaultVisible,
                spatialCell.has_value(),
                spatialCell.has_value() ? spatialCell->x : 0,
                spatialCell.has_value() ? spatialCell->z : 0);

        const BuildClock::time_point geometryStarted = BuildClock::now();
        auto &preparedMesh = preparedMeshes[instance.meshIndex];
        if (!preparedMesh.has_value()) {
            preparedMesh.emplace(PrepareMesh(mesh));
        }
        AppendInstance(accumulators[key], *preparedMesh, mesh.indices,
                       instance.worldTransform,
                       materialState.worldXz ? (1.0f / 16.0f) : 0.0f);
        result.telemetry.geometryBuildNanoseconds +=
                ElapsedNanoseconds(geometryStarted, BuildClock::now());

        if (defaultVisible) {
            ++result.defaultVisibleInstanceCount;
            result.defaultTriangleCount += mesh.indices.size() / 3u;
            if (!hasDefaultBounds) {
                result.defaultBoundsMin = worldBounds.minimum;
                result.defaultBoundsMax = worldBounds.maximum;
                hasDefaultBounds = true;
            } else {
                ExpandBounds(worldBounds.minimum, result.defaultBoundsMin,
                             result.defaultBoundsMax);
                ExpandBounds(worldBounds.maximum, result.defaultBoundsMin,
                             result.defaultBoundsMax);
            }
        }
    }

    const BuildClock::time_point finalizationStarted = BuildClock::now();
    std::set<std::uint32_t> submittedMaterials;
    std::set<std::pair<std::int64_t, std::int64_t>> populatedSpatialCells;
    result.batches.reserve(accumulators.size());
    for (auto &[key, accumulator] : accumulators) {
        if (accumulator.indices.empty()) {
            continue;
        }
        StaticVisualBatch batch;
        batch.vertices = QByteArray(
                reinterpret_cast<const char *>(accumulator.vertices.data()),
                static_cast<qsizetype>(accumulator.vertices.size() *
                                       sizeof(VisualVertex)));
        batch.indices = QByteArray(
                reinterpret_cast<const char *>(accumulator.indices.data()),
                static_cast<qsizetype>(accumulator.indices.size() *
                                       sizeof(std::uint32_t)));
        batch.boundsMin = accumulator.boundsMin;
        batch.boundsMax = accumulator.boundsMax;
        batch.materialClass = key.materialClass;
        batch.sourceMaterialIndex = key.sourceMaterialIndex;
        batch.alphaMode = key.alphaMode;
        batch.purpose = key.purpose;
        batch.hasVertexColors = key.vertexColors;
        batch.doubleSided = key.doubleSided;
        batch.defaultVisible = key.defaultVisible;
        batch.spatiallyPartitioned = key.spatiallyPartitioned;
        batch.cellX = key.cellX;
        batch.cellZ = key.cellZ;
        batch.sourceInstanceCount = accumulator.sourceInstanceCount;
        batch.triangleCount = accumulator.indices.size() / 3u;
        result.telemetry.submittedTriangleCount += batch.triangleCount;
        submittedMaterials.insert(key.sourceMaterialIndex);
        if (key.spatiallyPartitioned) {
            ++result.telemetry.spatialBatchCount;
            populatedSpatialCells.emplace(key.cellX, key.cellZ);
        } else {
            ++result.telemetry.unpartitionedBatchCount;
        }
        result.batches.push_back(std::move(batch));
    }
    result.telemetry.submittedBatchCount = result.batches.size();
    result.telemetry.submittedMaterialCount = submittedMaterials.size();
    result.telemetry.populatedSpatialCellCount =
            populatedSpatialCells.size();
    result.telemetry.finalizationNanoseconds = ElapsedNanoseconds(
            finalizationStarted, BuildClock::now());
    result.telemetry.totalBuildNanoseconds =
            ElapsedNanoseconds(buildStarted, BuildClock::now());
    return result;
}

}  // namespace forevertas::viewer
