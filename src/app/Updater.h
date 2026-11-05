#pragma once

#include "update/ReleaseInfo.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkReply;

namespace vs {

class EngineController;

// Updates from the GitHub releases (see ReleaseInfo.h).
//  - Some seconds after start (if enabled) it asks GitHub for the latest release; a newer one is offered in a
//    dialog: update now, later, or skip this version.
//  - "Update now" downloads the installer, checks its size and SHA-256 against what GitHub reports, and runs
//    it silently (Windows asks for administrator rights). Setup closes VoiceStudio, installs, and starts it again.
//  - The first start of a newer version shows what changed (the release notes).
class Updater : public QObject {
    Q_OBJECT
    // idle | checking | upToDate | available | downloading | installing | error
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)            // of the available release (Markdown)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)       // download, 0..1
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool promptVisible READ promptVisible NOTIFY changed)
    Q_PROPERTY(bool checkAtStart READ checkAtStart WRITE setCheckAtStart NOTIFY changed)
    // "What's new" after an update
    Q_PROPERTY(bool whatsNewVisible READ whatsNewVisible NOTIFY changed)
    Q_PROPERTY(QString whatsNewNotes READ whatsNewNotes NOTIFY changed)
    Q_PROPERTY(QString whatsNewUrl READ whatsNewUrl NOTIFY changed)

public:
    explicit Updater(EngineController& controller, QObject* parent = nullptr);
    ~Updater() override;

    // At application start: "what's new" if this version is new here, then the delayed update check.
    void start();

    [[nodiscard]] QString state() const { return state_; }
    [[nodiscard]] QString currentVersion() const;
    [[nodiscard]] QString latestVersion() const { return latest_.version; }
    [[nodiscard]] QString notes() const { return latest_.notes; }
    [[nodiscard]] QString releaseUrl() const { return latest_.pageUrl; }
    [[nodiscard]] double progress() const { return progress_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] bool promptVisible() const { return promptVisible_; }
    [[nodiscard]] bool checkAtStart() const;
    void setCheckAtStart(bool enabled);
    [[nodiscard]] bool whatsNewVisible() const { return whatsNewVisible_; }
    [[nodiscard]] QString whatsNewNotes() const { return whatsNewNotes_; }
    [[nodiscard]] QString whatsNewUrl() const { return whatsNewUrl_; }

    Q_INVOKABLE void checkNow();      // also shows a skipped version again
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void later();         // closes the dialog; Setup still offers the update
    Q_INVOKABLE void skipVersion();   // no automatic offer for this version
    Q_INVOKABLE void showPrompt();
    Q_INVOKABLE void closeWhatsNew();

signals:
    void changed();
    // A newer version was found by the automatic check (the tray tells the user while the window is hidden).
    void updateFound(const QString& version);

private:
    void check(bool manual);
    void fetchWhatsNew(const QString& version);
    void downloadFinished();
    void runInstaller(const QString& path);
    void setState(const QString& state, const QString& error = {});
    [[nodiscard]] QNetworkReply* get(const QString& url, int timeoutMs);

    EngineController& controller_;
    QNetworkAccessManager network_;
    QString state_ = QStringLiteral("idle");
    ReleaseInfo latest_;
    double progress_ = 0.0;
    QString error_;
    bool promptVisible_ = false;
    bool whatsNewVisible_ = false;
    QString whatsNewNotes_;
    QString whatsNewUrl_;
    QPointer<QNetworkReply> download_;
    QFile file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
};

} // namespace vs
