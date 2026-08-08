#include "viewer/native_material.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace forevertas::viewer {
namespace {

struct ShaderRule {
    std::string_view path;
    bool worldXz = false;
    bool transparent = false;
    bool doubleSided = false;
    bool unlit = false;
    bool additive = false;
    bool subtractive = false;
    bool invisible = false;
    bool water = false;
    bool clamp = false;
    bool flipV = false;
    float opacity = 1.0f;
    NativeAlbedoAlphaUsage albedoAlphaUsage = NativeAlbedoAlphaUsage::Ignore;
};

constexpr std::array<std::string_view, 14> AlbedoPriority{{
        "diffuse", "diffusegloss", "blend1", "panorama", "advert", "glow",
        "soil", "grass", "foam1", "gdiffuse", "pxzdiffuse", "pydiffuse",
        "basecolor", "pxzbasecolor"}};

// Interoperability table expressed independently from the observed gbx3d
// behavior. Matching is case/slash-insensitive and accepts archive prefixes.
constexpr std::array<ShaderRule, 41> ShaderRules{{
        {"techno/media/material/pdiff pdiff pa px2", true},
        {"techno2/media/material/pdiff pdiff pa px2 grass2", true},
        {"techno2/media/material/pdiff pdiff pa tocc px2 grass", true},
        {"techno2/media/material/pdiff pdiff pa tocc px2 grass nolightv",
         true, false, false, true},
        {"techno2/media/material/tdiff_spec_nrm tocc cspecsoft",
         false, true},
        {"techno/media/material/tdiff px2 trans", false, true, true},
        {"techno/media/material/tdiff px2 trans 2sided", false, true, true},
        {"techno/media/material/tdiffg px2 cspec fcout trans", false, true},
        {"techno/media/material/tdiffg px2 cspecl trans", false, true},
        {"techno/media/material/tdiff px2 trans normy pc3only",
         false, true, true},
        {"techno/media/material/pdiff fresnel px2", true},
        {"techno/media/material/sky", false, false, false, true},
        {"techno/media/material/tdiffg px2 cspecl_pixel", false, false, false,
         false, false, false, false, false, false, false, 1.0f,
         NativeAlbedoAlphaUsage::Specular},
        {"vehicles/media/material/sportcarglass", false, true, false,
         false, false, false, false, false, false, false, 0.89f,
         NativeAlbedoAlphaUsage::Opacity},
        {"techno/media/material/tadd", false, true, false, false, true},
        {"techno/media/material/tadd zbias", false, true, false, false, true},
        {"techno/media/material/tadd night", false, true, false, false, true},
        {"techno/media/material/tadd night zbias",
         false, true, false, false, true},
        {"island/media/material/modellightvolume",
         false, true, false, false, true},
        {"alpine/media/material/alpinesignsselfillum",
         false, true, false, true, true},
        {"techno2/media/material/tselfi add",
         false, true, false, true, true},
        {"techno2/media/material/vdep fence",
         false, false, false, false, false, false, true},
        {"techno/media/material/shadowskirt",
         false, false, false, false, false, false, true},
        {"techno2/media/material/tdiff_spec_nrm tocc cspecsoft nolightv",
         false, true, false, true},
        {"techno/media/material/pdisp pdiff px2", true},
        {"island/media/material/modelalpha1sidedlight", false, true},
        {"island/media/material/modelalpha2sidednolight",
         false, true, true, true},
        {"techno2/media/material/soilgen21", true},
        {"techno2/media/material/tdiff_spec_nrm tocc cspecsoft trans",
         false, true},
        {"techno3/media/material/tech3 block tdiffa_spec_norm",
         false, true},
        {"techno3/media/material/tech3 block pdiff_spec_norm", true},
        {"techno3/media/material/tech3 block pdiff_spec_norm grassx2", true},
        {"techno/media/material/sea",
         false, true, true, false, false, false, false, true},
        {"techno/media/material/seamultiy",
         false, true, true, false, false, false, false, true},
        {"techno3/media/material/tech3 sea",
         false, true, true, false, false, false, false, true},
        {"techno3/media/material/tech3_block_tdiffablend_specnorm_cubeout",
         false, true},
        {"island/media/material/islandbeachfoam",
         false, true, true, true, true, false, false, false, true},
        {":glass", false, true, true, false, false, false, false,
         false, false, false, 0.89f},
        {":fakeshad", false, false, false, false, false, false, true},
        {"techno3/media/material/sky/tech3 sky",
         false, false, false, true, false, false, true},
        {"techno3/media/material/tech3 warp pyapxzdiff", true},
}};

constexpr std::array<ShaderRule, 4> AdditionalShaderRules{{
        {"techno3/media/material/tech3 warp_pyadiff_to_pdiffpgrassx2", true},
        {"island/media/material/islandsky", false, false, false, true},
        {"sky/media/material/skyday", false, false, false, true},
        {":ddsflipy", false, false, false, false, false, false, false,
         false, false, true},
}};

std::string Normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.find("//") != std::string::npos) {
        value.erase(value.find("//"), 1u);
    }
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string SamplerKey(std::string value) {
    value = Normalize(std::move(value));
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char character) {
                                   return std::isalnum(character) == 0;
                               }),
                value.end());
    return value;
}

