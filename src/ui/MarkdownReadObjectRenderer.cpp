#include "MarkdownReadObjectRenderer.h"

#include "MathRender.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>

namespace {
constexpr qreal HorizontalInset = 12.0;
constexpr qreal CodeHeaderHeight = 30.0;
constexpr qreal CodePadding = 12.0;

QFont codeFont(const QFont &base) {
    QFont font(base);
    font.setFamilies({QStringLiteral("Menlo"), QStringLiteral("Consolas"),
                      QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("Liberation Mono"),
                      QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace);
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(qMax(8.0, font.pointSizeF() * 0.92));
    return font;
}

qreal wrappedCodeHeight(const QString &code, const QFont &font, qreal width) {
    static QHash<QString, qreal> cache;
    static QStringList order;
    const QString key = font.toString() + QLatin1Char(':') +
                        QString::number(width, 'f', 1) + QLatin1Char(':') + code;
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return cached.value();

    const QStringList lines = code.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAnywhere);
    qreal height = 0.0;
    for (const QString &text : lines) {
        QTextLayout layout(text.isEmpty() ? QStringLiteral(" ") : text, font);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width);
            height += line.height();
        }
        layout.endLayout();
    }
    height = qMax(height, QFontMetricsF(font).lineSpacing());
    cache.insert(key, height);
    order.append(key);
    constexpr int MaxCodeLayoutEntries = 256;
    while (order.size() > MaxCodeLayoutEntries)
        cache.remove(order.takeFirst());
    return height;
}

QPixmap imagePixmap(const QString &path, const QSize &logicalSize, qreal dpr) {
    const QFileInfo info(path);
    if (!info.isFile() || logicalSize.isEmpty())
        return {};
    const QSize deviceSize = (QSizeF(logicalSize) * dpr).toSize();
    const QString key =
        QStringLiteral("read-image:%1:%2:%3:%4:%5x%6")
            .arg(info.absoluteFilePath())
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch())
            .arg(dpr)
            .arg(deviceSize.width())
            .arg(deviceSize.height());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached))
        return cached;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setScaledSize(deviceSize);
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

void drawWrappedCode(QPainter &painter, const QRectF &rect, const QString &code,
                     const QFont &font) {
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAnywhere);
    const QStringList lines = code.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    qreal y = 0.0;
    for (const QString &text : lines) {
        QTextLayout layout(text.isEmpty() ? QStringLiteral(" ") : text, font);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(rect.width());
            line.setPosition(QPointF(0.0, y));
            y += line.height();
        }
        layout.endLayout();
        layout.draw(&painter, rect.topLeft());
    }
}
} // namespace

MarkdownReadObjectRenderer::MarkdownReadObjectRenderer(QObject *parent)
    : QObject(parent) {}

qreal MarkdownReadObjectRenderer::availableWidth(QTextDocument *document,
                                                  const QTextFormat &format) {
    qreal width = document ? document->textWidth() : -1.0;
    if (width <= 0.0 || width > 100000.0)
        width = format.property(FallbackWidthProperty).toDouble();
    const qreal margin = document ? document->documentMargin() * 2.0 : 32.0;
    return qMax(qreal(48.0), width - margin - HorizontalInset * 2.0);
}

QSizeF MarkdownReadObjectRenderer::fittedImageSize(const QTextFormat &format,
                                                   qreal width) {
    const QSize source = format.property(SourceSizeProperty).toSize();
    const qreal maxHeight =
        qMax(qreal(72.0), format.property(MaxHeightProperty).toDouble());
    if (!source.isValid())
        return QSizeF(width, qMin(maxHeight, qreal(96.0)));
    const QSizeF fitted = QSizeF(source).scaled(QSizeF(width, maxHeight),
                                                Qt::KeepAspectRatio);
    return {qMax(qreal(48.0), fitted.width()),
            qMax(qreal(48.0), fitted.height())};
}

QSizeF MarkdownReadObjectRenderer::intrinsicSize(QTextDocument *document, int,
                                                 const QTextFormat &format) {
    const Kind objectKind = static_cast<Kind>(
        format.property(KindProperty).toInt());
    const QTextCharFormat character = format.toCharFormat();
    const qreal width = availableWidth(document, format);
    const QFontMetricsF metrics(character.font());

    switch (objectKind) {
    case Kind::Image: {
        const QSizeF image = fittedImageSize(format, width);
        return {width, image.height() + 20.0};
    }
    case Kind::InlineMath: {
        const QFont font = MathRender::mathFont(character.font(), false);
        const QSizeF size = MathRender::measure(
            format.stringProperty(PayloadProperty), font);
        return {qMax(qreal(2.0), size.width() + 2.0),
                qMax(metrics.height(), size.height() + 2.0)};
    }
    case Kind::DisplayMath: {
        const QFont font = MathRender::mathFont(character.font(), true);
        const QSizeF size = MathRender::measure(
            format.stringProperty(PayloadProperty), font, true);
        return {width, qMax(metrics.height() * 2.4, size.height() + 24.0)};
    }
    case Kind::Rule:
        return {width, qMax(qreal(18.0), metrics.height())};
    case Kind::Checkbox: {
        const qreal side = qMax(qreal(12.0), metrics.ascent() * 0.9);
        return {side + 2.0, qMax(side + 2.0, metrics.height())};
    }
    case Kind::CodeBlock: {
        const QFont font = codeFont(character.font());
        const qreal bodyWidth = qMax(qreal(20.0), width - CodePadding * 2.0);
        const qreal bodyHeight = wrappedCodeHeight(
            format.stringProperty(PayloadProperty), font, bodyWidth);
        return {width, CodeHeaderHeight + CodePadding * 2.0 + bodyHeight};
    }
    case Kind::None:
        break;
    }
    return {metrics.horizontalAdvance(QChar::ObjectReplacementCharacter),
            metrics.height()};
}

