#pragma once

#include <QWidget>

class QPixmap;
class QTimer;
class QEnterEvent;
class QMouseEvent;

// A small, procedurally generated, *layered* creature drawn entirely with
// QPainter from a single 64-bit seed. Each note gets one (seeded from its
// title + text) so the note has a memorable visual identity. The seed alone
// determines every trait — archetype, palette, body, ears, eyes, accessory —
// so the same seed always re-creates the same creature (it survives restarts
// via the stored seed; no image is saved).
//
// It sits in the app's corner. Hovering plays a gentle idle bob + blink;
// clicking it opens the vault's mascot gallery (the clicked() signal).
class Mascot : public QWidget {
    Q_OBJECT
public:
    explicit Mascot(QWidget *parent = nullptr);

    // Show the creature for `seed` (0 hides the widget). `kind` names a
    // user-defined creature to draw instead of the built-in one the seed picks;
    // empty (or unknown on this machine) falls back to the built-in creature.
    void setMascot(quint64 seed, const QString &kind = QString());
    quint64 seed() const { return m_seed; }

    // Derive a creature seed from a note's generation inputs (title + body).
    static quint64 seedFor(const QString &title, const QString &body);

    // At generation, decide whether this seed rolls a user-defined creature (one
    // discovered in the mascots folder) instead of a built-in. Returns its kind
    // name, or empty for a built-in. Independent of the built-in trait roll, and
    // stable for a given seed + discovered set — call it once and store the
    // result in the note's seed line so the choice travels with the note.
    static QString kindForSeed(quint64 seed);

    // Paint the creature for `seed` (or user creature `kind`) into a transparent
    // pixmap — used by the gallery to show every note's mascot without live
    // widgets.
    static QPixmap renderPixmap(quint64 seed, const QString &kind, QSize size);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    quint64 m_seed = 0;
    QString m_kind;           // user-creature name, or empty for a built-in
    QTimer *m_idle = nullptr; // drives the hover bob/blink animation
    int m_tick = 0;           // animation phase, advanced while hovered
    bool m_hovered = false;
};
