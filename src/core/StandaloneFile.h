#pragma once

#include <QByteArray>
#include <QString>

// Serialization for a Markdown document opened outside a vault. Standalone
// files keep their original UTF-8 BOM and line-ending convention, and saves are
// atomic so an interrupted write cannot leave an arbitrary user file truncated.
namespace StandaloneFile {

struct Document {
    QString path;
    QString content;
    QByteArray lineEnding = QByteArrayLiteral("\n");
    bool utf8Bom = false;
};

bool hasMarkdownExtension(const QString &path);
bool load(const QString &path, Document *document, QString *error = nullptr);
bool save(const Document &document, const QString &content,
          QString *error = nullptr);

} // namespace StandaloneFile
