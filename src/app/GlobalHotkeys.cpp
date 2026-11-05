#include "app/GlobalHotkeys.h"

#include "app/EngineController.h"
#include "diagnostics/Logger.h"
#include "settings/SettingsStore.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QVariantMap>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vs {

namespace {

constexpr int kFirstId = 0x5653; // RegisterHotKey ids, per window

bool isModifierKey(unsigned vk) {
    switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
        return true;
    default:
        return false;
    }
}

unsigned modifiersOf(Qt::KeyboardModifiers m) {
    unsigned bits = 0;
    if (m & Qt::ControlModifier) bits |= Hotkey::kCtrl;
    if (m & Qt::AltModifier) bits |= Hotkey::kAlt;
    if (m & Qt::ShiftModifier) bits |= Hotkey::kShift;
    if (m & Qt::MetaModifier) bits |= Hotkey::kWin;
    return bits;
}

} // namespace

GlobalHotkeys::GlobalHotkeys(EngineController& controller, QObject* parent)
    : QObject(parent),
      controller_(controller),
      actions_{{{"showWindow", "Show or hide VoiceStudio", AppSettings::kDefaultHotkeyShowWindow},
                {"mute", "Mute or unmute the microphone", AppSettings::kDefaultHotkeyMute}}} {
    connect(&controller_, &EngineController::hotkeysChanged, this, [this] {
        if (capturing_.isEmpty()) {
            registerAll();
        }
    });
}

