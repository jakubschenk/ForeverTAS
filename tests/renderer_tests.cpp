#include "viewer/material_classifier.h"
#include "viewer/race_geometry.h"
#include "viewer/ray_tracing_scene.h"
#include "viewer/visual_scene_pipeline.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using forevertas::viewer::ClassifyMaterial;
using forevertas::viewer::MaterialSemanticContext;
using forevertas::viewer::ReplacementFor;
using forevertas::viewer::ReplacementMaterialClass;
using forevertas::viewer::StaticVisualAlphaMode;
using forevertas::viewer::StaticVisualBatch;
using forevertas::viewer::StaticVisualBatchOptions;
using forevertas::viewer::StaticVisualMaterialState;
using forevervalidator::experimental::PhysicsSandboxRenderInstance;
using forevervalidator::experimental::PhysicsSandboxRenderLayer;
using forevervalidator::experimental::PhysicsSandboxRenderMaterial;
using forevervalidator::experimental::PhysicsSandboxRenderMesh;
using forevervalidator::experimental::PhysicsSandboxRenderScene;
using forevervalidator::experimental::PhysicsSandboxScenePurpose;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

PhysicsSandboxRenderMaterial Named(const char *name) {
    PhysicsSandboxRenderMaterial material;
    material.sourcePath = name;
    material.surfaceMaterialId = 0xffu;
    return material;
}

bool TestClassification() {
    struct Case {
        const char *name;
        ReplacementMaterialClass expected;
    };
    constexpr std::array<Case, 14> cases{{
            {"StadiumRoadAsphalt", ReplacementMaterialClass::Asphalt},
            {"ConcreteWall", ReplacementMaterialClass::Concrete},
            {"DirtGround", ReplacementMaterialClass::Dirt},
            {"GrassField", ReplacementMaterialClass::Grass},
            {"SteelRail", ReplacementMaterialClass::Metal},
            {"PaintedMetalRed", ReplacementMaterialClass::PaintedMetal},
            {"PlasticTrim", ReplacementMaterialClass::Plastic},
            {"RubberBarrier", ReplacementMaterialClass::Rubber},
            {"GlassWindow", ReplacementMaterialClass::Glass},
            {"SponsorSign", ReplacementMaterialClass::Signage},
            {"NeonEmissive", ReplacementMaterialClass::Emissive},
            {"WaterPool", ReplacementMaterialClass::Water},
            {"NeutralDefault", ReplacementMaterialClass::Neutral},
            {"UnclassifiedSurface", ReplacementMaterialClass::Unknown},
    }};
    bool okay = true;
    for (const Case &test : cases) {
        okay &= Check(
                ClassifyMaterial(Named(test.name)) == test.expected,
                test.name);
    }

    PhysicsSandboxRenderMaterial remapped =
            Named("Materials/Replacement/Grass");
    remapped.modelPath = "Models/Original/Concrete";
    okay &= Check(ClassifyMaterial(remapped) ==
                          ReplacementMaterialClass::Grass,
                  "final remap material did not take classification priority");
    PhysicsSandboxRenderMaterial water = Named("Generic");
    water.water = true;
    okay &= Check(ClassifyMaterial(water) ==
                          ReplacementMaterialClass::Water,
                  "water render metadata did not override generic paths");
    PhysicsSandboxRenderMaterial surfaceFallback =
            Named("UnclassifiedSurface");
    surfaceFallback.surfaceMaterialId = 2u;
    okay &= Check(ClassifyMaterial(surfaceFallback) ==
                          ReplacementMaterialClass::Grass,
                  "surface-material fallback did not classify grass");
    PhysicsSandboxRenderMaterial renderTarget =
            Named("UnclassifiedSurface");
    renderTarget.renderTarget = true;
    renderTarget.surfaceMaterialId = 4u;
    okay &= Check(ClassifyMaterial(renderTarget) ==
                          ReplacementMaterialClass::Neutral,
                  "render-target fallback was not deterministic");

    const auto contextual = [](std::uint8_t surface, const char *block,
                               const char *component = "") {
        PhysicsSandboxRenderMaterial material = Named("UnclassifiedSurface");
        material.surfaceMaterialId = surface;
        MaterialSemanticContext context;
        context.blockName = block;
        context.componentIdentity = component;
        return ClassifyMaterial(material, context);
    };
    okay &= Check(contextual(7u, "StadiumRoadMainTurbo") ==
                          ReplacementMaterialClass::Turbo,
                  "turbo surface did not receive its semantic override");
    okay &= Check(contextual(28u, "StadiumRoadMainCheckpoint") ==
                          ReplacementMaterialClass::Checkpoint,
                  "checkpoint panel did not receive its semantic override");
    okay &= Check(contextual(28u, "StadiumRoadMainFinishLine") ==
                          ReplacementMaterialClass::StartFinish,
                  "finish panel did not receive its semantic override");
    okay &= Check(contextual(16u, "StadiumRoadMain") ==
                          ReplacementMaterialClass::Asphalt,
                  "road provenance did not classify asphalt");
    okay &= Check(contextual(6u, "StadiumRoadDirtHigh") ==
                          ReplacementMaterialClass::Dirt,
                  "dirt-road provenance did not classify dirt");
    okay &= Check(contextual(2u, "StadiumGrass") ==
                          ReplacementMaterialClass::Grass,
                  "grass provenance did not classify grass");
    PhysicsSandboxRenderMaterial grassClip =
            Named("UnclassifiedSurface");
    grassClip.surfaceMaterialId = 2u;
    MaterialSemanticContext grassClipContext;
    grassClipContext.blockName = "StadiumGrassClip";
    grassClipContext.purpose = PhysicsSandboxScenePurpose::Clip;
    okay &= Check(ClassifyMaterial(grassClip, grassClipContext) ==
                          ReplacementMaterialClass::Grass,
                  "grass clip did not classify as ground grass");
    PhysicsSandboxRenderMaterial groundCover =
            Named("UnclassifiedSurface");
    groundCover.surfaceMaterialId = 0u;
    MaterialSemanticContext groundCoverContext;
    groundCoverContext.blockName = "StadiumRoadDirtHigh";
    groundCoverContext.grassGroundCover = true;
    okay &= Check(ClassifyMaterial(groundCover, groundCoverContext) ==
                          ReplacementMaterialClass::Grass,
                  "flat block ground cover did not classify as grass");
    okay &= Check(contextual(13u, "StadiumPool") ==
                          ReplacementMaterialClass::Water,
                  "pool provenance did not classify water");
    okay &= Check(contextual(28u, "", "Flags") ==
                          ReplacementMaterialClass::Signage,
                  "component identity did not classify signage");
    okay &= Check(contextual(22u, "StadiumRoadMainStartLine") ==
                          ReplacementMaterialClass::Emissive,
                  "start-line light did not classify as emissive");
    return okay;
}

