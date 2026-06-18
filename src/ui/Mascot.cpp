#include "Mascot.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <random>

Mascot::Mascot(QWidget *parent) : QWidget(parent) {
    // The global stylesheet paints every QWidget with the app background; this
    // objectName lets the QSS keep the mascot transparent so only the creature
    // shows over the editor.
    setObjectName(QStringLiteral("mascot"));
    // Decorative only — never steal clicks/scroll from the editor underneath.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // The creature is drawn on a 100x110 logical canvas; this size keeps a
    // crisp ~1.3x scale on a normal display while staying out of the way.
    setFixedSize(132, 146);
    hide();
}

quint64 Mascot::seedFor(const QString &title, const QString &body) {
    // Mix two differently-seeded hashes of (title + body) into 64 bits so
    // similar notes still land on visibly different creatures. The exact value
    // doesn't matter — only that it's stable for the same input.
    const QString s = title + QChar('\n') + body;
    const quint64 hi = qHash(s, 0x9e3779b97f4a7c15ULL);
    const quint64 lo = qHash(s, 0x2545f4914f6cdd1dULL);
    quint64 seed = (hi << 32) ^ lo ^ (hi >> 16);
    return seed ? seed : 0x1ULL; // never 0 (that's the "no creature" sentinel)
}

void Mascot::setSeed(quint64 seed) {
    if (seed == m_seed)
        return;
    m_seed = seed;
    setVisible(seed != 0);
    update();
}

