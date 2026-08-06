#include "VaultSettings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QSettings>
#include <QStringList>
#include <QVariant>

namespace VaultSettings {
namespace {

QString normalizedRoot(const QString &vaultRoot) {
    if (vaultRoot.isEmpty())
        return {};
    const QDir dir(vaultRoot);
    QString root = dir.canonicalPath();
    if (root.isEmpty())
        root = dir.absolutePath();
    root = QDir::cleanPath(root);
#if defined(Q_OS_WIN)
    root = root.toCaseFolded();
#endif
    return root;
}

QString groupForRoot(const QString &root) {
    const QByteArray digest = QCryptographicHash::hash(
        root.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("vaults/") + QString::fromLatin1(digest);
}

const QStringList &legacyKeys() {
    static const QStringList keys{
        QStringLiteral("homeNote"), QStringLiteral("newNoteFolder"),
        QStringLiteral("templatesFolder")};
    return keys;
}

} // namespace

QString value(const QString &vaultRoot, const QString &key,
              const QString &fallback) {
    const QString root = normalizedRoot(vaultRoot);
    if (root.isEmpty() || key.isEmpty())
        return fallback;
    QSettings settings;
    settings.beginGroup(groupForRoot(root));
    return settings.value(key, fallback).toString();
}

void setValue(const QString &vaultRoot, const QString &key,
              const QString &value) {
    const QString root = normalizedRoot(vaultRoot);
    if (root.isEmpty() || key.isEmpty())
        return;
    QSettings settings;
    settings.beginGroup(groupForRoot(root));
    // Keep the source path beside the opaque collision-resistant group id so
    // the native/INI settings remain understandable when inspected by hand.
    settings.setValue(QStringLiteral("rootPath"), root);
    settings.setValue(key, value);
}

void remove(const QString &vaultRoot, const QString &key) {
    const QString root = normalizedRoot(vaultRoot);
    if (root.isEmpty() || key.isEmpty())
        return;
    QSettings settings;
    settings.beginGroup(groupForRoot(root));
    settings.remove(key);
}

void migrateLegacyForLastVault() {
    QSettings settings;
    const QString lastVault =
        settings.value(QStringLiteral("lastVault")).toString();
    const QString root = normalizedRoot(lastVault);
    if (root.isEmpty())
        return;

    QHash<QString, QVariant> legacy;
    for (const QString &key : legacyKeys())
        if (settings.contains(key))
            legacy.insert(key, settings.value(key));
    if (legacy.isEmpty())
        return;

    settings.beginGroup(groupForRoot(root));
    settings.setValue(QStringLiteral("rootPath"), root);
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it)
        if (!settings.contains(it.key()))
            settings.setValue(it.key(), it.value());
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return;

    // Remove the unscoped values only after the per-vault copy is complete.
    for (const QString &key : legacyKeys())
        settings.remove(key);
}

} // namespace VaultSettings
