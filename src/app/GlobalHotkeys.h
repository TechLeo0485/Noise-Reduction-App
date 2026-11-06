#pragma once

#include "core/Hotkey.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <array>
#include <functional>

class QQuickWindow;

namespace vs {

class EngineController;

// System-wide keyboard shortcuts (RegisterHotKey): show / hide the window, mute / unmute the microphone, switch
// to the next voice and turn the voice effects off / on. They work
// while another app has the focus and while the window is hidden in the tray. The keys are stored in the
// settings (EngineController::hotkey()); the Setup page changes them by capturing the next key press.
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    // [{action, label, text, defaultText, error}]; error: why the shortcut is not active ("" if it is).
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    // The action whose keys are being captured, or "".
    Q_PROPERTY(QString capturing READ capturing NOTIFY changed)
    // Feedback for the capture in progress (e.g. why the keys just pressed cannot be used).
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    explicit GlobalHotkeys(EngineController& controller, QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    // Registers the shortcuts on `window` (which receives WM_HOTKEY). `toggleWindow` shows or hides it.
    void attach(QQuickWindow& window, std::function<void()> toggleWindow);

    [[nodiscard]] QVariantList items() const;
    [[nodiscard]] QString capturing() const { return capturing_; }
    [[nodiscard]] QString message() const { return message_; }
    // The shortcut text of an action ("" = off), e.g. for the tray menu.
    [[nodiscard]] QString text(const QString& action) const;

    // Starts capturing: the next key combination pressed in the window becomes the action's shortcut
    // (Esc cancels). The shortcuts are unregistered meanwhile, so the current keys can be pressed again.
    Q_INVOKABLE void capture(const QString& action);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void clear(const QString& action);
    Q_INVOKABLE void restoreDefault(const QString& action);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Action {
        const char* key;
        const char* label;
        const char* defaultText;
        bool registered = false;
        QString error;
    };

    [[nodiscard]] int indexOf(const QString& action) const;
    void registerAll();
    void unregisterAll();
    void endCapture();
    void trigger(int index);

    EngineController& controller_;
    QQuickWindow* window_ = nullptr;
    void* hwnd_ = nullptr;
    std::function<void()> toggleWindow_;
    std::array<Action, 4> actions_;
    QString capturing_;
    QString message_;
};

} // namespace vs
