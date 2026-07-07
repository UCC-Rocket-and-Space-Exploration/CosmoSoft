pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

Rectangle {
    id: root
    color: "#070d16"

    property var pathPoints: []
    property var altitudeProfile: []
    property int visiblePointCount: 0
    property vector3d currentPosition: Qt.vector3d(0, 0, 0)
    property real currentAltitude: 0
    property real currentSpeed: 0
    property real elapsedFlightSeconds: 0
    property real flightDurationSeconds: 0
    property real peakHeight: 0
    property real maxAltitude: 0
    property real maxSpeed: 0
    property bool hasSpeed: false
    property bool profileMode: false
    property real replayProgress: 0
    property real cameraProgress: 0
    property int phase: 0
    property string flightTitle: "Flight replay"
    property string rocketOrTeam: ""
    property string terrainTextureUrl: ""
    property string heightMapUrl: ""
    property string terrainAttribution: ""
    property real terrainWidth: 1200
    property real terrainDepth: 1200
    property real terrainRelief: 45

    onReplayProgressChanged: altitudeCanvas.requestPaint()
    onAltitudeProfileChanged: altitudeCanvas.requestPaint()

    View3D {
        id: scene
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#070d16"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            tonemapMode: SceneEnvironment.TonemapModeFilmic
        }

        Node {
            id: cameraTarget
            position: root.phase === 2 ? Qt.vector3d(0, 100, 0) : root.currentPosition
        }

        PerspectiveCamera {
            id: camera
            clipFar: 10000
            clipNear: 1
            fieldOfView: root.width < root.height ? 52 : 45
            lookAtNode: cameraTarget
            position: {
                if (root.phase === 2)
                    return Qt.vector3d(720, 540, 720)
                var angle = 0.5 + root.cameraProgress * 1.4
                var radius = root.phase === 0 ? 760 : 360 + 120 * Math.sin(root.cameraProgress * Math.PI)
                return Qt.vector3d(root.currentPosition.x + Math.cos(angle) * radius,
                                   root.currentPosition.y + 190 + 90 * Math.sin(root.cameraProgress * Math.PI),
                                   root.currentPosition.z + Math.sin(angle) * radius)
            }
        }

        DirectionalLight {
            eulerRotation.x: -55
            eulerRotation.y: -30
            brightness: 1.2
            color: "#d7e8ff"
            castsShadow: true
            shadowFactor: 80
        }
        DirectionalLight {
            eulerRotation.x: -20
            eulerRotation.y: 140
            brightness: 0.45
            color: "#5b8bc7"
        }

        Model {
            id: terrain
            position: Qt.vector3d(0, -55, 0)
            geometry: HeightFieldGeometry {
                heightMap: root.heightMapUrl
                extents: Qt.vector3d(root.terrainWidth,
                                     Math.max(10, root.terrainRelief),
                                     root.terrainDepth)
                smoothShading: true
            }
            materials: [
                PrincipledMaterial {
                    baseColor: "#172337"
                    baseColorMap: Texture { source: root.terrainTextureUrl }
                    roughness: 0.92
                    metalness: 0.0
                }
            ]
        }

        Repeater3D {
            model: root.pathPoints
            delegate: Model {
                id: pathPoint
                required property var modelData
                required property int index
                source: "#Sphere"
                position: Qt.vector3d(modelData.x, modelData.y, modelData.z)
                scale: Qt.vector3d(pathPoint.index < root.visiblePointCount ? 0.075 : 0.045,
                                   pathPoint.index < root.visiblePointCount ? 0.075 : 0.045,
                                   pathPoint.index < root.visiblePointCount ? 0.075 : 0.045)
                opacity: pathPoint.index < root.visiblePointCount ? 1.0 : 0.20
                materials: [
                    PrincipledMaterial {
                        baseColor: pathPoint.index < root.visiblePointCount ? "#51c9ff" : "#44627d"
                        emissiveFactor: pathPoint.index < root.visiblePointCount
                                        ? Qt.vector3d(0.15, 0.55, 0.85)
                                        : Qt.vector3d(0.02, 0.04, 0.06)
                        roughness: 0.35
                    }
                ]
            }
        }

        Model {
            id: rocketGlow
            visible: root.phase === 1
            source: "#Sphere"
            position: root.currentPosition
            scale: Qt.vector3d(0.22, 0.22, 0.22)
            opacity: 0.16
            materials: PrincipledMaterial {
                baseColor: "#ff7a37"
                emissiveFactor: Qt.vector3d(1.0, 0.22, 0.04)
            }
        }
        Model {
            visible: root.phase === 1
            source: "#Sphere"
            position: root.currentPosition
            scale: Qt.vector3d(0.09, 0.09, 0.09)
            materials: PrincipledMaterial {
                baseColor: "#ff8a45"
                emissiveFactor: Qt.vector3d(0.65, 0.13, 0.02)
                roughness: 0.28
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#66000000" }
            GradientStop { position: 0.55; color: "#00000000" }
            GradientStop { position: 1.0; color: "#b3000000" }
        }
    }

    Column {
        visible: root.phase === 0
        anchors.left: parent.left
        anchors.leftMargin: parent.width * 0.075
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width * 0.72
        spacing: 12
        Text {
            width: parent.width
            text: root.flightTitle.length > 0 ? root.flightTitle : "Flight replay"
            color: "#f4f8ff"
            font.family: "Workbench"
            font.pixelSize: Math.max(40, root.width * 0.055)
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            visible: root.rocketOrTeam.length > 0
            text: root.rocketOrTeam
            color: "#75d4ff"
            font.family: "Red Hat Mono"
            font.pixelSize: Math.max(18, root.width * 0.023)
        }
        Text {
            text: root.profileMode ? "ALTITUDE PROFILE" : "3D FLIGHT PATH"
            color: "#9db0c7"
            font.family: "Red Hat Mono"
            font.pixelSize: Math.max(14, root.width * 0.015)
            font.letterSpacing: 2
        }
    }

    Row {
        visible: root.phase === 1
        anchors.left: parent.left
        anchors.leftMargin: parent.width * 0.055
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.065
        spacing: root.width * 0.045

        Repeater {
            model: root.hasSpeed ? 3 : 2
            delegate: Column {
                id: liveMetric
                required property int index
                spacing: 2
                Text {
                    text: liveMetric.index === 0 ? "ALTITUDE" : (liveMetric.index === 1 && root.hasSpeed ? "SPEED" : "FLIGHT TIME")
                    color: "#8ca3ba"
                    font.family: "Red Hat Mono"
                    font.pixelSize: Math.max(12, root.width * 0.014)
                    font.letterSpacing: 1.5
                }
                Text {
                    text: liveMetric.index === 0 ? root.currentAltitude.toFixed(1) + " m"
                          : (liveMetric.index === 1 && root.hasSpeed ? root.currentSpeed.toFixed(1) + " m/s"
                             : root.formatTime(root.elapsedFlightSeconds))
                    color: "#f4f8ff"
                    font.family: "Red Hat Mono"
                    font.pixelSize: Math.max(22, root.width * 0.033)
                    font.weight: Font.Bold
                }
            }
        }
    }

    Rectangle {
        visible: root.phase === 1
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: parent.width * 0.055
        anchors.rightMargin: parent.width * 0.055
        anchors.bottomMargin: parent.height * 0.055
        height: Math.max(80, parent.height * 0.105)
        radius: 12
        color: "#d90b1421"
        border.color: "#334d6886"

        Canvas {
            id: altitudeCanvas
            anchors.fill: parent
            anchors.margins: 10
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (!root.altitudeProfile || root.altitudeProfile.length < 2)
                    return
                var maximum = Math.max(1, root.peakHeight)
                var n = root.altitudeProfile.length
                ctx.beginPath()
                for (var i = 0; i < n; ++i) {
                    var x = i / (n - 1) * width
                    var y = height - Math.max(0, root.altitudeProfile[i]) / maximum * (height - 8)
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                ctx.strokeStyle = "#385770"
                ctx.lineWidth = 2
                ctx.stroke()
                var active = Math.max(1, Math.round(root.replayProgress * (n - 1)))
                ctx.beginPath()
                for (var j = 0; j <= active; ++j) {
                    var ax = j / (n - 1) * width
                    var ay = height - Math.max(0, root.altitudeProfile[j]) / maximum * (height - 8)
                    if (j === 0) ctx.moveTo(ax, ay); else ctx.lineTo(ax, ay)
                }
                ctx.strokeStyle = "#58cdfb"
                ctx.lineWidth = 4
                ctx.stroke()
            }
        }
    }

    Rectangle {
        visible: root.phase === 2
        anchors.centerIn: parent
        width: parent.width * (parent.width > parent.height ? 0.82 : 0.84)
        height: parent.height * (parent.width > parent.height ? 0.52 : 0.58)
        radius: 20
        color: "#e60b1421"
        border.width: 1
        border.color: "#37516c"

        Column {
            anchors.fill: parent
            anchors.margins: parent.width * 0.055
            spacing: parent.height * 0.06
            Text {
                width: parent.width
                text: root.flightTitle.length > 0 ? root.flightTitle : "Flight complete"
                color: "#f4f8ff"
                font.family: "Workbench"
                font.pixelSize: Math.max(34, root.width * 0.045)
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Grid {
                id: summaryGrid
                width: parent.width
                columns: root.width > root.height ? 3 : 1
                spacing: root.width > root.height ? parent.width * 0.035 : 14
                Repeater {
                    model: root.hasSpeed ? 3 : 2
                    delegate: Rectangle {
                        id: summaryTile
                        required property int index
                        width: root.width > root.height
                               ? (summaryGrid.width - summaryGrid.spacing * 2) / 3
                               : summaryGrid.width
                        height: root.width > root.height ? 150 : 125
                        radius: 12
                        color: "#d9132133"
                        border.color: "#304b66"
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: summaryTile.index === 0 ? "PEAK HEIGHT" : (summaryTile.index === 1 && root.hasSpeed ? "TOP SPEED" : "FLIGHT TIME")
                                color: "#8ca3ba"
                                font.family: "Red Hat Mono"
                                font.pixelSize: Math.max(11, root.width * 0.012)
                                font.letterSpacing: 1.2
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: summaryTile.index === 0 ? root.peakHeight.toFixed(1) + " m"
                                      : (summaryTile.index === 1 && root.hasSpeed ? root.maxSpeed.toFixed(1) + " m/s"
                                         : root.formatTime(root.flightDurationSeconds))
                                color: "#f4f8ff"
                                font.family: "Red Hat Mono"
                                font.pixelSize: Math.max(24, root.width * 0.033)
                                font.weight: Font.Bold
                            }
                            Text {
                                visible: summaryTile.index === 0
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Max altitude " + root.maxAltitude.toFixed(1) + " m"
                                color: "#71869c"
                                font.family: "Red Hat Mono"
                                font.pixelSize: Math.max(10, root.width * 0.011)
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: parent.width * 0.032
        text: "COSMOSOFT"
        color: "#b8d8ee"
        opacity: 0.82
        font.family: "Red Hat Mono"
        font.pixelSize: Math.max(12, root.width * 0.014)
        font.weight: Font.Bold
        font.letterSpacing: 1.5
    }

    Text {
        visible: root.terrainAttribution.length > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: parent.width * 0.018
        text: root.terrainAttribution
        color: "#b6c5d5"
        opacity: 0.78
        font.family: "Red Hat Mono"
        font.pixelSize: Math.max(9, root.width * 0.009)
    }

    function formatTime(seconds) {
        var value = Math.max(0, Math.round(seconds))
        var minutes = Math.floor(value / 60)
        var remainder = value % 60
        return minutes + ":" + (remainder < 10 ? "0" : "") + remainder
    }
}