void MarkdownReadObjectRenderer::drawObject(QPainter *painter,
                                            const QRectF &rect,
                                            QTextDocument *, int,
                                            const QTextFormat &format) {
    if (!painter)
        return;
    const Kind objectKind = static_cast<Kind>(
        format.property(KindProperty).toInt());
    const QTextCharFormat character = format.toCharFormat();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    switch (objectKind) {
    case Kind::Image: {
        const QSizeF imageSize = fittedImageSize(format, rect.width());
        const QRectF imageRect(
            rect.left() + (rect.width() - imageSize.width()) / 2.0,
            rect.top() + 10.0, imageSize.width(), imageSize.height());
        const QString path = format.stringProperty(PathProperty);
        const QPixmap pixmap = imagePixmap(
            path, imageSize.toSize(), painter->device()->devicePixelRatioF());
        painter->setPen(QPen(QColor(0x2b, 0x4a, 0x39), 1.0));
        painter->setBrush(QColor(0x10, 0x11, 0x13));
        painter->drawRoundedRect(imageRect, 7.0, 7.0);
        if (!pixmap.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(imageRect.adjusted(1, 1, -1, -1), 6, 6);
            painter->setClipPath(clip);
            const QSizeF logical = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
            const QPointF topLeft(
                imageRect.left() + (imageRect.width() - logical.width()) / 2.0,
                imageRect.top() + (imageRect.height() - logical.height()) / 2.0);
            painter->drawPixmap(topLeft, pixmap);
        } else {
            painter->setPen(QColor(0x79, 0x9a, 0x88));
            QString label = format.stringProperty(LabelProperty).trimmed();
            if (label.isEmpty())
                label = format.stringProperty(PayloadProperty);
            if (label.size() > 58)
                label = label.left(55) + QStringLiteral("…");
            painter->drawText(imageRect.adjusted(14, 10, -14, -10),
                              Qt::AlignCenter | Qt::TextWordWrap,
                              tr("Image not found\n%1").arg(label));
        }
        break;
    }
    case Kind::InlineMath:
        MathRender::paint(*painter, rect,
                          format.stringProperty(PayloadProperty),
                          MathRender::mathFont(character.font(), false),
                          QColor(0x6f, 0xcf, 0xc0), MathRender::Align::Inline,
                          rect.height() - 2.0);
        break;
    case Kind::DisplayMath:
        painter->fillRect(rect, QColor(0x13, 0x1c, 0x18));
        MathRender::paint(*painter, rect,
                          format.stringProperty(PayloadProperty),
                          MathRender::mathFont(character.font(), true),
                          QColor(0x6f, 0xcf, 0xc0), MathRender::Align::Display);
        break;
    case Kind::Rule: {
        painter->setPen(QPen(QColor(0x3b, 0x61, 0x4d), 1.4));
        painter->drawLine(QPointF(rect.left(), rect.center().y()),
                          QPointF(rect.right(), rect.center().y()));
        break;
    }
    case Kind::Checkbox: {
        const qreal side = qMin(rect.width() - 2.0, rect.height() - 2.0);
        const QRectF box(rect.left() + 1.0,
                         rect.center().y() - side / 2.0, side, side);
        const bool checked = format.boolProperty(CheckedProperty);
        const QColor accent(0x2b, 0xbf, 0x74);
        painter->setPen(QPen(accent, 1.5));
        painter->setBrush(checked ? accent : Qt::NoBrush);
        painter->drawRoundedRect(box, 3.0, 3.0);
        if (checked) {
            painter->setPen(QPen(QColor(0x10, 0x18, 0x14), 1.6,
                                 Qt::SolidLine, Qt::RoundCap,
                                 Qt::RoundJoin));
            const QPointF points[] = {
                {box.left() + side * 0.23, box.top() + side * 0.52},
                {box.left() + side * 0.42, box.top() + side * 0.70},
                {box.left() + side * 0.79, box.top() + side * 0.29}};
            painter->drawPolyline(points, 3);
        }
        break;
    }
    case Kind::CodeBlock: {
        painter->setPen(QPen(QColor(0x2a, 0x49, 0x39), 1.0));
        painter->setBrush(QColor(0x12, 0x1d, 0x18));
        painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        QRectF header(rect.left(), rect.top(), rect.width(), CodeHeaderHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x19, 0x2a, 0x21));
        painter->drawRoundedRect(header.adjusted(1, 1, -1, 5), 6, 6);
        painter->drawRect(header.adjusted(1, 6, -1, 0));

        QFont labelFont = character.font();
        labelFont.setPointSizeF(qMax(8.0, labelFont.pointSizeF() * 0.78));
        labelFont.setWeight(QFont::DemiBold);
        painter->setFont(labelFont);
        painter->setPen(QColor(0x83, 0xa6, 0x93));
        painter->drawText(header.adjusted(10, 0, -76, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          format.stringProperty(LanguageProperty));

        const QRectF copy = codeCopyButtonRect(rect);
        painter->setPen(QPen(QColor(0x3a, 0x61, 0x4d), 1.0));
        painter->setBrush(QColor(0x16, 0x24, 0x1c));
        painter->drawRoundedRect(copy, 4, 4);
        painter->setPen(QColor(0xa3, 0xc4, 0xb3));
        painter->drawText(copy, Qt::AlignCenter, tr("Copy"));

        const QRectF body = rect.adjusted(CodePadding,
                                          CodeHeaderHeight + CodePadding,
                                          -CodePadding, -CodePadding);
        painter->setClipRect(body);
        const QFont font = codeFont(character.font());
        painter->setFont(font);
        painter->setPen(QColor(0xc7, 0xdd, 0xd1));
        drawWrappedCode(*painter, body,
                        format.stringProperty(PayloadProperty), font);
        break;
    }
    case Kind::None:
        break;
    }
    painter->restore();
}

