#pragma once

#include <QFont>
#include <QTextBlock>

class QTextDocument;
class QTextCursor;

// Builds the presentation-only QTextDocument used by Read Mode. The Markdown
// source document remains untouched and authoritative; this renderer emits a
// small, deterministic subset of rich text using Qt's native document model.
class MarkdownReadRenderer {
public:
    struct Options {
        QFont baseFont;
        int lineSpacing = 100;
        QString imageBasePath;
        QString vaultRootPath;
        qreal fallbackWidth = 720.0;
        qreal maxImageHeight = 520.0;
    };

    static void render(QTextDocument *target, const QString &source,
                       const Options &options);

    // Internal anchor encoding shared with MarkdownEditor. Wiki-note targets
    // use a private scheme so they remain distinguishable from external URLs
    // after the Markdown source has been replaced by presentation text.
    static QString wikiLinkHref(const QString &target);
    static QString wikiTargetFromHref(const QString &href);

    // Bidirectional source mapping. Compact runs live in QTextBlockUserData so
    // mapping does not split character formats or make normal layout heavier.
    // Cursor direction and selections are preserved across document swaps.
    static QTextCursor mapToReadCursor(QTextDocument *readDocument,
                                       const QTextCursor &sourceCursor);
    static QTextCursor mapToSourceCursor(QTextDocument *sourceDocument,
                                         const QTextCursor &readCursor);

    // Coarse block mapping remains useful for scroll anchoring.
    static int sourceBlockNumber(const QTextBlock &block);
    static QTextBlock blockForSourceBlock(QTextDocument *document,
                                          int sourceBlockNumber);
};
