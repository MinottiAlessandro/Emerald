#include "MarkdownWikiLinkScanner.h"

#include "MarkdownComment.h"
#include "WikiLink.h"

#include <QRegularExpression>
#include <algorithm>

namespace MarkdownWikiLinkScanner {
namespace {

QVector<QPair<int, int>> inlineCodeSpans(const QString &line) {
  QVector<QPair<int, int>> spans;
  int pos = 0;
  while (pos < line.size()) {
    const int start = line.indexOf(QLatin1Char('`'), pos);
    if (start < 0)
      break;
    int ticks = 1;
    while (start + ticks < line.size() &&
           line.at(start + ticks) == QLatin1Char('`'))
      ++ticks;
    const QString delimiter(ticks, QLatin1Char('`'));
    const int end = line.indexOf(delimiter, start + ticks);
    if (end < 0)
      break;
    spans.append({start, end + ticks});
    pos = end + ticks;
  }
  return spans;
}

} // namespace

QVector<Link> scan(const QString &content) {
  QVector<Link> links;
  const QList<MarkdownComment::Range> commentRanges =
      MarkdownComment::ranges(content);
  static const QRegularExpression fenceRe(
      QStringLiteral("^\\s*(`{3,}|~{3,})\\s*(\\S*).*$"));

  bool insideFence = false;
  QChar fenceCharacter;
  int fenceLength = 0;
  int lineStart = 0;
  int lineNumber = 1;
  while (lineStart <= content.size()) {
    int lineEnd = content.indexOf(QLatin1Char('\n'), lineStart);
    if (lineEnd < 0)
      lineEnd = content.size();
    const QString lineText = content.mid(lineStart, lineEnd - lineStart);

    const QRegularExpressionMatch fence = fenceRe.match(lineText);
    if (fence.hasMatch()) {
      const QString marker = fence.captured(1);
      if (!insideFence) {
        insideFence = true;
        fenceCharacter = marker.front();
        fenceLength = marker.size();
      } else if (marker.front() == fenceCharacter &&
                 marker.size() >= fenceLength && fence.captured(2).isEmpty()) {
        insideFence = false;
        fenceCharacter = QChar();
        fenceLength = 0;
      }
    } else if (!insideFence) {
      const QVector<QPair<int, int>> codeSpans = inlineCodeSpans(lineText);
      const auto overlapsCode = [&codeSpans](int start, int end) {
        return std::any_of(codeSpans.cbegin(), codeSpans.cend(),
                           [start, end](const auto &span) {
                             return start < span.second && end > span.first;
                           });
      };

      auto linkIt = WikiLink::pattern().globalMatch(lineText);
      while (linkIt.hasNext()) {
        const QRegularExpressionMatch match = linkIt.next();
        const int start = int(match.capturedStart());
        const int end = int(match.capturedEnd());
        if (overlapsCode(start, end) ||
            MarkdownComment::overlaps(commentRanges, lineStart + start,
                                      lineStart + end))
          continue;
        links.push_back({WikiLink::cleanTarget(match.captured(1)),
                         lineStart + start, end - start, lineNumber});
      }
    }

    if (lineEnd == content.size())
      break;
    lineStart = lineEnd + 1;
    ++lineNumber;
  }
  return links;
}

} // namespace MarkdownWikiLinkScanner
