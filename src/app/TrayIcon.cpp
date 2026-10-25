#include "app/TrayIcon.h"

#include "app/EngineController.h"

#include <QApplication>
#include <QQuickWindow>

namespace vs {

TrayIcon::TrayIcon(EngineController& controller, QQuickWindow& window, QObject* parent)
    : QObject(parent), controller_(controller), window_(window), tray_(QApplication::windowIcon()) {
    available_ = QSystemTrayIcon::isSystemTrayAvailable();
    if (!available_) {
        return;
    }

    open_ = menu_.addAction(QStringLiteral("Open VoiceStudio"));
    QFont bold = open_->font();
    bold.setBold(true);
    open_->setFont(bold);
    menu_.addSeparator();
    mute_ = menu_.addAction(QStringLiteral("Mute microphone"));
    mute_->setCheckable(true);
    isolation_ = menu_.addAction(QStringLiteral("Strict voice isolation"));
    isolation_->setCheckable(true);
    menu_.addSeparator();
    quit_ = menu_.addAction(QStringLiteral("Quit VoiceStudio"));

    connect(open_, &QAction::triggered, this, &TrayIcon::showWindow);
    connect(mute_, &QAction::triggered, this,
            [this](bool checked) { controller_.setParam(QStringLiteral("masterMute"), checked); });
    connect(isolation_, &QAction::triggered, this,
            [this](bool checked) { controller_.setParam(QStringLiteral("isolationEnabled"), checked); });
    connect(quit_, &QAction::triggered, qApp, &QCoreApplication::quit);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showWindow();
        }
    });
    connect(&tray_, &QSystemTrayIcon::messageClicked, this, &TrayIcon::showWindow);

    connect(&controller_, &EngineController::paramsChanged, this, &TrayIcon::refresh);
    connect(&controller_, &EngineController::isolationChanged, this, &TrayIcon::refresh);
    connect(&controller_, &EngineController::statusChanged, this, &TrayIcon::refresh);
    connect(&controller_, &EngineController::hiddenToTray, this, &TrayIcon::notifyHidden);

    tray_.setContextMenu(&menu_);
    refresh();
    tray_.show();
}

void TrayIcon::showWindow() {
    if (window_.windowState() & Qt::WindowMinimized) {
        window_.showNormal();
    } else {
        window_.show();
    }
    window_.raise();
    window_.requestActivate();
}

void TrayIcon::refresh() {
    if (!available_) {
        return;
    }
    const QVariantMap params = controller_.params();
    const QVariantMap iso = controller_.isolation();
    const bool muted = params.value(QStringLiteral("masterMute")).toBool();
    const bool isolating = params.value(QStringLiteral("isolationEnabled")).toBool();
    mute_->setChecked(muted);
    isolation_->setChecked(isolating);
    isolation_->setEnabled(isolating || iso.value(QStringLiteral("available")).toBool());

    QString state;
    if (!controller_.engineEnabled()) {
        state = QStringLiteral("Audio engine is off");
    } else if (!controller_.engineRunning()) {
        state = QStringLiteral("Waiting for the microphone...");
    } else if (muted) {
        state = QStringLiteral("Microphone muted");
    } else if (isolating) {
        state = iso.value(QStringLiteral("silencing")).toBool() ? QStringLiteral("Strict isolation: microphone silenced")
                                                                : QStringLiteral("Strict voice isolation on");
    } else {
        state = QStringLiteral("Processing your voice");
    }
    tray_.setToolTip(QStringLiteral("VoiceStudio - %1").arg(state));
}

void TrayIcon::notifyHidden() {
    if (!available_ || notified_) {
        return;
    }
    notified_ = true; // once per run
    tray_.showMessage(QStringLiteral("VoiceStudio is still running"),
                      QStringLiteral("Your voice keeps being processed. Click the tray icon to open the window, "
                                     "or right-click it to quit."),
                      QApplication::windowIcon(), 5000);
}

} // namespace vs