bool TestReplacementParametersAndTextures() {
    bool okay = true;
    const auto emissive =
            ReplacementFor(ReplacementMaterialClass::Emissive);
    const auto grass =
            ReplacementFor(ReplacementMaterialClass::Grass);
    const auto asphalt =
            ReplacementFor(ReplacementMaterialClass::Asphalt);
    const auto dirt = ReplacementFor(ReplacementMaterialClass::Dirt);
    const auto concrete =
            ReplacementFor(ReplacementMaterialClass::Concrete);
    okay &= Check(emissive.emissiveStrength > 0.0f,
                  "emissive replacement has no emission");
    okay &= Check(!asphalt.applyVertexColors &&
                          !grass.applyVertexColors &&
                          !dirt.applyVertexColors &&
                          !concrete.applyVertexColors &&
                          std::fabs(asphalt.worldUvScale - 0.25f) < 0.001f &&
                          std::fabs(grass.worldUvScale - 0.25f) < 0.001f &&
                          std::fabs(dirt.worldUvScale - 0.25f) < 0.001f &&
                          std::fabs(concrete.worldUvScale - 0.25f) < 0.001f,
                  "driving surfaces do not use consistent world-space UVs");
    okay &= Check(
            !ReplacementFor(ReplacementMaterialClass::Neutral)
                     .applyVertexColors &&
                    ReplacementFor(ReplacementMaterialClass::Unknown)
                            .applyVertexColors,
            "replacement vertex-color policy differs from the ray tracer");

    constexpr std::array<const char *, 17> names{{
            "asphalt", "concrete", "dirt", "grass", "metal",
            "painted_metal", "plastic", "rubber", "glass", "signage",
            "emissive", "turbo", "checkpoint", "start_finish", "water",
            "neutral", "unknown"}};
    struct DetailExpectation {
        ReplacementMaterialClass materialClass;
        const char *stem;
    };
    constexpr std::array<DetailExpectation, 5> detailExpectations{{
            {ReplacementMaterialClass::Asphalt, "asphalt"},
            {ReplacementMaterialClass::Dirt, "dirt"},
            {ReplacementMaterialClass::Metal, "metal"},
            {ReplacementMaterialClass::PaintedMetal, "painted_metal"},
            {ReplacementMaterialClass::Rubber, "rubber"},
    }};
    QSet<QByteArray> baseTextureHashes;
    QSet<QByteArray> normalTextureHashes;
    QSet<QByteArray> roughnessTextureHashes;
    QSet<QString> expectedAssetFiles;
    const QString root = QStringLiteral(FOREVERTAS_SOURCE_DIR) +
            QStringLiteral("/assets/materials/");
    for (std::size_t index = 0u; index < names.size(); ++index) {
        const char *const name = names[index];
        const auto materialClass =
                static_cast<ReplacementMaterialClass>(index);
        const auto replacement = ReplacementFor(materialClass);
        const QString baseFileName = QString::fromLatin1(name) +
                QStringLiteral("_base.png");
        expectedAssetFiles.insert(baseFileName);
        const QString basePath = root + QString::fromLatin1(name) +
                QStringLiteral("_base.png");
        const QImage baseImage(basePath);
        okay &= Check(
                replacement.materialClass == materialClass &&
                        !replacement.name.isEmpty() &&
                        replacement.debugColor.isValid() &&
                        replacement.baseTexture ==
                                QStringLiteral("qrc:/materials/") +
                                        baseFileName,
                "replacement identity metadata was invalid");
        okay &= Check(
                std::isfinite(replacement.roughness) &&
                        replacement.roughness >= 0.0f &&
                        replacement.roughness <= 1.0f &&
                        std::isfinite(replacement.metalness) &&
                        replacement.metalness >= 0.0f &&
                        replacement.metalness <= 1.0f &&
                        std::isfinite(replacement.normalStrength) &&
                        replacement.normalStrength >= 0.0f &&
                        replacement.normalStrength <= 1.0f &&
                        std::isfinite(replacement.specularAmount) &&
                        replacement.specularAmount >= 0.0f &&
                        replacement.specularAmount <= 1.0f &&
                        std::isfinite(replacement.clearcoatAmount) &&
                        replacement.clearcoatAmount >= 0.0f &&
                        replacement.clearcoatAmount <= 1.0f &&
                        std::isfinite(replacement.clearcoatRoughness) &&
                        replacement.clearcoatRoughness >= 0.0f &&
                        replacement.clearcoatRoughness <= 1.0f &&
                        std::isfinite(replacement.transmissionFactor) &&
                        replacement.transmissionFactor >= 0.0f &&
                        replacement.transmissionFactor <= 1.0f &&
                        std::isfinite(replacement.indexOfRefraction) &&
                        replacement.indexOfRefraction >= 1.0f &&
                        replacement.indexOfRefraction <= 3.0f,
                "replacement PBR scalar metadata was outside valid bounds");
        okay &= Check(!baseImage.isNull(),
                      "replacement base texture did not load");
        okay &= Check(baseImage.width() == 512 &&
                              baseImage.height() == 512,
                      "replacement base texture was not exactly 512 square");
        if (std::string(name) != "concrete") {
            QSet<QRgb> sampledBaseColors;
            for (int y = 0; y < baseImage.height(); y += 4) {
                for (int x = 0; x < baseImage.width(); x += 4) {
                    sampledBaseColors.insert(baseImage.pixel(x, y));
                }
            }
            okay &= Check(
                    sampledBaseColors.size() > 64,
                    "replacement base texture is a low-information placeholder");
        }
        QFile baseFile(basePath);
        okay &= Check(baseFile.open(QIODevice::ReadOnly),
                      "replacement texture bytes were not readable");
        if (baseFile.isOpen()) {
            baseTextureHashes.insert(QCryptographicHash::hash(
                    baseFile.readAll(), QCryptographicHash::Sha256));
        }
    }
    okay &= Check(baseTextureHashes.size() ==
                          static_cast<qsizetype>(names.size()),
                  "replacement base textures are not visibly distinct assets");

    for (const DetailExpectation &expectation : detailExpectations) {
        const QString stem = QString::fromLatin1(expectation.stem);
        const QString normalFileName =
                stem + QStringLiteral("_normal.png");
        const QString roughnessFileName =
                stem + QStringLiteral("_roughness.png");
        expectedAssetFiles.insert(normalFileName);
        expectedAssetFiles.insert(roughnessFileName);
        const auto replacement = ReplacementFor(expectation.materialClass);
        okay &= Check(
                replacement.normalTexture ==
                                QStringLiteral("qrc:/materials/") +
                                        normalFileName &&
                        replacement.roughnessTexture ==
                                QStringLiteral("qrc:/materials/") +
                                        roughnessFileName &&
                        replacement.normalStrength > 0.0f,
                "photo-scanned replacement did not expose its PBR maps");

        const QString normalPath = root + normalFileName;
        const QString roughnessPath = root + roughnessFileName;
        const QImage normalImage(normalPath);
        const QImage roughnessImage(roughnessPath);
        okay &= Check(!normalImage.isNull() &&
                              normalImage.width() == 512 &&
                              normalImage.height() == 512,
                      "replacement normal map was not a 512 square image");
        okay &= Check(!roughnessImage.isNull() &&
                              roughnessImage.width() == 512 &&
                              roughnessImage.height() == 512,
                      "replacement roughness map was not a 512 square image");

        QSet<QRgb> sampledNormalColors;
        QSet<QRgb> sampledRoughnessColors;
        std::int64_t normalRed = 0;
        std::int64_t normalGreen = 0;
        std::int64_t normalBlue = 0;
        bool roughnessIsGrayscale = true;
        for (int y = 0; y < 512; y += 4) {
            for (int x = 0; x < 512; x += 4) {
                const QColor normal = normalImage.pixelColor(x, y);
                const QColor roughness = roughnessImage.pixelColor(x, y);
                sampledNormalColors.insert(normal.rgba());
                sampledRoughnessColors.insert(roughness.rgba());
                normalRed += normal.red();
                normalGreen += normal.green();
                normalBlue += normal.blue();
                roughnessIsGrayscale &=
                        roughness.red() == roughness.green() &&
                        roughness.green() == roughness.blue();
            }
        }
        okay &= Check(
                sampledNormalColors.size() > 64 &&
                        normalBlue > normalRed &&
                        normalBlue > normalGreen,
                "replacement normal map was not tangent-space detail");
        okay &= Check(
                sampledRoughnessColors.size() > 8 && roughnessIsGrayscale,
                "replacement roughness map lacked scalar surface detail");

        QFile normalFile(normalPath);
        QFile roughnessFile(roughnessPath);
        okay &= Check(normalFile.open(QIODevice::ReadOnly) &&
                              roughnessFile.open(QIODevice::ReadOnly),
                      "replacement PBR texture bytes were not readable");
        if (normalFile.isOpen()) {
            normalTextureHashes.insert(QCryptographicHash::hash(
                    normalFile.readAll(), QCryptographicHash::Sha256));
        }
        if (roughnessFile.isOpen()) {
            roughnessTextureHashes.insert(QCryptographicHash::hash(
                    roughnessFile.readAll(), QCryptographicHash::Sha256));
        }
    }
    okay &= Check(
            normalTextureHashes.size() ==
                            static_cast<qsizetype>(detailExpectations.size()) &&
                    roughnessTextureHashes.size() ==
                            static_cast<qsizetype>(detailExpectations.size()),
            "replacement PBR maps were duplicated assets");

    for (std::size_t index = 0u; index < names.size(); ++index) {
        const auto materialClass =
                static_cast<ReplacementMaterialClass>(index);
        const bool expectsDetail = std::any_of(
                detailExpectations.cbegin(), detailExpectations.cend(),
                [materialClass](const DetailExpectation &expectation) {
                    return expectation.materialClass == materialClass;
                });
        const auto replacement = ReplacementFor(materialClass);
        okay &= Check(
                expectsDetail ||
                        (replacement.normalTexture.isEmpty() &&
                         replacement.roughnessTexture.isEmpty() &&
                         replacement.normalStrength == 0.0f),
                "generated or flat replacement fabricated PBR detail maps");
    }

    const auto glass = ReplacementFor(ReplacementMaterialClass::Glass);
    const auto water = ReplacementFor(ReplacementMaterialClass::Water);
    const auto metal = ReplacementFor(ReplacementMaterialClass::Metal);
    const auto paintedMetal =
            ReplacementFor(ReplacementMaterialClass::PaintedMetal);
    okay &= Check(
            glass.transmissionFactor > 0.8f &&
                    water.transmissionFactor > 0.7f &&
                    glass.indexOfRefraction > water.indexOfRefraction &&
                    metal.specularAmount > dirt.specularAmount &&
                    paintedMetal.clearcoatAmount > 0.0f &&
                    paintedMetal.clearcoatRoughness > 0.0f,
            "replacement optical metadata was not material-specific");

    const QDir materialDirectory(root);
    QSet<QString> actualAssetFiles;
    const QStringList materialFiles = materialDirectory.entryList(
            {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QString &materialFile : materialFiles) {
        actualAssetFiles.insert(materialFile);
    }
    okay &= Check(actualAssetFiles == expectedAssetFiles,
                  "replacement material PNG asset set was not exact");

    const QImage concreteBase(root +
                              QStringLiteral("concrete_base.png"));
    const auto isUniform = [](const QImage &image) {
        if (image.isNull()) {
            return false;
        }
        const QColor color = image.pixelColor(0, 0);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) != color) {
                    return false;
                }
            }
        }
        return true;
    };
    okay &= Check(isUniform(concreteBase),
                  "concrete replacement is not flat gray");
    okay &= Check(concreteBase.pixelColor(0, 0) == QColor("#a4a69f"),
                  "concrete replacement does not use the requested flat values");

    const QImage grassBase(root + QStringLiteral("grass_base.png"));
    std::int64_t grassRed = 0;
    std::int64_t grassGreen = 0;
    std::int64_t grassBlue = 0;
    for (int y = 0; y < grassBase.height(); y += 4) {
        for (int x = 0; x < grassBase.width(); x += 4) {
            const QColor color = grassBase.pixelColor(x, y);
            grassRed += color.red();
            grassGreen += color.green();
            grassBlue += color.blue();
        }
    }
    okay &= Check(grassGreen * 4 > grassRed * 5 &&
                          grassGreen * 6 > grassBlue * 7,
                  "grass ground cover is not recognizably green");

    const QImage turboBase(root + QStringLiteral("turbo_base.png"));
    const auto countArrowPixels =
            [&turboBase](int left, int right, bool yellow) {
        int count = 0;
        for (int y = 0; y < turboBase.height(); ++y) {
            for (int x = left; x < right; ++x) {
                const QColor color = turboBase.pixelColor(x, y);
                const bool match =
                        yellow
                        ? color.red() > 130 && color.green() > 90 &&
                                  color.red() > color.blue() * 1.5 &&
                                  color.green() > color.blue() * 1.4
                        : color.blue() > 110 && color.green() > 100 &&
                                  color.blue() > color.red() * 1.3 &&
                                  color.green() > color.red() * 1.3;
                count += match ? 1 : 0;
            }
        }
        return count;
    };
    const int cyanTail = countArrowPixels(96, 160, false);
    const int cyanHead = countArrowPixels(256, 320, false);
    const int yellowTail = countArrowPixels(288, 352, true);
    const int yellowHead = countArrowPixels(448, 512, true);
    okay &= Check(cyanTail > cyanHead * 2 &&
                          yellowTail > yellowHead * 2,
                  "turbo chevrons do not point right");
    return okay;
}

