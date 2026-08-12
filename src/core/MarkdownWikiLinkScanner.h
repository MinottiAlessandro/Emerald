#pragma once

#include <QString>
#include <QVector>

// Semantic wiki-link discovery for vault-wide features. Unlike the visual
// highlighter, this scanner deliberately ignores examples inside inline and
// fenced code. Broken-link reporting, the graph index, and future backlink
// features share it so they cannot disagree about what constitutes a link.
namespace MarkdownWikiLinkScanner {

struct Link {
  QString target; // cleaned target; empty for a local [[#heading]] reference
  int position = 0;
  int length = 0;
  int line = 0; // one-based source line
};

QVector<Link> scan(const QString &content);

} // namespace MarkdownWikiLinkScanner
