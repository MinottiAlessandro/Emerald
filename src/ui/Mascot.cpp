#include "Mascot.h"

#include "MascotCatalog.h"
#include <QEnterEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QPolygonF>
#include <QSettings>
#include <QTimer>
#include <QtMath>
#include <random>

// ---------------------------------------------------------------------------
// The creature is assembled from a small library of interchangeable parts
// (body shape, head-topper, wings/back, eyes, mouth, tail, pattern, palette).
// A seed first picks one of many *archetypes* — normal animals, mythological
// creatures, and the odd object — each of which is just a preset that selects
// parts (with some variation left to chance). Everything is drawn with QPainter
// on a 100 x 110 logical canvas, in back-to-front layers.
// ---------------------------------------------------------------------------
namespace {

enum Topper { TopNone, TopCatEars, TopBearEars, TopBunnyEars, TopMouseEars,
              TopHornsSmall, TopHornsCurved, TopUnicorn, TopAntlers, TopHalo,
              TopCrown, TopWizardHat, TopAntennae, TopLeaf, TopFlame, TopTuft,
              TopTopHat, TopSpikes };
enum Back { BackNone, BackBatWings, BackFeatherWings, BackFairyWings, BackCape };
enum Eyes { EyeDot, EyeSparkle, EyeSleepy, EyeOval, EyeHappy, EyeAngry,
            EyeCyclops, EyeStar, EyeWink };
enum Mouth { MouthSmile, MouthCat, MouthOpen, MouthFang, MouthBeak, MouthGrin,
             MouthFlat };
enum BodyShape { BodyRound, BodyTall, BodyWide, BodySlime, BodyGhost,
                 BodyBottle, BodyStar, BodyGem, BodyMushroom, BodyRobot,
                 BodyOcto, BodyJelly, BodyCactus, BodySnowman };
enum Tail { TailNone, TailCat, TailBushy, TailDevil, TailFish, TailThin,
            TailFluffy };
enum Pattern { PatNone, PatSpots, PatStripes, PatScales };
enum PalMode { PalNormal, PalFiery, PalGhostly, PalMetallic, PalPastel };

enum Arche { A_Cat, A_Fox, A_Bear, A_Bunny, A_Mouse, A_Frog, A_Owl, A_Penguin,
             A_Fish, A_Deer, A_Dragon, A_Unicorn, A_Demon, A_Angel, A_Phoenix,
             A_Ghost, A_Slime, A_Golem, A_Fairy, A_Griffin, A_Cyclops, A_Robot,
             A_Mushroom, A_Potion, A_Star, A_Crystal, A_Dino, A_Octopus,
             A_Jellyfish, A_Snowman, A_Cactus, A_GhostCat, A_Bat, A_Bee,
             A_COUNT };

struct Traits {
    int body = BodyRound, topper = TopNone, back = BackNone, eyes = EyeDot;
    int mouth = MouthSmile, tail = TailNone, pattern = PatNone, pal = PalNormal;
    bool cheeks = false, feet = true, belly = true, gem = false;
    bool sparkles = false, whiskers = false, carrot = false;
    double hue = 0.0;
};

struct RNG {
    std::mt19937_64 &r;
    double uni(double a, double b) {
        return std::uniform_real_distribution<double>(a, b)(r);
    }
    int pick(int n) { return int(std::uniform_int_distribution<int>(0, n - 1)(r)); }
    bool chance(double p) { return uni(0.0, 1.0) < p; }
};

Traits rollTraits(RNG &g) {
    Traits t;
    t.hue = g.uni(0.0, 1.0);
    switch (g.pick(A_COUNT)) {
    case A_Cat:
        t.topper = TopCatEars; t.eyes = g.chance(.5) ? EyeSparkle : EyeDot;
        t.mouth = MouthCat; t.tail = TailCat; t.whiskers = true;
        t.cheeks = g.chance(.6); break;
    case A_Fox:
        t.topper = TopCatEars; t.tail = TailBushy; t.mouth = MouthSmile;
        t.hue = g.uni(0.03, 0.09); t.cheeks = g.chance(.4); break;
    case A_Bear:
        t.topper = TopBearEars; t.body = g.chance(.5) ? BodyWide : BodyRound;
        t.mouth = MouthSmile; if (g.chance(.6)) t.hue = g.uni(0.05, 0.10); break;
    case A_Bunny:
        t.topper = TopBunnyEars; t.tail = TailFluffy; t.cheeks = true;
        t.eyes = g.chance(.5) ? EyeSparkle : EyeDot; break;
    case A_Mouse:
        t.topper = TopMouseEars; t.tail = TailThin; t.pal = PalMetallic;
        t.cheeks = g.chance(.5); break;
    case A_Frog:
        t.body = BodyWide; t.eyes = EyeSparkle; t.mouth = MouthGrin;
        t.hue = g.uni(0.28, 0.40); break;
    case A_Owl:
        t.topper = TopTuft; t.mouth = MouthBeak; t.eyes = EyeSparkle;
        t.back = g.chance(.5) ? BackFeatherWings : BackNone; t.pattern = PatSpots;
        t.feet = false; t.hue = g.uni(0.06, 0.12); break;
    case A_Penguin:
        t.body = BodyTall; t.mouth = MouthBeak; t.hue = g.uni(0.58, 0.66); break;
    case A_Fish:
        t.body = BodyWide; t.tail = TailFish; t.mouth = MouthOpen;
        t.eyes = EyeSparkle; t.feet = false; t.pattern = PatScales;
        t.hue = g.uni(0.48, 0.62); break;
    case A_Deer:
        t.topper = TopAntlers; t.eyes = g.chance(.5) ? EyeSleepy : EyeDot;
        t.tail = TailFluffy; t.hue = g.uni(0.06, 0.10); t.cheeks = g.chance(.4);
        break;
    case A_Dragon:
        t.topper = g.chance(.5) ? TopHornsCurved : TopHornsSmall;
        t.back = BackBatWings; t.mouth = MouthFang; t.tail = TailFish;
        t.pattern = PatScales; t.gem = g.chance(.3);
        t.hue = g.chance(.5) ? g.uni(0.28, 0.42) : g.uni(0.0, 0.04); break;
    case A_Unicorn:
        t.topper = TopUnicorn; t.tail = TailFluffy; t.eyes = EyeSparkle;
        t.sparkles = true; t.gem = true; t.pal = PalPastel; break;
    case A_Demon:
        t.topper = TopHornsCurved; t.tail = TailDevil; t.mouth = MouthFang;
        t.eyes = EyeAngry; t.hue = g.uni(0.0, 0.03); break;
    case A_Angel:
        t.topper = TopHalo; t.back = BackFeatherWings; t.eyes = EyeHappy;
        t.cheeks = true; t.pal = PalPastel; break;
    case A_Phoenix:
        t.topper = TopFlame; t.back = BackFeatherWings; t.mouth = MouthBeak;
        t.eyes = EyeSparkle; t.tail = TailFish; t.pal = PalFiery;
        t.hue = g.uni(0.02, 0.08); break;
    case A_Ghost:
        t.body = BodyGhost; t.eyes = g.chance(.5) ? EyeSleepy : EyeOval;
        t.mouth = MouthOpen; t.pal = PalGhostly; t.feet = false;
        t.belly = false; break;
    case A_Slime:
        t.body = BodySlime; t.topper = g.chance(.4) ? TopAntennae : TopNone;
        t.eyes = EyeDot; t.feet = false; t.belly = false;
        t.pal = g.chance(.5) ? PalGhostly : PalNormal; break;
    case A_Golem:
        t.body = g.chance(.5) ? BodyGem : BodyRound; t.gem = true;
        t.pal = PalMetallic; t.belly = false; t.hue = g.uni(0.05, 0.5); break;
    case A_Fairy:
        t.back = BackFairyWings; t.eyes = EyeSparkle; t.sparkles = true;
        t.topper = g.chance(.5) ? TopAntennae : TopNone; t.pal = PalPastel;
        t.cheeks = true; break;
    case A_Griffin:
        t.topper = TopTuft; t.back = BackFeatherWings; t.mouth = MouthBeak;
        t.tail = TailCat; t.feet = false; t.hue = g.uni(0.08, 0.13); break;
    case A_Cyclops:
        t.eyes = EyeCyclops; t.topper = TopHornsSmall; t.mouth = MouthFang;
        t.hue = g.uni(0.30, 0.46); break;
    case A_Robot:
        t.body = BodyRobot; t.topper = TopAntennae; t.eyes = EyeOval;
        t.mouth = MouthFlat; t.pal = PalMetallic; t.belly = false; break;
    case A_Mushroom:
        t.body = BodyMushroom; t.mouth = MouthSmile; t.pattern = PatSpots;
        t.cheeks = g.chance(.6); t.belly = false; t.feet = false;
        t.hue = g.chance(.5) ? g.uni(0.0, 0.04) : g.uni(0.06, 0.10); break;
    case A_Potion:
        t.body = BodyBottle; t.mouth = MouthSmile; t.sparkles = g.chance(.5);
        t.belly = false; t.feet = false; break;
    case A_Star:
        t.body = BodyStar; t.eyes = EyeHappy; t.sparkles = true;
        t.belly = false; t.feet = false; t.hue = g.uni(0.12, 0.16); break;
    case A_Crystal:
        t.body = BodyGem; t.eyes = EyeSparkle; t.gem = true; t.sparkles = true;
        t.pal = PalMetallic; t.belly = false; t.feet = false; break;
    case A_Dino:
        t.body = g.chance(.5) ? BodyTall : BodyRound; t.topper = TopSpikes;
        t.mouth = MouthFang; t.tail = TailFish; t.hue = g.uni(0.26, 0.42); break;
    case A_Octopus:
        t.body = BodyOcto; t.eyes = EyeSparkle; t.feet = false; t.belly = false;
        t.hue = g.uni(0.55, 0.95); break;
    case A_Jellyfish:
        t.body = BodyJelly; t.eyes = g.chance(.5) ? EyeHappy : EyeDot;
        t.feet = false; t.belly = false; t.pal = PalGhostly;
        t.sparkles = g.chance(.4); break;
    case A_Snowman:
        t.body = BodySnowman; t.topper = TopTopHat; t.carrot = true;
        t.mouth = MouthFlat; t.feet = false; t.belly = false; break;
    case A_Cactus:
        t.body = BodyCactus; t.eyes = g.chance(.5) ? EyeHappy : EyeDot;
        t.feet = false; t.belly = false; t.cheeks = g.chance(.5);
        t.hue = g.uni(0.30, 0.40); break;
    case A_GhostCat:
        t.body = BodyGhost; t.topper = TopCatEars; t.eyes = EyeSleepy;
        t.whiskers = true; t.mouth = MouthCat; t.pal = PalGhostly;
        t.feet = false; t.belly = false; break;
    case A_Bat:
        t.topper = TopCatEars; t.back = BackBatWings; t.mouth = MouthFang;
        t.hue = g.uni(0.70, 0.82); break;
    case A_Bee:
        t.body = g.chance(.5) ? BodyRound : BodyWide; t.topper = TopAntennae;
        t.back = BackFairyWings; t.pattern = PatStripes;
        t.hue = g.uni(0.12, 0.16); break;
    }
    return t;
}

// --- Palette -------------------------------------------------------------
struct Palette {
    QColor body, edge, belly, accent, dark, white;
};

QColor hsv(double h, double s, double v, double a = 1.0) {
    QColor c = QColor::fromHsvF(h - std::floor(h), qBound(0.0, s, 1.0),
                               qBound(0.0, v, 1.0));
    c.setAlphaF(a);
    return c;
}

Palette buildPalette(int mode, double hue) {
    double sat = 0.52, val = 0.76, alpha = 1.0;
    switch (mode) {
    case PalFiery:    sat = 0.88; val = 0.97; break;
    case PalGhostly:  sat = 0.12; val = 0.95; alpha = 0.74; break;
    case PalMetallic: sat = 0.22; val = 0.74; break;
    case PalPastel:   sat = 0.40; val = 0.93; break;
    }
    Palette p;
    p.body = hsv(hue, sat, val, alpha);
    p.edge = hsv(hue, qMin(sat + 0.18, 0.9), val * 0.64, alpha);
    p.belly = hsv(hue, sat * 0.35, qMin(val + 0.16, 0.98), alpha);
    p.accent = hsv(hue + 0.5, 0.55, 0.82);
    p.dark = QColor(0x22, 0x22, 0x28);
    p.white = (mode == PalGhostly) ? hsv(hue, 0.06, 0.99) : QColor(Qt::white);
    return p;
}

// --- Drawing context -----------------------------------------------------
struct Ctx {
    QPainter &p;
    double cx, groundY, bw, bh, bodyTop, bcy, faceCy, faceDX;
    Palette pal;
    QPen edge() const {
        QPen e(pal.edge, 1.6);
        e.setJoinStyle(Qt::RoundJoin);
        e.setCapStyle(Qt::RoundCap);
        return e;
    }
};

// Rounded blob with a wavy (scalloped) bottom — slimes and ghosts.
void wavyBlob(Ctx &c, int bumps, double dip) {
    const double L = c.cx - c.bw / 2, R = c.cx + c.bw / 2, T = c.bodyTop;
    const double B = c.groundY, mid = T + c.bh * 0.45;
    QPainterPath path;
    path.moveTo(L, mid);
    path.quadTo(L, T, c.cx, T);
    path.quadTo(R, T, R, mid);
    path.lineTo(R, B - dip);
    const double seg = (R - L) / bumps;
    for (int i = 0; i < bumps; ++i)
        path.quadTo(R - seg * (i + 0.5), B + dip, R - seg * (i + 1), B - dip);
    path.closeSubpath();
    c.p.setPen(c.edge());
    c.p.setBrush(c.pal.body);
    c.p.drawPath(path);
}

void drawBody(Ctx &c, const Traits &t) {
    QPainter &p = c.p;
    // Soft contact shadow (skip for floaty things).
    if (t.feet || t.body == BodyRobot) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 55));
        p.drawEllipse(QPointF(c.cx, c.groundY + 2), c.bw * 0.44, 5.0);
    }
    p.setPen(c.edge());
    p.setBrush(c.pal.body);

    switch (t.body) {
    case BodyRound:
    case BodyTall:
    case BodyWide: {
        const double r = qMin(c.bw, c.bh) * 0.46;
        p.drawRoundedRect(QRectF(c.cx - c.bw / 2, c.bodyTop, c.bw, c.bh), r, r);
        break;
    }
    case BodySlime: wavyBlob(c, 4, 4.5); break;
    case BodyGhost: wavyBlob(c, 3, 7.0); break;
    case BodyRobot: {
        QRectF b(c.cx - c.bw / 2, c.bodyTop, c.bw, c.bh);
        p.drawRoundedRect(b, 9, 9);
        p.setBrush(c.pal.belly); // screen panel
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(c.cx - c.bw * 0.32, c.bodyTop + c.bh * 0.18,
                                 c.bw * 0.64, c.bh * 0.5), 5, 5);
        break;
    }
    case BodyBottle: {
        const double neckW = c.bw * 0.42, bodyW = c.bw;
        const double shoulder = c.bodyTop + c.bh * 0.30;
        QPainterPath path;
        path.moveTo(c.cx - neckW / 2, c.bodyTop);
        path.lineTo(c.cx - neckW / 2, shoulder);
        path.quadTo(c.cx - bodyW / 2, shoulder, c.cx - bodyW / 2,
                    (shoulder + c.groundY) / 2);
        path.quadTo(c.cx - bodyW / 2, c.groundY, c.cx, c.groundY);
        path.quadTo(c.cx + bodyW / 2, c.groundY, c.cx + bodyW / 2,
                    (shoulder + c.groundY) / 2);
        path.quadTo(c.cx + bodyW / 2, shoulder, c.cx + neckW / 2, shoulder);
        path.lineTo(c.cx + neckW / 2, c.bodyTop);
        p.drawPath(path);
        // Liquid + cork.
        p.setBrush(c.pal.belly);
        p.setPen(Qt::NoPen);
        p.drawChord(QRectF(c.cx - bodyW / 2 + 3, shoulder, bodyW - 6,
                           (c.groundY - shoulder) * 1.4), 0, -180 * 16);
        p.setBrush(c.pal.edge);
        p.drawRoundedRect(QRectF(c.cx - neckW / 2 - 1, c.bodyTop - 5,
                                 neckW + 2, 6), 2, 2);
        break;
    }
    case BodyStar: {
        const double cyc = c.bodyTop + c.bh / 2, Rr = c.bw / 2, rr = c.bw * 0.22;
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double ang = -M_PI / 2 + i * M_PI / 5;
            const double rad = (i % 2) ? rr : Rr;
            star << QPointF(c.cx + std::cos(ang) * rad, cyc + std::sin(ang) * rad);
        }
        QPainterPath path;
        path.addPolygon(star);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case BodyGem: {
        const double cyc = c.bodyTop + c.bh / 2, w = c.bw / 2, h = c.bh / 2;
        QPolygonF gem;
        gem << QPointF(c.cx, cyc - h) << QPointF(c.cx + w, cyc - h * 0.35)
            << QPointF(c.cx + w * 0.6, cyc + h) << QPointF(c.cx - w * 0.6, cyc + h)
            << QPointF(c.cx - w, cyc - h * 0.35);
        QPainterPath path;
        path.addPolygon(gem);
        path.closeSubpath();
        p.drawPath(path);
        // Facet glints.
        QPen fp(c.pal.white, 1.0);
        fp.setColor(QColor(255, 255, 255, 70));
        p.setPen(fp);
        p.drawLine(QPointF(c.cx, cyc - h), QPointF(c.cx - w * 0.35, cyc));
        p.drawLine(QPointF(c.cx, cyc - h), QPointF(c.cx + w * 0.35, cyc));
        break;
    }
    case BodyMushroom: {
        const double capH = c.bh * 0.55, stemW = c.bw * 0.46;
        const double capBot = c.bodyTop + capH;
        // Stem.
        p.setBrush(c.pal.belly);
        p.drawRoundedRect(QRectF(c.cx - stemW / 2, capBot - 4, stemW,
                                 c.groundY - capBot + 4), 7, 7);
        // Cap dome.
        p.setBrush(c.pal.body);
        QPainterPath cap;
        cap.moveTo(c.cx - c.bw / 2, capBot);
        cap.quadTo(c.cx - c.bw / 2, c.bodyTop, c.cx, c.bodyTop);
        cap.quadTo(c.cx + c.bw / 2, c.bodyTop, c.cx + c.bw / 2, capBot);
        cap.quadTo(c.cx, capBot + 7, c.cx - c.bw / 2, capBot);
        p.drawPath(cap);
        break;
    }
    case BodyOcto: {
        const int legs = 5;
        const double baseY = c.bodyTop + c.bh * 0.72;
        for (int i = 0; i < legs; ++i) {
            const double x = c.cx - c.bw * 0.36 + i * (c.bw * 0.72 / (legs - 1));
            QPainterPath leg;
            leg.moveTo(x - 4, baseY);
            leg.quadTo(x - 5, c.groundY + 5, x, c.groundY + 2);
            leg.quadTo(x + 5, c.groundY + 5, x + 4, baseY);
            p.drawPath(leg);
        }
        p.drawEllipse(QRectF(c.cx - c.bw / 2, c.bodyTop, c.bw, c.bh)); // head
        break;
    }
    case BodyJelly: {
        const double L = c.cx - c.bw / 2, R = c.cx + c.bw / 2;
        const double T = c.bodyTop, mb = c.bodyTop + c.bh * 0.68;
        QPainterPath bell;
        bell.moveTo(L, mb);
        bell.quadTo(L, T, c.cx, T);
        bell.quadTo(R, T, R, mb);
        const int bumps = 4, seg = (R - L) / bumps;
        for (int i = 0; i < bumps; ++i)
            bell.quadTo(R - seg * (i + 0.5), mb + 6, R - seg * (i + 1), mb);
        bell.closeSubpath();
        p.drawPath(bell);
        QPen tp(c.pal.edge, 2);
        tp.setCapStyle(Qt::RoundCap);
        p.setPen(tp);
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 5; ++i) {
            const double x = c.cx - c.bw * 0.30 + i * (c.bw * 0.6 / 4);
            QPainterPath tt;
            tt.moveTo(x, mb + 2);
            tt.quadTo(x + 4, (mb + c.groundY) / 2, x, c.groundY + 4);
            p.drawPath(tt);
        }
        break;
    }
    case BodyCactus: {
        const double colW = c.bw * 0.42;
        QRectF col(c.cx - colW / 2, c.bodyTop, colW, c.bh);
        p.drawRoundedRect(col, colW * 0.5, colW * 0.5);
        auto arm = [&](double sgn) {
            const double ax = c.cx + sgn * colW * 0.45, ay = c.bcy + 6;
            QPainterPath a;
            a.moveTo(ax, ay);
            a.lineTo(ax + sgn * 13, ay);
            a.lineTo(ax + sgn * 13, ay - 12);
            QPen e(c.pal.edge, 11); e.setCapStyle(Qt::RoundCap);
            QPen f(c.pal.body, 8); f.setCapStyle(Qt::RoundCap);
            p.setBrush(Qt::NoBrush);
            p.setPen(e); p.drawPath(a);
            p.setPen(f); p.drawPath(a);
        };
        arm(-1); arm(1);
        p.setPen(c.edge());
        p.setBrush(c.pal.body);
        p.drawRoundedRect(col, colW * 0.5, colW * 0.5); // over arm roots
        QPen sp(QColor(255, 255, 255, 130), 1);
        p.setPen(sp);
        for (int i = 0; i < 5; ++i) {
            const double y = c.bodyTop + 8 + i * (c.bh - 14) / 4.0;
            p.drawLine(QPointF(c.cx - 3, y), QPointF(c.cx - 6, y - 2));
            p.drawLine(QPointF(c.cx + 3, y), QPointF(c.cx + 6, y - 2));
        }
        p.setPen(c.edge());
        p.setBrush(QColor(0xb0, 0x6a, 0x3a)); // terracotta pot
        p.drawRoundedRect(QRectF(c.cx - colW * 0.75, c.groundY - 9,
                                 colW * 1.5, 11), 2, 2);
        break;
    }
    case BodySnowman: {
        const QColor snow(0xf2, 0xf6, 0xfb), snowEdge(0xc4, 0xd4, 0xe4);
        QPen se(snowEdge, 1.6);
        se.setJoinStyle(Qt::RoundJoin);
        p.setPen(se);
        p.setBrush(snow);
        const double headR = c.bw * 0.30, botR = c.bw * 0.45;
        const double botCy = c.groundY - botR;
        const double headCy = botCy - botR - headR * 0.45;
        p.drawEllipse(QPointF(c.cx, botCy), botR, botR);
        p.drawEllipse(QPointF(c.cx, headCy), headR, headR);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x33, 0x33, 0x3a));
        for (int i = 0; i < 2; ++i)
            p.drawEllipse(QPointF(c.cx, botCy - 4 + i * 9), 1.6, 1.6);
        break;
    }
    }

    // Little feet poking out at the base.
    if (t.feet && t.body != BodyMushroom) {
        p.setBrush(c.pal.edge);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(c.cx - c.bw * 0.20, c.groundY - 2), 7, 4.5);
        p.drawEllipse(QPointF(c.cx + c.bw * 0.20, c.groundY - 2), 7, 4.5);
    }
}

