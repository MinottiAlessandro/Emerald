#pragma once

#include <QRegularExpression>
#include <QString>

// The per-note mascot seed lives inline, as the very first line of the note's
// .md file, written as an HTML comment so it stays invisible in any other
// Markdown renderer (GitHub, Obsidian, a plain preview) yet remains plain text
// the user can read and edit:
//
//     <!-- mascot: 13845229104471 -->
//
// The seed alone re-creates the creature, so nothing else needs to be stored —
// the mascot travels with the note (copy/move/rename the file and it comes
// along) and leaves no separate metadata behind. Editing or deleting this line
// changes or removes the mascot. The editor keeps the line hidden until the
// caret reaches the top of the file (see MarkdownEditor).
namespace MascotSeed {

// The canonical header line for `seed` (no trailing newline).
inline QString line(quint64 seed) {
    return QStringLiteral("<!-- mascot: ") + QString::number(seed) +
           QStringLiteral(" -->");
}

// The seed encoded in a single line of text, or 0 if it isn't a mascot header.
// Tolerant of spacing and case so a hand-edited line still parses.
inline quint64 fromLine(const QString &text) {
    static const QRegularExpression re(
        QStringLiteral("^<!--\\s*mascot:\\s*(\\d+)\\s*-->\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1).toULongLong() : 0;
}

// `content` with a leading mascot header line removed — used when the line must
// not count as note text (seed hashing, full-text indexing). Content without a
// header line is returned unchanged.
inline QString strip(const QString &content) {
    const int nl = content.indexOf(QLatin1Char('\n'));
    const QString first = nl < 0 ? content : content.left(nl);
    if (fromLine(first) != 0)
        return nl < 0 ? QString() : content.mid(nl + 1);
    return content;
}

} // namespace MascotSeed
