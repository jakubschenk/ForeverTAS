import QtQuick3D

CustomMaterial {
    property real revealTimeSeconds: 0.0

    shadingMode: CustomMaterial.Unshaded
    sourceBlend: CustomMaterial.SrcAlpha
    destinationBlend: CustomMaterial.OneMinusSrcAlpha
    cullMode: Material.NoCulling
    depthDrawMode: Material.NeverDepthDraw
    vertexShader: Qt.resolvedUrl("shaders/skidmark.vert")
    fragmentShader: Qt.resolvedUrl("shaders/skidmark.frag")
}
