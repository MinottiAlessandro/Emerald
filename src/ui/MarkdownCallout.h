#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QStringView>

// Small, allocation-light parser and visual palette for Obsidian-compatible
// callout title lines:
//
//     > [!tip]
//     > [!warning] Custom title
//
// A marker is a title only when it starts a quote group. At a nested depth, a
// preceding shallower quote therefore still permits a nested callout.
namespace MarkdownCallout {

struct QuotePrefix {
    int depth = 0;
    int contentStart = -1;
};

struct TitleLine {
    QuotePrefix quote;
    int markerStart = -1;
    int markerEnd = -1;
    int typeStart = -1;
    int typeLength = 0;
    int titleStart = -1;
    QString type;

    bool valid() const { return markerStart >= 0; }
    bool hasCustomTitle() const { return titleStart >= 0; }
};

inline QuotePrefix quotePrefix(QStringView text) {
    int pos = 0;
    while (pos < text.size() &&
           (text.at(pos) == QLatin1Char(' ') ||
            text.at(pos) == QLatin1Char('\t')))
        ++pos;

    QuotePrefix result;
    while (pos < text.size() && text.at(pos) == QLatin1Char('>')) {
        ++result.depth;
        ++pos;
        while (pos < text.size() &&
               (text.at(pos) == QLatin1Char(' ') ||
                text.at(pos) == QLatin1Char('\t')))
            ++pos;
    }
    if (result.depth > 0)
        result.contentStart = pos;
    return result;
}

inline bool isTypeCharacter(QChar ch) {
    return (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
           (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
           (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
           ch == QLatin1Char('-') || ch == QLatin1Char('_');
}

inline TitleLine titleLine(QStringView text, int previousQuoteDepth) {
    TitleLine result;
    result.quote = quotePrefix(text);
    if (result.quote.depth == 0 ||
        previousQuoteDepth >= result.quote.depth)
        return result;

    const int markerStart = result.quote.contentStart;
    if (markerStart + 4 > text.size() ||
        text.at(markerStart) != QLatin1Char('[') ||
        text.at(markerStart + 1) != QLatin1Char('!'))
        return result;

    int close = markerStart + 2;
    while (close < text.size() && isTypeCharacter(text.at(close)))
        ++close;
    if (close == markerStart + 2 || close >= text.size() ||
        text.at(close) != QLatin1Char(']'))
        return result;

    result.markerStart = markerStart;
    result.markerEnd = close + 1;
    result.typeStart = markerStart + 2;
    result.typeLength = close - result.typeStart;
    result.type = text.mid(result.typeStart, result.typeLength)
                      .toString()
                      .toLower();

    int titleStart = result.markerEnd;
    while (titleStart < text.size() && text.at(titleStart).isSpace())
        ++titleStart;
    if (titleStart < text.size())
        result.titleStart = titleStart;
    return result;
}

inline QString defaultTitle(const QString &type) {
    QString title = type.toLower();
    bool capitalize = true;
    for (int i = 0; i < title.size(); ++i) {
        QChar ch = title.at(i);
        if (ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            title[i] = QLatin1Char(' ');
            capitalize = true;
        } else if (capitalize) {
            title[i] = ch.toUpper();
            capitalize = false;
        }
    }
    if (title == QStringLiteral("Faq"))
        return QStringLiteral("FAQ");
    return title;
}

inline const QStringList &supportedTypes() {
    static const QStringList types{
        QStringLiteral("note"),      QStringLiteral("abstract"),
        QStringLiteral("summary"),   QStringLiteral("tldr"),
        QStringLiteral("info"),      QStringLiteral("todo"),
        QStringLiteral("tip"),       QStringLiteral("hint"),
        QStringLiteral("important"), QStringLiteral("success"),
        QStringLiteral("check"),     QStringLiteral("done"),
        QStringLiteral("question"),  QStringLiteral("help"),
        QStringLiteral("faq"),       QStringLiteral("warning"),
        QStringLiteral("caution"),   QStringLiteral("attention"),
        QStringLiteral("failure"),   QStringLiteral("fail"),
        QStringLiteral("missing"),   QStringLiteral("danger"),
        QStringLiteral("error"),     QStringLiteral("bug"),
        QStringLiteral("example"),   QStringLiteral("quote"),
        QStringLiteral("cite")};
    return types;
}

inline QString emoji(const QString &type) {
    const QString key = type.toLower();
    if (key == QStringLiteral("note"))      return QStringLiteral("📝");
    if (key == QStringLiteral("abstract"))  return QStringLiteral("📋");
    if (key == QStringLiteral("summary"))   return QStringLiteral("📑");
    if (key == QStringLiteral("tldr"))      return QStringLiteral("⚡");
    if (key == QStringLiteral("info"))      return QStringLiteral("ℹ️");
    if (key == QStringLiteral("todo"))      return QStringLiteral("☑️");
    if (key == QStringLiteral("tip"))       return QStringLiteral("💡");
    if (key == QStringLiteral("hint"))      return QStringLiteral("🔎");
    if (key == QStringLiteral("important")) return QStringLiteral("❗");
    if (key == QStringLiteral("success"))   return QStringLiteral("✅");
    if (key == QStringLiteral("check"))     return QStringLiteral("✔️");
    if (key == QStringLiteral("done"))      return QStringLiteral("🏁");
    if (key == QStringLiteral("question"))  return QStringLiteral("❓");
    if (key == QStringLiteral("help"))      return QStringLiteral("🆘");
    if (key == QStringLiteral("faq"))       return QStringLiteral("💬");
    if (key == QStringLiteral("warning"))   return QStringLiteral("⚠️");
    if (key == QStringLiteral("caution"))   return QStringLiteral("🚧");
    if (key == QStringLiteral("attention")) return QStringLiteral("📣");
    if (key == QStringLiteral("failure"))   return QStringLiteral("❌");
    if (key == QStringLiteral("fail"))      return QStringLiteral("⛔");
    if (key == QStringLiteral("missing"))   return QStringLiteral("🕳️");
    if (key == QStringLiteral("danger"))    return QStringLiteral("🚨");
    if (key == QStringLiteral("error"))     return QStringLiteral("🛑");
    if (key == QStringLiteral("bug"))       return QStringLiteral("🐛");
    if (key == QStringLiteral("example"))   return QStringLiteral("🧪");
    if (key == QStringLiteral("quote"))     return QStringLiteral("💭");
    if (key == QStringLiteral("cite"))      return QStringLiteral("📚");
    return QStringLiteral("🔖");
}

inline QColor accent(const QString &type) {
    const QString key = type.toLower();
    if (key == QStringLiteral("warning") ||
        key == QStringLiteral("caution") ||
        key == QStringLiteral("attention"))
        return QColor(0xf0, 0xa3, 0x4a);
    if (key == QStringLiteral("danger") ||
        key == QStringLiteral("error") ||
        key == QStringLiteral("failure") ||
        key == QStringLiteral("fail") || key == QStringLiteral("missing") ||
        key == QStringLiteral("bug"))
        return QColor(0xe8, 0x66, 0x71);
    if (key == QStringLiteral("question") ||
        key == QStringLiteral("help") || key == QStringLiteral("faq"))
        return QColor(0xb2, 0x83, 0xe6);
    if (key == QStringLiteral("example"))
        return QColor(0xa6, 0x7a, 0xdf);
    if (key == QStringLiteral("abstract") ||
        key == QStringLiteral("summary") || key == QStringLiteral("tldr"))
        return QColor(0x55, 0xc6, 0xd6);
    if (key == QStringLiteral("note") || key == QStringLiteral("info") ||
        key == QStringLiteral("todo"))
        return QColor(0x58, 0xb9, 0xe8);
    if (key == QStringLiteral("quote") || key == QStringLiteral("cite"))
        return QColor(0x9a, 0xb0, 0xa4);
    // Tip, hint, important, success/check/done, and custom callout types use
    // Emerald's familiar green accent.
    return QColor(0x2b, 0xbf, 0x74);
}

inline QColor surface(const QString &type, bool title) {
    const QColor tint = accent(type);
    const QColor base(0x12, 0x1d, 0x18);
    const int strength = title ? 24 : 13;
    const auto channel = [strength](int background, int foreground) {
        return (background * (100 - strength) + foreground * strength) / 100;
    };
    return QColor(channel(base.red(), tint.red()),
                  channel(base.green(), tint.green()),
                  channel(base.blue(), tint.blue()));
}

} // namespace MarkdownCallout