void drawBelly(Ctx &c) {
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(c.pal.belly);
    c.p.drawEllipse(QRectF(c.cx - c.bw * 0.26, c.bcy - c.bh * 0.02,
                           c.bw * 0.52, c.bh * 0.5));
}

void drawPattern(Ctx &c, const Traits &t, RNG &g) {
    QPainter &p = c.p;
    p.setPen(Qt::NoPen);
    if (t.pattern == PatSpots) {
        p.setBrush(t.body == BodyMushroom ? QColor(255, 255, 255, 210)
                                          : hsv(t.hue, 0.5, 0.82, 0.7).rgba());
        const int n = 3 + g.pick(3);
        const double topY = (t.body == BodyMushroom) ? c.bodyTop + c.bh * 0.18
                                                     : c.bcy - c.bh * 0.20;
        for (int i = 0; i < n; ++i)
            p.drawEllipse(QPointF(c.cx + g.uni(-c.bw * 0.3, c.bw * 0.3),
                                  topY + g.uni(-4, 8)),
                          g.uni(2.5, 4.5), g.uni(2.5, 4.5));
    } else if (t.pattern == PatStripes) {
        QPen sp(hsv(t.hue, 0.55, 0.6, 0.6), 2.4);
        p.setPen(sp);
        for (int i = -1; i <= 1; ++i)
            p.drawArc(QRectF(c.cx - c.bw * 0.3, c.bcy - c.bh * 0.25 + i * 9,
                             c.bw * 0.6, 14), 200 * 16, 140 * 16);
    } else if (t.pattern == PatScales) {
        p.setBrush(hsv(t.hue, 0.5, 0.62, 0.5));
        for (int r = 0; r < 2; ++r)
            for (int i = -1; i <= 1; ++i)
                p.drawEllipse(QPointF(c.cx + i * c.bw * 0.18 + (r ? c.bw * 0.09 : 0),
                                      c.bcy - c.bh * 0.05 + r * 7),
                              3.2, 3.2);
    }
}

