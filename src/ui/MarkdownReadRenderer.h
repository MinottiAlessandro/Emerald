#pragma once

#include <QFont>
#include <QTextBlock>

class QTextDocument;

// Builds the presentation-only QTextDocument used by Read Mode. The Markdown
// source document remains untouched and authoritative; this renderer emits a
// small, deterministic subset of rich text using Qt's native document model.
class MarkdownReadRenderer {
public:
    struct Options {
        QFont baseFont;
        int lineSpacing = 100;
    };

    static void render(QTextDocument *target, const QString &source,
                       const Options &options);

    // Lightweight source mapping used to keep the reader near the same place
    // when documents are swapped. Fine-grained selection mapping is added in a
    // later branch; block-level mapping is enough for stable scrolling here.
    static int sourceBlockNumber(const QTextBlock &block);
    static QTextBlock blockForSourceBlock(QTextDocument *document,
                                          int sourceBlockNumber);
};
