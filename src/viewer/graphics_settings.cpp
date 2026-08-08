#include "viewer/graphics_settings.h"

#include <QMetaType>
#include <QSettings>
#include <QVariant>

namespace forevertas::viewer {
namespace {

constexpr char kRenderModeKey[] = "graphics/renderMode";
constexpr char kLightingModeKey[] = "graphics/lightingMode";
constexpr char kMsaaSamplesKey[] = "graphics/msaaSamples";
constexpr char kTextureFilteringKey[] = "graphics/textureFiltering";
constexpr char kWorldShadowsKey[] = "graphics/worldShadows";

constexpr char kDefaultRenderMode[] = "textured";
constexpr char kDefaultLightingMode[] = "authored";
constexpr char kDefaultTextureFiltering[] = "trilinear";
constexpr int kDefaultMsaaSamples = 2;
constexpr bool kDefaultWorldShadows = false;
constexpr char kRenderModeMigrationValue[] = "textured-rt";
constexpr char kRenderModeNeutral[] = "neutral";
constexpr char kRenderModeMaterialDebug[] = "material-debug";
constexpr char kLightingLit[] = "lit";
constexpr char kLightingAuthored[] = "authored";
constexpr char kTextureFilteringBilinear[] = "bilinear";
constexpr char kTextureFilteringTrilinear[] = "trilinear";
constexpr char kTextureFilteringAnisotropic[] = "anisotropic";

QSettings OpenSettings(const QString &path) {
    return path.isEmpty() ? QSettings() : QSettings(path, QSettings::IniFormat);
}

bool IsRenderMode(const QString &value) {
    return value == QStringLiteral("textured") ||
            value == QString::fromLatin1(kRenderModeNeutral) ||
            value == QStringLiteral("collision") ||
            value == QStringLiteral("wireframe") ||
            value == QString::fromLatin1(kRenderModeMaterialDebug);
}

bool IsLightingMode(const QString &value) {
    return value == QString::fromLatin1(kLightingAuthored) ||
            value == QString::fromLatin1(kLightingLit);
}

bool IsTextureFiltering(const QString &value) {
    return value == QString::fromLatin1(kTextureFilteringBilinear) ||
            value == QString::fromLatin1(kTextureFilteringTrilinear) ||
            value == QString::fromLatin1(kTextureFilteringAnisotropic);
}

bool IsMsaaSamples(int value) {
    return value == 0 || value == 2 || value == 4;
}

}  // namespace

GraphicsSettings::GraphicsSettings(QObject *parent) : QObject(parent) {
    Load();
}

GraphicsSettings::GraphicsSettings(const QString &settingsFile, QObject *parent)
    : QObject(parent), settingsFile_(settingsFile) {
    Load();
}

QString GraphicsSettings::renderMode() const {
    return renderMode_;
}

QString GraphicsSettings::lightingMode() const {
    return lightingMode_;
}

int GraphicsSettings::msaaSamples() const {
    return msaaSamples_;
}

QString GraphicsSettings::textureFiltering() const {
    return textureFiltering_;
}

bool GraphicsSettings::worldShadows() const {
    return worldShadows_;
}

void GraphicsSettings::setRenderMode(const QString &value) {
    bool repaired = false;
    const QString repairedValue = RenderModeFromString(value, &repaired);
    if (repairedValue == renderMode_) {
        if (!repaired) {
            return;
        }
    } else {
        renderMode_ = repairedValue;
        emit renderModeChanged();
    }
    Persist(QString::fromLatin1(kRenderModeKey), renderMode_);
}

void GraphicsSettings::setLightingMode(const QString &value) {
    bool repaired = false;
    const QString repairedValue = LightingModeFromString(value, &repaired);
    if (repairedValue == lightingMode_) {
        if (!repaired) {
            return;
        }
    } else {
        lightingMode_ = repairedValue;
        emit lightingModeChanged();
    }
    Persist(QString::fromLatin1(kLightingModeKey), lightingMode_);
}

void GraphicsSettings::setMsaaSamples(int value) {
    const int repairedValue = IsMsaaSamples(value) ? value : kDefaultMsaaSamples;
    const bool repaired = repairedValue != value;
    if (repairedValue == msaaSamples_) {
        if (!repaired) {
            return;
        }
    } else {
        msaaSamples_ = repairedValue;
        emit msaaSamplesChanged();
    }
    Persist(QString::fromLatin1(kMsaaSamplesKey), msaaSamples_);
}

void GraphicsSettings::setTextureFiltering(const QString &value) {
    bool repaired = false;
    const QString repairedValue = TextureFilteringFromString(value, &repaired);
    if (repairedValue == textureFiltering_) {
        if (!repaired) {
            return;
        }
    } else {
        textureFiltering_ = repairedValue;
        emit textureFilteringChanged();
    }
    Persist(QString::fromLatin1(kTextureFilteringKey), textureFiltering_);
}

void GraphicsSettings::setWorldShadows(bool value) {
    if (value == worldShadows_) {
        return;
    }
    worldShadows_ = value;
    emit worldShadowsChanged();
    Persist(QString::fromLatin1(kWorldShadowsKey), worldShadows_);
}

QString GraphicsSettings::RenderModeFromString(const QString &value,
                                              bool *repaired) {
    if (value == QString::fromLatin1(kRenderModeMigrationValue)) {
        *repaired = true;
        return QString::fromLatin1(kDefaultRenderMode);
    }
    if (IsRenderMode(value)) {
        *repaired = false;
        return value;
    }
    *repaired = true;
    return QString::fromLatin1(kDefaultRenderMode);
}

QString GraphicsSettings::LightingModeFromString(const QString &value,
                                                bool *repaired) {
    if (IsLightingMode(value)) {
        *repaired = false;
        return value;
    }
    *repaired = true;
    return QString::fromLatin1(kDefaultLightingMode);
}

int GraphicsSettings::MsaaSamplesFromValue(const QVariant &value,
                                          bool *repaired) {
    bool converted = false;
    const int msaaSamples = value.toInt(&converted);
    if (!converted || !IsMsaaSamples(msaaSamples)) {
        *repaired = true;
        return kDefaultMsaaSamples;
    }
    *repaired = false;
    return msaaSamples;
}

QString GraphicsSettings::TextureFilteringFromString(const QString &value,
                                                   bool *repaired) {
    if (IsTextureFiltering(value)) {
        *repaired = false;
        return value;
    }
    *repaired = true;
    return QString::fromLatin1(kDefaultTextureFiltering);
}

bool GraphicsSettings::WorldShadowsFromValue(const QVariant &value,
                                            bool *repaired) {
    if (value.userType() == QMetaType::Bool) {
        *repaired = false;
        return value.toBool();
    }
    bool converted = false;
    const int integer = value.toInt(&converted);
    if (converted && (integer == 0 || integer == 1)) {
        *repaired = false;
        return integer != 0;
    }
    if (value.userType() == QMetaType::QString) {
        const QString text = value.toString().toLower();
        if (text == QStringLiteral("false") ||
            text == QStringLiteral("0") ||
            text == QStringLiteral("no") ||
            text == QStringLiteral("off")) {
            *repaired = false;
            return false;
        }
        if (text == QStringLiteral("true") ||
            text == QStringLiteral("1") ||
            text == QStringLiteral("yes") ||
            text == QStringLiteral("on")) {
            *repaired = false;
            return true;
        }
    }
    *repaired = true;
    return kDefaultWorldShadows;
}

void GraphicsSettings::Load() {
    QSettings settings = OpenSettings(settingsFile_);
    bool renderModeRepaired = false;
    bool lightingModeRepaired = false;
    bool msaaSamplesRepaired = false;
    bool textureFilteringRepaired = false;
    bool worldShadowsRepaired = false;

    renderMode_ = RenderModeFromString(
            settings.value(QString::fromLatin1(kRenderModeKey),
                           QString::fromLatin1(kDefaultRenderMode))
                    .toString(),
            &renderModeRepaired);
    lightingMode_ = LightingModeFromString(
            settings.value(QString::fromLatin1(kLightingModeKey),
                           QString::fromLatin1(kDefaultLightingMode))
                    .toString(),
            &lightingModeRepaired);
    msaaSamples_ = MsaaSamplesFromValue(
            settings.value(QString::fromLatin1(kMsaaSamplesKey),
                           kDefaultMsaaSamples),
            &msaaSamplesRepaired);
    textureFiltering_ = TextureFilteringFromString(
            settings.value(QString::fromLatin1(kTextureFilteringKey),
                           QString::fromLatin1(kDefaultTextureFiltering))
                    .toString(),
            &textureFilteringRepaired);
    worldShadows_ = WorldShadowsFromValue(
            settings.value(QString::fromLatin1(kWorldShadowsKey),
                           kDefaultWorldShadows),
            &worldShadowsRepaired);

    const bool repaired = renderModeRepaired || lightingModeRepaired ||
            msaaSamplesRepaired || textureFilteringRepaired ||
            worldShadowsRepaired;
    if (repaired) {
        Persist();
    }
}

void GraphicsSettings::Persist() const {
    Persist(QString::fromLatin1(kRenderModeKey), renderMode_);
    Persist(QString::fromLatin1(kLightingModeKey), lightingMode_);
    Persist(QString::fromLatin1(kMsaaSamplesKey), msaaSamples_);
    Persist(QString::fromLatin1(kTextureFilteringKey), textureFiltering_);
    Persist(QString::fromLatin1(kWorldShadowsKey), worldShadows_);
}

void GraphicsSettings::Persist(const QString &key, const QString &value) const {
    QSettings settings = OpenSettings(settingsFile_);
    settings.setValue(key, value);
    settings.sync();
}

void GraphicsSettings::Persist(const QString &key, int value) const {
    QSettings settings = OpenSettings(settingsFile_);
    settings.setValue(key, value);
    settings.sync();
}

void GraphicsSettings::Persist(const QString &key, bool value) const {
    QSettings settings = OpenSettings(settingsFile_);
    settings.setValue(key, value);
    settings.sync();
}

}  // namespace forevertas::viewer
