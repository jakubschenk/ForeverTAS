#include "viewer/skidmark_geometry.h"

#include <QCoreApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QUrl>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

QMutex messageMutex;
QStringList capturedMessages;
QtMessageHandler previousMessageHandler = nullptr;

void CaptureMessage(QtMsgType type, const QMessageLogContext &context,
                    const QString &message) {
    {
        QMutexLocker lock(&messageMutex);
        capturedMessages.push_back(message);
    }
    if (previousMessageHandler != nullptr) {
        previousMessageHandler(type, context, message);
    }
}

bool WaitForExposure(QQuickWindow *window, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!window->isExposed() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(2);
    }
    return window->isExposed();
}

bool HasShaderFailure() {
    QMutexLocker lock(&messageMutex);
    for (const QString &message : capturedMessages) {
        const QString lowered = message.toLower();
        if (lowered.contains(QStringLiteral("shader")) &&
            (lowered.contains(QStringLiteral("failed")) ||
             lowered.contains(QStringLiteral("error")))) {
            std::cerr << "shader diagnostic: "
                      << message.toLocal8Bit().constData() << '\n';
            return true;
        }
    }
    return false;
}

std::size_t DarkPixelCount(const QImage &source) {
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    std::size_t count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row =
                reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = row[x];
            if (qRed(pixel) < 245 || qGreen(pixel) < 245 ||
                qBlue(pixel) < 245) {
                ++count;
            }
        }
    }
    return count;
}

forevertas::viewer::SkidmarkMeshData MakeSmokeMesh() {
    using forevertas::viewer::SkidmarkSample;

    SkidmarkSample first;
    first.timeMs = 0;
    first.wheels[0].groundPosition = {0.0f, 0.0f, -0.10f};
    first.wheels[0].contact = true;
    first.wheels[0].sliding = true;
    first.wheels[0].surface = 0;
    first.wheels[1].groundPosition = {0.9f, 0.0f, -0.10f};

    SkidmarkSample second = first;
    second.timeMs = 50;
    second.wheels[0].groundPosition.setZ(0.10f);
    second.wheels[1].groundPosition.setZ(0.10f);

    return forevertas::viewer::BuildSkidmarkMesh({first, second});
}

}  // namespace

int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "windows");
    qputenv("QSG_RHI_BACKEND", "d3d11");
    qputenv("QSG_RENDER_LOOP", "basic");