// Ears, horns, crowns, halos... everything that sits on the head.
void drawTopper(Ctx &c, const Traits &t) {
    QPainter &p = c.p;
    const double dx = c.bw * 0.30, ty = c.bodyTop;
    auto mirror = [&](auto fn) { fn(-1.0); fn(1.0); };
    p.setPen(c.edge());

    switch (t.topper) {
    case TopCatEars:
        mirror([&](double s) {
            const double x = c.cx + s * dx;
            QPainterPath ear;
            ear.moveTo(x - 9, ty + 12);
            ear.quadTo(x - 2, ty - 18, x + 8, ty + 6);
            ear.closeSubpath();
            p.setPen(c.edge());
            p.setBrush(c.pal.body);
            p.drawPath(ear);
            p.setPen(Qt::NoPen);
            p.setBrush(c.pal.accent);
            QPainterPath in;
            in.moveTo(x - 4, ty + 6);
            in.quadTo(x - 1, ty - 8, x + 4, ty + 3);
            in.closeSubpath();
            p.drawPath(in);
        });
        break;
    case TopBearEars:
        mirror([&](double s) {
            const double x = c.cx + s * dx;
            p.setPen(c.edge());
            p.setBrush(c.pal.body);
            p.drawEllipse(QPointF(x, ty + 3), 11, 11);
            p.setPen(Qt::NoPen);
            p.setBrush(c.pal.accent);
            p.drawEllipse(QPointF(x, ty + 4), 6, 6);
        });
        break;
    case TopBunnyEars:
        mirror([&](double s) {
            const double x = c.cx + s * dx * 0.7;
            p.setPen(c.edge());
            p.setBrush(c.pal.body);
            p.drawEllipse(QRectF(x - 6, ty - 30, 12, 40));
            p.setPen(Qt::NoPen);
            p.setBrush(c.pal.accent);
            p.drawEllipse(QRectF(x - 3, ty - 25, 6, 30));
        });
        break;
    case TopMouseEars:
        mirror([&](double s) {
            const double x = c.cx + s * dx * 1.05;
            p.setPen(c.edge());
            p.setBrush(c.pal.body);
            p.drawEllipse(QPointF(x, ty + 2), 13, 13);
            p.setPen(Qt::NoPen);
            p.setBrush(c.pal.accent);
            p.drawEllipse(QPointF(x, ty + 2), 8, 8);
        });
        break;
    case TopHornsSmall:
        p.setBrush(c.pal.accent);
        mirror([&](double s) {
            const double x = c.cx + s * dx;
            QPainterPath h;
            h.moveTo(x - 4, ty + 6);
            h.quadTo(x - 1, ty - 12, x + 4, ty + 4);
            h.closeSubpath();
            p.drawPath(h);
        });
        break;
    case TopHornsCurved:
        p.setBrush(c.pal.belly);
        mirror([&](double s) {
            const double x = c.cx + s * dx;
            QPainterPath h;
            h.moveTo(x - s * 3, ty + 8);
            h.quadTo(x + s * 12, ty - 4, x + s * 7, ty - 18);
            h.quadTo(x + s * 2, ty - 6, x - s * 3, ty + 8);
            p.drawPath(h);
        });
        break;
    case TopUnicorn: {
        p.setBrush(hsv(0.13, 0.55, 0.95)); // gold
        QPainterPath horn;
        horn.moveTo(c.cx - 4, ty + 4);
        horn.lineTo(c.cx, ty - 20);
        horn.lineTo(c.cx + 4, ty + 4);
        horn.closeSubpath();
        p.drawPath(horn);
        QPen sp(hsv(0.10, 0.4, 0.7), 1.0);
        p.setPen(sp);
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(c.cx - 3, ty - 2 - i * 6),
                       QPointF(c.cx + 3, ty - 5 - i * 6));
        break;
    }
    case TopAntlers: {
        QPen ap(hsv(0.08, 0.45, 0.55), 2.0);
        ap.setCapStyle(Qt::RoundCap);
        p.setPen(ap);
        auto branch = [&](double s) {
            const double x = c.cx + s * 8;
            p.drawLine(QPointF(x, ty + 6), QPointF(x + s * 4, ty - 14));
            p.drawLine(QPointF(x + s * 2, ty - 4), QPointF(x + s * 9, ty - 6));
            p.drawLine(QPointF(x + s * 3, ty - 9), QPointF(x + s * 8, ty - 16));
        };
        branch(-1); branch(1);
        break;
    }
    case TopHalo: {
        QPen hp(hsv(0.14, 0.5, 1.0), 2.6);
        p.setPen(hp);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(c.cx - 13, ty - 16, 26, 9));
        break;
    }
    case TopCrown: {
        p.setPen(Qt::NoPen);
        p.setBrush(hsv(0.13, 0.6, 0.95));
        QPolygonF cr;
        cr << QPointF(c.cx - 14, ty + 4) << QPointF(c.cx - 14, ty - 6)
           << QPointF(c.cx - 7, ty + 1) << QPointF(c.cx, ty - 9)
           << QPointF(c.cx + 7, ty + 1) << QPointF(c.cx + 14, ty - 6)
           << QPointF(c.cx + 14, ty + 4);
        p.drawPolygon(cr);
        p.setBrush(hsv(0.95, 0.6, 0.9));
        p.drawEllipse(QPointF(c.cx, ty - 3), 2.2, 2.2);
        break;
    }
    case TopWizardHat: {
        p.setPen(c.edge());
        p.setBrush(hsv(0.72, 0.5, 0.55));
        QPolygonF hat;
        hat << QPointF(c.cx, ty - 26) << QPointF(c.cx + 14, ty + 4)
            << QPointF(c.cx - 14, ty + 4);
        p.drawPolygon(hat);
        p.setPen(Qt::NoPen);
        p.setBrush(hsv(0.14, 0.6, 0.95));
        p.drawEllipse(QPointF(c.cx + 2, ty - 12), 1.8, 1.8);
        break;
    }
    case TopAntennae:
        mirror([&](double s) {
            const double x = c.cx + s * 6;
            QPen ap(c.pal.edge, 1.4);
            ap.setCapStyle(Qt::RoundCap);
            p.setPen(ap);
            p.drawLine(QPointF(x, ty + 5), QPointF(x + s * 4, ty - 9));
            p.setPen(Qt::NoPen);
            p.setBrush(c.pal.accent);
            p.drawEllipse(QPointF(x + s * 4, ty - 10), 2.6, 2.6);
        });
        break;
    case TopLeaf: {
        p.setPen(Qt::NoPen);
        p.setBrush(hsv(0.38, 0.55, 0.7));
        const double sx = c.cx, sy = ty + 2;
        for (double s : {-1.0, 1.0}) {
            QPainterPath l;
            l.moveTo(sx, sy);
            l.quadTo(sx + s * 9, sy - 6, sx + s * 12, sy - 13);
            l.quadTo(sx + s * 4, sy - 11, sx, sy);
            p.drawPath(l);
        }
        break;
    }
    case TopFlame: {
        p.setPen(Qt::NoPen);
        for (double s : {-1.0, 0.0, 1.0}) {
            p.setBrush(hsv(0.04 + (s == 0 ? 0.04 : 0), 0.85, 0.98));
            QPainterPath f;
            const double x = c.cx + s * 9;
            f.moveTo(x - 5, ty + 6);
            f.quadTo(x - 4, ty - 10, x, ty - 18 - (s == 0 ? 4 : 0));
            f.quadTo(x + 4, ty - 10, x + 5, ty + 6);
            f.quadTo(x, ty + 2, x - 5, ty + 6);
            p.drawPath(f);
        }
        break;
    }
    case TopTuft: {
        QPen tp(c.pal.edge, 1.6);
        tp.setCapStyle(Qt::RoundCap);
        p.setPen(tp);
        for (int i = -1; i <= 1; ++i)
            p.drawLine(QPointF(c.cx + i * 3, ty + 5),
                       QPointF(c.cx + i * 4.5, ty - 7));
        break;
    }
    case TopTopHat: {
        p.setPen(c.edge());
        p.setBrush(QColor(0x2a, 0x2a, 0x30));
        p.drawRoundedRect(QRectF(c.cx - 12, ty - 2, 24, 3.5), 1, 1); // brim
        p.drawRect(QRectF(c.cx - 8, ty - 16, 16, 15));               // crown
        p.setPen(Qt::NoPen);
        p.setBrush(c.pal.accent);
        p.drawRect(QRectF(c.cx - 8, ty - 6, 16, 3)); // band
        break;
    }
    case TopSpikes:
        p.setPen(c.edge());
        p.setBrush(c.pal.belly);
        for (int i = -1; i <= 1; ++i) {
            QPainterPath sp;
            const double x = c.cx + i * 10;
            sp.moveTo(x - 6, ty + 8);
            sp.lineTo(x, ty - 10 - (i == 0 ? 2 : 0));
            sp.lineTo(x + 6, ty + 8);
            sp.closeSubpath();
            p.drawPath(sp);
        }
        break;
    }
}

