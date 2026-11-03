#include "GlobalPlaybackShortcut.hpp"

#include <KActionCollection>
#include <KGlobalAccel>

#include <QAction>
#include <QCoreApplication>
#include <QKeySequence>
#include <QPointer>

namespace wekde {
namespace {
constexpr auto ComponentName = "org.wekde.wallpaperengine.controls";
constexpr auto ActionName = "togglePlayback";

QPointer<GlobalPlaybackShortcut> activeOwner;

struct GlobalAction {
    KActionCollection* collection = nullptr;
    QAction* action = nullptr;
};

GlobalAction* globalAction() {
    static GlobalAction* state = nullptr;
    if (state) {
        return state;
    }
    QObject* const app = QCoreApplication::instance();
    if (!app) {
        return nullptr;
    }
    state = new GlobalAction;
    state->collection = new KActionCollection(app, QString::fromLatin1(ComponentName));
    state->collection->setComponentName(QString::fromLatin1(ComponentName));
    state->collection->setComponentDisplayName(QStringLiteral("Wallpaper Engine for KDE"));
    state->action = state->collection->addAction(QString::fromLatin1(ActionName));
    state->action->setText(QStringLiteral("Toggle Wallpaper Playback"));
    KGlobalAccel::self()->setDefaultShortcut(state->action, {}, KGlobalAccel::NoAutoloading);
    QObject::connect(state->action, &QAction::triggered, state->collection, [] {
        if (activeOwner) {
            emit activeOwner->toggleRequested();
        }
    });
    return state;
}

QKeySequence parseSequence(const QString& text) {
    QKeySequence result = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (!result.isEmpty() && (result.toString(QKeySequence::PortableText) == text
            || result.toString(QKeySequence::NativeText) == text)) {
        return result;
    }
    result = QKeySequence::fromString(text, QKeySequence::NativeText);
    if (!result.isEmpty() && result.toString(QKeySequence::NativeText) == text) {
        return result;
    }
    return {};
}
}

GlobalPlaybackShortcut::GlobalPlaybackShortcut(QObject* parent)
    : QObject(parent) {}

GlobalPlaybackShortcut::~GlobalPlaybackShortcut() {
    if (activeOwner != this) {
        return;
    }
    if (GlobalAction* state = globalAction()) {
        KGlobalAccel::self()->setShortcut(state->action, {}, KGlobalAccel::NoAutoloading);
        KGlobalAccel::self()->removeAllShortcuts(state->action);
    }
    activeOwner = nullptr;
}

QString GlobalPlaybackShortcut::shortcut() const { return m_shortcut; }
QString GlobalPlaybackShortcut::errorCode() const { return m_errorCode; }
QString GlobalPlaybackShortcut::message() const { return m_message; }

void GlobalPlaybackShortcut::writeShortcut(const QString& value) {
    setShortcut(value);
}

bool GlobalPlaybackShortcut::setShortcut(const QString& value) {
    const QString input = value.trimmed();
    if (input.isEmpty()) {
        if (activeOwner == this) {
            if (GlobalAction* state = globalAction()) {
                KGlobalAccel::self()->setShortcut(state->action, {}, KGlobalAccel::NoAutoloading);
                KGlobalAccel::self()->removeAllShortcuts(state->action);
            }
            activeOwner = nullptr;
        }
        if (!m_shortcut.isEmpty()) {
            m_shortcut.clear();
            emit shortcutChanged();
        }
        setStatus({}, {});
        return true;
    }

    const QKeySequence sequence = parseSequence(input);
    if (sequence.isEmpty()) {
        setStatus(QStringLiteral("invalidShortcut"),
            QStringLiteral("Choose a valid global key sequence"));
        return false;
    }
    const QString normalized = sequence.toString(QKeySequence::PortableText);
    if (activeOwner && activeOwner != this) {
        setStatus(QStringLiteral("shortcutAlreadyManaged"),
            QStringLiteral("Another wallpaper controls instance owns the global shortcut"));
        return false;
    }
    if (activeOwner == this && normalized == m_shortcut) {
        setStatus({}, {});
        return true;
    }

    GlobalAction* state = globalAction();
    if (!state || !state->action) {
        setStatus(QStringLiteral("globalShortcutsUnavailable"),
            QStringLiteral("KDE global shortcuts are unavailable"));
        return false;
    }
    if (!KGlobalAccel::isGlobalShortcutAvailable(sequence, QString::fromLatin1(ComponentName))) {
        setStatus(QStringLiteral("shortcutConflict"),
            QStringLiteral("That key sequence is already assigned to another action"));
        return false;
    }

    const QList<QKeySequence> previous = KGlobalAccel::self()->shortcut(state->action);
    if (!KGlobalAccel::self()->setShortcut(state->action, {sequence}, KGlobalAccel::NoAutoloading)
        || !KGlobalAccel::self()->shortcut(state->action).contains(sequence)) {
        if (!previous.isEmpty()) {
            KGlobalAccel::self()->setShortcut(state->action, previous, KGlobalAccel::NoAutoloading);
        } else {
            KGlobalAccel::self()->setShortcut(state->action, {}, KGlobalAccel::NoAutoloading);
            KGlobalAccel::self()->removeAllShortcuts(state->action);
        }
        setStatus(QStringLiteral("shortcutConflict"),
            QStringLiteral("KDE did not accept that global key sequence"));
        return false;
    }

    activeOwner = this;
    if (m_shortcut != normalized) {
        m_shortcut = normalized;
        emit shortcutChanged();
    }
    setStatus({}, {});
    return true;
}

void GlobalPlaybackShortcut::setStatus(const QString& code, const QString& text) {
    if (m_errorCode == code && m_message == text) {
        return;
    }
    m_errorCode = code;
    m_message = text;
    emit statusChanged();
}

} // namespace wekde
