#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QQuickWindow;
class QScreen;

namespace wekde
{

class WallpaperRuntimeRegistration : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem* targetItem READ targetItem WRITE setTargetItem NOTIFY targetItemChanged)
    Q_PROPERTY(QVariantMap containmentContext READ containmentContext WRITE setContainmentContext
                   NOTIFY containmentContextChanged)
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registrationChanged)
    Q_PROPERTY(QString instanceId READ instanceId NOTIFY registrationChanged)
    Q_PROPERTY(QString outputName READ outputName NOTIFY registrationChanged)
    Q_PROPERTY(QString activityId READ activityId NOTIFY registrationChanged)
    Q_PROPERTY(QString containmentId READ containmentId NOTIFY registrationChanged)

public:
    explicit WallpaperRuntimeRegistration(QObject* parent = nullptr);
    ~WallpaperRuntimeRegistration() override = default;

    QQuickItem* targetItem() const;
    void        setTargetItem(QQuickItem* item);

    QVariantMap containmentContext() const;
    void        setContainmentContext(const QVariantMap& context);

    bool    isRegistered() const;
    QString instanceId() const;
    QString outputName() const;
    QString activityId() const;
    QString containmentId() const;

    Q_INVOKABLE void refresh();

    static QString uniqueOutputName(const QString& screenName, const QStringList& screenNames);

signals:
    void targetItemChanged();
    void containmentContextChanged();
    void registrationChanged();
    void registrationRejected(const QString& reason);

protected:
    struct OutputIdentity {
        QPointer<QQuickWindow> window;
        QPointer<QScreen>      screen;
        QString                name;
        QStringList            availableNames;
        QRectF                 geometry;
    };

    virtual OutputIdentity currentOutput() const;

private:
    void handleWindowChanged(QQuickWindow* window);
    void handleScreenChanged(QScreen* screen);
    void invalidate(const QString& reason = {});
    bool contextIsEligible(const OutputIdentity& output, QString* reason) const;
    bool outputIsCurrent(const OutputIdentity& output, const QString& connector) const;

    QPointer<QQuickItem>   m_targetItem;
    QPointer<QQuickWindow> m_window;
    QPointer<QScreen>      m_screen;
    QVariantMap            m_containmentContext;
    QString                m_instanceId;
    QString                m_outputName;
    QString                m_activityId;
    QString                m_containmentId;
    bool                   m_registered = false;
};

} // namespace wekde