bool TestClipPlanesAndPurposeFiltering() {
    using forevertas::viewer::CalculateCameraClipPlanes;
    using forevertas::viewer::IsDefaultVisualInstance;
    using forevertas::viewer::IsDefaultVisualPurpose;
    const auto closeCamera = CalculateCameraClipPlanes(
            {0.0f, 2.0f, 30.0f}, 30.0f, {-100.0f, -10.0f, -150.0f},
            {100.0f, 80.0f, 150.0f});
    const auto farCamera = CalculateCameraClipPlanes(
            {0.0f, 2.0f, 300.0f}, 300.0f, {-100.0f, -10.0f, -150.0f},
            {100.0f, 80.0f, 150.0f});
    const auto enclosingSky = CalculateCameraClipPlanes(
            {0.0f, 2.0f, 3.0f}, 3.0f, {-28000.0f, -15000.0f, -28000.0f},
            {29000.0f, 15000.0f, 29000.0f});
    bool okay =
            Check(closeCamera.nearPlane >= 0.1f &&
                          closeCamera.farPlane > closeCamera.nearPlane &&
                          closeCamera.farPlane < 5000.0f,
                  "camera clip planes retained a forced 5000-unit far plane");
    okay &= Check(farCamera.nearPlane > closeCamera.nearPlane &&
                          farCamera.farPlane > closeCamera.farPlane,
                  "camera clip planes did not respond to camera distance");
    okay &= Check(enclosingSky.farPlane > 5000.0f &&
                          enclosingSky.nearPlane < 1.0f &&
                          enclosingSky.farPlane / enclosingSky.nearPlane <=
                                  50001.0f,
                  "enclosing sky bounds made close camera use unusable planes");
    okay &= Check(
            IsDefaultVisualPurpose(PhysicsSandboxScenePurpose::PlacedBlock) &&
                    IsDefaultVisualPurpose(
                            PhysicsSandboxScenePurpose::Environment) &&
                    IsDefaultVisualPurpose(
                            PhysicsSandboxScenePurpose::Generated) &&
                    !IsDefaultVisualPurpose(PhysicsSandboxScenePurpose::Clip) &&
                    !IsDefaultVisualPurpose(
                            PhysicsSandboxScenePurpose::Helper) &&
                    !IsDefaultVisualPurpose(
                            PhysicsSandboxScenePurpose::CheckpointTrigger) &&
                    IsDefaultVisualInstance(
                            PhysicsSandboxScenePurpose::Clip,
                            "StadiumGrassClip") &&
                    !IsDefaultVisualInstance(
                            PhysicsSandboxScenePurpose::Clip,
                            "CollisionClip"),
            "default purpose filtering lost intentional grass clips");
    return okay;
}