bool EndsWithPath(std::string_view value, std::string_view suffix) {
    if (value == suffix) return true;
    if (value.size() <= suffix.size()) return false;
    const std::size_t offset = value.size() - suffix.size();
    return value[offset - 1u] == '/' && value.substr(offset) == suffix;
}

void ApplyRule(const ShaderRule &rule, NativeMaterialProfile *profile) {
    profile->worldXz |= rule.worldXz;
    profile->renderState.worldXz |= rule.worldXz;
    profile->renderState.doubleSided |= rule.doubleSided;
    profile->unlit |= rule.unlit;
    profile->visible &= !rule.invisible;
    profile->water |= rule.water;
    profile->repeat &= !rule.clamp;
    profile->flipV |= rule.flipV;
    profile->opacity = std::min(profile->opacity, rule.opacity);
    if (rule.albedoAlphaUsage != NativeAlbedoAlphaUsage::Ignore) {
        profile->albedoAlphaUsage = rule.albedoAlphaUsage;
    } else if (rule.transparent &&
               profile->albedoAlphaUsage == NativeAlbedoAlphaUsage::Ignore) {
        profile->albedoAlphaUsage = NativeAlbedoAlphaUsage::Opacity;
    }
    if (rule.subtractive) {
        profile->renderState.alphaMode = StaticVisualAlphaMode::Subtractive;
    } else if (rule.additive) {
        profile->renderState.alphaMode = StaticVisualAlphaMode::Additive;
    } else if (rule.transparent &&
               profile->renderState.alphaMode == StaticVisualAlphaMode::Opaque) {
        profile->renderState.alphaMode = StaticVisualAlphaMode::Blended;
    }
}

template <std::size_t Size>
void ApplyRules(const std::array<ShaderRule, Size> &rules,
                std::string_view path, NativeMaterialProfile *profile) {
    for (const ShaderRule &rule : rules) {
        if (EndsWithPath(path, rule.path)) ApplyRule(rule, profile);
    }
}

int FindSampler(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        std::string_view sampler) {
    for (std::size_t index = 0; index < material.bitmaps.size(); ++index) {
        if (SamplerKey(material.bitmaps[index].samplerName) == sampler) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int FindFirstSampler(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        std::initializer_list<std::string_view> samplers) {
    for (std::string_view sampler : samplers) {
        const int index = FindSampler(material, sampler);
        if (index >= 0) return index;
    }
    return -1;
}

}  // namespace

NativeMaterialProfile ResolveNativeMaterialProfile(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material) {
    NativeMaterialProfile profile;
    profile.renderState.alphaMode = StaticVisualAlphaMode::Opaque;
    for (std::string_view sampler : AlbedoPriority) {
        profile.albedoBitmap = FindSampler(material, sampler);
        if (profile.albedoBitmap >= 0) {
            if (sampler == "diffusegloss") {
                profile.albedoAlphaUsage = NativeAlbedoAlphaUsage::Specular;
            }
            break;
        }
    }
    profile.normalBitmap = FindSampler(material, "normal");
    profile.specularBitmap =
            FindFirstSampler(material, {"specular", "pxzspecular"});
    profile.blend2Bitmap = FindSampler(material, "blend2");
    profile.blendMaskBitmap = FindSampler(material, "blendi");
    profile.blend3Bitmap = FindFirstSampler(
            material, {"blend3", "borders", "soilfix", "diffuseblenda"});
    profile.water = material.water;
    if (material.water) {
        profile.renderState.alphaMode = StaticVisualAlphaMode::Blended;
        profile.renderState.doubleSided = true;
        profile.albedoAlphaUsage = NativeAlbedoAlphaUsage::Opacity;
    }

    const std::string shader = Normalize(material.shaderPath);
    ApplyRules(ShaderRules, shader, &profile);
    ApplyRules(AdditionalShaderRules, shader, &profile);
    return profile;
}

void ApplyNativeAlbedoTransparency(bool hasTransparency,
                                   bool hasPartialTransparency,
                                   NativeMaterialProfile *profile) {
    if (profile == nullptr || !hasTransparency ||
        profile->albedoAlphaUsage != NativeAlbedoAlphaUsage::Opacity ||
        profile->renderState.alphaMode != StaticVisualAlphaMode::Opaque) {
        return;
    }
    profile->renderState.alphaMode = hasPartialTransparency
            ? StaticVisualAlphaMode::Blended
            : StaticVisualAlphaMode::Masked;
}

QString StaticVisualAlphaModeName(StaticVisualAlphaMode mode) {
    switch (mode) {
    case StaticVisualAlphaMode::Opaque: return QStringLiteral("opaque");
    case StaticVisualAlphaMode::Masked: return QStringLiteral("masked");
    case StaticVisualAlphaMode::Blended: return QStringLiteral("blended");
    case StaticVisualAlphaMode::Additive: return QStringLiteral("additive");
    case StaticVisualAlphaMode::Subtractive:
        return QStringLiteral("subtractive");
    case StaticVisualAlphaMode::Unknown:
    default: return QStringLiteral("unknown");
    }
}

}  // namespace forevertas::viewer
