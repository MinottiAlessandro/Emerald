#pragma once

#include <QColor>

// Shared visual tokens for Markdown constructs rendered by both the live
// editor highlighter and the presentation-only Read Mode document.
namespace MarkdownStyle {
inline QColor highlightForeground() { return QColor(0x10, 0x18, 0x14); }
inline QColor highlightBackground() { return QColor(0x7e, 0xe0, 0xa8); }
} // namespace MarkdownStyle
