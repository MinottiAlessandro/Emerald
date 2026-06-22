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
// A creature generated from a user-supplied "kind" (a hand-drawn creature the
// user dropped into their mascots folder) records that name too, so it stays
// reproducible and travels with the note even though the kind catalog is
// machine-local; on a machine without that art it falls back to the built-in
// creature the seed alone produces:
//
//     <!-- mascot: 13845229104471 kind:my-dragon -->
//
// The seed (plus kind, when present) re-creates the creature, so nothing else
// needs to be stored — the mascot travels with the note (copy/move/rename the
// file and it comes along) and leaves no separate metadata behind. Editing or
// deleting this line changes or removes the mascot. The editor keeps the line
// hidden until the caret reaches the top of the file (see MarkdownEditor).
namespace MascotSeed {

// The canonical header line for `seed` (no trailing newline), with an optional
// user-creature kind.
inline QString line(quint64 seed, const QString &kind = QString()) {
    QString s = QStringLiteral("<!-- mascot: ") + QString::number(seed);
    if (!kind.isEmpty())
        s += QStringLiteral(" kind:") + kind;
    return s + QStringLiteral(" -->");
}

namespace detail {
// One parser for both fields. Tolerant of spacing and case so a hand-edited line
// still parses; the kind (if any) is a folder-safe slug.
inline QRegularExpressionMatch matchLine(const QString &text) {
    static const QRegularExpression re(
        QStringLiteral("^<!--\\s*mascot:\\s*(\\d+)"
                       "(?:\\s+kind:([A-Za-z0-9._-]+))?\\s*-->\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text);
}
} // namespace detail

// The seed encoded in a single line of text, or 0 if it isn't a mascot header.
inline quint64 fromLine(const QString &text) {
    const QRegularExpressionMatch m = detail::matchLine(text);
    return m.hasMatch() ? m.captured(1).toULongLong() : 0;
}

// The user-creature kind on a mascot header line, or empty (none / not a header).
inline QString kindFromLine(const QString &text) {
    const QRegularExpressionMatch m = detail::matchLine(text);
    return m.hasMatch() ? m.captured(2) : QString();
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
