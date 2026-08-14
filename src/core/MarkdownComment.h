#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <algorithm>

// HTML comments are valid Markdown source and Emerald treats them as
// author-only note content:
//
//     visible <!-- hidden --> text
//     <!--
//     a hidden block
//     -->
//
// One scanner is shared by rendering, search, links and editor interaction so
// a comment cannot disappear visually while still leaking into Graph View or
// search results. Comment-looking text inside inline/fenced code or math stays
// literal. Newlines are deliberately retained by strip() so removing a comment
// never joins otherwise separate Markdown rows.
namespace MarkdownComment {

struct Range {
    int start = 0;
    int end = 0; // exclusive
};

struct LineAnalysis {
    QList<Range> ranges;
    bool continuesComment = false;
    bool continuesDisplayMath = false;

    QString masked(const QString &line, QChar fill = QLatin1Char(' ')) const {
        QString result = line;
        for (const Range &range : ranges)
            for (int i = qMax(0, range.start);
                 i < range.end && i < result.size(); ++i)
                result[i] = fill;
        return result;
    }

    bool contains(int column) const {
        for (const Range &range : ranges)
            if (column >= range.start && column < range.end)
                return true;
        return false;
    }
};

inline int closingRun(const QString &line, int start, QChar marker,
                      int runLength) {
    const QString delimiter(runLength, marker);
    return line.indexOf(delimiter, start);
}

// Analyse one source row. The two state inputs make the same routine useful to
// QSyntaxHighlighter (incremental, one block at a time) and to the full-source
// scanner below. Ranges use row-local UTF-16 positions.
inline LineAnalysis analyzeLine(const QString &line,
                                bool startsInsideComment = false,
                                bool startsInsideDisplayMath = false) {
    LineAnalysis result;
    bool inComment = startsInsideComment;
    int commentStart = inComment ? 0 : -1;

    // Match MathRender's display grammar without introducing a core -> UI
    // dependency. Once a row belongs to a display formula the renderer treats
    // that complete row as formula source, including anything after its closing
    // delimiter, so comment-looking text on it must remain literal.
    if (!inComment && startsInsideDisplayMath) {
        result.continuesDisplayMath =
            !line.contains(QStringLiteral("$$"));
        return result;
    }
    if (!inComment) {
        static const QRegularExpression displayRe(QStringLiteral(
            "^\\s*\\$\\$\\s*(\\S(?:.*\\S)?)\\s*\\$\\$\\s*$"));
        static const QRegularExpression displayOpenRe(
            QStringLiteral("^\\s*\\$\\$"));
        if (displayRe.match(line).hasMatch())
            return result;
        if (displayOpenRe.match(line).hasMatch()) {
            result.continuesDisplayMath = true;
            return result;
        }
    }

    // Keep inline-math recognition identical to MathRender::pattern(). This
    // avoids treating currency or a stray dollar delimiter as a formula and
    // accidentally exposing a real comment to search, Graph View, or Read Mode.
    static const QRegularExpression inlineMathRe(QStringLiteral(
        "(?<![\\\\$])\\$(?!\\$)(\\S(?:[^$\\n]*\\S)?)\\$(?!\\$)"));

    for (int pos = 0; pos < line.size();) {
        if (inComment) {
            const int close = line.indexOf(QStringLiteral("-->"), pos);
            if (close < 0) {
                result.ranges.append({commentStart, int(line.size())});
                result.continuesComment = true;
                return result;
            }
            result.ranges.append({commentStart, close + 3});
            pos = close + 3;
            inComment = false;
            commentStart = -1;
            continue;
        }

        // Backslash escaping is honoured before recognising any delimiter.
        if (line.at(pos) == QLatin1Char('\\') && pos + 1 < line.size()) {
            pos += 2;
            continue;
        }

        // Inline code is verbatim. Match the complete backtick run so examples
        // such as ``<!-- literal -->`` remain ordinary code.
        if (line.at(pos) == QLatin1Char('`')) {
            int ticks = 1;
            while (pos + ticks < line.size() &&
                   line.at(pos + ticks) == QLatin1Char('`'))
                ++ticks;
            const int close = closingRun(line, pos + ticks,
                                         QLatin1Char('`'), ticks);
            if (close >= 0) {
                pos = close + ticks;
                continue;
            }
        }

        // Inline math is verbatim, using the same delimiter constraints as the
        // renderer (not merely the next pair of dollar signs).
        if (line.at(pos) == QLatin1Char('$')) {
            const QRegularExpressionMatch math = inlineMathRe.match(
                line, pos, QRegularExpression::NormalMatch,
                QRegularExpression::AnchorAtOffsetMatchOption);
            if (math.hasMatch()) {
                pos = int(math.capturedEnd());
                continue;
            }
        }

        if (line.mid(pos, 4) == QStringLiteral("<!--")) {
            inComment = true;
            commentStart = pos;
            pos += 4;
            continue;
        }
        ++pos;
    }

    result.continuesComment = inComment;
    if (inComment)
        result.ranges.append({commentStart, int(line.size())});
    return result;
}

inline QList<Range> ranges(const QString &content) {
    QList<Range> result;
    static const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(`{3,}|~{3,})(?:\\s*(.*))?$"));

    bool inFence = false;
    QChar fenceMarker;
    int fenceLength = 0;
    bool inComment = false;
    bool inDisplayMath = false;
    int lineStart = 0;
    while (lineStart <= content.size()) {
        int lineEnd = content.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0)
            lineEnd = content.size();
        const QString line = content.mid(lineStart, lineEnd - lineStart);

        const QRegularExpressionMatch fence = fenceRe.match(line);
        bool fenceLine = false;
        if (!inComment && !inDisplayMath && fence.hasMatch()) {
            const QString marker = fence.captured(1);
            if (!inFence) {
                inFence = true;
                fenceMarker = marker.front();
                fenceLength = marker.size();
                fenceLine = true;
            } else if (marker.front() == fenceMarker &&
                       marker.size() >= fenceLength &&
                       fence.captured(2).trimmed().isEmpty()) {
                inFence = false;
                fenceMarker = QChar();
                fenceLength = 0;
                fenceLine = true;
            }
        }

        if (!inFence && !fenceLine) {
            const LineAnalysis lineResult =
                analyzeLine(line, inComment, inDisplayMath);
            for (const Range &range : lineResult.ranges)
                result.append(
                    {lineStart + range.start, lineStart + range.end});
            inComment = lineResult.continuesComment;
            inDisplayMath = lineResult.continuesDisplayMath;
        }

        if (lineEnd == content.size())
            break;
        lineStart = lineEnd + 1;
    }
    return result;
}

inline bool overlaps(const QList<Range> &commentRanges, int start, int end) {
    // ranges() returns ordered, disjoint spans. Jump directly to the first span
    // that can extend beyond `start` so link-heavy notes do not degrade into a
    // links × comments scan.
    const auto candidate = std::lower_bound(
        commentRanges.cbegin(), commentRanges.cend(), start,
        [](const Range &range, int position) { return range.end <= position; });
    return candidate != commentRanges.cend() && candidate->start < end;
}

inline QString masked(const QString &content,
                      const QList<Range> &commentRanges,
                      QChar fill = QLatin1Char(' ')) {
    QString result = content;
    for (const Range &range : commentRanges)
        for (int i = range.start; i < range.end && i < result.size(); ++i)
            if (result.at(i) != QLatin1Char('\n'))
                result[i] = fill;
    return result;
}

inline QString masked(const QString &content,
                      QChar fill = QLatin1Char(' ')) {
    return masked(content, ranges(content), fill);
}

inline QString strip(const QString &content) {
    const QList<Range> commentRanges = ranges(content);
    if (commentRanges.isEmpty())
        return content;
    QString result;
    result.reserve(content.size());
    int nextRange = 0;
    for (int i = 0; i < content.size(); ++i) {
        while (nextRange < commentRanges.size() &&
               i >= commentRanges.at(nextRange).end)
            ++nextRange;
        const bool hidden =
            nextRange < commentRanges.size() &&
            i >= commentRanges.at(nextRange).start &&
            i < commentRanges.at(nextRange).end;
        if (!hidden || content.at(i) == QLatin1Char('\n'))
            result += content.at(i);
    }
    return result;
}

} // namespace MarkdownComment