void drawBack(Ctx &c, const Traits &t) {
    QPainter &p = c.p;
    const double wy = c.bcy - c.bh * 0.05;
    auto mirror = [&](auto fn) { fn(-1.0); fn(1.0); };
    switch (t.back) {
    case BackBatWings:
        p.setPen(c.edge());
        p.setBrush(c.pal.accent);
        mirror([&](double s) {
            QPainterPath w;
            const double x = c.cx + s * c.bw * 0.34;
            w.moveTo(x, wy - 8);
            w.lineTo(x + s * 24, wy - 14);
            w.lineTo(x + s * 18, wy - 4);
            w.lineTo(x + s * 26, wy + 4);
            w.lineTo(x + s * 17, wy + 4);
            w.lineTo(x + s * 22, wy + 14);
            w.lineTo(x, wy + 10);
            w.closeSubpath();
            p.drawPath(w);
        });
        break;
    case BackFeatherWings:
        p.setPen(c.edge());
        p.setBrush(c.pal.white);
        mirror([&](double s) {
            const double x = c.cx + s * c.bw * 0.36;
            for (int i = 0; i < 3; ++i)
                p.drawEllipse(QRectF(x + s * (2 + i * 6) - 7, wy - 12 + i * 8,
                                     16, 12));
        });
        break;
    case BackFairyWings:
        p.setPen(Qt::NoPen);
        mirror([&](double s) {
            QColor wing = c.pal.accent;
            wing.setAlpha(110);
            p.setBrush(wing);
            const double x = c.cx + s * c.bw * 0.28;
            p.drawEllipse(QRectF(qMin(x, x + s * 22), wy - 18, 22, 18));
            p.drawEllipse(QRectF(qMin(x, x + s * 18), wy, 18, 16));
        });
        break;
    case BackCape:
        p.setPen(Qt::NoPen);
        p.setBrush(c.pal.accent);
        QPainterPath cape;
        cape.moveTo(c.cx - c.bw * 0.4, c.bodyTop + 6);
        cape.lineTo(c.cx + c.bw * 0.4, c.bodyTop + 6);
        cape.lineTo(c.cx + c.bw * 0.5, c.groundY);
        cape.lineTo(c.cx - c.bw * 0.5, c.groundY);
        cape.closeSubpath();
        p.drawPath(cape);
        break;
    }
}