PhysicsSandboxRenderMesh TriangleMesh() {
    PhysicsSandboxRenderMesh mesh;
    mesh.vertices.resize(3u);
    mesh.vertices[0].position = {0.0f, 0.0f, 0.0f};
    mesh.vertices[1].position = {1.0f, 0.0f, 0.0f};
    mesh.vertices[2].position = {0.0f, 0.0f, 1.0f};
    for (auto &vertex : mesh.vertices) {
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    }
    mesh.vertices[1].uv0 = {1.0f, 0.0f};
    mesh.vertices[2].uv0 = {0.0f, 1.0f};
    mesh.indices = {0u, 1u, 2u};
    mesh.subsets = {{0u, 3u, 0u}};
    mesh.boundsMin = {0.0f, 0.0f, 0.0f};
    mesh.boundsMax = {1.0f, 0.0f, 1.0f};
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    return mesh;
}

PhysicsSandboxRenderMesh GrassBladeMesh() {
    PhysicsSandboxRenderMesh mesh;
    mesh.vertices.resize(64u);
    mesh.indices.reserve(96u);
    constexpr float ZStep = 15.75f / 4.0f;
    for (std::uint32_t quad = 0u; quad < 16u; ++quad) {
        const float x = static_cast<float>(quad % 4u) * 8.0f;
        const float z = static_cast<float>(quad / 4u) * ZStep;
        const std::uint32_t base = quad * 4u;
        const std::array<forevervalidator::Vector3, 4> positions{{
                {x, 0.0f, z},
                {x + 8.0f, 0.0f, z},
                {x + 8.0f, 0.5f, z + ZStep},
                {x, 0.5f, z + ZStep}}};
        for (std::uint32_t corner = 0u; corner < 4u; ++corner) {
            auto &vertex = mesh.vertices[base + corner];
            vertex.position = positions[corner];
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1u, base + 2u,
                             base, base + 2u, base + 3u});
    }
    mesh.subsets = {{0u, static_cast<std::uint32_t>(mesh.indices.size()), 0u}};
    mesh.boundsMin = {0.0f, 0.0f, 0.0f};
    mesh.boundsMax = {32.0f, 0.5f, 15.75f};
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    return mesh;
}

