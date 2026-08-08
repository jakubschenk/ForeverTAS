#include "viewer/material_classifier.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace forevertas::viewer {
namespace {

std::string Lower(std::string text) {
    std::transform(
            text.begin(), text.end(), text.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
    return text;
}

std::string Normalize(std::string text) {
    text = Lower(std::move(text));
    constexpr const char *IgnoredQualifier = "replacement";
    for (std::size_t offset = text.find(IgnoredQualifier);
         offset != std::string::npos;
         offset = text.find(IgnoredQualifier, offset)) {
        text.replace(offset, std::char_traits<char>::length(
                                     IgnoredQualifier), " ");
    }
    return text;
}

std::string SearchText(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material) {
    std::string text = material.sourcePath + " " + material.modelPath + " " +
            material.shaderPath;
    for (const auto &bitmap : material.bitmaps) {
        text += " " + bitmap.samplerName + " " + bitmap.sourcePath;
    }
    return Normalize(std::move(text));
}

bool ContainsAny(const std::string &text,
                 std::initializer_list<const char *> words) {
    return std::any_of(
            words.begin(), words.end(),
            [&text](const char *word) {
                return text.find(word) != std::string::npos;
            });
}

ReplacementMaterialClass ClassifyText(const std::string &text) {
    if (ContainsAny(text, {"turbo", "booster", "boostpad"})) {
        return ReplacementMaterialClass::Turbo;
    }
    if (ContainsAny(text, {"water", "pool", "river"})) {
        return ReplacementMaterialClass::Water;
    }
    if (ContainsAny(text, {"glass", "window", "windscreen"})) {
        return ReplacementMaterialClass::Glass;
    }
    if (ContainsAny(text, {"emiss", "neon", "glow", "light", "lamp"})) {
        return ReplacementMaterialClass::Emissive;
    }
    if (ContainsAny(
                text,
                {"sign", "banner", "arrow", "sponsor", "advert", "panel"})) {
        return ReplacementMaterialClass::Signage;
    }
    if (ContainsAny(text, {"asphalt", "tarmac", "road", "pavement"})) {
        return ReplacementMaterialClass::Asphalt;
    }
    if (ContainsAny(text, {"concrete", "cement", "kerb", "curb"})) {
        return ReplacementMaterialClass::Concrete;
    }
    if (ContainsAny(text, {"dirt", "mud", "soil", "gravel", "sand"})) {
        return ReplacementMaterialClass::Dirt;
    }
    if (ContainsAny(text, {"grass", "turf", "vegetation", "foliage"})) {
        return ReplacementMaterialClass::Grass;
    }
    if (ContainsAny(text, {"rubber", "tyre", "tire"})) {
        return ReplacementMaterialClass::Rubber;
    }
    const bool metal = ContainsAny(
            text,
            {"metal", "steel", "iron", "aluminium", "aluminum",
             "chrome", "grate", "rail"});
    if (metal && ContainsAny(text, {"paint", "color", "colour"})) {
        return ReplacementMaterialClass::PaintedMetal;
    }
    if (metal) {
        return ReplacementMaterialClass::Metal;
    }
    if (ContainsAny(text, {"plastic", "polymer", "vinyl"})) {
        return ReplacementMaterialClass::Plastic;
    }
    if (ContainsAny(text, {"default", "neutral", "generic", "base"})) {
        return ReplacementMaterialClass::Neutral;
    }
    return ReplacementMaterialClass::Unknown;
}

bool IsSurface(std::uint8_t surfaceId,
               std::initializer_list<std::uint8_t> values) {
    return std::find(values.begin(), values.end(), surfaceId) != values.end();
}

QString TexturePath(const char *texture) {
    return QStringLiteral("qrc:/materials/") +
            QString::fromLatin1(texture);
}

void SetDetailMaps(ReplacementMaterial &material,
                   const char *normalTexture,
                   const char *roughnessTexture,
                   float normalStrength) {
    material.normalTexture = TexturePath(normalTexture);
    material.roughnessTexture = TexturePath(roughnessTexture);
    material.normalStrength = normalStrength;
}

ReplacementMaterialClass ClassifySemanticContext(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        const MaterialSemanticContext &context) {
    const std::string block = Normalize(context.blockName);
    const std::string component =
            Normalize(context.componentIdentity + " " + context.descriptorPath);
    const std::uint8_t surface = material.surfaceMaterialId;

    if (IsSurface(surface, {7u, 26u, 30u})) {
        return ReplacementMaterialClass::Turbo;
    }
    if (material.water || surface == 13u) {
        return ReplacementMaterialClass::Water;
    }
    if (surface == 0u && context.grassGroundCover) {
        return ReplacementMaterialClass::Grass;
    }
    if (ContainsAny(block, {"startline", "finishline", "multilap"})) {
        if (surface == 28u) {
            return ReplacementMaterialClass::StartFinish;
        }
        if (surface == 22u) {
            return ReplacementMaterialClass::Emissive;
        }
    }
    if (ContainsAny(block, {"checkpoint"})) {
        if (surface == 28u) {
            return ReplacementMaterialClass::Checkpoint;
        }
        if (surface == 22u) {
            return ReplacementMaterialClass::Emissive;
        }
    }
    if (ContainsAny(block, {"turbo"}) && surface == 22u) {
        return ReplacementMaterialClass::Emissive;
    }
    if (ContainsAny(block, {"dirt"}) && IsSurface(surface, {5u, 6u, 8u, 17u})) {
        return ReplacementMaterialClass::Dirt;
    }
    if (ContainsAny(block, {"road", "platform", "ramp", "loop"}) &&
        IsSurface(surface, {1u, 16u, 18u, 19u})) {
        return ReplacementMaterialClass::Asphalt;
    }
    if (ContainsAny(block, {"grass"}) && IsSurface(surface, {2u, 20u})) {
        return ReplacementMaterialClass::Grass;
    }
    if (ContainsAny(block, {"pool"}) && surface == 13u) {
        return ReplacementMaterialClass::Water;
    }
    if ((ContainsAny(block, {"pub", "sign", "banner"}) ||
         ContainsAny(component, {"flags", "sign", "banner"})) &&
        surface == 28u) {
        return ReplacementMaterialClass::Signage;
    }
    return ReplacementMaterialClass::Unknown;
}

ReplacementMaterial Make(
        ReplacementMaterialClass materialClass,
        const char *name,
        const char *debugColor,
        const char *texture,
        float roughness,
        float metalness) {
    ReplacementMaterial result;
    result.materialClass = materialClass;
    result.name = QString::fromLatin1(name);
    result.debugColor = QColor(QString::fromLatin1(debugColor));
    result.baseTexture = TexturePath(texture);
    result.roughness = roughness;
    result.metalness = metalness;
    return result;
}

}  // namespace

ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material) {
    return ClassifyMaterial(material, {});
}

ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        const MaterialSemanticContext &context) {
    const ReplacementMaterialClass semantic =
            ClassifySemanticContext(material, context);
    if (semantic != ReplacementMaterialClass::Unknown) {
        return semantic;
    }
    if (material.water) {
        return ReplacementMaterialClass::Water;
    }
    std::string finalMaterial = material.sourcePath;
    for (const auto &bitmap : material.bitmaps) {
        finalMaterial += " " + bitmap.samplerName + " " +
                bitmap.sourcePath;
    }
    const ReplacementMaterialClass direct =
            ClassifyText(Normalize(std::move(finalMaterial)));
    if (direct != ReplacementMaterialClass::Unknown) {
        return direct;
    }
    const ReplacementMaterialClass fallback =
            ClassifyText(SearchText(material));
    if (fallback != ReplacementMaterialClass::Unknown) {
        return fallback;
    }
    if (material.cubeMap || material.renderTarget) {
        return ReplacementMaterialClass::Neutral;
    }
    switch (material.surfaceMaterialId) {
    case 0u:
    case 12u:
    case 24u:
        return ReplacementMaterialClass::Concrete;
    case 1u:
    case 16u:
    case 18u:
    case 19u:
        return ReplacementMaterialClass::Asphalt;
    case 2u:
    case 20u:
    case 25u:
        return ReplacementMaterialClass::Grass;
    case 4u:
    case 22u:
        return ReplacementMaterialClass::Metal;
    case 5u:
    case 6u:
    case 8u:
    case 17u:
        return ReplacementMaterialClass::Dirt;
    case 9u:
    case 10u:
    case 23u:
    case 27u:
        return ReplacementMaterialClass::Rubber;
    case 7u:
    case 26u:
    case 30u:
        return ReplacementMaterialClass::Turbo;
    case 13u:
        return ReplacementMaterialClass::Water;
    case 15u:
        return ReplacementMaterialClass::Signage;
    case 3u:
    case 14u:
    case 21u:
    case 28u:
    case 29u:
        return ReplacementMaterialClass::Neutral;
    default:
        return ReplacementMaterialClass::Unknown;
    }
}

