#include "AppTheme.h"

#include <QApplication>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <iterator>

namespace {
AppTheme::Id activeTheme = AppTheme::Id::Dark;

struct ColorPair {
    QRgb dark;
    QRgb light;
};

// Dark-theme colors are the stable keys because they are already used by the
// stylesheet and custom painters. Keeping their light equivalents here avoids
// duplicating the large QSS file and makes adding a third bundled palette a
// small data change rather than a renderer rewrite.
constexpr ColorPair lightColors[] = {
    {0xff0b2418, 0xff0b2418}, {0xff101113, 0xfff0f3f1},
    {0xff101814, 0xff183126}, {0xff111412, 0xfff2f5f3},
    {0xff111d17, 0xfff4f7f5}, {0xff121512, 0xfff6f8f7},
    {0xff121d18, 0xfff1f5f2},
    {0xff131c18, 0xfff2f5f3}, {0xff141619, 0xffedf1ef},
    {0xff15241c, 0xffebf3ee}, {0xff16241c, 0xffe8f1ec},
    {0xff17181b, 0xfffbfcfb}, {0xff171a18, 0xfff8faf9},
    {0xff171b18, 0xfff2f5f3}, {0xff181c19, 0xffffffff},
    {0xff18221d, 0xffe1ebe5}, {0xff18241e, 0xffe6f0ea},
    {0xff19261f, 0xffe5eee8}, {0xff192a21, 0xffdcebe2},
    {0xff193728, 0xffd8ebdf}, {0xff1a211d, 0xffedf2ef},
    {0xff1a3527, 0xffd8eadf}, {0xff1b1f1c, 0xffffffff},
    {0xff1b211d, 0xfff0f5f2}, {0xff1c2d24, 0xffe2eee7},
    {0xff1c3a2c, 0xffdfede5}, {0xff1d3d2d, 0xffd9ece1},
    {0xff1f4733, 0xffd7eadf}, {0xff1f4a33, 0xffc9e4d5},
    {0xff202c25, 0xffe4ede8}, {0xff20382b, 0xffbacbc1},
    {0xff222f28, 0xffe8efeb},
    {0xff237349, 0xff237349}, {0xff24362d, 0xffccd7d0},
    {0xff263c31, 0xffc4d2ca}, {0xff288454, 0xff1d7042},
    {0xff294035, 0xffb7c9bf}, {0xff294637, 0xffaec5b7},
    {0xff2a4939, 0xffabc3b5}, {0xff2b4a39, 0xffa8c0b2},
    {0xff2bbf74, 0xff167b46}, {0xff2d5c43, 0xff8eb29e},
    {0xff2f4a3b, 0xffaabfb3}, {0xff2f9a62, 0xff2f9a62},
    {0xff315140, 0xff9fb8aa}, {0xff365344, 0xff91ad9d},
    {0xff397957, 0xff4e8a68}, {0xff39bd79, 0xff15824a},
    {0xff39d983, 0xff78d9a0}, {0xff3a5e4b, 0xff7d9f8c},
    {0xff3a614d, 0xff789886}, {0xff3b614d, 0xff789886},
    {0xff47715a, 0xff719580},
    {0xff496558, 0xff6a8376}, {0xff4f6257, 0xff829087},
    {0xff4f6555, 0xff7a897f}, {0xff4f6559, 0xff7b8981},
    {0xff4f7565, 0xff60766a}, {0xff522d2d, 0xfff8e2e2},
    {0xff52b58a, 0xff267c58}, {0xff55c6d6, 0xff167985},
    {0xff56cf8c, 0xff147a43}, {0xff56d995, 0xff168552},
    {0xff58b9e8, 0xff1674a0},
    {0xff5e7d6d, 0xff687f73}, {0xff633434, 0xfff1d3d3},
    {0xff6d8e7c, 0xff63766b}, {0xff6f8e7e, 0xff5f7569},
    {0xff6fcfc0, 0xff187d72}, {0xff789384, 0xff63766c},
    {0xff794141, 0xffd09a9a}, {0xff799a88, 0xff596f64},
    {0xff7ee0a8, 0xffb9ebcd}, {0xff7ee0b0, 0xff147747},
    {0xff83a693, 0xff536c5e}, {0xff8faf9e, 0xff526c5e},
    {0xff92b3a2, 0xff526b5e}, {0xff9a584c, 0xff9a584c},
    {0xff9ab0a4, 0xff667c70}, {0xff9b5656, 0xffc77b7b},
    {0xff9fbaac, 0xff4c6357}, {0xff9fbeae, 0xff4a6256},
    {0xffa3c4b3, 0xff405a4c}, {0xffa67adf, 0xff6b45a1},
    {0xffa6c7b8, 0xff3e574b},
    {0xffa8c3b5, 0xff40594c}, {0xffa9c8b8, 0xff3d5749},
    {0xffaac5b7, 0xff3e584a}, {0xffabc7b8, 0xff3e574b},
    {0xffb18172, 0xff9a584c}, {0xffb283e6, 0xff7041a6},
    {0xffb8d4c5, 0xff395348},
    {0xffb9d6c7, 0xff385348}, {0xffc7ddd1, 0xff31493d},
    {0xffc8dfd3, 0xff32483d}, {0xffc8e0d4, 0xff314b3e},
    {0xffcfe8dc, 0xff294236}, {0xffd5e9de, 0xff294036},
    {0xffd7eee2, 0xff20352a}, {0xffd9eee3, 0xff21372c},
    {0xffe1f4ea, 0xff13281d}, {0xffe3f5ec, 0xff182d22},
    {0xffe86671, 0xffb82f3d}, {0xffef6b73, 0xffc6323e},
    {0xffeffaf4, 0xffffffff}, {0xfff0a34a, 0xffa85e0d},
    {0xfff2d8d8, 0xff712d2d}
};

QColor mappedColor(const QColor &reference, AppTheme::Id theme) {
    if (theme == AppTheme::Id::Dark || !reference.isValid())
        return reference;
    const QRgb opaque = reference.rgb();
    const auto match = std::lower_bound(
        std::begin(lightColors), std::end(lightColors), opaque,
        [](const ColorPair &pair, QRgb value) { return pair.dark < value; });
    if (match != std::end(lightColors) && match->dark == opaque) {
        QColor result = QColor::fromRgb(match->light);
        result.setAlpha(reference.alpha());
        return result;
    }
    return reference;
}
} // namespace