bool TestStaticBatching() {
    PhysicsSandboxRenderScene scene;
    scene.meshes.push_back(TriangleMesh());
    PhysicsSandboxRenderMesh groundCoverMesh = TriangleMesh();
    groundCoverMesh.vertices[1].position.x = 32.0f;
    groundCoverMesh.vertices[2].position.z = 32.0f;
    for (auto &vertex : groundCoverMesh.vertices) {
        vertex.normal = {0.312249f, 0.95f, 0.0f};
    }
    groundCoverMesh.boundsMax = {32.0f, 0.0f, 32.0f};
    PhysicsSandboxRenderMesh overlappingGroundCoverMesh = groundCoverMesh;
    scene.meshes.push_back(std::move(groundCoverMesh));
    scene.meshes.push_back(std::move(overlappingGroundCoverMesh));
    scene.meshes.push_back(GrassBladeMesh());
    PhysicsSandboxRenderMaterial turbo;
    turbo.surfaceMaterialId = 7u;
    scene.materials.push_back(turbo);
    PhysicsSandboxRenderMaterial concrete;
    concrete.surfaceMaterialId = 0u;
    scene.materials.push_back(concrete);
    PhysicsSandboxRenderMaterial dirt;
    dirt.surfaceMaterialId = 6u;
    scene.materials.push_back(dirt);

    PhysicsSandboxRenderInstance placed;
    placed.meshIndex = 0u;
    placed.materialIndex = 0u;
    placed.purpose = PhysicsSandboxScenePurpose::PlacedBlock;
    placed.provenance.blockName = "StadiumRoadMainTurbo";
    placed.worldTransform.basisX = {2.0f, 0.0f, 0.0f};
    placed.worldTransform.basisY = {0.0f, 3.0f, 0.0f};
    placed.worldTransform.basisZ = {0.0f, 0.0f, 4.0f};
    placed.worldTransform.translation = {10.0f, 20.0f, 30.0f};
    scene.instances.push_back(placed);
    scene.instances.push_back(placed);

    PhysicsSandboxRenderInstance clip = placed;
    clip.meshIndex = 1u;
    clip.materialIndex = 1u;
    clip.purpose = PhysicsSandboxScenePurpose::Clip;
    clip.provenance.blockName = "StadiumGrassClip";
    clip.worldTransform.basisX = {1.0f, 0.0f, 0.0f};
    clip.worldTransform.basisY = {0.0f, 1.0f, 0.0f};
    clip.worldTransform.basisZ = {0.0f, 0.0f, 1.0f};
    clip.worldTransform.translation = {-5.0f, 0.0f, 0.0f};
    scene.instances.push_back(clip);

    PhysicsSandboxRenderInstance overlappingClip = clip;
    overlappingClip.meshIndex = 2u;
    overlappingClip.worldTransform.translation = {11.0f, 0.0f, 0.0f};
    scene.instances.push_back(overlappingClip);

    PhysicsSandboxRenderInstance blades = clip;
    blades.meshIndex = 3u;
    blades.purpose = PhysicsSandboxScenePurpose::PlacedBlock;
    blades.provenance.blockName = "StadiumGrass";
    scene.instances.push_back(blades);

    PhysicsSandboxRenderInstance dirtGround = clip;
    dirtGround.materialIndex = 2u;
    dirtGround.purpose = PhysicsSandboxScenePurpose::PlacedBlock;
    dirtGround.provenance.blockName = "StadiumRoadDirtHigh";
    dirtGround.worldTransform.translation = {35.0f, 0.0f, 0.0f};
    scene.instances.push_back(dirtGround);

    PhysicsSandboxRenderInstance background = placed;
    background.renderLayer = PhysicsSandboxRenderLayer::Background;
    scene.instances.push_back(background);

    PhysicsSandboxRenderInstance hidden = placed;
    hidden.visible = false;
    scene.instances.push_back(hidden);

    PhysicsSandboxRenderInstance lowerLod = placed;
    lowerLod.lodLevel = 1u;
    lowerLod.worldTransform.translation = {70.0f, 20.0f, 30.0f};
    scene.instances.push_back(lowerLod);

    const auto result = forevertas::viewer::BuildStaticVisualBatches(scene);
    const auto repeat = forevertas::viewer::BuildStaticVisualBatches(scene);
    bool okay = Check(
            result.visibleSourceInstanceCount == 7u &&
                    result.defaultVisibleInstanceCount == 4u &&
                    result.defaultTriangleCount == 4u &&
                    result.duplicateInstanceCount == 1u &&
                    result.skippedBackgroundInstanceCount == 1u &&
                    result.skippedBackgroundTriangleCount == 1u &&
                    result.skippedGrassBladeInstanceCount == 1u &&
                    result.skippedGrassBladeTriangleCount == 32u &&
                    result.batches.size() == 3u,
            "static batch counts, blade removal, or duplicate suppression "
            "were incorrect, or a lower-detail LOD reached the preview");
    const auto turboBatch = std::find_if(
            result.batches.cbegin(), result.batches.cend(),
            [](const StaticVisualBatch &batch) {
                return batch.materialClass == ReplacementMaterialClass::Turbo;
            });
    okay &= Check(turboBatch != result.batches.cend(),
                  "turbo geometry did not reach a turbo batch");
    const auto grassClipBatch = std::find_if(
            result.batches.cbegin(), result.batches.cend(),
            [](const StaticVisualBatch &batch) {
                return batch.materialClass == ReplacementMaterialClass::Grass &&
                       batch.purpose == PhysicsSandboxScenePurpose::Clip &&
                       batch.defaultVisible;
            });
    okay &= Check(grassClipBatch != result.batches.cend(),
                  "intentional grass ground-cover clip did not reach the scene");
    if (grassClipBatch != result.batches.cend()) {
        constexpr std::size_t FloatCount = 17u;
        const auto *vertices = reinterpret_cast<const float *>(
                grassClipBatch->vertices.constData());
        const std::size_t vertexCount =
                static_cast<std::size_t>(grassClipBatch->vertices.size()) /
                (FloatCount * sizeof(float));
        bool uvInsideTile = true;
        bool authoredNormalsPreserved = true;
        for (std::size_t vertexIndex = 0u; vertexIndex < vertexCount;
             ++vertexIndex) {
            const float *vertex = vertices + vertexIndex * FloatCount;
            uvInsideTile &= vertex[9] >= -0.001f &&
                            vertex[9] <= 1.001f &&
                            vertex[10] >= -0.001f &&
                            vertex[10] <= 1.001f;
            authoredNormalsPreserved &=
                    std::fabs(vertex[3] - 0.312249f) < 0.001f &&
                    std::fabs(vertex[4] - 0.95f) < 0.001f &&
                    std::fabs(vertex[5]) < 0.001f;
        }
        const auto repeatedGrassClipBatch = std::find_if(
                repeat.batches.cbegin(), repeat.batches.cend(),
                [](const StaticVisualBatch &batch) {
                    return batch.materialClass ==
                                   ReplacementMaterialClass::Grass &&
                           batch.purpose ==
                                   PhysicsSandboxScenePurpose::Clip &&
                           batch.defaultVisible;
                });
        okay &= Check(
                grassClipBatch->sourceInstanceCount == 2u &&
                        grassClipBatch->triangleCount == 2u &&
                        vertexCount == 6u && uvInsideTile &&
                        authoredNormalsPreserved &&
                        repeatedGrassClipBatch != repeat.batches.cend() &&
                        grassClipBatch->vertices ==
                                repeatedGrassClipBatch->vertices &&
                        grassClipBatch->indices ==
                                repeatedGrassClipBatch->indices,
                "grass ground did not preserve authored UV geometry without "
                "randomized remeshing");
    }
    const auto dirtBatch = std::find_if(
            result.batches.cbegin(), result.batches.cend(),
            [](const StaticVisualBatch &batch) {
                return batch.materialClass == ReplacementMaterialClass::Dirt &&
                       batch.purpose ==
                               PhysicsSandboxScenePurpose::PlacedBlock &&
                       batch.defaultVisible;
            });
    const auto repeatedDirtBatch = std::find_if(
            repeat.batches.cbegin(), repeat.batches.cend(),
            [](const StaticVisualBatch &batch) {
                return batch.materialClass == ReplacementMaterialClass::Dirt &&
                       batch.purpose ==
                               PhysicsSandboxScenePurpose::PlacedBlock &&
                       batch.defaultVisible;
            });
    okay &= Check(
            dirtBatch != result.batches.cend() &&
                    dirtBatch->triangleCount == 1u &&
                    dirtBatch->vertices.size() ==
                            static_cast<qsizetype>(3u * 17u * sizeof(float)) &&
                    repeatedDirtBatch != repeat.batches.cend() &&
                    dirtBatch->vertices == repeatedDirtBatch->vertices &&
                    dirtBatch->indices == repeatedDirtBatch->indices,
            "dirt ground was unexpectedly expanded into randomized tiles");
    if (turboBatch != result.batches.cend()) {
        constexpr std::size_t FloatCount = 17u;
        const auto *vertices = reinterpret_cast<const float *>(
                turboBatch->vertices.constData());
        okay &= Check(
                turboBatch->sourceInstanceCount == 1u &&
                        turboBatch->triangleCount == 1u &&
                        turboBatch->indices.size() ==
                                static_cast<qsizetype>(3u *
                                                       sizeof(std::uint32_t)) &&
                        std::fabs(vertices[0] - 10.0f) < 0.001f &&
                        std::fabs(vertices[1] - 20.0f) < 0.001f &&
                        std::fabs(vertices[2] - 30.0f) < 0.001f &&
                        std::fabs(vertices[FloatCount] - 12.0f) < 0.001f &&
                        std::fabs(vertices[3] - 0.0f) < 0.001f &&
                        std::fabs(vertices[4] - 1.0f) < 0.001f &&
                        std::fabs(vertices[5] - 0.0f) < 0.001f &&
                        std::fabs(vertices[9] - 0.0f) < 0.001f &&
                        std::fabs(vertices[FloatCount + 9] - 1.0f) < 0.001f,
                "static batching did not preserve transforms, normals, or UVs");
    }
    const auto rayTracingScene =
            forevertas::viewer::BuildRayTracingScene(result.batches);
    std::uint32_t expectedRayTracingTriangles = 0u;
    for (const StaticVisualBatch &batch : result.batches) {
        if (batch.defaultVisible) {
            expectedRayTracingTriangles +=
                    static_cast<std::uint32_t>(
                            batch.indices.size() /
                            static_cast<qsizetype>(
                                    3u * sizeof(std::uint32_t)));
        }
    }
    okay &= Check(
            rayTracingScene != nullptr &&
                    rayTracingScene->triangleCount ==
                            expectedRayTracingTriangles &&
                    rayTracingScene->triangleCount >=
                            result.defaultTriangleCount &&
                    rayTracingScene->vertexCount > 0u &&
                    rayTracingScene->bvhNodeCount > 0u &&
                    rayTracingScene->bvhNodeCount <
                            rayTracingScene->triangleCount * 2u &&
                    rayTracingScene->materialCount == 17u &&
                    rayTracingScene->vertices.size() ==
                            static_cast<qsizetype>(
                                    rayTracingScene->vertexCount * 80u) &&
                    rayTracingScene->triangles.size() ==
                            static_cast<qsizetype>(
                                    rayTracingScene->triangleCount * 16u) &&
                    rayTracingScene->bvhNodes.size() ==
                            static_cast<qsizetype>(
                                    rayTracingScene->bvhNodeCount * 48u),
            "ray tracing scene did not preserve the visible geometry or "
            "produce a compact GPU BVH");
    if (rayTracingScene != nullptr) {
        okay &= Check(
                rayTracingScene->materials.size() ==
                        static_cast<qsizetype>(17u * 12u * sizeof(float)),
                "ray tracing material table did not use the unified surface "
                "layout");
        if (rayTracingScene->materials.size() ==
            static_cast<qsizetype>(17u * 12u * sizeof(float))) {
            std::array<float, 17u * 12u> parameters{};
            std::memcpy(parameters.data(),
                        rayTracingScene->materials.constData(),
                        rayTracingScene->materials.size());
            for (std::uint32_t index = 0u; index < 17u; ++index) {
                const auto replacement = ReplacementFor(
                        static_cast<ReplacementMaterialClass>(index));
                const float *const material =
                        parameters.data() + index * 12u;
                okay &= Check(
                        std::fabs(material[0] - replacement.roughness) <
                                        0.0001f &&
                                std::fabs(material[1] -
                                          replacement.metalness) <
                                        0.0001f &&
                                std::fabs(material[2] -
                                           replacement.emissiveStrength) <
                                        0.0001f &&
                                (material[3] > 0.5f) ==
                                        replacement.applyVertexColors &&
                                std::fabs(material[4] -
                                          replacement.normalStrength) <
                                        0.0001f &&
                                std::fabs(material[5] -
                                          replacement.specularAmount) <
                                        0.0001f &&
                                std::fabs(material[6] -
                                          replacement.clearcoatAmount) <
                                        0.0001f &&
                                std::fabs(material[7] -
                                          replacement.clearcoatRoughness) <
                                        0.0001f &&
                                std::fabs(material[8] -
                                          replacement.transmissionFactor) <
                                        0.0001f &&
                                std::fabs(material[9] -
                                          replacement.indexOfRefraction) <
                                        0.0001f &&
                                (material[10] > 0.5f) ==
                                        !replacement.normalTexture.isEmpty() &&
                                (material[11] > 0.5f) ==
                                        !replacement.roughnessTexture.isEmpty(),
                        "ray tracing material table diverged from the shared "
                        "replacement contract");
            }
        }
    }
    return okay;
}

