#include "viewer/ray_tracing_scene.h"

#include "viewer/material_classifier.h"

#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

namespace forevertas::viewer {
namespace {

constexpr std::uint32_t kLeafTriangleCount = 4u;
constexpr std::uint32_t kMaterialClassCount = 17u;

struct GpuVertex {
    std::array<float, 4> position{};
    std::array<float, 4> normal{};
    std::array<float, 4> tangent{};
    std::array<float, 4> uv{};
    std::array<float, 4> color{};
};

struct GpuTriangle {
    std::array<std::uint32_t, 4> data{};
};

struct GpuBvhNode {
    std::array<float, 4> boundsMin{};
    std::array<float, 4> boundsMax{};
    std::array<std::uint32_t, 4> metadata{};
};

struct GpuMaterial {
    std::array<float, 4> surface{};
    std::array<float, 4> optical{};
    std::array<float, 4> transmission{};
};

static_assert(sizeof(GpuVertex) == 80u);
static_assert(sizeof(GpuTriangle) == 16u);
static_assert(sizeof(GpuBvhNode) == 48u);
static_assert(sizeof(GpuMaterial) == 48u);

struct BuildTriangle {
    GpuTriangle triangle;
    QVector3D boundsMin;
    QVector3D boundsMax;
    QVector3D centroid;
};

template <typename T>
QByteArray ToBytes(const std::vector<T> &values) {
    if (values.empty()) return {};
    return QByteArray(
            reinterpret_cast<const char *>(values.data()),
            static_cast<qsizetype>(values.size() * sizeof(T)));
}

QVector3D Minimum(const QVector3D &a, const QVector3D &b) {
    return {std::min(a.x(), b.x()), std::min(a.y(), b.y()),
            std::min(a.z(), b.z())};
}

QVector3D Maximum(const QVector3D &a, const QVector3D &b) {
    return {std::max(a.x(), b.x()), std::max(a.y(), b.y()),
            std::max(a.z(), b.z())};
}

int LongestAxis(const QVector3D &extent) {
    if (extent.y() > extent.x() && extent.y() >= extent.z()) return 1;
    if (extent.z() > extent.x() && extent.z() > extent.y()) return 2;
    return 0;
}

float AxisValue(const QVector3D &value, int axis) {
    if (axis == 1) return value.y();
    if (axis == 2) return value.z();
    return value.x();
}

std::uint32_t BuildBvhNode(std::vector<BuildTriangle> &triangles,
                           std::uint32_t first,
                           std::uint32_t count,
                           std::vector<GpuBvhNode> &nodes) {
    const float infinity = std::numeric_limits<float>::infinity();
    QVector3D boundsMin(infinity, infinity, infinity);
    QVector3D boundsMax(-infinity, -infinity, -infinity);
    QVector3D centroidMin(infinity, infinity, infinity);
    QVector3D centroidMax(-infinity, -infinity, -infinity);
    for (std::uint32_t index = first; index < first + count; ++index) {
        boundsMin = Minimum(boundsMin, triangles[index].boundsMin);
        boundsMax = Maximum(boundsMax, triangles[index].boundsMax);
        centroidMin = Minimum(centroidMin, triangles[index].centroid);
        centroidMax = Maximum(centroidMax, triangles[index].centroid);
    }

    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(nodes.size());
    nodes.emplace_back();
    GpuBvhNode &node = nodes.back();
    node.boundsMin = {boundsMin.x(), boundsMin.y(), boundsMin.z(), 0.0f};
    node.boundsMax = {boundsMax.x(), boundsMax.y(), boundsMax.z(), 0.0f};

    if (count <= kLeafTriangleCount) {
        node.metadata = {0u, 0u, first, count};
        return nodeIndex;
    }

    const int axis = LongestAxis(centroidMax - centroidMin);
    const std::uint32_t leftCount = count / 2u;
    const auto begin = triangles.begin() + first;
    const auto middle = begin + leftCount;
    const auto end = begin + count;
    std::nth_element(begin, middle, end,
                     [axis](const BuildTriangle &a,
                            const BuildTriangle &b) {
                         return AxisValue(a.centroid, axis) <
                                AxisValue(b.centroid, axis);
                     });

    const std::uint32_t left =
            BuildBvhNode(triangles, first, leftCount, nodes);
    const std::uint32_t right = BuildBvhNode(
            triangles, first + leftCount, count - leftCount, nodes);
    nodes[nodeIndex].metadata = {left, right, 0u, 0u};
    return nodeIndex;
}

}  // namespace

std::shared_ptr<const RayTracingSceneData> BuildRayTracingScene(
        const std::vector<StaticVisualBatch> &batches) {
    auto result = std::make_shared<RayTracingSceneData>();
    std::vector<GpuVertex> vertices;
    std::vector<BuildTriangle> buildTriangles;

    for (const StaticVisualBatch &batch : batches) {
        if (!batch.defaultVisible ||
            batch.vertices.size() % StaticVisualVertexStride != 0 ||
            batch.indices.size() %
                            static_cast<qsizetype>(sizeof(std::uint32_t)) !=
                    0) {
            continue;
        }

        const std::uint32_t baseVertex =
                static_cast<std::uint32_t>(vertices.size());
        const qsizetype sourceVertexCount =
                batch.vertices.size() / StaticVisualVertexStride;
        const float *const sourceVertices =
                reinterpret_cast<const float *>(batch.vertices.constData());
        vertices.reserve(vertices.size() +
                         static_cast<std::size_t>(sourceVertexCount));
        for (qsizetype index = 0; index < sourceVertexCount; ++index) {
            const float *const source = sourceVertices + index * 17;
            GpuVertex vertex;
            vertex.position = {source[0], source[1], source[2], 1.0f};
            vertex.normal = {source[3], source[4], source[5], 0.0f};
            vertex.tangent = {source[6], source[7], source[8], 0.0f};
            vertex.uv = {source[9], source[10], source[11], source[12]};
            vertex.color = {source[13], source[14], source[15], source[16]};
            vertices.push_back(vertex);
        }

        const std::uint32_t *const indices =
                reinterpret_cast<const std::uint32_t *>(
                        batch.indices.constData());
        const qsizetype indexCount =
                batch.indices.size() /
                static_cast<qsizetype>(sizeof(std::uint32_t));
        const std::uint32_t materialClass =
                static_cast<std::uint32_t>(batch.materialClass);
        buildTriangles.reserve(
                buildTriangles.size() +
                static_cast<std::size_t>(indexCount / 3));
        for (qsizetype index = 0; index + 2 < indexCount; index += 3) {
            const std::uint32_t localA = indices[index];
            const std::uint32_t localB = indices[index + 1];
            const std::uint32_t localC = indices[index + 2];
            if (localA >= static_cast<std::uint32_t>(sourceVertexCount) ||
                localB >= static_cast<std::uint32_t>(sourceVertexCount) ||
                localC >= static_cast<std::uint32_t>(sourceVertexCount)) {
                continue;
            }
            const std::uint32_t a = baseVertex + localA;
            const std::uint32_t b = baseVertex + localB;
            const std::uint32_t c = baseVertex + localC;
            const QVector3D pa(vertices[a].position[0],
                               vertices[a].position[1],
                               vertices[a].position[2]);
            const QVector3D pb(vertices[b].position[0],
                               vertices[b].position[1],
                               vertices[b].position[2]);
            const QVector3D pc(vertices[c].position[0],
                               vertices[c].position[1],
                               vertices[c].position[2]);
            BuildTriangle triangle;
            triangle.triangle.data = {a, b, c, materialClass};
            triangle.boundsMin = Minimum(Minimum(pa, pb), pc);
            triangle.boundsMax = Maximum(Maximum(pa, pb), pc);
            triangle.centroid = (pa + pb + pc) / 3.0f;
            buildTriangles.push_back(triangle);
        }
    }

    std::vector<GpuBvhNode> nodes;
    if (!buildTriangles.empty()) {
        nodes.reserve(buildTriangles.size() * 2u);
        BuildBvhNode(buildTriangles, 0u,
                     static_cast<std::uint32_t>(buildTriangles.size()),
                     nodes);
    }

    std::vector<GpuTriangle> triangles;
    triangles.reserve(buildTriangles.size());
    for (const BuildTriangle &triangle : buildTriangles) {
        triangles.push_back(triangle.triangle);
    }

    std::vector<GpuMaterial> materials;
    materials.reserve(kMaterialClassCount);
    for (std::uint32_t index = 0u; index < kMaterialClassCount; ++index) {
        const ReplacementMaterial replacement =
                ReplacementFor(static_cast<ReplacementMaterialClass>(index));
        GpuMaterial material;
        material.surface = {
                replacement.roughness, replacement.metalness,
                replacement.emissiveStrength,
                replacement.applyVertexColors ? 1.0f : 0.0f};
        material.optical = {
                replacement.normalStrength, replacement.specularAmount,
                replacement.clearcoatAmount,
                replacement.clearcoatRoughness};
        material.transmission = {
                replacement.transmissionFactor,
                replacement.indexOfRefraction,
                replacement.normalTexture.isEmpty() ? 0.0f : 1.0f,
                replacement.roughnessTexture.isEmpty() ? 0.0f : 1.0f};
        materials.push_back(material);
    }

    result->vertices = ToBytes(vertices);
    result->triangles = ToBytes(triangles);
    result->bvhNodes = ToBytes(nodes);
    result->materials = ToBytes(materials);
    result->vertexCount = static_cast<std::uint32_t>(vertices.size());
    result->triangleCount =
            static_cast<std::uint32_t>(triangles.size());
    result->bvhNodeCount = static_cast<std::uint32_t>(nodes.size());
    result->materialCount = static_cast<std::uint32_t>(materials.size());
    return result;
}

}  // namespace forevertas::viewer