namespace AppTheme {

Id current() { return activeTheme; }

Id fromKey(const QString &value) {
    return value.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0
               ? Id::Light
               : Id::Dark;
}

QString key(Id id) {
    return id == Id::Light ? QStringLiteral("light")
                           : QStringLiteral("dark");
}

QColor color(const QColor &darkReference) {
    return mappedColor(darkReference, activeTheme);
}

QPalette palette(Id id) {
    const auto themed = [id](const char *hex) {
        return mappedColor(QColor(QString::fromLatin1(hex)), id);
    };
    QPalette result;
    result.setColor(QPalette::Window, themed("#17181b"));
    result.setColor(QPalette::WindowText, themed("#a9c8b8"));
    result.setColor(QPalette::Base, themed("#17181b"));
    result.setColor(QPalette::AlternateBase, themed("#101113"));
    result.setColor(QPalette::Text, themed("#d7eee2"));
    result.setColor(QPalette::Button, themed("#121512"));
    result.setColor(QPalette::ButtonText, themed("#a8c3b5"));
    result.setColor(QPalette::Highlight, themed("#1f4a33"));
    result.setColor(QPalette::HighlightedText, themed("#d7eee2"));
    result.setColor(QPalette::Link, themed("#2bbf74"));
    result.setColor(QPalette::ToolTipBase, themed("#121512"));
    result.setColor(QPalette::ToolTipText, themed("#d7eee2"));
    result.setColor(QPalette::PlaceholderText, themed("#4f6555"));
    result.setColor(QPalette::Disabled, QPalette::Text,
                    themed("#4f6559"));
    result.setColor(QPalette::Disabled, QPalette::WindowText,
                    themed("#4f6559"));
    result.setColor(QPalette::Disabled, QPalette::ButtonText,
                    themed("#4f6559"));
    return result;
}

QString styleSheet(Id id) {
    QFile source(QStringLiteral(":/emerald.qss"));
    if (!source.open(QIODevice::ReadOnly))
        return {};
    const QString darkStyle = QString::fromUtf8(source.readAll());
    if (id == Id::Dark)
        return darkStyle;

    static const QRegularExpression hexColor(
        QStringLiteral("#[0-9a-fA-F]{6}"));
    QString result;
    result.reserve(darkStyle.size());
    qsizetype copied = 0;
    auto matches = hexColor.globalMatch(darkStyle);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        result += darkStyle.sliced(copied, match.capturedStart() - copied);
        result += mappedColor(QColor(match.captured()), id).name();
        copied = match.capturedEnd();
    }
    result += darkStyle.sliced(copied);
    result.replace(QStringLiteral("rgba(16, 17, 19, 205)"),
                   QStringLiteral("rgba(255, 255, 255, 225)"));
    return result;
}

void apply(QApplication &application, Id id) {
    activeTheme = id;
    application.setPalette(palette(id));
    application.setStyleSheet(styleSheet(id));
}

} // namespace AppTheme
