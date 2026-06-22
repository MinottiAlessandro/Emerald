#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>

class QPainter;
class QRectF;

// Where a body's overlays mount, in the 100x110 mascot canvas (see
// docs/MASCOT_ART_SPEC.md). Hand-drawn bodies don't follow the procedural
// geometry, so each body ships its own anchor record in art/anchors.json and
// the compositor places ears/eyes/tail/wings at these points.
struct BodyAnchors {
    QPointF face{50, 50};    // centre of the eye/mouth cluster
    QPointF topper{50, 22};  // bottom-centre of ears/hats/horns
    QPointF tail{72, 70};    // where the tail attaches (lower side)
    QPointF back{50, 46};    // centre of wings/cape, drawn behind the body
    QPointF ground{50, 96};  // feet baseline
    double eyeGap = 11.0;    // half the distance between the two eyes
    bool valid = false;      // false when no record exists for the body
};

// Loads hand-drawn SVG mascot parts and their anchor manifest from a layered
// set of roots: the built-in art baked into the binary (qrc ":/art") plus an
// optional user folder, so end users can add or override parts — and, with a
// recipe, whole creatures — by dropping files, no rebuild. A part is an SVG at
// <root>/<slot>/<name>.svg authored on the 100x110 canvas. User roots win over
// the built-in, and a missing part is simply "not found" so the renderer falls
// back to the procedural drawing. One shared instance, loaded lazily.
class MascotCatalog {
public:
    static MascotCatalog &shared();

    // True when an SVG for this slot/name exists in any root.
    bool hasPart(const QString &slot, const QString &name) const;

    // Render the part into `target` (in the painter's current coordinates),
    // recolouring every element tagged data-slot="X" with tint.value("X")
    // (alpha honoured). A no-op when the part is absent.
    void renderPart(QPainter &p, const QString &slot, const QString &name,
                    const QRectF &target, const QHash<QString, QColor> &tint) const;

    // Anchor record for a body name (built-in overlaid by user roots);
    // .valid == false when no record exists.
    BodyAnchors anchors(const QString &body) const;

    // Names of user-defined whole creatures, discovered as folders under any
    // root's creatures/ holding their layer SVGs (creatures/<kind>/body.svg,
    // …). Sorted + de-duplicated so the set is stable across runs and machines,
    // which is what lets generation roll one deterministically. The layer SVGs
    // are then read with hasPart/renderPart using slot "creatures/<kind>".
    QStringList kinds() const;

    // True when creature `kind` has art (at least a body) on this machine.
    bool hasKind(const QString &kind) const;

    // Prepend a user art root (a folder holding <slot>/<name>.svg and an
    // optional anchors.json). Highest priority; idempotent.
    void addRoot(const QString &dir);

private:
    MascotCatalog();
    void loadAnchors();
    QByteArray rawPart(const QString &slot, const QString &name) const;

    QStringList m_roots; // search order, highest priority first; ":/art" last
    mutable QHash<QString, QByteArray> m_rawCache; // "slot/name" -> svg ("" miss)
    QHash<QString, BodyAnchors> m_anchors;
    bool m_anchorsLoaded = false;
    mutable QStringList m_kinds;       // discovered user creatures, sorted
    mutable bool m_kindsLoaded = false;
};