QTextCharFormat MarkdownReadObjectRenderer::imageFormat(
    const QFont &baseFont, const QString &resolvedPath, const QString &target,
    const QString &altText, const QSize &sourceSize, qreal fallbackWidth,
    qreal maxHeight) {
    QTextCharFormat format;
    format.setFont(baseFont);
    format.setObjectType(ObjectType);
    format.setProperty(KindProperty, int(Kind::Image));
    format.setProperty(PathProperty, resolvedPath);
    format.setProperty(PayloadProperty, target);
    format.setProperty(LabelProperty, altText);
    format.setProperty(SourceSizeProperty, sourceSize);
    format.setProperty(FallbackWidthProperty, fallbackWidth);
    format.setProperty(MaxHeightProperty, maxHeight);
    return format;
}

QTextCharFormat MarkdownReadObjectRenderer::inlineMathFormat(
    const QFont &baseFont, const QString &formula) {
    QTextCharFormat format;
    format.setFont(baseFont);
    format.setObjectType(ObjectType);
    format.setProperty(KindProperty, int(Kind::InlineMath));
    format.setProperty(PayloadProperty, formula);
    return format;
}

QTextCharFormat MarkdownReadObjectRenderer::displayMathFormat(
    const QFont &baseFont, const QString &formula, qreal fallbackWidth) {
    QTextCharFormat format = inlineMathFormat(baseFont, formula);
    format.setProperty(KindProperty, int(Kind::DisplayMath));
    format.setProperty(FallbackWidthProperty, fallbackWidth);
    return format;
}

QTextCharFormat MarkdownReadObjectRenderer::ruleFormat(const QFont &baseFont,
                                                       qreal fallbackWidth) {
    QTextCharFormat format;
    format.setFont(baseFont);
    format.setObjectType(ObjectType);
    format.setProperty(KindProperty, int(Kind::Rule));
    format.setProperty(FallbackWidthProperty, fallbackWidth);
    return format;
}

QTextCharFormat MarkdownReadObjectRenderer::checkboxFormat(const QFont &baseFont,
                                                           bool checked) {
    QTextCharFormat format;
    format.setFont(baseFont);
    format.setObjectType(ObjectType);
    format.setProperty(KindProperty, int(Kind::Checkbox));
    format.setProperty(CheckedProperty, checked);
    return format;
}

QTextCharFormat MarkdownReadObjectRenderer::codeBlockFormat(
    const QFont &baseFont, const QString &language, const QString &code,
    qreal fallbackWidth) {
    QTextCharFormat format;
    format.setFont(baseFont);
    format.setObjectType(ObjectType);
    format.setProperty(KindProperty, int(Kind::CodeBlock));
    format.setProperty(LanguageProperty,
                       language.trimmed().isEmpty() ? QStringLiteral("Text")
                                                    : language.trimmed());
    format.setProperty(PayloadProperty, code);
    format.setProperty(FallbackWidthProperty, fallbackWidth);
    return format;
}

MarkdownReadObjectRenderer::Kind
MarkdownReadObjectRenderer::kind(const QTextCharFormat &format) {
    if (format.objectType() != ObjectType)
        return Kind::None;
    return static_cast<Kind>(format.property(KindProperty).toInt());
}

QString MarkdownReadObjectRenderer::codeText(const QTextCharFormat &format) {
    return kind(format) == Kind::CodeBlock
               ? format.stringProperty(PayloadProperty)
               : QString();
}

QRectF MarkdownReadObjectRenderer::codeCopyButtonRect(
    const QRectF &objectRect) {
    return QRectF(objectRect.right() - 64.0, objectRect.top() + 5.0,
                  54.0, 20.0);
}
