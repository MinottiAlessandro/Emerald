#include "LegacyMascotMigration.h"

#include "MascotSeed.h"
#include "Vault.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace LegacyMascotMigration {

namespace {
constexpr qint64 kMaxStoreBytes = 4 * 1024 * 1024;

QString readUtf8(QFile &file) {
    QString content = QString::fromUtf8(file.readAll());
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return content;
}
} // namespace

Result run(Vault &vault) {
    Result result;
    const QFileInfo requestedStoreDir(
        QDir(vault.root()).filePath(QStringLiteral(".emerald")));
    if (requestedStoreDir.isSymbolicLink())
        return result;
    const QString requestedStore = QDir(vault.root()).filePath(
        QStringLiteral(".emerald/mascots.json"));
    if (QFileInfo(requestedStore).isSymbolicLink())
        return result;
    const QString jsonPath = vault.resolveExistingFileWithinRoot(
        QStringLiteral(".emerald/mascots.json"));
    if (jsonPath.isEmpty())
        return result;
    result.storeFound = true;

    QFile store(jsonPath);
    if (store.size() > kMaxStoreBytes || !store.open(QIODevice::ReadOnly))
        return result;
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(store.readAll(), &parseError);
    store.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return result;

    const QJsonValue mascotsValue =
        document.object().value(QStringLiteral("mascots"));
    if (!mascotsValue.isObject())
        return result;

    const QJsonObject mascots = mascotsValue.toObject();
    for (auto it = mascots.constBegin(); it != mascots.constEnd(); ++it) {
        bool seedOk = false;
        const quint64 seed =
            it.value().toObject().value(QStringLiteral("seed"))
                .toString().toULongLong(&seedOk);
        const QString requestedPath = QDir::fromNativeSeparators(it.key());
        if (!seedOk || seed == 0 ||
            QFileInfo(requestedPath).suffix().compare(
                QStringLiteral("md"), Qt::CaseInsensitive) != 0) {
            ++result.skippedEntries;
            continue;
        }

        const QString notePath =
            vault.resolveExistingFileWithinRoot(requestedPath);
        if (notePath.isEmpty()) {
            ++result.skippedEntries;
            continue;
        }

        QFile note(notePath);
        if (!note.open(QIODevice::ReadOnly)) {
            ++result.skippedEntries;
            continue;
        }
        const QString content = readUtf8(note);
        note.close();
        const int nl = content.indexOf(QLatin1Char('\n'));
        const QString first = nl < 0 ? content : content.left(nl);
        if (MascotSeed::fromLine(first) != 0)
            continue;

        if (vault.write(notePath,
                        MascotSeed::line(seed) + QLatin1Char('\n') + content))
            ++result.migratedEntries;
        else
            ++result.skippedEntries;
    }

    result.storeRemoved = QFile::remove(jsonPath);
    if (result.storeRemoved)
        QDir().rmdir(QFileInfo(jsonPath).absolutePath()); // only when now empty
    return result;
}

} // namespace LegacyMascotMigration
