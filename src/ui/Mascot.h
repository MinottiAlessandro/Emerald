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

    // 0 hides the widget; any other value draws the matching creature.
    void setSeed(quint64 seed);
    quint64 seed() const { return m_seed; }

    // Derive a creature seed from a note's generation inputs (title + body).
    static quint64 seedFor(const QString &title, const QString &body);

    // Paint the creature for `seed` into a transparent pixmap — used by the
    // gallery to show every note's mascot without live widgets.
    static QPixmap renderPixmap(quint64 seed, QSize size);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    quint64 m_seed = 0;
    QTimer *m_idle = nullptr; // drives the hover bob/blink animation
    int m_tick = 0;           // animation phase, advanced while hovered
    bool m_hovered = false;
};
