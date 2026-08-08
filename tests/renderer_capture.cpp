#include "app/input_preview_binding.h"
#include "app/search_controller.h"
#include "mutations/replay_input_script.h"
#include "viewer/graphics_settings.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <QVector3D>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>

namespace {

constexpr int kLoadTimeoutMs = 180000;
constexpr int kRenderTimeoutMs = 30000;
constexpr int kRequiredSettledFrames = 8;
constexpr double kMaximumCaptureAspectRatio = 8.0;

struct CameraOptions {
    double yaw = 35.0;
    double pitch = -20.0;
    double distance = 38.0;
    double fieldOfView = 55.0;
};

struct CaptureOptions {
    QString packsDirectory;
    QString replayPath;
    QString outputPath;
    QString mode = QStringLiteral("textured");
    QString textureFiltering = QStringLiteral("trilinear");
    bool replayInputs = false;
    bool skidmarks = true;
    double tickFraction = 0.5;
    QSize size{1280, 720};
    CameraOptions camera;
    bool includePaths = false;
};

struct ImageStatistics {
    int minimumLuminance = 255;
    int maximumLuminance = 0;
    double meanLuminance = 0.0;
    double opaqueFraction = 0.0;
    qsizetype distinctColors = 0;
    qsizetype sampleCount = 0;
};

void PrintError(const QString &message) {
    std::cerr << message.toLocal8Bit().constData() << '\n';
}

bool WaitUntil(const std::function<bool()> &predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(2);
    }
    return true;
}

