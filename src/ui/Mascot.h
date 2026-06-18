#pragma once

#include <QWidget>

// A small, procedurally generated, *layered* creature drawn entirely with
// QPainter from a single 64-bit seed. Each note gets one (seeded from its
// title + text) so the note has a memorable visual identity. The seed alone
// determines every trait — palette, body, ears, eyes, accessory — so the same
// seed always re-creates the same creature (it survives restarts via the
// stored seed; no image is saved).
//
// Purely decorative: it floats in the corner and lets mouse events through to
// the editor beneath. Generating/removing is driven from the menu, not here.
class Mascot : public QWidget {
public:
    explicit Mascot(QWidget *parent = nullptr);

    // 0 hides the widget; any other value draws the matching creature.
    void setSeed(quint64 seed);
    quint64 seed() const { return m_seed; }

    // Derive a creature seed from a note's generation inputs (title + body).
    static quint64 seedFor(const QString &title, const QString &body);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    quint64 m_seed = 0;
};
