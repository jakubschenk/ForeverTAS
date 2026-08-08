#ifndef FOREVERTAS_VIEWER_VISUAL_SCENE_PIPELINE_H
#define FOREVERTAS_VIEWER_VISUAL_SCENE_PIPELINE_H

#include "viewer/material_classifier.h"

#include <forevervalidator/experimental/physics_sandbox.h>

#include <QByteArray>
#include <QString>
#include <QVector3D>

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace forevertas::viewer {

constexpr int StaticVisualVertexStride = 17 * sizeof(float);
constexpr float DefaultStaticVisualCellSize = 128.0f;

enum class StaticVisualAlphaMode : std::uint8_t {
    Opaque,
    Masked,
    Blended,
    Additive,
    Subtractive,
    Unknown,
};

struct StaticVisualMaterialState {
    StaticVisualAlphaMode alphaMode = StaticVisualAlphaMode::Unknown;
    bool doubleSided = false;
};

struct StaticVisualBatchOptions {
    // Indexed by PhysicsSandboxRenderInstance::materialIndex. Missing entries
    // intentionally remain Unknown so callers cannot accidentally guess alpha
    // semantics from a material name.
    std::vector<StaticVisualMaterialState> materialStates;
    float cellSize = DefaultStaticVisualCellSize;
};

struct CameraClipPlanes {
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

CameraClipPlanes CalculateCameraClipPlanes(const QVector3D &cameraPosition,
                                           float cameraDistance,
                                           const QVector3D &boundsMin,
                                           const QVector3D &boundsMax);

bool IsDefaultVisualPurpose(
        forevervalidator::experimental::PhysicsSandboxScenePurpose purpose);
bool IsDefaultVisualInstance(
        forevervalidator::experimental::PhysicsSandboxScenePurpose purpose,
        std::string_view blockName);

struct StaticVisualBatch {
    QByteArray vertices;
    QByteArray indices;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
    ReplacementMaterialClass materialClass = ReplacementMaterialClass::Unknown;
    std::uint32_t sourceMaterialIndex =
            std::numeric_limits<std::uint32_t>::max();
    StaticVisualAlphaMode alphaMode = StaticVisualAlphaMode::Unknown;
    forevervalidator::experimental::PhysicsSandboxScenePurpose purpose =
            forevervalidator::experimental::PhysicsSandboxScenePurpose::
                    Environment;
    bool hasVertexColors = false;
    bool doubleSided = false;
    bool defaultVisible = false;
    bool spatiallyPartitioned = false;
    std::int64_t cellX = 0;
    std::int64_t cellZ = 0;
    std::uint64_t sourceInstanceCount = 0u;
    std::uint64_t triangleCount = 0u;
};

struct StaticVisualPipelineTelemetry {
    // Source triangles accepted after visibility, LOD, background, duplicate,
    // and grass-blade filtering, before any terrain tessellation.
    std::uint64_t acceptedSourceTriangleCount = 0u;
    // Triangles emitted by this pipeline and submitted through its batches.
    std::uint64_t submittedTriangleCount = 0u;
    std::uint64_t submittedBatchCount = 0u;
    std::uint64_t submittedMaterialCount = 0u;
    // This is the number of populated XZ cells. Camera-visible cells are a
    // renderer concern and cannot be truthfully measured during scene build.
    std::uint64_t populatedSpatialCellCount = 0u;
    std::uint64_t spatialBatchCount = 0u;
    std::uint64_t unpartitionedBatchCount = 0u;
    std::uint64_t spatiallyPartitionedInstanceCount = 0u;
    std::uint64_t unknownAlphaInstanceCount = 0u;

    std::uint64_t materialClassificationNanoseconds = 0u;
    std::uint64_t geometryBuildNanoseconds = 0u;
    std::uint64_t finalizationNanoseconds = 0u;
    std::uint64_t totalBuildNanoseconds = 0u;
};

struct StaticVisualBatchResult {
    std::vector<StaticVisualBatch> batches;
    StaticVisualPipelineTelemetry telemetry;
    QVector3D defaultBoundsMin{};
    QVector3D defaultBoundsMax{};
    std::uint64_t sourceMeshCount = 0u;
    std::uint64_t visibleSourceInstanceCount = 0u;
    std::uint64_t defaultVisibleInstanceCount = 0u;
    std::uint64_t defaultTriangleCount = 0u;
    std::uint64_t duplicateInstanceCount = 0u;
    std::uint64_t invalidInstanceCount = 0u;
    std::uint64_t skippedBackgroundInstanceCount = 0u;
    std::uint64_t skippedBackgroundTriangleCount = 0u;
    std::uint64_t skippedGrassBladeInstanceCount = 0u;
    std::uint64_t skippedGrassBladeTriangleCount = 0u;
};

StaticVisualBatchResult BuildStaticVisualBatches(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene);
StaticVisualBatchResult BuildStaticVisualBatches(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene,
        const StaticVisualBatchOptions &options);

}  // namespace forevertas::viewer

#endif
