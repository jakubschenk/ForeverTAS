#include "viewer/graphics_settings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QString>
#include <QVariant>

#include <iostream>

namespace {

using forevertas::viewer::GraphicsSettings;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

QString SettingsPath(QTemporaryDir &temporaryDirectory) {
    return temporaryDirectory.filePath(QStringLiteral("graphics-settings.ini"));
}

bool TestDefaults() {
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return false;
    }
    const QString path = SettingsPath(temporaryDirectory);
    QSettings(path, QSettings::IniFormat).clear();

    const GraphicsSettings settings(path);
    bool okay = Check(settings.renderMode() == QStringLiteral("textured"),
                      "renderMode default was incorrect");
    okay &= Check(settings.lightingMode() == QStringLiteral("authored"),
                  "lightingMode default was incorrect");
    okay &= Check(settings.msaaSamples() == 2, "msaaSamples default was not 2");
    okay &= Check(settings.textureFiltering() ==
                          QStringLiteral("trilinear"),
                  "textureFiltering default was not trilinear");
    okay &= Check(!settings.worldShadows(),
                  "worldShadows default was not false");
    okay &= Check(settings.skidmarksEnabled(),
                  "skidmarksEnabled default was not true");
    return okay;
}

bool TestPersistence() {
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return false;
    }
    const QString path = SettingsPath(temporaryDirectory);
    QSettings settingsFile(path, QSettings::IniFormat);
    settingsFile.clear();

    {
        GraphicsSettings settings(path);
        settings.setRenderMode(QStringLiteral("wireframe"));
        settings.setLightingMode(QStringLiteral("lit"));
        settings.setMsaaSamples(4);
        settings.setTextureFiltering(QStringLiteral("anisotropic"));
        settings.setWorldShadows(true);
        settings.setSkidmarksEnabled(false);
    }

    QSettings restoredFile(path, QSettings::IniFormat);
    bool okay = Check(restoredFile.value(QStringLiteral("graphics/renderMode"))
                                  .toString() == QStringLiteral("wireframe"),
                      "renderMode was not persisted");
    okay &= Check(restoredFile.value(QStringLiteral("graphics/lightingMode"))
                               .toString() == QStringLiteral("lit"),
                   "lightingMode was not persisted");
    okay &= Check(restoredFile.value(QStringLiteral("graphics/msaaSamples")).toInt() ==
                          4,
                  "msaaSamples was not persisted");
    okay &= Check(restoredFile.value(QStringLiteral("graphics/textureFiltering"))
                               .toString() == QStringLiteral("anisotropic"),
                   "textureFiltering was not persisted");
    okay &= Check(restoredFile.value(QStringLiteral("graphics/worldShadows"))
                                  .toBool() == true,
                  "worldShadows was not persisted");
    okay &= Check(
            !restoredFile.value(QStringLiteral("graphics/skidmarksEnabled"))
                     .toBool(),
            "skidmarksEnabled was not persisted");

    const GraphicsSettings restored(path);
    okay &= Check(restored.renderMode() == QStringLiteral("wireframe"),
                  "renderMode was not restored");
    okay &= Check(restored.lightingMode() == QStringLiteral("lit"),
                  "lightingMode was not restored");
    okay &= Check(restored.msaaSamples() == 4,
                  "msaaSamples was not restored");
    okay &= Check(restored.textureFiltering() == QStringLiteral("anisotropic"),
                  "textureFiltering was not restored");
    okay &= Check(restored.worldShadows(),
                  "worldShadows was not restored");
    okay &= Check(!restored.skidmarksEnabled(),
                  "skidmarksEnabled was not restored");
    return okay;
}

bool TestInvalidRepairs() {
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return false;
    }
    const QString path = SettingsPath(temporaryDirectory);
    {
        QSettings damaged(path, QSettings::IniFormat);
        damaged.setValue(QStringLiteral("graphics/renderMode"),
                         QStringLiteral("not-a-mode"));
        damaged.setValue(QStringLiteral("graphics/lightingMode"),
                         QStringLiteral("overexposed"));
        damaged.setValue(QStringLiteral("graphics/msaaSamples"), 10);
        damaged.setValue(QStringLiteral("graphics/textureFiltering"),
                         QStringLiteral("pixelated"));
        damaged.setValue(QStringLiteral("graphics/worldShadows"), 2);
        damaged.setValue(QStringLiteral("graphics/skidmarksEnabled"), 2);
        damaged.sync();
    }

    const GraphicsSettings repaired(path);
    bool okay = Check(repaired.renderMode() == QStringLiteral("textured"),
                      "invalid renderMode was not repaired");
    okay &= Check(repaired.lightingMode() == QStringLiteral("authored"),
                  "invalid lightingMode was not repaired");
    okay &= Check(repaired.msaaSamples() == 2,
                  "invalid msaaSamples was not repaired");
    okay &= Check(repaired.textureFiltering() == QStringLiteral("trilinear"),
                  "invalid textureFiltering was not repaired");
    okay &= Check(!repaired.worldShadows(),
                  "invalid worldShadows was not repaired");
    okay &= Check(repaired.skidmarksEnabled(),
                  "invalid skidmarksEnabled was not repaired");

    QSettings repairedFile(path, QSettings::IniFormat);
    okay &= Check(repairedFile.value(QStringLiteral("graphics/renderMode"))
                                  .toString() == QStringLiteral("textured"),
                  "repaired renderMode was not persisted");
    okay &= Check(repairedFile.value(QStringLiteral("graphics/lightingMode"))
                                  .toString() == QStringLiteral("authored"),
                  "repaired lightingMode was not persisted");
    okay &= Check(repairedFile.value(QStringLiteral("graphics/msaaSamples")).toInt() ==
                          2,
                  "repaired msaaSamples was not persisted");
    okay &= Check(repairedFile.value(QStringLiteral("graphics/textureFiltering"))
                                  .toString() == QStringLiteral("trilinear"),
                  "repaired textureFiltering was not persisted");
    okay &= Check(!repairedFile.value(QStringLiteral("graphics/worldShadows"))
                                  .toBool(),
                  "repaired worldShadows was not persisted");
    okay &= Check(
            repairedFile.value(QStringLiteral("graphics/skidmarksEnabled"))
                    .toBool(),
            "repaired skidmarksEnabled was not persisted");
    return okay;
}