bool TestSpatialBatchingAndTelemetry() {
    PhysicsSandboxRenderScene scene;
    scene.meshes.push_back(TriangleMesh());
    for (std::uint32_t materialIndex = 0u; materialIndex < 6u;
         ++materialIndex) {
        PhysicsSandboxRenderMaterial material;
        material.id = 100u + materialIndex;
        material.surfaceMaterialId = 0u;
        scene.materials.push_back(std::move(material));
    }

    const auto addInstance = [&scene](std::uint32_t materialIndex, float x,
                                      float z) {
        PhysicsSandboxRenderInstance instance;
        instance.meshIndex = 0u;
        instance.materialIndex = materialIndex;
        instance.purpose = PhysicsSandboxScenePurpose::PlacedBlock;
        instance.worldTransform.translation = {x, 0.0f, z};
        scene.instances.push_back(std::move(instance));
    };

    addInstance(0u, 0.0f, 0.0f);
    addInstance(0u, 127.0f, 0.0f);
    addInstance(0u, 128.0f, 0.0f);
    addInstance(0u, -2.0f, 0.0f);
    addInstance(1u, 0.0f, 0.0f);
    addInstance(2u, 130.0f, 260.0f);
    addInstance(2u, 190.0f, 300.0f);
    addInstance(3u, 0.0f, 130.0f);
    addInstance(3u, 110.0f, 130.0f);
    addInstance(4u, 256.0f, 260.0f);
    addInstance(4u, 384.0f, 260.0f);
    addInstance(5u, 0.0f, 260.0f);
    addInstance(5u, 500.0f, 260.0f);

    StaticVisualBatchOptions options;
    options.materialStates = {
            {StaticVisualAlphaMode::Opaque, false, true},
            {StaticVisualAlphaMode::Masked, false},
            {StaticVisualAlphaMode::Blended, true},
            {StaticVisualAlphaMode::Additive, false},
            {StaticVisualAlphaMode::Subtractive, false},
            {StaticVisualAlphaMode::Unknown, false},
    };

    const auto result =
            forevertas::viewer::BuildStaticVisualBatches(scene, options);
    const auto repeat =
            forevertas::viewer::BuildStaticVisualBatches(scene, options);
    const auto legacy =
            forevertas::viewer::BuildStaticVisualBatches(scene);

    const auto findBatch = [&result](std::uint32_t sourceMaterialIndex,
                                     bool spatiallyPartitioned,
                                     std::int64_t cellX,
                                     std::int64_t cellZ) {
        return std::find_if(
                result.batches.cbegin(), result.batches.cend(),
                [=](const StaticVisualBatch &batch) {
                    return batch.sourceMaterialIndex == sourceMaterialIndex &&
                           batch.spatiallyPartitioned ==
                                   spatiallyPartitioned &&
                           batch.cellX == cellX && batch.cellZ == cellZ;
                });
    };

    const auto opaqueCell = findBatch(0u, true, 0, 0);
    const auto maskedCell = findBatch(1u, true, 0, 0);
    const auto blendedCell = findBatch(2u, false, 0, 0);
    const auto additiveCell = findBatch(3u, false, 0, 0);
    const auto subtractiveCell = findBatch(4u, false, 0, 0);
    const auto unknownBatch = findBatch(5u, false, 0, 0);

    bool okay = Check(
            result.batches.size() == 8u &&
                    result.telemetry.acceptedSourceTriangleCount == 13u &&
                    result.telemetry.submittedTriangleCount == 13u &&
                    result.telemetry.submittedBatchCount == 8u &&
                    result.telemetry.submittedMaterialCount == 6u &&
                    result.telemetry.populatedSpatialCellCount == 3u &&
                    result.telemetry.spatialBatchCount == 4u &&
                    result.telemetry.unpartitionedBatchCount == 4u &&
                    result.telemetry.spatiallyPartitionedInstanceCount == 5u &&
                    result.telemetry.unknownAlphaInstanceCount == 2u,
            "spatial batching telemetry did not describe submitted geometry");
    okay &= Check(
            opaqueCell != result.batches.cend() &&
                    opaqueCell->alphaMode == StaticVisualAlphaMode::Opaque &&
                    !opaqueCell->doubleSided &&
                    opaqueCell->sourceInstanceCount == 2u &&
                    opaqueCell->triangleCount == 2u &&
                    maskedCell != result.batches.cend() &&
                    maskedCell->alphaMode == StaticVisualAlphaMode::Masked &&
                    maskedCell->sourceInstanceCount == 1u &&
                    blendedCell != result.batches.cend() &&
                    blendedCell->alphaMode ==
                            StaticVisualAlphaMode::Blended &&
                    blendedCell->doubleSided &&
                    !blendedCell->spatiallyPartitioned &&
                    blendedCell->sourceInstanceCount == 2u &&
                    additiveCell != result.batches.cend() &&
                    additiveCell->alphaMode == StaticVisualAlphaMode::Additive &&
                    !additiveCell->spatiallyPartitioned &&
                    additiveCell->sourceInstanceCount == 2u &&
                    subtractiveCell != result.batches.cend() &&
                    subtractiveCell->alphaMode ==
                            StaticVisualAlphaMode::Subtractive &&
                    !subtractiveCell->spatiallyPartitioned &&
                    subtractiveCell->sourceInstanceCount == 2u &&
                    unknownBatch != result.batches.cend() &&
                    unknownBatch->alphaMode ==
                            StaticVisualAlphaMode::Unknown &&
                    !unknownBatch->spatiallyPartitioned &&
                    unknownBatch->sourceInstanceCount == 2u,
            "source material or render state collapsed across spatial batches");
    if (opaqueCell != result.batches.cend()) {
        constexpr std::size_t FloatCount = 17u;
        const auto *vertices = reinterpret_cast<const float *>(
                opaqueCell->vertices.constData());
        const std::size_t vertexCount =
                static_cast<std::size_t>(opaqueCell->vertices.size()) /
                (FloatCount * sizeof(float));
        bool exactWorldXz = vertexCount == 6u;
        for (std::size_t index = 0u; index < vertexCount; ++index) {
            const float *vertex = vertices + index * FloatCount;
            exactWorldXz &=
                    std::fabs(vertex[9] - vertex[0] / 16.0f) < 0.0001f &&
                    std::fabs(vertex[10] - vertex[2] / 16.0f) < 0.0001f;
        }
        okay &= Check(exactWorldXz,
                      "world-XZ material mapping was not world.xz / 16");
    }
    okay &= Check(
            repeat.telemetry.acceptedSourceTriangleCount ==
                            result.telemetry.acceptedSourceTriangleCount &&
                    repeat.telemetry.submittedTriangleCount ==
                            result.telemetry.submittedTriangleCount &&
                    repeat.telemetry.submittedBatchCount ==
                            result.telemetry.submittedBatchCount &&
                    repeat.telemetry.submittedMaterialCount ==
                            result.telemetry.submittedMaterialCount &&
                    repeat.telemetry.populatedSpatialCellCount ==
                            result.telemetry.populatedSpatialCellCount &&
                    result.telemetry.totalBuildNanoseconds >=
                            result.telemetry
                                    .materialClassificationNanoseconds &&
                    result.telemetry.totalBuildNanoseconds >=
                            result.telemetry.geometryBuildNanoseconds &&
                    result.telemetry.totalBuildNanoseconds >=
                            result.telemetry.finalizationNanoseconds,
            "pipeline counters were nondeterministic or stage timings invalid");
    okay &= Check(
            legacy.batches.size() == 6u &&
                    legacy.telemetry.submittedMaterialCount == 6u &&
                    legacy.telemetry.submittedTriangleCount == 13u &&
                    legacy.telemetry.populatedSpatialCellCount == 0u &&
                    legacy.telemetry.spatialBatchCount == 0u &&
                    legacy.telemetry.unpartitionedBatchCount == 6u &&
                    legacy.telemetry.unknownAlphaInstanceCount == 13u &&
                    std::all_of(legacy.batches.cbegin(),
                                legacy.batches.cend(),
                                [](const StaticVisualBatch &batch) {
                                    return !batch.spatiallyPartitioned &&
                                           batch.alphaMode ==
                                                   StaticVisualAlphaMode::
                                                           Unknown;
                                }),
            "one-argument pipeline compatibility guessed material semantics");
    return okay;
}

