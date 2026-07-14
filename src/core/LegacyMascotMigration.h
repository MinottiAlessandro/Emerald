#pragma once

class Vault;

namespace LegacyMascotMigration {

struct Result {
    bool storeFound = false;
    bool storeRemoved = false;
    int migratedEntries = 0;
    int skippedEntries = 0;
};

// Fold the legacy .emerald/mascots.json seed store into note header lines.
// Every metadata and note path is resolved canonically within the vault before
// it is read, written, or removed. A malformed store is left untouched.
Result run(Vault &vault);

} // namespace LegacyMascotMigration
