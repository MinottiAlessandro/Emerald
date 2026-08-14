#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

class QApplication;

// Application-wide visual theme. The existing dark colors remain the
// canonical design references; color() maps those references to the active
// palette so custom-painted Markdown and graph elements stay in step with QSS.
namespace AppTheme {

enum class Id { Dark, Light };

Id current();
Id fromKey(const QString &key);
QString key(Id id);

QColor color(const QColor &darkReference);
QPalette palette(Id id);
QString styleSheet(Id id);
void apply(QApplication &application, Id id);

} // namespace AppTheme