void drawTail(Ctx &c, const Traits &t) {
    if (t.tail == TailNone)
        return;
    QPainter &p = c.p;
    const double s = (t.hue < 0.5) ? 1.0 : -1.0;
    const double tx = c.cx + s * c.bw * 0.46, ty = c.groundY - 8;
    p.setPen(c.edge());
    p.setBrush(c.pal.body);
    switch (t.tail) {
    case TailCat: {
        QPainterPath path;
        path.moveTo(tx, ty);
        path.quadTo(tx + s * 18, ty - 6, tx + s * 12, ty - 22);
        p.setBrush(Qt::NoBrush);
        QPen tp(c.pal.edge, 5);
        tp.setCapStyle(Qt::RoundCap);
        p.setPen(tp);
        p.drawPath(path);
        break;
    }
    case TailBushy:
        p.drawEllipse(QRectF(tx - 4, ty - 18, s * 16, 26));
        break;
    case TailFluffy:
        p.setBrush(c.pal.belly);
        p.drawEllipse(QPointF(tx, ty), 8, 8);
        break;
    case TailDevil: {
        QPen tp(c.pal.accent, 3);
        tp.setCapStyle(Qt::RoundCap);
        p.setPen(tp);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(tx, ty);
        path.quadTo(tx + s * 16, ty - 4, tx + s * 12, ty - 18);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(c.pal.accent);
        QPolygonF tip;
        tip << QPointF(tx + s * 12, ty - 24) << QPointF(tx + s * 18, ty - 16)
            << QPointF(tx + s * 7, ty - 16);
        p.drawPolygon(tip);
        break;
    }
    case TailFish: {
        QPainterPath path; // fan tail
        path.moveTo(tx - s * 6, ty);
        path.lineTo(tx + s * 16, ty - 12);
        path.lineTo(tx + s * 14, ty);
        path.lineTo(tx + s * 16, ty + 12);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case TailThin: {
        QPen tp(c.pal.edge, 2);
        tp.setCapStyle(Qt::RoundCap);
        p.setPen(tp);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(tx, ty + 2);
        path.quadTo(tx + s * 20, ty - 2, tx + s * 16, ty - 16);
        p.drawPath(path);
        break;
    }
    }
}

void drawFace(Ctx &c, const Traits &t, bool blink) {
    QPainter &p = c.p;
    const double ey = c.faceCy, dx = c.faceDX;

    if (t.cheeks) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 130, 150, 120));
        p.drawEllipse(QPointF(c.cx - dx - 5, ey + 7), 5, 3.2);
        p.drawEllipse(QPointF(c.cx + dx + 5, ey + 7), 5, 3.2);
    }
    if (t.whiskers) {
        QPen wp(QColor(0, 0, 0, 70), 1.0);
        p.setPen(wp);
        for (int i = -1; i <= 1; ++i)
            for (double s : {-1.0, 1.0})
                p.drawLine(QPointF(c.cx + s * dx * 0.6, ey + 8 + i),
                           QPointF(c.cx + s * (dx + 12), ey + 6 + i * 2.5));
    }

    auto eye = [&](double x) {
        switch (t.eyes) {
        case EyeDot:
            p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
            p.drawEllipse(QPointF(x, ey), 3.4, 3.8);
            p.setBrush(Qt::white); p.drawEllipse(QPointF(x - 1, ey - 1.3), 1.1, 1.1);
            break;
        case EyeSparkle:
            p.setPen(Qt::NoPen); p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x, ey), 5, 5.6);
            p.setBrush(c.pal.dark); p.drawEllipse(QPointF(x, ey + 0.6), 3, 3.4);
            p.setBrush(Qt::white); p.drawEllipse(QPointF(x - 1.1, ey - 1.1), 1.3, 1.3);
            break;
        case EyeSleepy: {
            QPen ep(c.pal.dark, 1.8); ep.setCapStyle(Qt::RoundCap);
            p.setPen(ep); p.setBrush(Qt::NoBrush);
            QPainterPath a; a.moveTo(x - 3.5, ey); a.quadTo(x, ey + 3.2, x + 3.5, ey);
            p.drawPath(a); break;
        }
        case EyeOval:
            p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
            p.drawEllipse(QPointF(x, ey), 2.6, 4.4);
            p.setBrush(Qt::white); p.drawEllipse(QPointF(x - 0.8, ey - 1.7), 0.9, 0.9);
            break;
        case EyeHappy: {
            QPen ep(c.pal.dark, 1.8); ep.setCapStyle(Qt::RoundCap);
            p.setPen(ep); p.setBrush(Qt::NoBrush);
            QPainterPath a; a.moveTo(x - 3.5, ey + 1.5);
            a.quadTo(x, ey - 2.8, x + 3.5, ey + 1.5);
            p.drawPath(a); break;
        }
        case EyeAngry:
            p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
            p.drawEllipse(QPointF(x, ey + 1), 3.2, 3.4);
            { QPen bp(c.pal.dark, 1.6); bp.setCapStyle(Qt::RoundCap); p.setPen(bp); }
            p.drawLine(QPointF(x - 4, ey - 4),
                       QPointF(x + (x < c.cx ? 3 : -3), ey - 2));
            break;
        case EyeStar: {
            p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
            p.drawEllipse(QPointF(x, ey), 4, 4.4);
            p.setBrush(hsv(0.14, 0.5, 1.0));
            QPolygonF st;
            for (int i = 0; i < 10; ++i) {
                const double an = -M_PI / 2 + i * M_PI / 5, rad = (i % 2) ? 1.2 : 3.0;
                st << QPointF(x + std::cos(an) * rad, ey + std::sin(an) * rad);
            }
            p.drawPolygon(st); break;
        }
        case EyeWink:
            // handled per-side below
            break;
        }
    };

    auto closedEye = [&](double x) {
        QPen ep(c.pal.dark, 1.8);
        ep.setCapStyle(Qt::RoundCap);
        p.setPen(ep);
        p.setBrush(Qt::NoBrush);
        QPainterPath a;
        a.moveTo(x - 3.5, ey);
        a.quadTo(x, ey + 3.0, x + 3.5, ey);
        p.drawPath(a);
    };
    if (blink) {
        if (t.eyes == EyeCyclops)
            closedEye(c.cx);
        else {
            closedEye(c.cx - dx);
            closedEye(c.cx + dx);
        }
    } else if (t.eyes == EyeCyclops) {
        p.setPen(c.edge()); p.setBrush(Qt::white);
        p.drawEllipse(QPointF(c.cx, ey), 7, 7.5);
        p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
        p.drawEllipse(QPointF(c.cx, ey + 1), 3.6, 4);
        p.setBrush(Qt::white); p.drawEllipse(QPointF(c.cx - 1.4, ey - 1.4), 1.6, 1.6);
    } else if (t.eyes == EyeWink) {
        // left winks (^), right is a dot
        QPen ep(c.pal.dark, 1.8); ep.setCapStyle(Qt::RoundCap);
        p.setPen(ep); p.setBrush(Qt::NoBrush);
        QPainterPath a; a.moveTo(c.cx - dx - 3.5, ey + 1.5);
        a.quadTo(c.cx - dx, ey - 2.8, c.cx - dx + 3.5, ey + 1.5);
        p.drawPath(a);
        p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
        p.drawEllipse(QPointF(c.cx + dx, ey), 3.4, 3.8);
    } else {
        eye(c.cx - dx);
        eye(c.cx + dx);
    }

    // Nose + mouth.
    const double ny = ey + 7.5;
    if (t.carrot) {
        p.setPen(Qt::NoPen);
        p.setBrush(hsv(0.07, 0.85, 0.95));
        QPolygonF car;
        car << QPointF(c.cx - 2, ey + 1) << QPointF(c.cx + 2, ey + 1)
            << QPointF(c.cx + 1, ey + 10);
        p.drawPolygon(car);
    } else if (t.mouth != MouthBeak) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.cheeks ? QColor(255, 130, 150) : c.pal.edge);
        p.drawEllipse(QPointF(c.cx, ny), 2.0, 1.5);
    }
    QPen mp(c.pal.dark, 1.4);
    mp.setCapStyle(Qt::RoundCap);
    p.setPen(mp);
    p.setBrush(Qt::NoBrush);
    switch (t.mouth) {
    case MouthSmile: {
        QPainterPath m; m.moveTo(c.cx - 4, ny + 2);
        m.quadTo(c.cx, ny + 5.5, c.cx + 4, ny + 2); p.drawPath(m); break;
    }
    case MouthCat: {
        QPainterPath m; m.moveTo(c.cx - 5, ny + 2);
        m.quadTo(c.cx - 2.5, ny + 4.5, c.cx, ny + 2);
        m.quadTo(c.cx + 2.5, ny + 4.5, c.cx + 5, ny + 2); p.drawPath(m); break;
    }
    case MouthOpen:
        p.setPen(Qt::NoPen); p.setBrush(c.pal.dark);
        p.drawEllipse(QPointF(c.cx, ny + 3.5), 1.9, 2.3); break;
    case MouthFang: {
        QPainterPath m; m.moveTo(c.cx - 5, ny + 2);
        m.quadTo(c.cx, ny + 5, c.cx + 5, ny + 2); p.drawPath(m);
        p.setPen(Qt::NoPen); p.setBrush(Qt::white);
        for (double s : {-1.0, 1.0}) {
            QPolygonF f;
            f << QPointF(c.cx + s * 2.4, ny + 2.5) << QPointF(c.cx + s * 3.6, ny + 2.5)
              << QPointF(c.cx + s * 3.0, ny + 5.5);
            p.drawPolygon(f);
        }
        break;
    }
    case MouthBeak: {
        p.setPen(Qt::NoPen); p.setBrush(hsv(0.10, 0.7, 0.95));
        QPolygonF bk;
        bk << QPointF(c.cx - 4, ny - 1) << QPointF(c.cx + 4, ny - 1)
           << QPointF(c.cx, ny + 5);
        p.drawPolygon(bk); break;
    }
    case MouthGrin: {
        QPainterPath m; m.moveTo(c.cx - 7, ny + 1);
        m.quadTo(c.cx, ny + 7, c.cx + 7, ny + 1); p.drawPath(m); break;
    }
    case MouthFlat:
        p.drawLine(QPointF(c.cx - 4, ny + 3), QPointF(c.cx + 4, ny + 3)); break;
    }
}