ReplacementMaterial ReplacementFor(
        ReplacementMaterialClass materialClass) {
    ReplacementMaterial result;
    switch (materialClass) {
    case ReplacementMaterialClass::Asphalt:
        result = Make(materialClass, "Asphalt", "#343434",
                      "asphalt_base.png", 0.88f, 0.0f);
        SetDetailMaps(result, "asphalt_normal.png",
                      "asphalt_roughness.png", 0.45f);
        result.specularAmount = 0.34f;
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Concrete:
        result = Make(materialClass, "Concrete", "#d8d8d8",
                      "concrete_base.png", 0.82f, 0.0f);
        result.specularAmount = 0.32f;
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Dirt:
        result = Make(materialClass, "Dirt", "#9c5b22",
                      "dirt_base.png", 0.94f, 0.0f);
        SetDetailMaps(result, "dirt_normal.png",
                      "dirt_roughness.png", 0.55f);
        result.specularAmount = 0.26f;
        result.indexOfRefraction = 1.45f;
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Grass:
        result = Make(materialClass, "Grass", "#40d153",
                      "grass_base.png", 0.9f, 0.0f);
        result.specularAmount = 0.28f;
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Metal:
        result = Make(materialClass, "Metal", "#8ea6ff",
                      "metal_base.png", 0.34f, 0.82f);
        SetDetailMaps(result, "metal_normal.png",
                      "metal_roughness.png", 0.48f);
        result.specularAmount = 0.9f;
        return result;
    case ReplacementMaterialClass::PaintedMetal:
        result = Make(materialClass, "Painted metal", "#ff536e",
                      "painted_metal_base.png", 0.42f, 0.58f);
        SetDetailMaps(result, "painted_metal_normal.png",
                      "painted_metal_roughness.png", 0.38f);
        result.specularAmount = 0.55f;
        result.clearcoatAmount = 0.18f;
        result.clearcoatRoughness = 0.32f;
        result.indexOfRefraction = 1.52f;
        return result;
    case ReplacementMaterialClass::Plastic:
        result = Make(materialClass, "Plastic", "#ffb347",
                      "plastic_base.png", 0.46f, 0.0f);
        result.specularAmount = 0.46f;
        result.clearcoatAmount = 0.08f;
        result.clearcoatRoughness = 0.38f;
        result.indexOfRefraction = 1.46f;
        return result;
    case ReplacementMaterialClass::Rubber:
        result = Make(materialClass, "Rubber", "#7f50a8",
                      "rubber_base.png", 0.96f, 0.0f);
        SetDetailMaps(result, "rubber_normal.png",
                      "rubber_roughness.png", 0.52f);
        result.specularAmount = 0.24f;
        result.indexOfRefraction = 1.52f;
        return result;
    case ReplacementMaterialClass::Glass:
        result = Make(materialClass, "Glass", "#55dff5",
                      "glass_base.png", 0.12f, 0.05f);
        result.specularAmount = 1.0f;
        result.transmissionFactor = 0.92f;
        result.indexOfRefraction = 1.52f;
        return result;
    case ReplacementMaterialClass::Signage:
        result = Make(materialClass, "Signage", "#ffe347",
                      "signage_base.png", 0.48f, 0.0f);
        result.specularAmount = 0.43f;
        result.clearcoatAmount = 0.08f;
        result.clearcoatRoughness = 0.42f;
        return result;
    case ReplacementMaterialClass::Emissive:
        result = Make(materialClass, "Emissive", "#ff4fd2",
                      "emissive_base.png", 0.28f, 0.0f);
        result.specularAmount = 0.36f;
        result.emissiveStrength = 0.7f;
        return result;
    case ReplacementMaterialClass::Turbo:
        result = Make(materialClass, "Turbo", "#00fff0",
                      "turbo_base.png", 0.3f, 0.18f);
        result.specularAmount = 0.52f;
        result.clearcoatAmount = 0.2f;
        result.clearcoatRoughness = 0.3f;
        result.emissiveStrength = 0.55f;
        return result;
    case ReplacementMaterialClass::Checkpoint:
        result = Make(materialClass, "Checkpoint", "#168bd2",
                      "checkpoint_base.png", 0.4f, 0.08f);
        result.specularAmount = 0.48f;
        result.clearcoatAmount = 0.15f;
        result.clearcoatRoughness = 0.36f;
        result.emissiveStrength = 0.22f;
        return result;
    case ReplacementMaterialClass::StartFinish:
        result = Make(materialClass, "Start / finish", "#f3f4ef",
                      "start_finish_base.png", 0.52f, 0.05f);
        result.specularAmount = 0.42f;
        result.clearcoatAmount = 0.1f;
        result.clearcoatRoughness = 0.4f;
        return result;
    case ReplacementMaterialClass::Water:
        result = Make(materialClass, "Water", "#267cff",
                      "water_base.png", 0.18f, 0.0f);
        result.specularAmount = 1.0f;
        result.transmissionFactor = 0.78f;
        result.indexOfRefraction = 1.333f;
        return result;
    case ReplacementMaterialClass::Neutral:
        result = Make(materialClass, "Neutral", "#9ca3a0",
                      "neutral_base.png", 0.72f, 0.0f);
        result.specularAmount = 0.36f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Unknown:
    default:
        result = Make(ReplacementMaterialClass::Unknown,
                      "Unknown", "#ff38df",
                      "unknown_base.png", 0.7f, 0.0f);
        result.specularAmount = 0.4f;
        return result;
    }
}

QString MaterialClassName(ReplacementMaterialClass materialClass) {
    return ReplacementFor(materialClass).name;
}

}  // namespace forevertas::viewer