void Mascot::paintEvent(QPaintEvent *) {
    if (m_seed == 0)
        return;

    std::mt19937_64 rng(m_seed);
    auto uni = [&](double a, double b) {
        return std::uniform_real_distribution<double>(a, b)(rng);
    };
    auto pick = [&](int n) {
        return int(std::uniform_int_distribution<int>(0, n - 1)(rng));
    };
    auto chance = [&](double p) { return uni(0.0, 1.0) < p; };
    auto hsv = [](double h, double s, double v, double a = 1.0) {
        QColor c = QColor::fromHsvF(h - std::floor(h), qBound(0.0, s, 1.0),
                                    qBound(0.0, v, 1.0));
        c.setAlphaF(a);
        return c;
    };

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Map the 100 x 110 logical canvas into the widget, centered, with margin.
    const double pad = 6.0;
    const double s = qMin((width() - 2 * pad) / 100.0,
                          (height() - 2 * pad) / 110.0);
    p.translate((width() - 100 * s) / 2.0, (height() - 110 * s) / 2.0);
    p.scale(s, s);

    // --- Palette (seeded hue, tuned to read on Emerald's dark backdrop) ----
    const double hue = uni(0.0, 1.0);
    const double sat = uni(0.42, 0.62);
    const double val = uni(0.66, 0.82);
    const QColor body = hsv(hue, sat, val);
    const QColor bodyEdge = hsv(hue, qMin(sat + 0.15, 0.9), val * 0.7);
    const QColor belly = hsv(hue, sat * 0.35, qMin(val + 0.16, 0.97));
    // Ear insides / accessories: complementary half the time, else analogous.
    const double accHue = hue + (chance(0.5) ? 0.5 : uni(0.06, 0.12));
    const QColor accent = hsv(accHue, 0.55, 0.82);
    const QColor pink = hsv(0.97, 0.45, 1.0);

    const double cx = 50.0;
    const double groundY = 96.0;

    // Body proportions: round / tall-egg / wide.
    double bw = 60, bh = 60;
    switch (pick(3)) {
    case 0: bw = 60; bh = 58; break; // round
    case 1: bw = 52; bh = 66; break; // tall
    case 2: bw = 66; bh = 52; break; // wide
    }
    const double bodyTop = groundY - bh;
    const double bcy = groundY - bh / 2.0;
    const QRectF bodyRect(cx - bw / 2, bodyTop, bw, bh);

    QPen edgePen(bodyEdge, 1.6);
    edgePen.setJoinStyle(Qt::RoundJoin);

    // ---- Layer 1: ears / horns (behind the body so they tuck into it) -----
    const int earType = pick(5);
    const double earDX = bw * 0.30;
    auto drawEar = [&](double sign) {
        const double ex = cx + sign * earDX;
        p.setPen(edgePen);
        if (earType == 0) { // upright cat triangle, rounded
            QPainterPath path;
            path.moveTo(ex - 9, bodyTop + 12);
            path.quadTo(ex - 2, bodyTop - 18, ex + 8, bodyTop + 6);
            path.closeSubpath();
            p.setBrush(body);
            p.drawPath(path);
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            QPainterPath in;
            in.moveTo(ex - 4, bodyTop + 6);
            in.quadTo(ex - 1, bodyTop - 8, ex + 4, bodyTop + 3);
            in.closeSubpath();
            p.drawPath(in);
        } else if (earType == 1) { // round bear ears
            p.setBrush(body);
            p.drawEllipse(QPointF(ex, bodyTop + 4), 11, 11);
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawEllipse(QPointF(ex, bodyTop + 5), 6, 6);
        } else if (earType == 2) { // tall bunny ears
            p.setBrush(body);
            p.drawEllipse(QRectF(ex - 6, bodyTop - 30, 12, 40));
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawEllipse(QRectF(ex - 3, bodyTop - 25, 6, 30));
        } else if (earType == 3) { // little horns
            p.setBrush(accent);
            QPainterPath horn;
            horn.moveTo(ex - 4, bodyTop + 6);
            horn.quadTo(ex - 1, bodyTop - 12, ex + 4, bodyTop + 4);
            horn.closeSubpath();
            p.drawPath(horn);
        }
        // earType 4: none.
    };
    drawEar(-1);
    drawEar(+1);

    // ---- Layer 2: tail (one side, behind the body) ------------------------
    const bool hasTail = chance(0.6);
    if (hasTail) {
        const double sgn = chance(0.5) ? 1.0 : -1.0;
        p.setPen(edgePen);
        p.setBrush(body);
        QPainterPath tail;
        const double tx = cx + sgn * bw * 0.48;
        tail.moveTo(tx, groundY - 14);
        tail.quadTo(tx + sgn * 20, groundY - 22, tx + sgn * 10, groundY - 2);
        tail.quadTo(tx + sgn * 2, groundY - 4, tx, groundY - 14);
        p.drawPath(tail);
    }

    // ---- Layer 3: shadow + body ------------------------------------------
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 55));
    p.drawEllipse(QPointF(cx, groundY + 2), bw * 0.46, 5.5);

    p.setPen(edgePen);
    p.setBrush(body);
    const double r = qMin(bw, bh) * 0.46;
    p.drawRoundedRect(bodyRect, r, r);

    // Feet peeking out at the base.
    p.setBrush(bodyEdge);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(cx - bw * 0.20, groundY - 2), 7, 4.5);
    p.drawEllipse(QPointF(cx + bw * 0.20, groundY - 2), 7, 4.5);

    // ---- Layer 4: belly patch --------------------------------------------
    p.setBrush(belly);
    p.drawEllipse(QRectF(cx - bw * 0.26, bcy - bh * 0.02, bw * 0.52, bh * 0.5));

    // ---- Layer 5: optional spots -----------------------------------------
    if (chance(0.35)) {
        p.setBrush(hsv(hue, sat, val * 0.82, 0.7));
        const int spots = 2 + pick(3);
        for (int i = 0; i < spots; ++i) {
            const double a = uni(0, 2 * M_PI), rad = uni(2, bw * 0.22);
            p.drawEllipse(QPointF(cx + std::cos(a) * rad,
                                  bcy - bh * 0.18 + std::sin(a) * rad * 0.6),
                          uni(2.0, 4.0), uni(2.0, 4.0));
        }
    }

    // ---- Layer 6: face ----------------------------------------------------
    const double eyeY = bcy - bh * 0.06;
    const double eyeDX = bw * (0.18 + uni(0.0, 0.04));
    const int eyeType = pick(4);

    // Cheeks (blush) behind the eyes.
    if (chance(0.55)) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(pink.red(), pink.green(), pink.blue(), 120));
        p.drawEllipse(QPointF(cx - eyeDX - 5, eyeY + 7), 5, 3.2);
        p.drawEllipse(QPointF(cx + eyeDX + 5, eyeY + 7), 5, 3.2);
    }

    auto drawEye = [&](double sign) {
        const double x = cx + sign * eyeDX;
        if (eyeType == 0) { // simple dot
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x20, 0x20, 0x24));
            p.drawEllipse(QPointF(x, eyeY), 3.4, 3.8);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x - 1, eyeY - 1.3), 1.1, 1.1);
        } else if (eyeType == 1) { // big sparkly
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x, eyeY), 5, 5.6);
            p.setBrush(QColor(0x22, 0x22, 0x28));
            p.drawEllipse(QPointF(x, eyeY + 0.6), 3, 3.4);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x - 1.1, eyeY - 1.1), 1.3, 1.3);
        } else if (eyeType == 2) { // sleepy downward arc
            QPen ep(QColor(0x20, 0x20, 0x24), 1.8);
            ep.setCapStyle(Qt::RoundCap);
            p.setPen(ep);
            p.setBrush(Qt::NoBrush);
            QPainterPath arc;
            arc.moveTo(x - 3.5, eyeY);
            arc.quadTo(x, eyeY + 3.2, x + 3.5, eyeY);
            p.drawPath(arc);
        } else { // tall oval
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x20, 0x20, 0x24));
            p.drawEllipse(QPointF(x, eyeY), 2.6, 4.2);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x - 0.8, eyeY - 1.6), 0.9, 0.9);
        }
    };
    drawEye(-1);
    drawEye(+1);

    // Nose + mouth.
    const double noseY = eyeY + 7.5;
    p.setPen(Qt::NoPen);
    p.setBrush(chance(0.5) ? pink : bodyEdge);
    p.drawEllipse(QPointF(cx, noseY), 2.2, 1.6);

    QPen mouthPen(QColor(0x2a, 0x2a, 0x2e), 1.4);
    mouthPen.setCapStyle(Qt::RoundCap);
    p.setPen(mouthPen);
    p.setBrush(Qt::NoBrush);
    QPainterPath mouth;
    switch (pick(3)) {
    case 0: // gentle smile
        mouth.moveTo(cx - 4, noseY + 2);
        mouth.quadTo(cx, noseY + 5.5, cx + 4, noseY + 2);
        break;
    case 1: // cat :3
        mouth.moveTo(cx - 5, noseY + 2);
        mouth.quadTo(cx - 2.5, noseY + 4.5, cx, noseY + 2);
        mouth.quadTo(cx + 2.5, noseY + 4.5, cx + 5, noseY + 2);
        break;
    case 2: // small open o
        p.setBrush(QColor(0x2a, 0x2a, 0x2e));
        p.drawEllipse(QPointF(cx, noseY + 3.5), 1.8, 2.2);
        break;
    }
    if (!mouth.isEmpty())
        p.drawPath(mouth);

    // ---- Layer 7: head-top accessory -------------------------------------
    const int acc = pick(4);
    if (acc == 0) { // leaf sprout (nods to Emerald)
        p.setPen(Qt::NoPen);
        p.setBrush(hsv(0.38, 0.55, 0.7));
        const double sx = cx, sy = bodyTop + 2;
        QPainterPath l1;
        l1.moveTo(sx, sy);
        l1.quadTo(sx - 9, sy - 6, sx - 12, sy - 13);
        l1.quadTo(sx - 4, sy - 11, sx, sy);
        QPainterPath l2;
        l2.moveTo(sx, sy);
        l2.quadTo(sx + 9, sy - 6, sx + 12, sy - 13);
        l2.quadTo(sx + 4, sy - 11, sx, sy);
        p.drawPath(l1);
        p.drawPath(l2);
    } else if (acc == 1) { // antenna with a dot
        QPen ap(accent, 1.4);
        ap.setCapStyle(Qt::RoundCap);
        p.setPen(ap);
        p.drawLine(QPointF(cx, bodyTop + 4), QPointF(cx + 4, bodyTop - 8));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(QPointF(cx + 4, bodyTop - 9), 2.4, 2.4);
    } else if (acc == 2) { // a little hair tuft
        QPen tp(bodyEdge, 1.6);
        tp.setCapStyle(Qt::RoundCap);
        p.setPen(tp);
        for (int i = -1; i <= 1; ++i)
            p.drawLine(QPointF(cx + i * 3, bodyTop + 5),
                       QPointF(cx + i * 4.5, bodyTop - 6));
    }
    // acc 3: bald.

    // ---- Layer 8: glossy highlight ---------------------------------------
    p.setPen(Qt::NoPen);
    QColor sheen = Qt::white;
    sheen.setAlphaF(0.16);
    p.setBrush(sheen);
    p.drawEllipse(QRectF(cx - bw * 0.30, bodyTop + bh * 0.10,
                         bw * 0.34, bh * 0.26));
}