#endif
    QGuiApplication application(argc, argv);
    const bool trajectoryMode =
            application.arguments().contains(QStringLiteral("--trajectory"));
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    previousMessageHandler = qInstallMessageHandler(CaptureMessage);

    const auto readContent = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const QString shaderName = trajectoryMode
            ? QStringLiteral("trajectory")
            : QStringLiteral("skidmark");
    const QString resourceBase =
            QStringLiteral(":/qt/qml/ForeverTAS/qml/shaders/") + shaderName;
    const QByteArray vertexSource =
            readContent(resourceBase + QStringLiteral(".vert"));
    const QByteArray fragmentSource =
            readContent(resourceBase + QStringLiteral(".frag"));
    const bool packagedShaders =
            !vertexSource.isEmpty() && !fragmentSource.isEmpty();
    if (!packagedShaders) {
        qInstallMessageHandler(previousMessageHandler);
        std::cerr << "packaged " << shaderName.toStdString()
                  << " shader resources are missing\n";
        return 1;
    }
    if (trajectoryMode) {
        const QByteArray materialSource = readContent(QStringLiteral(
                FOREVERTAS_SOURCE_DIR "/qml/TrajectoryMaterial.qml"));
        const QByteArray mainSource = readContent(QStringLiteral(
                FOREVERTAS_SOURCE_DIR "/qml/Main.qml"));
        const qsizetype rasterModelStart =
                mainSource.indexOf("objectName: \"trajectoryPathModel\"");
        const qsizetype rasterModelEnd = mainSource.indexOf(
                "model: window.viewer.skidmarkPaths", rasterModelStart);
        const QByteArray rasterModelSource =
                rasterModelStart >= 0 && rasterModelEnd > rasterModelStart
                ? mainSource.mid(rasterModelStart,
                                 rasterModelEnd - rasterModelStart)
                : QByteArray{};
        const qsizetype rayModelStart = mainSource.indexOf(
                "\"rayTracingTrajectoryPathModel\"");
        const qsizetype rayModelEnd =
                mainSource.indexOf("CuboidEditorScene {", rayModelStart);
        const QByteArray rayModelSource =
                rayModelStart >= 0 && rayModelEnd > rayModelStart
                ? mainSource.mid(rayModelStart,
                                 rayModelEnd - rayModelStart)
                : QByteArray{};
        const bool sourceContract =
                vertexSource.contains(
                        "POSITION = MODELVIEWPROJECTION_MATRIX * "
                        "vec4(VERTEX, 1.0);") &&
                vertexSource.contains(
                        "POSITION.z -= 0.000005 * POSITION.w;") &&
                !vertexSource.contains("VERTEX.y") &&
                !vertexSource.contains("MODEL_MATRIX") &&
                fragmentSource.contains("trajectoryColor.rgb") &&
                fragmentSource.contains(
                        "trajectoryColor.a * trajectoryOpacity") &&
                materialSource.contains(
                        "shadingMode: CustomMaterial.Unshaded") &&
                materialSource.contains(
                        "sourceBlend: CustomMaterial.SrcAlpha") &&
                materialSource.contains(
                        "destinationBlend: "
                        "CustomMaterial.OneMinusSrcAlpha") &&
                materialSource.contains(
                        "depthDrawMode: Material.NeverDepthDraw") &&
                materialSource.contains("cullMode: Material.NoCulling") &&
                mainSource.count("materials: TrajectoryMaterial") == 2 &&
                rasterModelSource.contains(
                        "trajectoryColor: modelData.color") &&
                rasterModelSource.contains(
                        "trajectoryOpacity: modelData.opacity") &&
                !rasterModelSource.contains("depthBias") &&
                rayModelSource.contains(
                        "trajectoryColor: modelData.color") &&
                rayModelSource.contains(
                        "trajectoryOpacity: modelData.opacity") &&
                !rayModelSource.contains("depthBias");
        if (!sourceContract) {
            qInstallMessageHandler(previousMessageHandler);
            std::cerr << "trajectory shader/material source contract failed\n";
            return 1;
        }
    }

    forevertas::viewer::SkidmarkGeometry geometry;
    geometry.setSkidmarkMesh(MakeSmokeMesh());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("testGeometry"),
                                             &geometry);
    QObject::connect(
            &engine, &QQmlApplicationEngine::warnings,
            [](const QList<QQmlError> &warnings) {
                for (const QQmlError &warning : warnings) {
                    std::cerr << warning.toString().toLocal8Bit().constData()
                              << '\n';
                }
            });

    const QUrl fixture = QUrl::fromLocalFile(
            QStringLiteral(FOREVERTAS_SOURCE_DIR "/tests/") +
            (trajectoryMode
                     ? QStringLiteral("trajectory_shader_smoke.qml")
                     : QStringLiteral("skidmark_shader_smoke.qml")));
    engine.load(fixture);
    if (engine.rootObjects().isEmpty()) {
        qInstallMessageHandler(previousMessageHandler);
        std::cerr << "failed to load " << shaderName.toStdString()
                  << " shader smoke scene\n";
        return 1;
    }

    if (trajectoryMode) {
        QObject *const rootObject = engine.rootObjects().constFirst();
        QObject *const model = rootObject->findChild<QObject *>(
                QStringLiteral("trajectorySmokeModel"));
        QObject *const material = rootObject->findChild<QObject *>(
                QStringLiteral("trajectorySmokeMaterial"));
        const bool runtimeContract =
                model != nullptr && material != nullptr &&
                !model->property("castsShadows").toBool() &&
                !model->property("receivesShadows").toBool() &&
                material->property("trajectoryColor").value<QColor>() ==
                        QColor(QStringLiteral("#c02020")) &&
                std::fabs(material->property("trajectoryOpacity").toDouble() -
                          0.85) < 0.0001;
        if (!runtimeContract) {
            qInstallMessageHandler(previousMessageHandler);
            std::cerr << "trajectory material runtime contract failed\n";
            return 1;
        }
    }

    auto *window =
            qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        qInstallMessageHandler(previousMessageHandler);
        std::cerr << "smoke scene root is not a QQuickWindow\n";
        return 1;
    }
    window->show();

    const bool exposed = WaitForExposure(window, 10000);
    for (int frame = 0; frame < 10; ++frame) {
        window->requestUpdate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    const QImage image = window->grabWindow();
    const bool shaderFailed = HasShaderFailure();
    qInstallMessageHandler(previousMessageHandler);

    if (!exposed) {
        std::cerr << shaderName.toStdString()
                  << " smoke scene was never exposed\n";
        return 1;
    }
    if (shaderFailed) {
        return 1;
    }
    if (image.isNull()) {
        std::cerr << "skidmark smoke scene grab was empty\n";
        return 1;
    }
    const std::size_t darkPixels = DarkPixelCount(image);
    if (darkPixels < 100u) {
        std::cerr << shaderName.toStdString() << " shader produced only "
                  << darkPixels
                  << " non-background pixels\n";
        return 1;
    }

    std::cout << shaderName.toStdString() << " shader smoke passed ("
              << darkPixels
              << " non-background pixels)\n";
    return 0;
}
