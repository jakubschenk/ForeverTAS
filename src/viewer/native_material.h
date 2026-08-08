#ifndef FOREVERTAS_VIEWER_NATIVE_MATERIAL_H
#define FOREVERTAS_VIEWER_NATIVE_MATERIAL_H

#include "viewer/visual_scene_pipeline.h"

#include <forevervalidator/experimental/physics_sandbox.h>

#include <QString>

namespace forevertas::viewer {

struct NativeMaterialProfile {
    int albedoBitmap = -1;
    int normalBitmap = -1;
    int specularBitmap = -1;
    int blend2Bitmap = -1;
    int blendMaskBitmap = -1;
    int blend3Bitmap = -1;
    StaticVisualMaterialState renderState{};
    float opacity = 1.0f;
    bool worldXz = false;
    bool unlit = false;
    bool visible = true;
    bool water = false;
    bool repeat = true;
    bool flipV = false;
};

NativeMaterialProfile ResolveNativeMaterialProfile(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material);

void ApplyNativeAlbedoTransparency(bool hasTransparency,
                                   bool hasPartialTransparency,
                                   NativeMaterialProfile *profile);

QString StaticVisualAlphaModeName(StaticVisualAlphaMode mode);

}  // namespace forevertas::viewer

#endif
