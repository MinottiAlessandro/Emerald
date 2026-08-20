#pragma once

#include <QString>
#include <QStringList>

// Vault-specific preferences stored in the platform's application settings,
// never inside the vault itself. Each canonical vault path gets an isolated
// QSettings group, so identically named folders and notes in different vaults
// cannot overwrite one another's configuration.
namespace VaultSettings {

QString value(const QString &vaultRoot, const QString &key,
              const QString &fallback = QString());
void setValue(const QString &vaultRoot, const QString &key,
              const QString &value);
QStringList stringListValue(const QString &vaultRoot, const QString &key,
                            const QStringList &fallback = {});
void setStringList(const QString &vaultRoot, const QString &key,
                   const QStringList &value);
void remove(const QString &vaultRoot, const QString &key);

// Older builds stored vault preferences globally. Move those values once into
// the last-opened vault's group, preserving any per-vault values that may
// already be present. The legacy singular spellLanguage key is normalized to
// the current spellLanguages list along the way.
void migrateLegacyForLastVault();

} // namespace VaultSettings