bool TestMirroredInstanceWinding() {
    PhysicsSandboxRenderScene scene;
    scene.meshes.push_back(TriangleMesh());
    scene.materials.push_back(Named("Concrete"));
    PhysicsSandboxRenderInstance instance;
    instance.meshIndex = 0u;
    instance.materialIndex = 0u;
    instance.purpose = PhysicsSandboxScenePurpose::PlacedBlock;
    instance.worldTransform.basisX = {-1.0f, 0.0f, 0.0f};
    scene.instances.push_back(instance);

    StaticVisualBatchOptions options;
    options.materialStates = {
            {StaticVisualAlphaMode::Opaque, false, false}};
    const auto result =
            forevertas::viewer::BuildStaticVisualBatches(scene, options);
    if (!Check(result.batches.size() == 1u,
               "mirrored instance did not produce one visual batch")) {
        return false;
    }
    const StaticVisualBatch &batch = result.batches.front();
    if (!Check(batch.indices.size() ==
                       static_cast<qsizetype>(3u * sizeof(std::uint32_t)),
               "mirrored instance produced an invalid index buffer")) {
        return false;
    }
    std::array<std::uint32_t, 3> indices{};
    std::memcpy(indices.data(), batch.indices.constData(), batch.indices.size());
    return Check(indices == std::array<std::uint32_t, 3>{0u, 2u, 1u},
                 "negative-determinant transform did not preserve front-face "
                 "winding for back-face culling");
}

bool TestLargeEnvironmentGrassDoesNotTessellate() {
    constexpr float MinimumX = -20739.1f;
    constexpr float MaximumX = 23353.9f;
    constexpr float MinimumZ = -20733.5f;
    constexpr float MaximumZ = 24526.8f;

    PhysicsSandboxRenderMesh mesh = TriangleMesh();
    mesh.vertices[0].position = {MinimumX, 0.0f, MinimumZ};
    mesh.vertices[1].position = {MaximumX, 0.0f, MinimumZ};
    mesh.vertices[2].position = {MinimumX, 0.0f, MaximumZ};
    mesh.boundsMin = {MinimumX, 0.0f, MinimumZ};
    mesh.boundsMax = {MaximumX, 0.0f, MaximumZ};

    PhysicsSandboxRenderMaterial grass;
    grass.surfaceMaterialId = 2u;

    PhysicsSandboxRenderInstance environment;
    environment.meshIndex = 0u;
    environment.materialIndex = 0u;
    environment.purpose = PhysicsSandboxScenePurpose::Environment;
    environment.provenance.descriptorPath =
            "Bay\\Media\\Solid\\B1C002CAC3AD9D4D908D52046B47ACECE6";

    PhysicsSandboxRenderScene scene;
    scene.meshes.push_back(std::move(mesh));
    scene.materials.push_back(grass);
    scene.instances.push_back(environment);

    const auto result = forevertas::viewer::BuildStaticVisualBatches(scene);
    const auto repeat = forevertas::viewer::BuildStaticVisualBatches(scene);
    const auto batch = std::find_if(
            result.batches.cbegin(), result.batches.cend(),
            [](const StaticVisualBatch &entry) {
                return entry.materialClass == ReplacementMaterialClass::Grass &&
                       entry.purpose ==
                               PhysicsSandboxScenePurpose::Environment;
            });
    const auto repeated = std::find_if(
            repeat.batches.cbegin(), repeat.batches.cend(),
            [](const StaticVisualBatch &entry) {
                return entry.materialClass == ReplacementMaterialClass::Grass &&
                       entry.purpose ==
                               PhysicsSandboxScenePurpose::Environment;
            });
    if (batch == result.batches.cend()) {
        return Check(false,
                     "large grass-classified environment geometry was lost");
    }

    constexpr qsizetype VertexStride =
            static_cast<qsizetype>(17u * sizeof(float));
    const auto *vertices = reinterpret_cast<const float *>(
            batch->vertices.constData());
    const bool authoredUvPreserved =
            batch->vertices.size() >= 3 * VertexStride &&
            std::fabs(vertices[9u]) < 0.001f &&
            std::fabs(vertices[10u]) < 0.001f &&
            std::fabs(vertices[17u + 9u] - 1.0f) < 0.001f &&
            std::fabs(vertices[34u + 10u] - 1.0f) < 0.001f;
    return Check(
            result.defaultVisibleInstanceCount == 1u &&
                    result.defaultTriangleCount == 1u &&
                    result.batches.size() == 1u &&
                    batch->sourceInstanceCount == 1u &&
                    batch->triangleCount == 1u &&
                    batch->vertices.size() == 3 * VertexStride &&
                    batch->indices.size() ==
                            static_cast<qsizetype>(3u * sizeof(std::uint32_t)) &&
                    authoredUvPreserved &&
                    repeated != repeat.batches.cend() &&
                    batch->vertices == repeated->vertices &&
                    batch->indices == repeated->indices,
            "large environment scenery did not retain one authored-UV "
            "triangle");
}

