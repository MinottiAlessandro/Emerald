#include "MascotCatalog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRectF>
#include <QSet>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

// Rewrite every element tagged data-slot="X" so its fill becomes tint["X"]
// (splitting alpha into fill-opacity, which SVG Tiny understands). Namespace
// processing is off so element/attribute names round-trip verbatim — we only
// touch fill/fill-opacity on tagged elements and pass everything else through.
QByteArray tintSvg(const QByteArray &svg, const QHash<QString, QColor> &tint) {
    QXmlStreamReader r(svg);
    r.setNamespaceProcessing(false);
    QByteArray out;
    QXmlStreamWriter w(&out);
    while (!r.atEnd()) {
        switch (r.readNext()) {
        case QXmlStreamReader::StartDocument:
            w.writeStartDocument();
            break;
        case QXmlStreamReader::EndDocument:
            w.writeEndDocument();
            break;
        case QXmlStreamReader::StartElement: {
            w.writeStartElement(r.qualifiedName().toString());
            const QString slot = r.attributes().value(QLatin1String("data-slot")).toString();
            const QColor c = slot.isEmpty() ? QColor() : tint.value(slot);
            for (const QXmlStreamAttribute &a : r.attributes()) {
                const QString n = a.qualifiedName().toString();
                if (c.isValid() && (n == QLatin1String("fill") ||
                                    n == QLatin1String("fill-opacity")))
                    continue; // replaced below
                w.writeAttribute(n, a.value().toString());
            }
            if (c.isValid()) {
                w.writeAttribute(QStringLiteral("fill"), c.name(QColor::HexRgb));
                if (c.alphaF() < 1.0)
                    w.writeAttribute(QStringLiteral("fill-opacity"),
                                     QString::number(c.alphaF(), 'g', 3));
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            w.writeEndElement();
            break;
        case QXmlStreamReader::Characters:
            w.writeCharacters(r.text().toString());
            break;
        default:
            break; // drop comments / PIs — irrelevant to rendering
        }
    }
    return out;
}

QPointF pt(const QJsonValue &v, QPointF fallback) {
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : fallback;
}

} // namespace

MascotCatalog &MascotCatalog::shared() {
    static MascotCatalog c;
    return c;
}

MascotCatalog::MascotCatalog() {
    m_roots << QStringLiteral(":/art"); // built-in, lowest priority
    // Per-user art folder, available across every vault. Users drop
    // <slot>/<name>.svg (and an anchors.json) here to add or override parts.
    const QString user =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!user.isEmpty())
        addRoot(user + QStringLiteral("/mascots"));
}

void MascotCatalog::addRoot(const QString &dir) {
    if (dir.isEmpty() || m_roots.contains(dir))
        return;
    m_roots.prepend(dir); // user roots win over the built-in
    // A new root can satisfy lookups that previously missed, so drop the caches.
    m_rawCache.clear();
    m_anchors.clear();
    m_anchorsLoaded = false;
    m_kinds.clear();
    m_kindsLoaded = false;
}

QStringList MascotCatalog::kinds() const {
    if (m_kindsLoaded)
        return m_kinds;
    m_kindsLoaded = true;
    QSet<QString> seen;
    for (const QString &root : m_roots) {
        QDir dir(root + QStringLiteral("/creatures"));
        const QStringList subs =
            dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : subs)
            if (QFile::exists(dir.filePath(name) + QStringLiteral("/body.svg")))
                seen.insert(name); // a creature must at least have a body
    }
    m_kinds = QStringList(seen.cbegin(), seen.cend());
    m_kinds.sort(); // stable order -> deterministic generation across machines
    return m_kinds;
}

bool MascotCatalog::hasKind(const QString &kind) const {
    return !kind.isEmpty() && kinds().contains(kind);
}

QByteArray MascotCatalog::rawPart(const QString &slot, const QString &name) const {
    const QString key = slot + QLatin1Char('/') + name;
    const auto it = m_rawCache.constFind(key);
    if (it != m_rawCache.constEnd())
        return *it;
    QByteArray bytes;
    for (const QString &root : m_roots) {
        QFile f(root + QLatin1Char('/') + key + QStringLiteral(".svg"));
        if (f.open(QIODevice::ReadOnly)) {
            bytes = f.readAll();
            break;
        }
    }
    m_rawCache.insert(key, bytes); // cache misses too (empty)
    return bytes;
}

bool MascotCatalog::hasPart(const QString &slot, const QString &name) const {
    return !rawPart(slot, name).isEmpty();
}

void MascotCatalog::renderPart(QPainter &p, const QString &slot,
                               const QString &name, const QRectF &target,
                               const QHash<QString, QColor> &tint) const {
    const QByteArray raw = rawPart(slot, name);
    if (raw.isEmpty())
        return;
    QSvgRenderer r(tintSvg(raw, tint));
    if (r.isValid())
        r.render(&p, target);
}

void MascotCatalog::loadAnchors() {
    m_anchorsLoaded = true;
    // Built-in first, then user roots override matching bodies.
    for (auto it = m_roots.crbegin(); it != m_roots.crend(); ++it) {
        QFile f(*it + QStringLiteral("/anchors.json"));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject bodies =
            QJsonDocument::fromJson(f.readAll()).object().value("bodies").toObject();
        for (auto b = bodies.constBegin(); b != bodies.constEnd(); ++b) {
            const QJsonObject o = b.value().toObject();
            BodyAnchors a;
            a.valid = true;
            a.face = pt(o.value("face"), a.face);
            a.topper = pt(o.value("topper"), a.topper);
            a.tail = pt(o.value("tail"), a.tail);
            a.back = pt(o.value("back"), a.back);
            a.ground = pt(o.value("ground"), a.ground);
            a.eyeGap = o.value("eyeGap").toDouble(a.eyeGap);
            m_anchors.insert(b.key(), a);
        }
    }
}

BodyAnchors MascotCatalog::anchors(const QString &body) const {
    if (!m_anchorsLoaded)
        const_cast<MascotCatalog *>(this)->loadAnchors();
    return m_anchors.value(body);
}
