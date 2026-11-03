#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QString>
#include <QVariantList>

class QScreen;
namespace Plasma {
class Corona;
}

namespace wekde
{

// Resolves the Qt primary screen to Plasma's screen id and exposes the
// panel-excluded global usable region from the owning Corona.
class PrimaryScreenMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *corona READ corona WRITE setCorona NOTIFY coronaChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(QString screenName READ screenName NOTIFY changed)
    Q_PROPERTY(QRect screenGeometry READ screenGeometry NOTIFY changed)
    Q_PROPERTY(QVariantList usableRegion READ usableRegion NOTIFY changed)

public:
    explicit PrimaryScreenMonitor(QObject *parent = nullptr);

    QObject *corona() const;
    void setCorona(QObject *corona);

    bool valid() const;
    QString screenName() const;
    QRect screenGeometry() const;
    QVariantList usableRegion() const;

    static QVariantList clipUsableRegion(const QRegion &region, const QRect &screenGeometry);

signals:
    void coronaChanged();
    void changed();

private:
    void reconnectScreenSignals();
    void reconnectCoronaSignals();
    void refresh();
    void setInvalid();

    QPointer<Plasma::Corona> m_corona;
    QPointer<QScreen> m_primaryScreen;
    QList<QMetaObject::Connection> m_screenConnections;
    QList<QMetaObject::Connection> m_coronaConnections;
    bool m_valid = false;
    uint m_screenId = 0;
    QString m_screenName;
    QRect m_screenGeometry;
    QVariantList m_usableRegion;
};

} // namespace wekde
