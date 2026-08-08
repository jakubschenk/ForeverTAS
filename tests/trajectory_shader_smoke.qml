import QtQuick
import QtQuick.Window
import QtQuick3D
import "../qml" as App

Window {
    width: 256
    height: 256
    visible: true
    color: "white"

    View3D {
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "white"
            antialiasingMode: SceneEnvironment.NoAA
        }

        PerspectiveCamera {
            id: smokeCamera
            position: Qt.vector3d(0, 0.65, 0)
            eulerRotation.x: -90
            clipNear: 0.01
            clipFar: 10
            fieldOfView: 25
        }

        camera: smokeCamera

        Model {
            objectName: "trajectorySmokeModel"
            geometry: testGeometry
            castsShadows: false
            receivesShadows: false
            materials: App.TrajectoryMaterial {
                objectName: "trajectorySmokeMaterial"
                trajectoryColor: "#c02020"
                trajectoryOpacity: 0.85
            }
        }
    }
}