void drawExtras(Ctx &c, const Traits &t, RNG &g) {
    QPainter &p = c.p;
    if (t.gem) {
        p.setPen(Qt::NoPen);
        p.setBrush(hsv(0.5, 0.5, 0.95));
        QPolygonF d;
        const double y = c.faceCy - c.bh * 0.18;
        d << QPointF(c.cx, y - 3) << QPointF(c.cx + 2.5, y) << QPointF(c.cx, y + 3)
          << QPointF(c.cx - 2.5, y);
        p.drawPolygon(d);
    }
    if (t.sparkles) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 220));
        for (int i = 0; i < 3; ++i) {
            const double x = c.cx + g.uni(-c.bw * 0.6, c.bw * 0.6);
            const double y = c.bodyTop + g.uni(-6, c.bh * 0.5);
            const double r = g.uni(1.2, 2.4);
            QPolygonF s;
            s << QPointF(x, y - r * 2) << QPointF(x + r * 0.5, y - r * 0.5)
              << QPointF(x + r * 2, y) << QPointF(x + r * 0.5, y + r * 0.5)
              << QPointF(x, y + r * 2) << QPointF(x - r * 0.5, y + r * 0.5)
              << QPointF(x - r * 2, y) << QPointF(x - r * 0.5, y - r * 0.5);
            p.drawPolygon(s);
        }
    }
    // Glossy sheen on the upper body.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 38));
    p.drawEllipse(QRectF(c.cx - c.bw * 0.30, c.bodyTop + c.bh * 0.10,
                         c.bw * 0.32, c.bh * 0.24));
}

// --- Hand-drawn art bridge ------------------------------------------------
// Map each trait to its catalog asset name (kebab-case; see
// docs/MASCOT_ART_SPEC.md). All append-only — never reorder or rename a name,
// or a stored seed would start rendering different parts. An empty name means
// "this slot is absent" (e.g. BackNone), so no SVG is looked up.
QString bodyAssetName(int body) {
    switch (body) {
    case BodyTall:     return QStringLiteral("tall");
    case BodyWide:     return QStringLiteral("wide");
    case BodySlime:    return QStringLiteral("slime");
    case BodyGhost:    return QStringLiteral("ghost");
    case BodyBottle:   return QStringLiteral("bottle");
    case BodyStar:     return QStringLiteral("star");
    case BodyGem:      return QStringLiteral("gem");
    case BodyMushroom: return QStringLiteral("mushroom");
    case BodyRobot:    return QStringLiteral("robot");
    case BodyOcto:     return QStringLiteral("octo");
    case BodyJelly:    return QStringLiteral("jelly");
    case BodyCactus:   return QStringLiteral("cactus");
    case BodySnowman:  return QStringLiteral("snowman");
    default:           return QStringLiteral("round");
    }
}

QString backAssetName(int back) {
    switch (back) {
    case BackBatWings:     return QStringLiteral("bat-wings");
    case BackFeatherWings: return QStringLiteral("feather-wings");
    case BackFairyWings:   return QStringLiteral("fairy-wings");
    case BackCape:         return QStringLiteral("cape");
    default:               return QString(); // BackNone
    }
}

QString tailAssetName(int tail) {
    switch (tail) {
    case TailCat:    return QStringLiteral("cat");
    case TailBushy:  return QStringLiteral("bushy");
    case TailDevil:  return QStringLiteral("devil");
    case TailFish:   return QStringLiteral("fish");
    case TailThin:   return QStringLiteral("thin");
    case TailFluffy: return QStringLiteral("fluffy");
    default:         return QString(); // TailNone
    }
}