bool TestMigration() {
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return false;
    }
    const QString path = SettingsPath(temporaryDirectory);
    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("graphics/renderMode"),
                        QStringLiteral("textured-rt"));
        legacy.setValue(QStringLiteral("graphics/lightingMode"),
                        QStringLiteral("authored"));
        legacy.setValue(QStringLiteral("graphics/msaaSamples"), 2);
        legacy.setValue(QStringLiteral("graphics/textureFiltering"),
                        QStringLiteral("trilinear"));
        legacy.setValue(QStringLiteral("graphics/worldShadows"), false);
        legacy.sync();
    }

    const GraphicsSettings settings(path);
    const bool okay =
            Check(settings.renderMode() == QStringLiteral("textured"),
                  "legacy textured-rt was not migrated");
    if (!okay) {
        return false;
    }

    const QSettings migrated(path, QSettings::IniFormat);
    return Check(migrated.value(QStringLiteral("graphics/renderMode"))
                         .toString() == QStringLiteral("textured"),
                 "migrated textured-rt was not persisted as textured");
}

bool TestSignals() {
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return false;
    }
    const QString path = SettingsPath(temporaryDirectory);
    GraphicsSettings settings(path);

    int renderChanges = 0;
    int lightingChanges = 0;
    int msaaChanges = 0;
    int filteringChanges = 0;
    int shadowChanges = 0;
    int skidmarkChanges = 0;

    QObject::connect(&settings, &GraphicsSettings::renderModeChanged, [&]() {
        ++renderChanges;
    });
    QObject::connect(&settings, &GraphicsSettings::lightingModeChanged, [&]() {
        ++lightingChanges;
    });
    QObject::connect(&settings, &GraphicsSettings::msaaSamplesChanged, [&]() {
        ++msaaChanges;
    });
    QObject::connect(&settings,
                     &GraphicsSettings::textureFilteringChanged, [&]() {
                         ++filteringChanges;
                     });
    QObject::connect(&settings, &GraphicsSettings::worldShadowsChanged, [&]() {
        ++shadowChanges;
    });
    QObject::connect(&settings, &GraphicsSettings::skidmarksEnabledChanged,
                     [&]() { ++skidmarkChanges; });

    settings.setRenderMode(QStringLiteral("wireframe"));
    settings.setRenderMode(QStringLiteral("wireframe"));
    settings.setLightingMode(QStringLiteral("lit"));
    settings.setLightingMode(QStringLiteral("lit"));
    settings.setMsaaSamples(0);
    settings.setMsaaSamples(0);
    settings.setTextureFiltering(QStringLiteral("anisotropic"));
    settings.setTextureFiltering(QStringLiteral("anisotropic"));
    settings.setWorldShadows(true);
    settings.setWorldShadows(true);
    settings.setSkidmarksEnabled(false);
    settings.setSkidmarksEnabled(false);

    bool okay = Check(renderChanges == 1, "renderModeChanged emitted twice");
    okay &= Check(lightingChanges == 1,
                  "lightingModeChanged emitted twice");
    okay &= Check(msaaChanges == 1, "msaaSamplesChanged emitted twice");
    okay &= Check(filteringChanges == 1,
                  "textureFilteringChanged emitted twice");
    okay &= Check(shadowChanges == 1, "worldShadowsChanged emitted twice");
    okay &= Check(skidmarkChanges == 1,
                  "skidmarksEnabledChanged emitted twice");
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);

    int failures = 0;
    const auto expect = [&failures](bool condition, const char *message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    expect(TestDefaults(), "default values were incorrect");
    expect(TestPersistence(), "persistence did not round-trip all fields");
    expect(TestInvalidRepairs(), "invalid stored settings were not repaired");
    expect(TestMigration(), "textured-rt migration did not run");
    expect(TestSignals(), "property-change signals were not correctly emitted");

    if (failures == 0) {
        std::cout << "graphics settings tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
