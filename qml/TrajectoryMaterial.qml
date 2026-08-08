import QtQuick
import QtQuick3D

CustomMaterial {
    property color trajectoryColor: "white"
    property real trajectoryOpacity: 1.0

    shadingMode: CustomMaterial.Unshaded
    sourceBlend: CustomMaterial.SrcAlpha
    destinationBlend: CustomMaterial.OneMinusSrcAlpha
    cullMode: Material.NoCulling
    depthDrawMode: Material.NeverDepthDraw
    vertexShader: Qt.resolvedUrl("shaders/trajectory.vert")
    fragmentShader: Qt.resolvedUrl("shaders/trajectory.frag")
}