QString topperAssetName(int top) {
    switch (top) {
    case TopCatEars:     return QStringLiteral("cat-ears");
    case TopBearEars:    return QStringLiteral("bear-ears");
    case TopBunnyEars:   return QStringLiteral("bunny-ears");
    case TopMouseEars:   return QStringLiteral("mouse-ears");
    case TopHornsSmall:  return QStringLiteral("horns-small");
    case TopHornsCurved: return QStringLiteral("horns-curved");
    case TopUnicorn:     return QStringLiteral("unicorn");
    case TopAntlers:     return QStringLiteral("antlers");
    case TopHalo:        return QStringLiteral("halo");
    case TopCrown:       return QStringLiteral("crown");
    case TopWizardHat:   return QStringLiteral("wizard-hat");
    case TopAntennae:    return QStringLiteral("antennae");
    case TopLeaf:        return QStringLiteral("leaf");
    case TopFlame:       return QStringLiteral("flame");
    case TopTuft:        return QStringLiteral("tuft");
    case TopTopHat:      return QStringLiteral("top-hat");
    case TopSpikes:      return QStringLiteral("spikes");
    default:             return QString(); // TopNone
    }
}

QString eyesAssetName(int eyes) {
    switch (eyes) {
    case EyeSparkle: return QStringLiteral("sparkle");
    case EyeSleepy:  return QStringLiteral("sleepy");
    case EyeOval:    return QStringLiteral("oval");
    case EyeHappy:   return QStringLiteral("happy");
    case EyeAngry:   return QStringLiteral("angry");
    case EyeCyclops: return QStringLiteral("cyclops");
    case EyeStar:    return QStringLiteral("star");
    case EyeWink:    return QStringLiteral("wink");
    default:         return QStringLiteral("dot"); // EyeDot
    }
}

QString mouthAssetName(int mouth) {
    switch (mouth) {
    case MouthCat:  return QStringLiteral("cat");
    case MouthOpen: return QStringLiteral("open");
    case MouthFang: return QStringLiteral("fang");
    case MouthBeak: return QStringLiteral("beak");
    case MouthGrin: return QStringLiteral("grin");
    case MouthFlat: return QStringLiteral("flat");
    default:        return QStringLiteral("smile"); // MouthSmile
    }
}

QString patternAssetName(int pattern) {
    switch (pattern) {
    case PatSpots:   return QStringLiteral("spots");
    case PatStripes: return QStringLiteral("stripes");
    case PatScales:  return QStringLiteral("scales");
    default:         return QString(); // PatNone
    }
}

// The 6 palette slots as the slot-name -> colour map the catalog tints with.
QHash<QString, QColor> tintFor(const Palette &p) {
    return {{QStringLiteral("body"), p.body},
            {QStringLiteral("edge"), p.edge},
            {QStringLiteral("belly"), p.belly},
            {QStringLiteral("accent"), p.accent},
            {QStringLiteral("dark"), p.dark},
            {QStringLiteral("white"), p.white}};
}

// A body's overlay mount points: the manifest record when the body is
// hand-drawn, else points derived from the procedural geometry so an SVG
// overlay placed on a procedural body still lands sensibly.
BodyAnchors anchorsFor(const Ctx &c, const Traits &t) {
    BodyAnchors a = MascotCatalog::shared().anchors(bodyAssetName(t.body));
    if (!a.valid) {
        a.face = QPointF(c.cx, c.faceCy);
        a.eyeGap = c.faceDX;
        a.topper = QPointF(c.cx, c.bodyTop);
        a.tail = QPointF(c.cx + c.bw * 0.46, c.groundY - 8);
        a.back = QPointF(c.cx, c.bcy - c.bh * 0.05);
        a.ground = QPointF(c.cx, c.groundY);
    }
    return a;
}

// Draw an SVG part across the whole canvas, shifted so the point it was
// authored around (the canonical mount `canonical`) lands on the body's actual
// mount point (`mount`). For the default body the two coincide and the shift is
// zero; other bodies nudge the overlay to fit.
void renderMount(QPainter &p, const QString &slot, const QString &name,
                 QPointF mount, QPointF canonical,
                 const QHash<QString, QColor> &tint) {
    p.save();
    p.translate(mount - canonical);
    MascotCatalog::shared().renderPart(p, slot, name, QRectF(0, 0, 100, 110), tint);
    p.restore();
}

// The accessories the procedural drawFace/drawExtras bundle in, rendered as
// individual SVG parts at their mount when the art exists (used on the
// hand-drawn path; `CN` holds the canonical anchors).
void renderExtrasArt(QPainter &p, const Traits &t, const BodyAnchors &A,
                     const BodyAnchors &CN, const QHash<QString, QColor> &tint) {
    MascotCatalog &cat = MascotCatalog::shared();
    auto add = [&](bool on, const QString &name, QPointF at, QPointF canon) {
        if (on && cat.hasPart(QStringLiteral("extra"), name))
            renderMount(p, QStringLiteral("extra"), name, at, canon, tint);
    };
    add(t.cheeks,   QStringLiteral("cheeks"),   A.face,   CN.face);
    add(t.whiskers, QStringLiteral("whiskers"), A.face,   CN.face);
    add(t.carrot,   QStringLiteral("carrot"),   A.face,   CN.face);
    add(t.gem,      QStringLiteral("gem"),      A.face,   CN.face);
    add(t.sparkles, QStringLiteral("sparkles"), A.back,   CN.back);
    add(t.feet,     QStringLiteral("feet"),     A.ground, CN.ground);
}

// Composite a user-defined creature (a folder of layer SVGs the user dropped
// into their mascots catalog) in the standard z-order, rendered as authored —
// no palette tinting, since the user drew its final colours.
void drawUserCreature(QPainter &p, const QString &kind, bool blink) {
    MascotCatalog &cat = MascotCatalog::shared();
    const QString slot = QStringLiteral("creatures/") + kind;
    const QRectF full(0, 0, 100, 110);
    const QHash<QString, QColor> noTint;
    auto layer = [&](const QString &name) {
        if (cat.hasPart(slot, name))
            cat.renderPart(p, slot, name, full, noTint);
    };
    layer(QStringLiteral("back"));
    layer(QStringLiteral("tail"));
    layer(QStringLiteral("body"));
    layer(QStringLiteral("pattern"));
    layer(QStringLiteral("topper"));
    if (const QString closed = QStringLiteral("eyes-closed");
        blink && cat.hasPart(slot, closed))
        cat.renderPart(p, slot, closed, full, noTint);
    else
        layer(QStringLiteral("eyes"));
    layer(QStringLiteral("mouth"));
}

