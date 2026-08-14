#pragma once

#include "AppTheme.h"

#include <QColor>
#include <QTextFormat>

// Shared visual tokens for Markdown constructs rendered by both the live
// editor highlighter and the presentation-only Read Mode document.
namespace MarkdownStyle {
inline QColor highlightForeground() {
    return AppTheme::color(QColor(0x10, 0x18, 0x14));
}
inline QColor highlightBackground() {
    return AppTheme::color(QColor(0x7e, 0xe0, 0xa8));
}

// Presentation metadata shared by the live editor and Read Mode. A nested
// list row points at the rendered block of its nearest preceding shallower
// item. Keeping this on the block format makes hierarchy lookup and fold hit
// testing constant-time instead of repeatedly walking backwards through a
// potentially very large note.
inline constexpr int CalloutTypeProperty = QTextFormat::UserProperty + 520;
inline constexpr int CalloutDepthProperty = QTextFormat::UserProperty + 521;
inline constexpr int CalloutTitleProperty = QTextFormat::UserProperty + 522;
inline constexpr int ListDepthProperty = QTextFormat::UserProperty + 523;
inline constexpr int ListParentBlockProperty = QTextFormat::UserProperty + 524;
} // namespace MarkdownStyle
