#include "ContentSecurity.h"

#include <QDir>
#include <QFileInfo>

namespace ContentSecurity {

QUrl externalUrl(const QString &target) {
    const QUrl url = QUrl::fromUserInput(target.trimmed());
    if (!url.isValid())
        return {};

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") &&
        scheme != QStringLiteral("https") &&
        scheme != QStringLiteral("mailto"))
        return {};
    if ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
        url.host().isEmpty())
        return {};
    return url;
}

QString resolveLocalImage(const QString &target, const QString &basePath,
                          const QString &vaultRoot) {
    if (target.isEmpty() || basePath.isEmpty() || vaultRoot.isEmpty())
        return {};

    const QString normalized = QDir::fromNativeSeparators(target);
    QString candidatePath;
    if (QDir::isAbsolutePath(normalized)) {
        candidatePath = normalized;
    } else {
        const QUrl url(target);
        if (url.isValid() && !url.scheme().isEmpty()) {
            if (!url.isLocalFile())
                return {};
            candidatePath = url.toLocalFile();
        } else {
            candidatePath = QDir(basePath).filePath(normalized);
        }
    }

    const QString root = QDir(vaultRoot).canonicalPath();
    const QFileInfo candidate(candidatePath);
    if (root.isEmpty() || !candidate.exists() || !candidate.isFile())
        return {};
    const QString canonical = candidate.canonicalFilePath();
    if (canonical.isEmpty())
        return {};

#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString rootPrefix =
        root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return canonical.startsWith(rootPrefix, cs) ? canonical : QString();
}

} // namespace ContentSecurity