// Roll the seed's traits, lay out geometry + palette, and paint the layered
// creature into `p` (offset vertically by `bob`, eyes shut when `blink`). A
// non-empty `kind` present in the catalog draws that user creature instead.
// Shared by the live widget and the gallery's pixmaps.
void drawCreature(QPainter &p, quint64 seed, const QString &kind, int w, int h,
                  double bob, bool blink) {
    if (seed == 0)
        return;

    p.setRenderHint(QPainter::Antialiasing);
    const double pad = 5.0;
    const double s = qMin((w - 2 * pad) / 100.0, (h - 2 * pad) / 110.0);
    p.save();
    p.translate((w - 100 * s) / 2.0, (h - 110 * s) / 2.0 + bob);
    p.scale(s, s);

    // A user-defined creature short-circuits the built-in roll; an unknown kind
    // (e.g. a note opened on a machine without that art) falls through to the
    // built-in creature the seed produces.
    if (!kind.isEmpty() && MascotCatalog::shared().hasKind(kind)) {
        drawUserCreature(p, kind, blink);
        p.restore();
        return;
    }

    std::mt19937_64 rng(seed);
    RNG g{rng};
    const Traits t = rollTraits(g);

    // Body proportions per shape (+ where the face sits on it).
    double bw = 60, bh = 58;
    switch (t.body) {
    case BodyTall:     bw = 52; bh = 66; break;
    case BodyWide:     bw = 66; bh = 52; break;
    case BodySlime:    bw = 64; bh = 50; break;
    case BodyGhost:    bw = 56; bh = 64; break;
    case BodyBottle:   bw = 50; bh = 64; break;
    case BodyStar:     bw = 76; bh = 76; break;
    case BodyGem:      bw = 56; bh = 64; break;
    case BodyMushroom: bw = 72; bh = 64; break;
    case BodyRobot:    bw = 58; bh = 56; break;
    case BodyOcto:     bw = 64; bh = 50; break;
    case BodyJelly:    bw = 60; bh = 50; break;
    case BodyCactus:   bw = 60; bh = 64; break;
    case BodySnowman:  bw = 56; bh = 76; break;
    default: break;
    }
    Ctx c{p, 50.0, 96.0, bw, bh, 0, 0, 0, bw * 0.19};
    c.bodyTop = c.groundY - bh;
    c.bcy = c.groundY - bh / 2;
    c.faceCy = c.bcy - bh * 0.04;
    c.pal = buildPalette(t.pal, t.hue);
    if (t.body == BodyStar)   { c.faceCy = c.bodyTop + bh / 2 + 2; c.faceDX = bw * 0.13; }
    if (t.body == BodyGem)    { c.faceCy = c.bodyTop + bh * 0.55; c.faceDX = bw * 0.15; }
    if (t.body == BodyBottle) { c.faceCy = c.bodyTop + bh * 0.66; c.faceDX = bw * 0.20; }
    if (t.body == BodyMushroom){ c.faceCy = c.bodyTop + bh * 0.74; c.faceDX = bw * 0.13; }
    if (t.body == BodyOcto)   { c.faceCy = c.bodyTop + bh * 0.46; c.faceDX = bw * 0.18; }
    if (t.body == BodyJelly)  { c.faceCy = c.bodyTop + bh * 0.40; c.faceDX = bw * 0.16; }
    if (t.body == BodyCactus) { c.faceDX = bw * 0.12; }
    if (t.body == BodySnowman) {
        const double headR = bw * 0.30, botR = bw * 0.45;
        c.faceCy = (c.groundY - botR) - botR - headR * 0.45;
        c.faceDX = headR * 0.5;
    }

    // Back-to-front layers. Each part renders from hand-drawn SVG art when the
    // catalog has it (placed at the body's anchor; see docs/MASCOT_ART_SPEC.md),
    // otherwise from the procedural draw functions — so art can land one
    // archetype at a time with no flag day.
    MascotCatalog &cat = MascotCatalog::shared();
    const QHash<QString, QColor> tint = tintFor(c.pal);
    const BodyAnchors A = anchorsFor(c, t); // this body's mount points
    const BodyAnchors CN;                   // canonical anchors overlays use
    const bool artBody = cat.hasPart(QStringLiteral("body"), bodyAssetName(t.body));

    // Wings / cape, behind the body.
    if (const QString n = backAssetName(t.back);
        !n.isEmpty() && cat.hasPart(QStringLiteral("back"), n))
        renderMount(p, QStringLiteral("back"), n, A.back, CN.back, tint);
    else
        drawBack(c, t);

    // Tail.
    if (const QString n = tailAssetName(t.tail);
        !n.isEmpty() && cat.hasPart(QStringLiteral("tail"), n))
        renderMount(p, QStringLiteral("tail"), n, A.tail, CN.tail, tint);
    else
        drawTail(c, t);

    // Body (with its belly baked into the art), then the pattern overlay. A
    // pattern is per-archetype while bodies are shared by shape, so it stays a
    // separate full-canvas layer (the artist authors it to sit on the body).
    // The procedural path draws body + belly + pattern itself.
    if (artBody) {
        cat.renderPart(p, QStringLiteral("body"), bodyAssetName(t.body),
                       QRectF(0, 0, 100, 110), tint);
        if (const QString n = patternAssetName(t.pattern);
            !n.isEmpty() && cat.hasPart(QStringLiteral("pattern"), n))
            cat.renderPart(p, QStringLiteral("pattern"), n,
                           QRectF(0, 0, 100, 110), tint);
    } else {
        drawBody(c, t);
        if (t.belly)
            drawBelly(c);
        drawPattern(c, t, g);
    }

    // Head topper.
    if (const QString n = topperAssetName(t.topper);
        !n.isEmpty() && cat.hasPart(QStringLiteral("topper"), n))
        renderMount(p, QStringLiteral("topper"), n, A.topper, CN.topper, tint);
    else
        drawTopper(c, t);

    // Face. The procedural drawFace bundles eyes + mouth + nose + cheeks /
    // whiskers and drawExtras adds gem / sparkles; the hand-drawn path (taken
    // for an art body) composites eyes and mouth at the face anchor plus the
    // accessories as separate SVG parts. Blink swaps in "<eyes>-closed" if drawn.
    if (artBody) {
        const QString eyes = eyesAssetName(t.eyes);
        const QString closed = eyes + QStringLiteral("-closed");
        const QString eyesNow =
            (blink && cat.hasPart(QStringLiteral("eyes"), closed)) ? closed : eyes;
        if (cat.hasPart(QStringLiteral("eyes"), eyesNow))
            renderMount(p, QStringLiteral("eyes"), eyesNow, A.face, CN.face, tint);
        if (const QString m = mouthAssetName(t.mouth);
            cat.hasPart(QStringLiteral("mouth"), m))
            renderMount(p, QStringLiteral("mouth"), m, A.face, CN.face, tint);
        renderExtrasArt(p, t, A, CN, tint);
    } else {
        drawFace(c, t, blink);
        drawExtras(c, t, g);
    }
    p.restore();
}

} // namespace

Mascot::Mascot(QWidget *parent) : QWidget(parent) {
    // The global stylesheet paints every QWidget with the app background; this
    // objectName lets the QSS keep the mascot transparent so only the creature
    // shows.
    setObjectName(QStringLiteral("mascot"));
    setFixedSize(132, 152);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Open the mascot gallery"));
    // A gentle idle bob + blink, but only while hovered — no idle CPU at rest.
    m_idle = new QTimer(this);
    m_idle->setInterval(33);
    connect(m_idle, &QTimer::timeout, this, [this] {
        ++m_tick;
        update();
    });
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

QString Mascot::kindForSeed(quint64 seed) {
    const QStringList ks = MascotCatalog::shared().kinds();
    if (ks.isEmpty() || seed == 0)
        return QString();
    // splitmix64 over the seed — an independent stream, so the kind choice
    // neither depends on nor disturbs the built-in trait roll. The pool is the
    // 34 built-ins plus every discovered user creature, each weighted equally;
    // built-in (index < A_COUNT) means "leave it to rollTraits", i.e. no kind.
    quint64 h = seed + 0x9E3779B97F4A7C15ULL;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h ^= h >> 31;
    const int idx = int(h % quint64(A_COUNT + ks.size()));
    return idx >= A_COUNT ? ks.at(idx - A_COUNT) : QString();
}

void Mascot::setMascot(quint64 seed, const QString &kind) {
    if (seed == m_seed && kind == m_kind)
        return;
    m_seed = seed;
    m_kind = kind;
    setVisible(seed != 0);
    update();
}

void Mascot::enterEvent(QEnterEvent *) {
    m_hovered = true;
    m_tick = 0;
    m_idle->start();
}

void Mascot::leaveEvent(QEvent *) {
    m_hovered = false;
    m_idle->stop();
    update(); // settle back to the rest pose
}

void Mascot::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton)
        emit clicked();
}

// "Image mode": when the user has enabled it and dropped images in the mascots
// folder, a note's seed maps to one of those images, drawn as a rounded tile
// (the whole square contained, centred, shifted by `bob` for the hover bounce)
// instead of the procedural creature. Returns false — leaving the painter
// untouched — when image mode is off or no image applies, so the caller falls
// back to drawCreature.
static bool drawImageMascot(QPainter &p, quint64 seed, int w, int h, double bob) {
    if (seed == 0 ||
        !QSettings().value(QStringLiteral("mascotImageMode"), false).toBool())
        return false;
    const QString path = MascotCatalog::shared().imageForSeed(seed);
    if (path.isEmpty())
        return false;
    QPixmap pm;
    if (!QPixmapCache::find(path, &pm)) { // decode once — these are MB-sized PNGs
        pm.load(path);
        QPixmapCache::insert(path, pm);
    }
    if (pm.isNull())
        return false;
    const QSize fit = pm.size().scaled(QSize(w, h), Qt::KeepAspectRatio);
    const QRectF r((w - fit.width()) / 2.0, (h - fit.height()) / 2.0 + bob,
                   fit.width(), fit.height());
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(r, qMin(r.width(), r.height()) * 0.16,
                        qMin(r.width(), r.height()) * 0.16);
    p.setClipPath(clip);
    p.drawPixmap(r, pm, QRectF(pm.rect()));
    return true;
}

void Mascot::paintEvent(QPaintEvent *) {
    if (m_seed == 0)
        return;
    const double bob = m_hovered ? std::sin(m_tick * 0.22) * 2.5 : 0.0;
    QPainter p(this);
    if (drawImageMascot(p, m_seed, width(), height(), bob))
        return;
    const bool blink = m_hovered && (m_tick % 78) < 4;
    drawCreature(p, m_seed, m_kind, width(), height(), bob, blink);
}

QPixmap Mascot::renderPixmap(quint64 seed, const QString &kind, QSize size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (seed != 0) {
        QPainter p(&img);
        if (!drawImageMascot(p, seed, size.width(), size.height(), 0.0))
            drawCreature(p, seed, kind, size.width(), size.height(), 0.0, false);
    }
    return QPixmap::fromImage(img);
}
