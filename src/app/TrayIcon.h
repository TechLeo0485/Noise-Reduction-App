#pragma once

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class QQuickWindow;

namespace vs {

class EngineController;

// Notification-area icon: VoiceStudio keeps processing audio after its window is closed.
// Left click opens the window; the menu offers mute, strict voice isolation and quit.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    TrayIcon(EngineController& controller, QQuickWindow& window, QObject* parent = nullptr);

    [[nodiscard]] bool available() const { return available_; }
    void showWindow();
    // Shows the global shortcuts next to the menu entries ("" = none).
    void setShortcutHints(const QString& open, const QString& mute);

private:
    void refresh();
    void notifyHidden();

    EngineController& controller_;
    QQuickWindow& window_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* open_ = nullptr;
    QAction* mute_ = nullptr;
    QAction* isolation_ = nullptr;
    QAction* quit_ = nullptr;
    bool available_ = false;
    bool notified_ = false;
};

} // namespace vs
