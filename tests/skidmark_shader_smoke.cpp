#include "viewer/skidmark_geometry.h"

#include <QCoreApplication>
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
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    previousMessageHandler = qInstallMessageHandler(CaptureMessage);

    const auto resourceHasContent = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) && !file.readAll().isEmpty();
    };
    const bool packagedShaders = resourceHasContent(QStringLiteral(
                                             ":/qt/qml/ForeverTAS/qml/"
                                             "shaders/skidmark.vert")) &&
            resourceHasContent(QStringLiteral(
                    ":/qt/qml/ForeverTAS/qml/shaders/skidmark.frag"));
    if (!packagedShaders) {
        qInstallMessageHandler(previousMessageHandler);
        std::cerr << "packaged skidmark shader resources are missing\n";
        return 1;
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

    const QUrl fixture = QUrl::fromLocalFile(QStringLiteral(
            FOREVERTAS_SOURCE_DIR "/tests/skidmark_shader_smoke.qml"));
    engine.load(fixture);
    if (engine.rootObjects().isEmpty()) {
        qInstallMessageHandler(previousMessageHandler);
        std::cerr << "failed to load skidmark shader smoke scene\n";
        return 1;
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
        std::cerr << "skidmark smoke scene was never exposed\n";
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
        std::cerr << "skidmark shader produced only " << darkPixels
                  << " non-background pixels\n";
        return 1;
    }

    std::cout << "skidmark shader smoke passed (" << darkPixels
              << " non-background pixels)\n";
    return 0;
}