bool TestIndexedGeometry() {
    constexpr int FloatCount = 17;
    std::array<float, FloatCount * 3> vertices{};
    vertices[0] = 0.0f;
    vertices[1] = 0.0f;
    vertices[2] = 0.0f;
    vertices[FloatCount] = 1.0f;
    vertices[FloatCount + 1] = 0.0f;
    vertices[FloatCount + 2] = 0.0f;
    vertices[FloatCount * 2] = 0.0f;
    vertices[FloatCount * 2 + 1] = 1.0f;
    vertices[FloatCount * 2 + 2] = 0.0f;
    const std::array<std::uint32_t, 3> indices{{0u, 1u, 2u}};

    forevertas::viewer::RaceGeometry geometry;
    geometry.setIndexedMesh(
            QByteArray(reinterpret_cast<const char *>(vertices.data()),
                       static_cast<qsizetype>(sizeof(vertices))),
            QByteArray(reinterpret_cast<const char *>(indices.data()),
                       static_cast<qsizetype>(sizeof(indices))),
            FloatCount * static_cast<int>(sizeof(float)),
            true,
            true,
            true,
            true,
            true,
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f},
            {{0, 3}});
    bool okay = Check(geometry.indexData().size() ==
                              static_cast<qsizetype>(sizeof(indices)),
                      "indexed geometry did not retain its index buffer");
    okay &= Check(geometry.attributeCount() == 7,
                  "indexed geometry did not expose every vertex attribute");
    constexpr std::array<QQuick3DGeometry::Attribute::Semantic, 7> semantics{{
            QQuick3DGeometry::Attribute::IndexSemantic,
            QQuick3DGeometry::Attribute::PositionSemantic,
            QQuick3DGeometry::Attribute::NormalSemantic,
            QQuick3DGeometry::Attribute::TangentSemantic,
            QQuick3DGeometry::Attribute::TexCoord0Semantic,
            QQuick3DGeometry::Attribute::TexCoord1Semantic,
            QQuick3DGeometry::Attribute::ColorSemantic}};
    constexpr std::array<int, 7> offsets{{
            0,
            0,
            3 * static_cast<int>(sizeof(float)),
            6 * static_cast<int>(sizeof(float)),
            9 * static_cast<int>(sizeof(float)),
            11 * static_cast<int>(sizeof(float)),
            13 * static_cast<int>(sizeof(float))}};
    constexpr std::array<QQuick3DGeometry::Attribute::ComponentType, 7>
            componentTypes{{
                    QQuick3DGeometry::Attribute::U32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type}};
    okay &= Check(geometry.stride() ==
                          FloatCount * static_cast<int>(sizeof(float)),
                  "indexed geometry stride was incorrect");
    for (int index = 0; index < geometry.attributeCount(); ++index) {
        const QQuick3DGeometry::Attribute attribute =
                geometry.attribute(index);
        okay &= Check(
                attribute.semantic == semantics[static_cast<std::size_t>(index)] &&
                        attribute.offset ==
                                offsets[static_cast<std::size_t>(index)] &&
                        attribute.componentType ==
                                componentTypes[static_cast<std::size_t>(index)],
                "indexed geometry attribute layout was incorrect");
    }
    okay &= Check(geometry.subsetCount() == 1 &&
                          geometry.subsetOffset(0) == 0 &&
                          geometry.subsetCount(0) == 3,
                  "indexed geometry subset was incorrect");
    geometry.clearMesh();
    okay &= Check(geometry.vertexData().isEmpty() &&
                          geometry.indexData().isEmpty(),
                  "geometry cleanup retained stale buffers");
    return okay;
}

bool TestRayTracingShaders() {
    QFile presentVertex(QStringLiteral(
            FOREVERTAS_SOURCE_DIR
            "/shaders/raytrace_present.vert"));
    QFile rayTracingCompute(QStringLiteral(
            FOREVERTAS_SOURCE_DIR "/shaders/raytrace.comp"));
    bool okay = Check(
            presentVertex.open(QIODevice::ReadOnly) &&
                    rayTracingCompute.open(QIODevice::ReadOnly),
            "ray tracing shader sources were not available");
    if (!okay) return false;

    const QByteArray presentSource = presentVertex.readAll();
    const QByteArray rayTracingSource = rayTracingCompute.readAll();
    okay &= Check(
            presentSource.contains("textureCoordinate = position;") &&
                    !presentSource.contains(
                            "textureCoordinate = position * 0.5"),
            "fullscreen ray tracing presentation did not preserve the "
            "complete viewport");
    okay &= Check(
            rayTracingSource.contains("realTimeRayTrace") &&
                    rayTracingSource.contains("sceneOccluded") &&
                    rayTracingSource.contains("reflectionHit") &&
                    !rayTracingSource.contains("pathTrace("),
            "ray tracing shader did not use the real-time game lighting "
            "path");
    okay &= Check(
            rayTracingSource.contains(
                    "vec2 screen = pixelPosition / "
                    "camera.viewportFrame.xy * 2.0 - 1.0;") &&
                    !rayTracingSource.contains("screen.y = -screen.y"),
            "ray tracing camera inverted the viewport Y coordinate");
    okay &= Check(
            rayTracingSource.contains(
                    "bool usesVertexColor = surface.w > 0.5;") &&
                    rayTracingSource.contains(
                            "max(surface.z, 0.0) * 2.2") &&
                    rayTracingSource.contains("distributionGgx") &&
                    rayTracingSource.contains("geometrySmith") &&
                    rayTracingSource.contains("fresnelSchlick") &&
                    rayTracingSource.contains("materialNormalTextures") &&
                    rayTracingSource.contains("materialRoughnessTextures") &&
                    rayTracingSource.contains(
                            "surface.x = clamp(surface.x * textureLod(") &&
                    rayTracingSource.contains(
                            "layout(rgba16f, binding = 9)") &&
                    !rayTracingSource.contains("materialIndex > 3u") &&
                    !rayTracingSource.contains("materialIndex != 15u"),
            "ray tracing shader bypassed the shared material contract");
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    bool okay = TestClassification();
    okay &= TestReplacementParametersAndTextures();
    okay &= TestClipPlanesAndPurposeFiltering();
    okay &= TestStaticBatching();
    okay &= TestSpatialBatchingAndTelemetry();
    okay &= TestMirroredInstanceWinding();
    okay &= TestLargeEnvironmentGrassDoesNotTessellate();
    okay &= TestIndexedGeometry();
    okay &= TestRayTracingShaders();
    return okay ? 0 : 1;
}
