#pragma once

#include <QObject>
#include <QRectF>
#include <QSize>
#include <QTextCharFormat>
#include <QTextObjectInterface>

class QPainter;
class QTextDocument;

// Paints the non-text presentation objects embedded by MarkdownReadRenderer.
// All payload is carried by QTextCharFormat properties, so the handler remains
// stateless, cheap to retain, and naturally invalidated by QTextDocument layout.
class MarkdownReadObjectRenderer final : public QObject,
                                         public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    enum class Kind {
        None = 0,
        Image,
        InlineMath,
        DisplayMath,
        Rule,
        Checkbox,
        CodeBlock,
    };

    static constexpr int ObjectType = QTextFormat::UserObject + 81;

    explicit MarkdownReadObjectRenderer(QObject *parent = nullptr);

    QSizeF intrinsicSize(QTextDocument *document, int positionInDocument,
                         const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect,
                    QTextDocument *document, int positionInDocument,
                    const QTextFormat &format) override;

    static QTextCharFormat imageFormat(const QFont &baseFont,
                                       const QString &resolvedPath,
                                       const QString &target,
                                       const QString &altText,
                                       const QString &title,
                                       const QSize &sourceSize,
                                       qreal fallbackWidth,
                                       qreal maxHeight,
                                       int requestedWidth = 0,
                                       int requestedHeight = 0,
                                       bool inlinePlacement = false);
    static QTextCharFormat inlineMathFormat(const QFont &baseFont,
                                            const QString &formula);
    static QTextCharFormat displayMathFormat(const QFont &baseFont,
                                             const QString &formula,
                                             qreal fallbackWidth);
    static QTextCharFormat ruleFormat(const QFont &baseFont,
                                      qreal fallbackWidth);
    static QTextCharFormat checkboxFormat(const QFont &baseFont, bool checked);
    static QTextCharFormat codeBlockFormat(const QFont &baseFont,
                                           const QString &language,
                                           const QString &code,
                                           qreal fallbackWidth,
                                           int sourceStart,
                                           int sourceLength);

    static Kind kind(const QTextCharFormat &format);
    static QString codeText(const QTextCharFormat &format);
    static int codeSourceStart(const QTextCharFormat &format);
    static int codeSourceLength(const QTextCharFormat &format);
    // Plain-text alternative used by selection copy and assistive UI for a
    // custom object that would otherwise appear as U+FFFC.
    static QString accessibleText(const QTextCharFormat &format);
    static QRectF codeCopyButtonRect(const QRectF &objectRect);

private:
    static qreal availableWidth(QTextDocument *document,
                                const QTextFormat &format);
    static QSizeF fittedImageSize(const QTextFormat &format, qreal width);

    enum Property {
        KindProperty = QTextFormat::UserProperty + 410,
        PayloadProperty,
        PathProperty,
        LabelProperty,
        SourceSizeProperty,
        FallbackWidthProperty,
        MaxHeightProperty,
        RequestedWidthProperty,
        RequestedHeightProperty,
        InlinePlacementProperty,
        CheckedProperty,
        LanguageProperty,
        CodeSourceStartProperty,
        CodeSourceLengthProperty,
    };
};