GlobalHotkeys::~GlobalHotkeys() {
    unregisterAll();
    if (window_ != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

void GlobalHotkeys::attach(QQuickWindow& window, std::function<void()> toggleWindow) {
    window_ = &window;
    hwnd_ = reinterpret_cast<void*>(window.winId());
    toggleWindow_ = std::move(toggleWindow);
    window.installEventFilter(this);
    QCoreApplication::instance()->installNativeEventFilter(this);
    registerAll();
}

int GlobalHotkeys::indexOf(const QString& action) const {
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        if (action == QLatin1String(actions_[i].key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVariantList GlobalHotkeys::items() const {
    QVariantList list;
    for (const Action& a : actions_) {
        QVariantMap m;
        m[QStringLiteral("action")] = QLatin1String(a.key);
        m[QStringLiteral("label")] = QLatin1String(a.label);
        m[QStringLiteral("text")] = controller_.hotkey(QLatin1String(a.key));
        m[QStringLiteral("defaultText")] = QLatin1String(a.defaultText);
        m[QStringLiteral("error")] = a.error;
        list.append(m);
    }
    return list;
}

QString GlobalHotkeys::text(const QString& action) const {
    const int i = indexOf(action);
    return i >= 0 && actions_[static_cast<std::size_t>(i)].registered ? controller_.hotkey(action) : QString();
}

void GlobalHotkeys::unregisterAll() {
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        if (actions_[i].registered) {
            UnregisterHotKey(static_cast<HWND>(hwnd_), kFirstId + static_cast<int>(i));
            actions_[i].registered = false;
        }
    }
}

void GlobalHotkeys::registerAll() {
    unregisterAll();
    if (hwnd_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        Action& a = actions_[i];
        a.error.clear();
        const QString text = controller_.hotkey(QLatin1String(a.key));
        Hotkey h;
        if (!parseHotkey(text.toStdString(), h) || !hotkeyProblem(h).empty()) {
            a.error = QStringLiteral("These keys cannot be used");
            continue;
        }
        if (h.empty()) {
            continue; // off
        }
        if (RegisterHotKey(static_cast<HWND>(hwnd_), kFirstId + static_cast<int>(i), h.modifiers | MOD_NOREPEAT, h.vk)) {
            a.registered = true;
        } else {
            a.error = QStringLiteral("Not active: another app already uses %1. Choose other keys.").arg(text);
            logWarning(std::string("Shortcut ") + text.toStdString() + " for \"" + a.label +
                       "\" is used by another app (error " + std::to_string(GetLastError()) + ")");
        }
    }
    emit changed();
}

void GlobalHotkeys::capture(const QString& action) {
    if (indexOf(action) < 0 || window_ == nullptr) {
        return;
    }
    unregisterAll();
    capturing_ = action;
    message_ = QStringLiteral("Press the new keys, e.g. Ctrl+Alt+M. Esc cancels.");
    emit changed();
}

void GlobalHotkeys::cancelCapture() {
    if (!capturing_.isEmpty()) {
        endCapture();
    }
}

void GlobalHotkeys::endCapture() {
    capturing_.clear();
    message_.clear();
    registerAll(); // emits changed()
}

void GlobalHotkeys::clear(const QString& action) {
    if (indexOf(action) >= 0) {
        controller_.setHotkey(action, QString());
        registerAll();
    }
}

void GlobalHotkeys::restoreDefault(const QString& action) {
    const int i = indexOf(action);
    if (i < 0) {
        return;
    }
    const QString fallback = QLatin1String(actions_[static_cast<std::size_t>(i)].defaultText);
    // The other action may have taken these keys meanwhile.
    for (const Action& other : actions_) {
        if (QLatin1String(other.key) != action && controller_.hotkey(QLatin1String(other.key)) == fallback) {
            controller_.setHotkey(QLatin1String(other.key), QString());
        }
    }
    controller_.setHotkey(action, fallback);
    registerAll();
}

bool GlobalHotkeys::eventFilter(QObject* watched, QEvent* event) {
    if (watched != window_ || capturing_.isEmpty()) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::WindowDeactivate:
        endCapture();
        return false;
    case QEvent::ShortcutOverride:
    case QEvent::KeyRelease:
        return true; // the keys belong to the capture, not to the page
    case QEvent::KeyPress: {
        const auto* key = static_cast<QKeyEvent*>(event);
        const auto vk = static_cast<unsigned>(key->nativeVirtualKey());
        const unsigned modifiers = modifiersOf(key->modifiers());
        if (key->isAutoRepeat() || isModifierKey(vk)) {
            return true;
        }
        if (vk == VK_ESCAPE && modifiers == 0) {
            endCapture();
            return true;
        }
        const Hotkey h{modifiers, vk};
        const std::string problem = hotkeyProblem(h);
        const QString text = QString::fromStdString(formatHotkey(h));
        if (!problem.empty()) {
            message_ = QString::fromStdString(problem) + QStringLiteral(". Esc cancels.");
            emit changed();
            return true;
        }
        for (const Action& other : actions_) {
            if (QLatin1String(other.key) != capturing_ && controller_.hotkey(QLatin1String(other.key)) == text) {
                message_ = QStringLiteral("%1 is already used for \"%2\". Press other keys, or Esc.")
                               .arg(text, QLatin1String(other.label));
                emit changed();
                return true;
            }
        }
        controller_.setHotkey(capturing_, text);
        endCapture();
        return true;
    }
    default:
        return QObject::eventFilter(watched, event);
    }
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/) {
    if (eventType != "windows_generic_MSG") {
        return false;
    }
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY || msg->hwnd != static_cast<HWND>(hwnd_)) {
        return false;
    }
    const auto index = static_cast<int>(msg->wParam) - kFirstId;
    if (index < 0 || index >= static_cast<int>(actions_.size())) {
        return false;
    }
    trigger(index);
    return true;
}

void GlobalHotkeys::trigger(int index) {
    const std::string action = actions_[static_cast<std::size_t>(index)].key;
    if (action == "showWindow") {
        if (toggleWindow_) {
            toggleWindow_();
        }
    } else if (action == "mute") {
        const bool muted = controller_.params().value(QStringLiteral("masterMute")).toBool();
        controller_.setParam(QStringLiteral("masterMute"), !muted);
        logInfo(muted ? "Microphone unmuted (shortcut)" : "Microphone muted (shortcut)");
    }
}

} // namespace vs
