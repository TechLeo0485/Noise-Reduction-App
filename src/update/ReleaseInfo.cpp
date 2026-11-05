#include "update/ReleaseInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace vs {

namespace {

QString withoutV(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        v.remove(0, 1);
    }
    return v;
}

void setError(QString* error, const QString& text) {
    if (error != nullptr) {
        *error = text;
    }
}

} // namespace

int compareVersions(const QString& a, const QString& b) {
    // Only the numeric core counts: "1.2.3-beta" compares like "1.2.3".
    const auto parts = [](const QString& v) {
        const QString core = withoutV(v).section(QLatin1Char('-'), 0, 0).section(QLatin1Char('+'), 0, 0);
        return core.split(QLatin1Char('.'));
    };
    const QStringList pa = parts(a);
    const QStringList pb = parts(b);
    for (qsizetype i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i].toInt() : 0;
        const int y = i < pb.size() ? pb[i].toInt() : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

bool parseRelease(const QByteArray& json, bool needInstaller, ReleaseInfo& out, QString* error) {
    out = {};
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        setError(error, QStringLiteral("GitHub sent an unexpected answer"));
        return false;
    }
    const QJsonObject r = doc.object();
    if (r.value(QLatin1String("draft")).toBool() || r.value(QLatin1String("prerelease")).toBool()) {
        setError(error, QStringLiteral("The release is not published yet"));
        return false;
    }
    static const QRegularExpression versionPattern(QStringLiteral("^\\d+(\\.\\d+){0,3}$"));
    out.version = withoutV(r.value(QLatin1String("tag_name")).toString());
    if (!versionPattern.match(out.version).hasMatch()) {
        setError(error, QStringLiteral("The release tag \"%1\" is not a version like v1.2.3")
                            .arg(r.value(QLatin1String("tag_name")).toString()));
        return false;
    }
    out.notes = r.value(QLatin1String("body")).toString().trimmed();
    out.pageUrl = r.value(QLatin1String("html_url")).toString();

    static const QRegularExpression assetPattern(QStringLiteral("^VoiceStudio-[0-9.]+-setup\\.exe$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    for (const QJsonValue& v : r.value(QLatin1String("assets")).toArray()) {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value(QLatin1String("name")).toString();
        if (!assetPattern.match(name).hasMatch()) {
            continue;
        }
        out.assetName = name;
        out.assetUrl = asset.value(QLatin1String("browser_download_url")).toString();
        out.assetSize = static_cast<qint64>(asset.value(QLatin1String("size")).toDouble());
        const QString digest = asset.value(QLatin1String("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:"), Qt::CaseInsensitive)) {
            out.sha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
        }
        break;
    }
    if (needInstaller) {
        if (out.assetUrl.isEmpty() || !out.assetUrl.startsWith(QLatin1String("https://"))) {
            setError(error, QStringLiteral("The release has no VoiceStudio-%1-setup.exe attached").arg(out.version));
            return false;
        }
        if (out.sha256.size() != 32) {
            setError(error, QStringLiteral("GitHub gave no checksum for the installer, so it cannot be verified"));
            return false;
        }
    }
    return true;
}

} // namespace vs
