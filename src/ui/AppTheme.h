#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QPalette>
#include <QString>

class QApplication;

// Application-wide visual theme. The existing dark colors remain the
// canonical design references; color() maps those references to the active
// palette so custom-painted Markdown and graph elements stay in step with QSS.
namespace AppTheme {

enum class Id { Dark, Light };

struct ColorRole {
    QString key;
    QString label;
    QColor darkReference;
};

struct CustomTheme {
    QString key;
    QString name;
    Id base = Id::Dark;
    QMap<QString, QColor> colors;

    bool isValid() const { return !key.isEmpty() && !name.trimmed().isEmpty(); }
};

Id current();
QString currentKey();
Id fromKey(const QString &key);
QString key(Id id);
bool isAvailable(const QString &key);
bool isCustom(const QString &key);
QString displayName(const QString &key);

QList<ColorRole> colorRoles();
QList<CustomTheme> customThemes();
CustomTheme customTheme(const QString &key);
CustomTheme makeCustomTheme(const QString &name, const QString &basedOnKey);
void saveCustomTheme(const CustomTheme &theme);
bool deleteCustomTheme(const QString &key);

QColor color(const QColor &darkReference);
QPalette palette(Id id);
QPalette palette(const QString &key);
QString styleSheet(Id id);
QString styleSheet(const QString &key);
void apply(QApplication &application, Id id);
void apply(QApplication &application, const QString &key);

} // namespace AppTheme