std::optional<double> ParseFiniteDouble(const QString &text) {
    bool okay = false;
    const double value = QLocale::c().toDouble(text, &okay);
    if (!okay || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<QSize> ParseSize(const QString &text) {
    static const QRegularExpression expression(
            QStringLiteral("^([1-9][0-9]*)[xX]([1-9][0-9]*)$"));
    const QRegularExpressionMatch match = expression.match(text.trimmed());
    if (!match.hasMatch()) return std::nullopt;

    bool widthOkay = false;
    bool heightOkay = false;
    const int width = match.captured(1).toInt(&widthOkay);
    const int height = match.captured(2).toInt(&heightOkay);
    const double aspect = heightOkay && height > 0
            ? static_cast<double>(width) / static_cast<double>(height)
            : 0.0;
    if (!widthOkay || !heightOkay || width < 64 || height < 64 ||
        width > 8192 || height > 8192 ||
        static_cast<qint64>(width) * static_cast<qint64>(height) >
                64ll * 1024ll * 1024ll ||
        aspect < 1.0 / kMaximumCaptureAspectRatio ||
        aspect > kMaximumCaptureAspectRatio) {
        return std::nullopt;
    }
    return QSize(width, height);
}

std::optional<CameraOptions> ParseCamera(const QString &text) {
    const QStringList fields = text.split(',', Qt::KeepEmptyParts);
    if (fields.size() != 4) return std::nullopt;

    const std::optional<double> yaw = ParseFiniteDouble(fields[0]);
    const std::optional<double> pitch = ParseFiniteDouble(fields[1]);
    const std::optional<double> distance = ParseFiniteDouble(fields[2]);
    const std::optional<double> fieldOfView = ParseFiniteDouble(fields[3]);
    if (!yaw || !pitch || !distance || !fieldOfView ||
        *pitch < -89.0 || *pitch > 89.0 || *distance < 0.1 ||
        *distance > 100000.0 || *fieldOfView < 10.0 ||
        *fieldOfView > 120.0) {
        return std::nullopt;
    }
    return CameraOptions{*yaw, *pitch, *distance, *fieldOfView};
}

std::optional<CaptureOptions> ParseOptions(QCoreApplication &application) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "Capture a deterministic ForeverTAS renderer PNG using the real "
            "Windows D3D11 QRhi backend."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption packsOption(
            {QStringLiteral("p"), QStringLiteral("packs")},
            QStringLiteral("Installed TrackMania Packs directory."),
            QStringLiteral("directory"));
    const QCommandLineOption replayOption(
            {QStringLiteral("r"), QStringLiteral("replay")},
            QStringLiteral("Replay or challenge file to load."),
            QStringLiteral("file"));
    const QCommandLineOption outputOption(
            {QStringLiteral("o"), QStringLiteral("output")},
            QStringLiteral("Destination PNG. Metadata is written beside it "
                           "with a .json suffix."),
            QStringLiteral("png"));
    const QCommandLineOption modeOption(
            {QStringLiteral("m"), QStringLiteral("mode")},
            QStringLiteral("Mode: textured, neutral, collision, wireframe, "
                           "or material-debug."),
            QStringLiteral("mode"), QStringLiteral("textured"));
    const QCommandLineOption tickOption(
            QStringLiteral("tick-fraction"),
            QStringLiteral("Timeline fraction from 0 through 1."),
            QStringLiteral("fraction"), QStringLiteral("0.5"));
    const QCommandLineOption textureFilteringOption(
            QStringLiteral("texture-filtering"),
            QStringLiteral("Texture filtering: bilinear, trilinear, or sharp."),
            QStringLiteral("filter"), QStringLiteral("trilinear"));
    const QCommandLineOption sizeOption(
            {QStringLiteral("s"), QStringLiteral("size")},
            QStringLiteral("Output size as WIDTHxHEIGHT (64..8192, aspect "
                           "ratio at most 8:1)."),
            QStringLiteral("size"), QStringLiteral("1280x720"));
    const QCommandLineOption cameraOption(
            {QStringLiteral("c"), QStringLiteral("camera")},
            QStringLiteral("Orbital camera as YAW,PITCH,DISTANCE,FOV."),
            QStringLiteral("camera"), QStringLiteral("35,-20,38,55"));
    const QCommandLineOption includePathsOption(
            QStringLiteral("include-paths"),
            QStringLiteral("Include absolute local paths in JSON and stdout. "
                           "Paths are redacted to file names by default."));
    const QCommandLineOption replayInputsOption(
            QStringLiteral("replay-inputs"),
            QStringLiteral("Extract and simulate the replay's recorded "
                           "inputs."));
    const QCommandLineOption disableSkidmarksOption(
            QStringLiteral("disable-skidmarks"),
            QStringLiteral("Disable skidmark geometry for visual A/B tests."));
    parser.addOptions({packsOption, replayOption, outputOption, modeOption,
                       tickOption, textureFilteringOption, sizeOption, cameraOption,
                       includePathsOption, replayInputsOption,
                       disableSkidmarksOption});
    parser.process(application);

    if (!parser.isSet(packsOption) || !parser.isSet(replayOption) ||
        !parser.isSet(outputOption)) {
        PrintError(QStringLiteral(
                "--packs, --replay, and --output are required. Use --help "
                "for the complete syntax."));
        return std::nullopt;
    }

    CaptureOptions options;
    options.packsDirectory =
            QFileInfo(parser.value(packsOption)).absoluteFilePath();
    options.replayPath =
            QFileInfo(parser.value(replayOption)).absoluteFilePath();
    options.outputPath =
            QFileInfo(parser.value(outputOption)).absoluteFilePath();
    options.mode = parser.value(modeOption).trimmed().toLower();
    options.textureFiltering =
            parser.value(textureFilteringOption).trimmed().toLower();
    options.includePaths = parser.isSet(includePathsOption);
    options.replayInputs = parser.isSet(replayInputsOption);
    options.skidmarks = !parser.isSet(disableSkidmarksOption);

    static const QStringList supportedModes{
            QStringLiteral("textured"), QStringLiteral("neutral"),
            QStringLiteral("collision"), QStringLiteral("wireframe"),
            QStringLiteral("material-debug")};
    if (!supportedModes.contains(options.mode)) {
        PrintError(QStringLiteral(
                "unsupported raster mode '%1'; expected one of: %2")
                           .arg(options.mode, supportedModes.join(", ")));
        return std::nullopt;
    }

    static const QStringList supportedTextureFiltering{
            QStringLiteral("bilinear"), QStringLiteral("trilinear"),
            QStringLiteral("sharp")};
    if (!supportedTextureFiltering.contains(options.textureFiltering)) {
        PrintError(QStringLiteral(
                "unsupported texture filtering '%1'; expected one of: %2")
                           .arg(options.textureFiltering,
                                supportedTextureFiltering.join(", ")));
        return std::nullopt;
    }

    const std::optional<double> tickFraction =
            ParseFiniteDouble(parser.value(tickOption));
    if (!tickFraction || *tickFraction < 0.0 || *tickFraction > 1.0) {
        PrintError(QStringLiteral("--tick-fraction must be between 0 and 1"));
        return std::nullopt;
    }
    options.tickFraction = *tickFraction;

    const std::optional<QSize> size = ParseSize(parser.value(sizeOption));
    if (!size) {
        PrintError(QStringLiteral(
                "--size must be WIDTHxHEIGHT, each dimension 64..8192, "
                "with at most 64 megapixels and an aspect ratio between "
                "1:8 and 8:1"));
        return std::nullopt;
    }
    options.size = *size;

    const std::optional<CameraOptions> camera =
            ParseCamera(parser.value(cameraOption));
    if (!camera) {
        PrintError(QStringLiteral(
                "--camera must be finite YAW,PITCH,DISTANCE,FOV with pitch "
                "in [-89,89], positive distance, and FOV in [10,120]"));
        return std::nullopt;
    }
    options.camera = *camera;

    const QFileInfo packsInfo(options.packsDirectory);
    const QFileInfo replayInfo(options.replayPath);
    if (!packsInfo.isDir() || !packsInfo.isReadable()) {
        PrintError(QStringLiteral("Packs directory is not readable: %1")
                           .arg(options.packsDirectory));
        return std::nullopt;
    }
    if (!replayInfo.isFile() || !replayInfo.isReadable()) {
        PrintError(QStringLiteral("replay is not readable: %1")
                           .arg(options.replayPath));
        return std::nullopt;
    }
    if (QFileInfo(options.outputPath).suffix().compare(
                QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        PrintError(QStringLiteral("--output must end in .png"));
        return std::nullopt;
    }
    return options;
}

template <typename T>
T *FindRequired(QObject *root, const QString &objectName) {
    return qobject_cast<T *>(
            root != nullptr ? root->findChild<QObject *>(objectName) : nullptr);
}

void HideObjects(QObject *root, const QString &objectName) {
    if (root == nullptr) return;
    const QList<QObject *> objects =
            root->findChildren<QObject *>(objectName);
    for (QObject *object : objects) {
        object->setProperty("visible", false);
    }
}

void HideCaptureChrome(QObject *root) {
    static const QStringList objectNames{
            QStringLiteral("timelinePanel"),
            QStringLiteral("settingsPanel"),
            QStringLiteral("raceViewerHeader"),
            QStringLiteral("playbackDock"),
            QStringLiteral("checkpointSplitOverlay"),
            QStringLiteral("manualDriveStatus"),
            QStringLiteral("scriptedTelemetry"),
            QStringLiteral("cameraFocusToolbar"),
            QStringLiteral("whiteboardOverlay"),
            QStringLiteral("whiteboardPlaneView"),
            QStringLiteral("rayTracingTrajectoryOverlay"),
            QStringLiteral("rasterCuboidEditorScene"),
            QStringLiteral("rasterCustomVolumeEditorScene"),
            QStringLiteral("rasterPoseTargetEditorScene"),
            QStringLiteral("trajectoryPathModel")};
    for (const QString &objectName : objectNames) {
        HideObjects(root, objectName);
    }
}

QJsonArray VectorJson(const QVector3D &value) {
    return {value.x(), value.y(), value.z()};
}

ImageStatistics StatisticsFor(const QImage &source) {
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    ImageStatistics result;
    if (image.isNull()) return result;

    const int stepX = std::max(1, image.width() / 320);
    const int stepY = std::max(1, image.height() / 180);
    QSet<QRgb> distinctColors;
    long double luminanceSum = 0.0;
    qsizetype opaqueSamples = 0;
    for (int y = stepY / 2; y < image.height(); y += stepY) {
        const QRgb *const scanline =
                reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = stepX / 2; x < image.width(); x += stepX) {
            const QRgb pixel = scanline[x];
            const int luminance = qGray(pixel);
            result.minimumLuminance =
                    std::min(result.minimumLuminance, luminance);
            result.maximumLuminance =
                    std::max(result.maximumLuminance, luminance);
            luminanceSum += luminance;
            opaqueSamples += qAlpha(pixel) >= 250 ? 1 : 0;
            distinctColors.insert(pixel & 0x00ffffffu);
            ++result.sampleCount;
        }
    }
    if (result.sampleCount > 0) {
        result.meanLuminance = static_cast<double>(
                luminanceSum / static_cast<long double>(result.sampleCount));
        result.opaqueFraction = static_cast<double>(opaqueSamples) /
                static_cast<double>(result.sampleCount);
    }
    result.distinctColors = distinctColors.size();
    return result;
}

bool IsBlank(const QImage &image, const ImageStatistics &statistics) {
    return image.isNull() || image.width() <= 0 || image.height() <= 0 ||
            statistics.sampleCount == 0 || statistics.opaqueFraction < 0.95 ||
            statistics.maximumLuminance - statistics.minimumLuminance < 8 ||
            statistics.distinctColors < 16;
}

bool SavePng(const QImage &image, const QString &path, QString *error) {
    const QFileInfo outputInfo(path);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        *error = QStringLiteral("could not create output directory: %1")
                         .arg(outputInfo.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, "png");
    writer.setCompression(9);
    if (!writer.write(image)) {
        *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool SaveJson(const QJsonObject &object, const QString &path, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0) {
        *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QString MetadataPathFor(const QString &pngPath) {
    const QFileInfo info(pngPath);
    return QDir(info.absolutePath())
            .filePath(info.completeBaseName() + QStringLiteral(".json"));
}

QByteArray FileSha256(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}

}  // namespace

int main(int argc, char **argv) {
#ifndef Q_OS_WIN
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    PrintError(QStringLiteral(
            "forevertas-renderer-capture requires Windows and D3D11"));
    return 2;
#else
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("d3d11"));
    qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("threaded"));
    qputenv("QT_SCALE_FACTOR", QByteArrayLiteral("1"));
    qunsetenv("QT_QUICK_BACKEND");
    qunsetenv("QSG_RHI_PREFER_SOFTWARE_RENDERER");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QLocale::setDefault(QLocale::c());

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
            QStringLiteral("ForeverTASCapture"));
    QCoreApplication::setOrganizationDomain(
            QStringLiteral("capture.forevertas.local"));
    QCoreApplication::setApplicationName(
            QStringLiteral("ForeverTASRendererCapture"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1"));

    const std::optional<CaptureOptions> parsed = ParseOptions(application);
    if (!parsed) return 2;
    const CaptureOptions options = *parsed;

    QTemporaryDir settingsDirectory(
            QDir::temp().filePath(QStringLiteral("forevertas-capture-XXXXXX")));
    if (!settingsDirectory.isValid()) {
        PrintError(QStringLiteral("could not create isolated settings directory"));
        return 1;
    }
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());
    QSettings().clear();

    std::atomic_bool rhiSeen{false};
    std::atomic_bool deviceSeen{false};
    std::atomic_bool countSettledFrames{false};
    std::atomic_int settledFrames{0};

    forevertas::app::SearchController controller;
    forevertas::viewer::RaceViewerController viewer;
    forevertas::viewer::GraphicsSettings graphicsSettings;
    graphicsSettings.setRenderMode(options.mode);
    graphicsSettings.setTextureFiltering(options.textureFiltering);
    graphicsSettings.setSkidmarksEnabled(options.skidmarks);
    forevertas::app::BindInputPreview(controller, viewer);
    if (options.replayInputs) {
        try {
            controller.setBaseInputScript(QString::fromStdString(
                    forevertas::ExtractReplayInputScript(
                            options.packsDirectory.toUtf8().toStdString(),
                            options.replayPath.toUtf8().toStdString())));
        } catch (const std::exception &exception) {
            PrintError(QStringLiteral("replay input extraction failed: %1")
                               .arg(QString::fromUtf8(exception.what())));
            return 1;
        }
    }
    forevertas::viewer::RegisterRaceViewerQmlTypes();

    QQmlApplicationEngine engine;
    QObject::connect(
            &engine, &QQmlApplicationEngine::warnings,
            &application, [](const QList<QQmlError> &warnings) {
                for (const QQmlError &warning : warnings) {
                    std::cerr << warning.toString().toLocal8Bit().constData()
                              << '\n';
                }
            });
    engine.setInitialProperties({
            {QStringLiteral("controller"),
             QVariant::fromValue(static_cast<QObject *>(&controller))},
             {QStringLiteral("viewer"),
              QVariant::fromValue(static_cast<QObject *>(&viewer))},
             {QStringLiteral("graphicsSettings"),
              QVariant::fromValue(static_cast<QObject *>(&graphicsSettings))}});
    engine.load(QUrl::fromLocalFile(
            QStringLiteral(FOREVERTAS_SOURCE_DIR "/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        PrintError(QStringLiteral("Main.qml did not load"));
        return 1;
    }

    QObject *const root = engine.rootObjects().front();
    auto *const window = qobject_cast<QQuickWindow *>(root);
    QQuickItem *const viewport =
            FindRequired<QQuickItem>(root, QStringLiteral("raceViewport"));
    QQuickItem *const rasterView =
            FindRequired<QQuickItem>(root, QStringLiteral("rasterMapView"));
    QObject *const rasterEnvironment = FindRequired<QObject>(
            root, QStringLiteral("mapEnvironment"));
    if (window == nullptr || viewport == nullptr || rasterView == nullptr ||
        rasterEnvironment == nullptr) {
        PrintError(QStringLiteral(
                "Main.qml did not expose the viewport capture items"));
        return 1;
    }
    QQuickItem *const captureView = rasterView;

    QObject::connect(
            window, &QQuickWindow::beforeRendering, window,
            [window, &rhiSeen, &deviceSeen]() {
                QSGRendererInterface *const renderer =
                        window->rendererInterface();
                if (renderer == nullptr ||
                    renderer->graphicsApi() !=
                            QSGRendererInterface::Direct3D11) {
                    return;
                }
                rhiSeen.store(
                        renderer->getResource(
                                window, QSGRendererInterface::RhiResource) !=
                                nullptr,
                        std::memory_order_relaxed);
                deviceSeen.store(
                        renderer->getResource(
                                window, QSGRendererInterface::DeviceResource) !=
                                nullptr,
                        std::memory_order_relaxed);
            },
            Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&]() {
        if (countSettledFrames.load(std::memory_order_relaxed)) {
            settledFrames.fetch_add(1, std::memory_order_relaxed);
        }
    });

    root->setProperty("minimumWidth", 1);
    root->setProperty("minimumHeight", 1);
    root->setProperty("renderMode", options.mode);
    if (!rasterEnvironment->setProperty("aoDither", false)) {
        PrintError(QStringLiteral(
                "could not disable temporal ambient-occlusion dithering"));
        return 1;
    }
    HideCaptureChrome(root);
    const double targetAspect = static_cast<double>(options.size.width()) /
            static_cast<double>(options.size.height());
    const int internalHeight = std::max(
            options.size.height(),
            static_cast<int>(std::ceil(680.0 / targetAspect)));
    window->setMinimumSize(QSize(1, 1));
    window->resize(
            QSize(static_cast<int>(std::lround(internalHeight * targetAspect)) +
                          1,
                  internalHeight));
    if (QScreen *const screen = QGuiApplication::primaryScreen()) {
        window->setScreen(screen);
        window->setPosition(screen->availableGeometry().topLeft() +
                            QPoint(16, 16));
    }
    window->show();
    window->requestActivate();

    if (!WaitUntil(
                [&]() {
                    window->requestUpdate();
                    return captureView->width() > 0.0 &&
                            captureView->height() > 0.0 &&
                            window->isSceneGraphInitialized();
                },
                kRenderTimeoutMs)) {
        PrintError(QStringLiteral("Qt Quick scene graph did not initialize"));
        return 1;
    }

    viewer.loadMap(options.packsDirectory, options.replayPath);
    if (!WaitUntil([&]() { return !viewer.loading(); }, kLoadTimeoutMs) ||
        !viewer.loaded()) {
        PrintError(QStringLiteral("map load failed: %1")
                           .arg(viewer.statusText()));
        return 1;
    }
    if (viewer.visualBatchCount() <= 0 || viewer.visualTriangleCount() <= 0) {
        PrintError(QStringLiteral(
                "loaded scene has no renderable visual batches"));
        return 1;
    }
    if (!WaitUntil([&]() { return viewer.tickCount() > 0; },
                   kLoadTimeoutMs)) {
        PrintError(QStringLiteral(
                "timed out waiting for the replay timeline to become "
                "available: %1")
                           .arg(viewer.statusText()));
        return 1;
    }

    viewer.pause();
    const qint64 lastTick = std::max<qint64>(0, viewer.tickCount() - 1);
    const qint64 requestedTick = static_cast<qint64>(
            std::llround(options.tickFraction * static_cast<double>(lastTick)));
    viewer.setCurrentTick(requestedTick);

    root->setProperty("renderMode", options.mode);
    viewport->setProperty("freeCamera", false);
    viewport->setProperty("orbitalCamera", true);
    viewport->setProperty("cuboidFocused", false);
    viewport->setProperty("hasObjectFocus", false);
    viewport->setProperty("orbitYaw", options.camera.yaw);
    viewport->setProperty("orbitPitch", options.camera.pitch);
    viewport->setProperty("orbitDistance", options.camera.distance);
    viewport->setProperty("cameraFieldOfView",
                          options.camera.fieldOfView);
    HideCaptureChrome(root);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    const double sourceAspect = captureView->height() > 0.0
            ? captureView->width() / captureView->height()
            : 0.0;
    if (sourceAspect <= 0.0 ||
        std::abs(sourceAspect - targetAspect) > 0.01) {
        PrintError(QStringLiteral(
                "capture viewport aspect %1 does not match requested aspect %2")
                           .arg(sourceAspect, 0, 'f', 6)
                           .arg(targetAspect, 0, 'f', 6));
        return 1;
    }

    settledFrames.store(0, std::memory_order_relaxed);
    countSettledFrames.store(true, std::memory_order_relaxed);
    if (!WaitUntil(
                [&]() {
                    window->requestUpdate();
                    return settledFrames.load(std::memory_order_relaxed) >=
                            kRequiredSettledFrames;
                },
                kRenderTimeoutMs)) {
        PrintError(QStringLiteral(
                "timed out waiting for settled D3D11 frames"));
        return 1;
    }
    countSettledFrames.store(false, std::memory_order_relaxed);

    QSGRendererInterface *const renderer = window->rendererInterface();
    if (renderer == nullptr ||
        renderer->graphicsApi() != QSGRendererInterface::Direct3D11 ||
        !QSGRendererInterface::isApiRhiBased(renderer->graphicsApi()) ||
        !rhiSeen.load(std::memory_order_relaxed) ||
        !deviceSeen.load(std::memory_order_relaxed)) {
        PrintError(QStringLiteral(
                "capture did not obtain a real D3D11 QRhi and device resource"));
        return 1;
    }

    QImage image;
    ImageStatistics statistics;
    QElapsedTimer captureTimer;
    captureTimer.start();
    while (captureTimer.elapsed() < kRenderTimeoutMs && image.isNull()) {
        const QSharedPointer<QQuickItemGrabResult> grab =
                captureView->grabToImage(options.size);
        const int remaining = std::max(
                1, kRenderTimeoutMs -
                           static_cast<int>(captureTimer.elapsed()));
        if (!grab ||
            !WaitUntil([&]() { return !grab->image().isNull(); },
                       remaining)) {
            break;
        }
        QImage candidate =
                grab->image().convertToFormat(QImage::Format_ARGB32);
        if (candidate.size() != options.size) {
            const double returnedAspect =
                    static_cast<double>(candidate.width()) /
                    static_cast<double>(candidate.height());
            const double requestedAspect =
                    static_cast<double>(options.size.width()) /
                    static_cast<double>(options.size.height());
            if (std::abs(returnedAspect - requestedAspect) > 0.002) {
                PrintError(QStringLiteral(
                        "capture returned %1x%2 instead of %3x%4")
                                   .arg(candidate.width())
                                   .arg(candidate.height())
                                   .arg(options.size.width())
                                   .arg(options.size.height()));
                return 1;
            }
            // QQuickItemGrabResult reports physical pixels on some
            // per-monitor-DPI setups even when a logical target size was
            // requested. Normalize that deterministic scale here.
            candidate = candidate.scaled(
                    options.size, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
            candidate.setDevicePixelRatio(1.0);
        }
        statistics = StatisticsFor(candidate);
        if (!IsBlank(candidate, statistics)) {
            image = std::move(candidate);
            break;
        }
        window->requestUpdate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (image.isNull()) {
        PrintError(QStringLiteral(
                "%1 capture stayed blank, clear, or transparent (luma "
                "%2..%3, colors %4, opaque %5)")
                           .arg(options.mode)
                           .arg(statistics.minimumLuminance)
                           .arg(statistics.maximumLuminance)
                           .arg(statistics.distinctColors)
                           .arg(statistics.opaqueFraction, 0, 'f', 4));
        return 1;
    }

    QString saveError;
    if (!SavePng(image, options.outputPath, &saveError)) {
        PrintError(QStringLiteral("could not save PNG: %1").arg(saveError));
        return 1;
    }
    const QByteArray imageHash = FileSha256(options.outputPath);
    if (imageHash.isEmpty()) {
        PrintError(QStringLiteral("could not hash saved PNG"));
        return 1;
    }

    const QVector3D cameraPosition =
            viewport->property("sceneCameraPosition").value<QVector3D>();
    const QVector3D cameraTarget =
            viewport->property("cameraTarget").value<QVector3D>();
    quint64 skidmarkRibbonSegments = 0u;
    quint64 skidmarkStamps = 0u;
    const QVariantList skidmarkPaths = viewer.skidmarkPaths();
    for (const QVariant &path : skidmarkPaths) {
        const QVariantMap fields = path.toMap();
        skidmarkRibbonSegments +=
                fields.value(QStringLiteral("ribbonSegmentCount")).toULongLong();
        skidmarkStamps +=
                fields.value(QStringLiteral("stampCount")).toULongLong();
    }
    qint64 sharpFilteringMaterials = 0;
    qint64 sharpFilteringMipmappedMaterials = 0;
    for (const QVariant &material : viewer.visualMaterials()) {
        const QVariantMap fields = material.toMap();
        const QString materialClass =
                fields.value(QStringLiteral("materialClass")).toString();
        const bool eligible =
                fields.value(QStringLiteral("alphaMode")).toString() ==
                        QStringLiteral("opaque") &&
                materialClass != QStringLiteral("Grass") &&
                materialClass != QStringLiteral("Dirt") &&
                materialClass != QStringLiteral("Asphalt");
        sharpFilteringMaterials += eligible ? 1 : 0;
        sharpFilteringMipmappedMaterials +=
                eligible &&
                        fields.value(QStringLiteral("albedoGenerateMipmaps"))
                                .toBool()
                ? 1
                : 0;
    }
    const QString rendererName = QStringLiteral("ForeverTAS Qt Quick 3D raster");
    const auto evidencePath = [includePaths = options.includePaths](
                                      const QString &path) {
        return includePaths ? path : QFileInfo(path).fileName();
    };
    QJsonObject metadata{
            {QStringLiteral("schemaVersion"), 2},
            {QStringLiteral("renderer"), rendererName},
            {QStringLiteral("graphicsApi"), QStringLiteral("Direct3D11")},
            {QStringLiteral("realRhi"), true},
            {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
            {QStringLiteral("mode"), options.mode},
            {QStringLiteral("textureFiltering"), options.textureFiltering},
            {QStringLiteral("inputSource"),
             options.replayInputs ? QStringLiteral("recorded-replay")
                                  : QStringLiteral("empty-preview")},
            {QStringLiteral("packsDirectory"),
             evidencePath(options.packsDirectory)},
            {QStringLiteral("replayPath"), evidencePath(options.replayPath)},
            {QStringLiteral("outputPng"), evidencePath(options.outputPath)},
            {QStringLiteral("pathsRedacted"), !options.includePaths},
            {QStringLiteral("pngSha256"), QString::fromLatin1(imageHash)},
            {QStringLiteral("tickFraction"), options.tickFraction},
            {QStringLiteral("requestedTick"), requestedTick},
            {QStringLiteral("actualTick"), viewer.currentTick()},
            {QStringLiteral("timeMs"), viewer.timeMs()},
            {QStringLiteral("tickCount"), viewer.tickCount()},
            {QStringLiteral("durationMs"), viewer.durationMs()},
            {QStringLiteral("settledFrames"),
             kRequiredSettledFrames},
            {QStringLiteral("isolatedSettings"), true},
            {QStringLiteral("captureItem"), captureView->objectName()},
            {QStringLiteral("rayTracing"),
             QJsonObject{
                     {QStringLiteral("requested"), false},
                     {QStringLiteral("supported"), false},
                     {QStringLiteral("active"), false},
                     {QStringLiteral("status"), QStringLiteral("disabled")}}},
            {QStringLiteral("size"),
             QJsonObject{{QStringLiteral("width"), image.width()},
                         {QStringLiteral("height"), image.height()},
                         {QStringLiteral("sourceWidth"), captureView->width()},
                         {QStringLiteral("sourceHeight"),
                          captureView->height()}}},
            {QStringLiteral("camera"),
             QJsonObject{{QStringLiteral("yaw"), options.camera.yaw},
                         {QStringLiteral("pitch"), options.camera.pitch},
                         {QStringLiteral("distance"),
                          options.camera.distance},
                         {QStringLiteral("fieldOfView"),
                          options.camera.fieldOfView},
                         {QStringLiteral("position"),
                          VectorJson(cameraPosition)},
                         {QStringLiteral("target"),
                          VectorJson(cameraTarget)},
                         {QStringLiteral("rayTracingPosition"),
                          VectorJson(cameraPosition)},
                         {QStringLiteral("rayTracingTarget"),
                          VectorJson(cameraTarget)},
                         {QStringLiteral("rayTracingUp"),
                          VectorJson(QVector3D(0.0f, 1.0f, 0.0f))},
                         {QStringLiteral("rayTracingFieldOfView"),
                          options.camera.fieldOfView}}},
            {QStringLiteral("scene"),
             QJsonObject{{QStringLiteral("visualBatches"),
                          viewer.visualBatchCount()},
                         {QStringLiteral("visualMeshes"),
                          viewer.visualMeshCount()},
                          {QStringLiteral("visualTriangles"),
                           viewer.visualTriangleCount()},
                          {QStringLiteral("materials"),
                           viewer.materialCount()},
                          {QStringLiteral("sharpFilteringMaterials"),
                           sharpFilteringMaterials},
                          {QStringLiteral("sharpFilteringMipmappedMaterials"),
                           sharpFilteringMipmappedMaterials},
                          {QStringLiteral("skidmarksEnabled"),
                           viewer.skidmarksEnabled()},
                          {QStringLiteral("skidmarkPaths"),
                           viewer.skidmarkCount()},
                          {QStringLiteral("skidmarkRibbonSegments"),
                           static_cast<qint64>(skidmarkRibbonSegments)},
                          {QStringLiteral("skidmarkStamps"),
                           static_cast<qint64>(skidmarkStamps)}}},
            {QStringLiteral("rendererTelemetry"),
             QJsonObject::fromVariantMap(viewer.rendererTelemetry())},
            {QStringLiteral("image"),
             QJsonObject{{QStringLiteral("minimumLuminance"),
                          statistics.minimumLuminance},
                         {QStringLiteral("maximumLuminance"),
                          statistics.maximumLuminance},
                         {QStringLiteral("meanLuminance"),
                          statistics.meanLuminance},
                         {QStringLiteral("opaqueFraction"),
                          statistics.opaqueFraction},
                         {QStringLiteral("sampleCount"),
                          statistics.sampleCount},
                         {QStringLiteral("distinctSampleColors"),
                          statistics.distinctColors}}}};

    const QString metadataPath = MetadataPathFor(options.outputPath);
    if (!SaveJson(metadata, metadataPath, &saveError)) {
        PrintError(QStringLiteral("could not save metadata JSON: %1")
                           .arg(saveError));
        return 1;
    }

    std::cout << "PNG "
              << evidencePath(options.outputPath).toLocal8Bit().constData()
              << '\n'
              << "JSON "
              << evidencePath(metadataPath).toLocal8Bit().constData() << '\n'
              << "SHA256 " << imageHash.constData() << '\n';
    window->hide();
    return 0;
#endif
}
