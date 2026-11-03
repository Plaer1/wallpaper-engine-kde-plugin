#pragma once

#include <QObject>
#include <QString>

namespace wekde {

class GlobalPlaybackShortcut : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString shortcut READ shortcut WRITE writeShortcut NOTIFY shortcutChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY statusChanged)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged)

public:
    explicit GlobalPlaybackShortcut(QObject* parent = nullptr);
    ~GlobalPlaybackShortcut() override;

    QString shortcut() const;
    QString errorCode() const;
    QString message() const;

    Q_INVOKABLE bool setShortcut(const QString& shortcut);

signals:
    void shortcutChanged();
    void statusChanged();
    void toggleRequested();

private:
    void writeShortcut(const QString& shortcut);
    void setStatus(const QString& errorCode, const QString& message);

    QString m_shortcut;
    QString m_errorCode;
    QString m_message;
};

} // namespace wekde
