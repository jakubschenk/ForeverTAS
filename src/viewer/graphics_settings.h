#ifndef FOREVERTAS_VIEWER_GRAPHICS_SETTINGS_H
#define FOREVERTAS_VIEWER_GRAPHICS_SETTINGS_H

#include <QObject>
#include <QString>
#include <QVariant>

namespace forevertas::viewer {

class GraphicsSettings final : public QObject {
    Q_OBJECT

    Q_PROPERTY(
            QString renderMode READ renderMode WRITE setRenderMode NOTIFY
                    renderModeChanged)
    Q_PROPERTY(QString lightingMode READ lightingMode WRITE
                       setLightingMode NOTIFY lightingModeChanged)
    Q_PROPERTY(int msaaSamples READ msaaSamples WRITE setMsaaSamples NOTIFY
                       msaaSamplesChanged)
    Q_PROPERTY(QString textureFiltering READ textureFiltering WRITE
                       setTextureFiltering NOTIFY textureFilteringChanged)
    Q_PROPERTY(bool worldShadows READ worldShadows WRITE setWorldShadows NOTIFY
                       worldShadowsChanged)
    Q_PROPERTY(bool vehicleContactShadows READ vehicleContactShadows WRITE
                       setVehicleContactShadows NOTIFY
                               vehicleContactShadowsChanged)
    Q_PROPERTY(bool skidmarksEnabled READ skidmarksEnabled WRITE
                       setSkidmarksEnabled NOTIFY skidmarksEnabledChanged)

public:
    explicit GraphicsSettings(QObject *parent = nullptr);
    explicit GraphicsSettings(const QString &settingsFile, QObject *parent = nullptr);

    QString renderMode() const;
    QString lightingMode() const;
    int msaaSamples() const;
    QString textureFiltering() const;
    bool worldShadows() const;
    bool vehicleContactShadows() const;
    bool skidmarksEnabled() const;

    void setRenderMode(const QString &value);
    void setLightingMode(const QString &value);
    void setMsaaSamples(int value);
    void setTextureFiltering(const QString &value);
    void setWorldShadows(bool value);
    void setVehicleContactShadows(bool value);
    void setSkidmarksEnabled(bool value);

signals:
    void renderModeChanged();
    void lightingModeChanged();
    void msaaSamplesChanged();
    void textureFilteringChanged();
    void worldShadowsChanged();
    void vehicleContactShadowsChanged();
    void skidmarksEnabledChanged();

private:
    static QString RenderModeFromString(const QString &value, bool *repaired);
    static QString LightingModeFromString(const QString &value, bool *repaired);
    static int MsaaSamplesFromValue(const QVariant &value, bool *repaired);
    static QString TextureFilteringFromString(const QString &value,
                                              bool *repaired);
    static bool WorldShadowsFromValue(const QVariant &value,
                                      bool *repaired);
    static bool VehicleContactShadowsFromValue(const QVariant &value,
                                               bool *repaired);
    static bool SkidmarksEnabledFromValue(const QVariant &value,
                                          bool *repaired);
    void Load();
    void Persist() const;
    void Persist(const QString &key, const QString &value) const;
    void Persist(const QString &key, int value) const;
    void Persist(const QString &key, bool value) const;

    QString settingsFile_;
    QString renderMode_ = QStringLiteral("textured");
    QString lightingMode_ = QStringLiteral("authored");
    int msaaSamples_ = 2;
    QString textureFiltering_ = QStringLiteral("trilinear");
    bool worldShadows_ = false;
    bool vehicleContactShadows_ = true;
    bool skidmarksEnabled_ = true;
};

}  // namespace forevertas::viewer

#endif
