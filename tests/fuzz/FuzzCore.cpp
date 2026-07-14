#include "core/ContentSecurity.h"
#include "core/MascotSeed.h"
#include "core/Perf.h"
#include "core/Vault.h"
#include "core/WikiLink.h"

#include <QLoggingCategory>
#include <QRegularExpressionMatchIterator>

#include <cstddef>
#include <cstdint>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf")

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
    constexpr std::size_t maxInputSize = 64 * 1024;
    if (size > maxInputSize)
        return 0;

    const QString input = QString::fromUtf8(
        reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));

    // Exercise every wiki-link match while bounding work on punctuation-heavy
    // inputs so the fuzzer spends its time exploring instead of timing out.
    QRegularExpressionMatchIterator matches =
        WikiLink::pattern().globalMatch(input);
    int visited = 0;
    while (matches.hasNext() && visited < 128) {
        const QString target = WikiLink::cleanTarget(matches.next().captured(1));
        Vault::isValidTitle(target);
        ++visited;
    }

    const qsizetype split = input.size() / 2;
    const QString oldTitle = input.left(split).left(128);
    const QString newTitle = input.mid(split).left(128);
    Vault::replaceLinkTargets(input, oldTitle, newTitle);
    Vault::titleFromPath(input.left(4096));

    const qsizetype newline = input.indexOf(QLatin1Char('\n'));
    const QString firstLine =
        (newline < 0 ? input : input.left(newline)).left(4096);
    MascotSeed::fromLine(firstLine);
    MascotSeed::kindFromLine(firstLine);
    MascotSeed::strip(input);

    ContentSecurity::externalUrl(input.left(4096));
    return 0;
}
