#include "StandaloneFile.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>

namespace StandaloneFile {
namespace {

void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}

QByteArray preferredLineEnding(const QByteArray &bytes) {
    qsizetype crlf = 0;
    qsizetype lf = 0;
    qsizetype cr = 0;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        if (bytes.at(i) == '\r') {
            if (i + 1 < bytes.size() && bytes.at(i + 1) == '\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (bytes.at(i) == '\n') {
            ++lf;
        }
    }
    if (crlf >= lf && crlf >= cr && crlf > 0)
        return QByteArrayLiteral("\r\n");
    if (cr > lf && cr > 0)
        return QByteArrayLiteral("\r");
    return QByteArrayLiteral("\n");
}

} // namespace

bool hasMarkdownExtension(const QString &path) {
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0 ||
           suffix.compare(QStringLiteral("markdown"), Qt::CaseInsensitive) == 0;
}

bool load(const QString &path, Document *document, QString *error) {
    if (!document) {
        setError(error, QStringLiteral("No destination was provided"));
        return false;
    }

    const QFileInfo info(path);
    if (!hasMarkdownExtension(path)) {
        setError(error, QStringLiteral("Only .md and .markdown files are supported"));
        return false;
    }
    if (!info.exists() || !info.isFile()) {
        setError(error, QStringLiteral("The Markdown file does not exist"));
        return false;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return false;
    }
    QByteArray bytes = file.readAll();
    constexpr char utf8Bom[] = {'\xEF', '\xBB', '\xBF'};
    const bool hasBom = bytes.startsWith(QByteArrayView(utf8Bom, 3));
    if (hasBom)
        bytes.remove(0, 3);

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString content = decoder.decode(bytes);
    if (decoder.hasError()) {
        setError(error,
                 QStringLiteral("The file is not valid UTF-8 and was left unchanged"));
        return false;
    }
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty() && !hasMarkdownExtension(canonical)) {
        setError(error,
                 QStringLiteral("The linked file is not a Markdown document"));
        return false;
    }
    document->path = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    document->content = std::move(content);
    document->lineEnding = preferredLineEnding(bytes);
    document->utf8Bom = hasBom;
    if (error)
        error->clear();
    return true;
}

bool save(const Document &document, const QString &content, QString *error) {
    if (document.path.isEmpty()) {
        setError(error, QStringLiteral("The standalone file has no path"));
        return false;
    }

    QString normalized = content;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QByteArray bytes = normalized.toUtf8();
    if (document.lineEnding != QByteArrayLiteral("\n"))
        bytes.replace(QByteArrayLiteral("\n"), document.lineEnding);
    if (document.utf8Bom)
        bytes.prepend(QByteArray::fromHex("EFBBBF"));

    const QFileDevice::Permissions permissions = QFile::permissions(document.path);
    QSaveFile file(document.path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (permissions != QFileDevice::Permissions())
        file.setPermissions(permissions);
    if (file.write(bytes) != bytes.size()) {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    if (error)
        error->clear();
    return true;
}

} // namespace StandaloneFile
