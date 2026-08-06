#pragma once

#include <QString>

// Vault-specific preferences stored in the platform's application settings,
// never inside the vault itself. Each canonical vault path gets an isolated
// QSettings group, so identically named folders and notes in different vaults
// cannot overwrite one another's configuration.
namespace VaultSettings {

QString value(const QString &vaultRoot, const QString &key,
              const QString &fallback = QString());
void setValue(const QString &vaultRoot, const QString &key,
              const QString &value);
void remove(const QString &vaultRoot, const QString &key);

// Emerald <= 1.6.1 stored homeNote, newNoteFolder, and templatesFolder as
// global keys. Move those values once into the last-opened vault's group,
// preserving any per-vault values that may already be present.
void migrateLegacyForLastVault();

} // namespace VaultSettings
