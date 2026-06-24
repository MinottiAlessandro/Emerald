#include "MathRender.h"

#include "core/Perf.h"
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QRectF>
#include <memory>
#include <vector>

namespace {

// \command (sans backslash) → Unicode glyph. A pragmatic subset that covers the
// vast majority of notes; unknown commands render as their literal name.
const QHash<QString, QString> &symbols() {
    static const QHash<QString, QString> map = {
        // Greek lower-case
        {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"},
        {"epsilon", "ε"}, {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"},
        {"theta", "θ"}, {"vartheta", "ϑ"}, {"iota", "ι"}, {"kappa", "κ"},
        {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"}, {"pi", "π"},
        {"varpi", "ϖ"}, {"rho", "ρ"}, {"varrho", "ϱ"}, {"sigma", "σ"},
        {"varsigma", "ς"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "φ"},
        {"varphi", "ϕ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"},
        // Greek upper-case
        {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"},
        {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"}, {"Upsilon", "Υ"},
        {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
        // Operators & big operators
        {"pm", "±"}, {"mp", "∓"}, {"times", "×"}, {"div", "÷"}, {"cdot", "·"},
        {"ast", "∗"}, {"star", "⋆"}, {"circ", "∘"}, {"bullet", "∙"},
        {"oplus", "⊕"}, {"ominus", "⊖"}, {"otimes", "⊗"}, {"sum", "∑"},
        {"prod", "∏"}, {"coprod", "∐"}, {"int", "∫"}, {"oint", "∮"},
        {"bigcup", "⋃"}, {"bigcap", "⋂"}, {"bigvee", "⋁"}, {"bigwedge", "⋀"},
        {"bigsqcup", "⨆"}, {"bigoplus", "⨁"}, {"bigotimes", "⨂"},
        {"partial", "∂"}, {"nabla", "∇"}, {"infty", "∞"}, {"setminus", "∖"},
        // Logic & sets
        {"forall", "∀"}, {"exists", "∃"}, {"neg", "¬"}, {"wedge", "∧"},
        {"vee", "∨"}, {"cap", "∩"}, {"cup", "∪"}, {"emptyset", "∅"},
        {"in", "∈"}, {"notin", "∉"}, {"ni", "∋"}, {"subset", "⊂"},
        {"supset", "⊃"}, {"subseteq", "⊆"}, {"supseteq", "⊇"},
        // Relations
        {"leq", "≤"}, {"le", "≤"}, {"geq", "≥"}, {"ge", "≥"}, {"neq", "≠"},
        {"ne", "≠"}, {"approx", "≈"}, {"equiv", "≡"}, {"cong", "≅"},
        {"sim", "∼"}, {"simeq", "≃"}, {"propto", "∝"}, {"ll", "≪"},
        {"gg", "≫"}, {"perp", "⊥"}, {"parallel", "∥"}, {"mid", "∣"},
        // Arrows
        {"rightarrow", "→"}, {"to", "→"}, {"leftarrow", "←"}, {"gets", "←"},
        {"Rightarrow", "⇒"}, {"Leftarrow", "⇐"}, {"leftrightarrow", "↔"},
        {"Leftrightarrow", "⇔"}, {"mapsto", "↦"}, {"uparrow", "↑"},
        {"downarrow", "↓"},
        // Delimiters (also usable bare, not just after \left/\right)
        {"langle", "⟨"}, {"rangle", "⟩"}, {"lfloor", "⌊"}, {"rfloor", "⌋"},
        {"lceil", "⌈"}, {"rceil", "⌉"},
        // Misc
        {"ldots", "…"}, {"cdots", "⋯"}, {"dots", "…"}, {"angle", "∠"},
        {"prime", "′"}, {"hbar", "ℏ"}, {"ell", "ℓ"}, {"Re", "ℜ"},
        {"Im", "ℑ"}, {"aleph", "ℵ"}, {"deg", "°"}, {"quad", "  "},
    };
    return map;
}

// A laid-out math box. The tree is built once, measured in place, then painted;
// no copies, so child links can be unique_ptr.
struct Box {
    enum Kind { Row, Sym, Scripts, Frac, Sqrt, Accent, Delim, Matrix } kind = Row;
    enum AccentKind { AcBar, AcHat, AcVec, AcTilde, AcDot, AcDDot };

    QString sym;                             // Sym: glyph(s); Delim: left glyph
    QString right;                           // Delim: right glyph
    QFont font;                              // font this box was measured with
    std::vector<std::unique_ptr<Box>> kids;  // Row/Frac[num,den]/Sqrt/Delim/Matrix
    std::unique_ptr<Box> base, sup, sub;     // Scripts / Accent(base)

    bool limits = false;   // Scripts: stack sup/sub as limits (display ∑)
    bool rule = true;      // Frac: draw the vinculum (false for \binom)
    bool fracDisplay = false; // Frac: lay parts out in display style (\dfrac)
    bool upright = false;  // Sym: roman (\text, \operatorname) not italic
    bool bold = false;     // Sym: bold (\textbf)
    AccentKind accent = AcBar;
    int rows = 0, cols = 0; // Matrix grid (kids are row-major)

    double w = 0, asc = 0, desc = 0; // metrics, filled by layout()
};

using BoxPtr = std::unique_ptr<Box>;

// Big operators that, in display style, carry their scripts stacked above and
// below (limits) rather than set to the side, e.g. ∑ from i=1 to n.
bool takesLimits(const QString &sym) {
    static const QString ops = QStringLiteral("∑∏∐⋃⋂⋁⋀⨆⨁⨂");
    return sym.size() == 1 && ops.contains(sym);
}

BoxPtr makeSym(const QString &s) {
    auto b = std::make_unique<Box>();
    b->kind = Box::Sym;
    b->sym = s;
    return b;
}

// --- Recursive-descent parser ------------------------------------------------
// parseRow stops at any token flagged in `stops`, leaving it unconsumed for the
// enclosing construct (a group's }, \left's \right, a matrix's & / \\ / \end).
enum Stop {
    StopBrace = 1,    // }
    StopRight = 2,    // \right
    StopAmp = 4,      // & (matrix column break)
    StopRowBreak = 8, // \\ (matrix row break)
    StopEnd = 16,     // \end
};

struct Parser {
    const QString &s;
    int i = 0;

    explicit Parser(const QString &str) : s(str) {}

    // The command name starting at i (without consuming), or empty.
    QString peekName() const {
        if (i >= s.size() || s.at(i) != QLatin1Char('\\'))
            return QString();
        int j = i + 1;
        QString n;
        while (j < s.size() && s.at(j).isLetter())
            n.append(s.at(j++));
        return n;
    }
    bool eatName(const QString &name) { // consume \name if present
        if (peekName() == name) {
            i += 1 + name.size();
            return true;
        }
        return false;
    }
    // Math mode ignores inter-token whitespace (so "\frac{a}\n  {b}" pairs up
    // across lines). \text keeps its spaces — it reads raw, not through here.
    void skipSpaces() {
        while (i < s.size() &&
               (s.at(i) == QLatin1Char(' ') || s.at(i) == QLatin1Char('\t')))
            ++i;
    }

    BoxPtr parseRow(int stops) {
        auto row = std::make_unique<Box>();
        row->kind = Box::Row;
        while (i < s.size()) {
            skipSpaces();
            if (i >= s.size())
                break;
            const QChar c = s.at(i);
            if ((stops & StopBrace) && c == QLatin1Char('}'))
                break;
            if ((stops & StopAmp) && c == QLatin1Char('&'))
                break;
            if ((stops & StopRowBreak) && c == QLatin1Char('\\') &&
                i + 1 < s.size() && s.at(i + 1) == QLatin1Char('\\'))
                break;
            if (c == QLatin1Char('\\')) {
                const QString n = peekName();
                if ((stops & StopRight) && n == QLatin1String("right"))
                    break;
                if ((stops & StopEnd) && n == QLatin1String("end"))
                    break;
            }
            BoxPtr el;
            if (c == QLatin1Char('^') || c == QLatin1Char('_'))
                el = makeSym(QString()); // a script with an empty base
            else
                el = parseElement();
            if (!el)
                break;
            // Trailing ^ / _ bind to the element just parsed.
            if (i < s.size() &&
                (s.at(i) == QLatin1Char('^') || s.at(i) == QLatin1Char('_')))
                el = parseScripts(std::move(el));
            row->kids.push_back(std::move(el));
        }
        return row;
    }

    BoxPtr parseScripts(BoxPtr base) {
        auto sc = std::make_unique<Box>();
        sc->kind = Box::Scripts;
        sc->base = std::move(base);
        while (i < s.size() &&
               (s.at(i) == QLatin1Char('^') || s.at(i) == QLatin1Char('_'))) {
            const bool sup = s.at(i) == QLatin1Char('^');
            ++i; // consume ^ or _
            skipSpaces();
            BoxPtr e = parseElement();
            if (!e)
                e = makeSym(QString());
            if (sup)
                sc->sup = std::move(e);
            else
                sc->sub = std::move(e);
        }
        return sc;
    }

    // One indivisible unit: a {group}, a \command, or a single character (a
    // surrogate pair — emoji etc. — counts as one).
    BoxPtr parseElement() {
        if (i >= s.size())
            return nullptr;
        const QChar c = s.at(i);
        if (c == QLatin1Char('{')) {
            ++i; // consume {
            BoxPtr row = parseRow(StopBrace);
            if (i < s.size() && s.at(i) == QLatin1Char('}'))
                ++i; // consume }
            return row;
        }
        if (c == QLatin1Char('\\'))
            return parseCommand();
        QString glyph(c);
        ++i;
        if (c.isHighSurrogate() && i < s.size() && s.at(i).isLowSurrogate())
            glyph.append(s.at(i++)); // keep emoji / astral chars whole
        return makeSym(glyph);
    }

    // An argument: like parseElement() but never null, so a truncated formula
    // (e.g. "\frac2" with no denominator) lays out as an empty box instead of
    // leaving a null child for layout() to dereference.
    BoxPtr parseArg() {
        skipSpaces();
        BoxPtr e = parseElement();
        return e ? std::move(e) : makeSym(QString());
    }

    // The literal text of a {…} argument (no math parsing) — for \text etc.
    QString parseTextArg() {
        QString out;
        if (i < s.size() && s.at(i) == QLatin1Char('{')) {
            ++i;
            int depth = 1;
            while (i < s.size() && depth > 0) {
                const QChar c = s.at(i);
                if (c == QLatin1Char('{'))
                    ++depth;
                else if (c == QLatin1Char('}') && --depth == 0) {
                    ++i;
                    break;
                }
                out.append(c);
                ++i;
            }
        } else if (i < s.size()) {
            out.append(s.at(i++));
        }
        return out;
    }

    // A delimiter token after \left / \right: a char like ( [ | or a command
    // like \{ \langle, or \. (the null delimiter → empty string).
    QString parseDelim() {
        skipSpaces();
        if (i >= s.size())
            return QString();
        if (s.at(i) == QLatin1Char('\\')) {
            ++i;
            QString n;
            while (i < s.size() && s.at(i).isLetter())
                n.append(s.at(i++));
            if (n.isEmpty()) { // \{ \} \| \.
                if (i >= s.size())
                    return QString();
                const QChar d = s.at(i++);
                if (d == QLatin1Char('|'))
                    return QStringLiteral("‖");
                if (d == QLatin1Char('.'))
                    return QString(); // null delimiter
                return QString(d);
            }
            if (n == QLatin1String("langle"))
                return QStringLiteral("⟨");
            if (n == QLatin1String("rangle"))
                return QStringLiteral("⟩");
            if (n == QLatin1String("vert") || n == QLatin1String("lvert") ||
                n == QLatin1String("rvert"))
                return QStringLiteral("|");
            if (n == QLatin1String("Vert") || n == QLatin1String("lVert") ||
                n == QLatin1String("rVert"))
                return QStringLiteral("‖");
            const auto it = symbols().constFind(n);
            return it != symbols().constEnd() ? it.value() : QString();
        }
        const QChar c = s.at(i++);
        if (c == QLatin1Char('.'))
            return QString(); // null delimiter
        return QString(c);
    }

    BoxPtr makeFrac(bool rule, bool display) {
        auto f = std::make_unique<Box>();
        f->kind = Box::Frac;
        f->rule = rule;
        f->fracDisplay = display;
        f->kids.push_back(parseArg()); // numerator
        f->kids.push_back(parseArg()); // denominator
        return f;
    }

    BoxPtr makeAccent(Box::AccentKind kind) {
        auto a = std::make_unique<Box>();
        a->kind = Box::Accent;
        a->accent = kind;
        a->base = parseArg();
        return a;
    }

    BoxPtr makeText(bool bold) {
        auto t = makeSym(parseTextArg());
        t->upright = true;
        t->bold = bold;
        return t;
    }

    BoxPtr makeDelims(const QString &l, const QString &r, BoxPtr inner) {
        auto d = std::make_unique<Box>();
        d->kind = Box::Delim;
        d->sym = l;
        d->right = r;
        d->kids.push_back(std::move(inner));
        return d;
    }

    BoxPtr parseMatrix(const QString &env) {
        auto m = std::make_unique<Box>();
        m->kind = Box::Matrix;
        std::vector<std::vector<BoxPtr>> grid;
        grid.emplace_back();
        while (i < s.size()) {
            grid.back().push_back(
                parseRow(StopAmp | StopRowBreak | StopEnd | StopRight));
            if (i < s.size() && s.at(i) == QLatin1Char('&')) {
                ++i; // next column
            } else if (i + 1 < s.size() && s.at(i) == QLatin1Char('\\') &&
                       s.at(i + 1) == QLatin1Char('\\')) {
                i += 2; // next row
                grid.emplace_back();
            } else {
                eatName(QStringLiteral("end"));
                parseTextArg(); // consume {env}
                break;
            }
        }
        // Drop a trailing empty row (from a final \\).
        if (grid.size() > 1 && grid.back().size() == 1 &&
            grid.back()[0]->kids.empty())
            grid.pop_back();
        m->rows = static_cast<int>(grid.size());
        for (auto &r : grid)
            m->cols = qMax(m->cols, static_cast<int>(r.size()));
        for (auto &r : grid) {
            for (int c = 0; c < m->cols; ++c) {
                if (c < static_cast<int>(r.size()))
                    m->kids.push_back(std::move(r[c]));
                else
                    m->kids.push_back(std::make_unique<Box>()); // empty cell
            }
        }
        // pmatrix/bmatrix/… wrap the grid in growing delimiters.
        if (env == QLatin1String("pmatrix"))
            return makeDelims("(", ")", std::move(m));
        if (env == QLatin1String("bmatrix"))
            return makeDelims("[", "]", std::move(m));
        if (env == QLatin1String("Bmatrix"))
            return makeDelims("{", "}", std::move(m));
        if (env == QLatin1String("vmatrix"))
            return makeDelims("|", "|", std::move(m));
        if (env == QLatin1String("Vmatrix"))
            return makeDelims("‖", "‖", std::move(m));
        return m;
    }

    BoxPtr parseCommand() {
        ++i; // consume backslash
        QString name;
        while (i < s.size() && s.at(i).isLetter())
            name.append(s.at(i++));
        if (name.isEmpty()) { // an escaped symbol like "\{" or "\,"
            if (i < s.size())
                return makeSym(QString(s.at(i++)));
            return makeSym(QStringLiteral("\\"));
        }
        if (name == QLatin1String("frac") || name == QLatin1String("tfrac"))
            return makeFrac(true, false);
        if (name == QLatin1String("dfrac"))
            return makeFrac(true, true);
        if (name == QLatin1String("binom") || name == QLatin1String("tbinom"))
            return makeDelims("(", ")", makeFrac(false, false));
        if (name == QLatin1String("dbinom"))
            return makeDelims("(", ")", makeFrac(false, true));
        if (name == QLatin1String("sqrt")) {
            auto sq = std::make_unique<Box>();
            sq->kind = Box::Sqrt;
            sq->kids.push_back(parseArg());
            return sq;
        }
        if (name == QLatin1String("hat"))
            return makeAccent(Box::AcHat);
        if (name == QLatin1String("widehat"))
            return makeAccent(Box::AcHat);
        if (name == QLatin1String("bar"))
            return makeAccent(Box::AcBar);
        if (name == QLatin1String("overline"))
            return makeAccent(Box::AcBar);
        if (name == QLatin1String("vec"))
            return makeAccent(Box::AcVec);
        if (name == QLatin1String("overrightarrow"))
            return makeAccent(Box::AcVec);
        if (name == QLatin1String("tilde") || name == QLatin1String("widetilde"))
            return makeAccent(Box::AcTilde);
        if (name == QLatin1String("dot"))
            return makeAccent(Box::AcDot);
        if (name == QLatin1String("ddot"))
            return makeAccent(Box::AcDDot);
        if (name == QLatin1String("text") || name == QLatin1String("mathrm") ||
            name == QLatin1String("operatorname"))
            return makeText(false);
        if (name == QLatin1String("textbf") || name == QLatin1String("mathbf"))
            return makeText(true);
        if (name == QLatin1String("left")) {
            const QString l = parseDelim();
            BoxPtr inner = parseRow(StopRight);
            QString r;
            if (eatName(QStringLiteral("right")))
                r = parseDelim();
            return makeDelims(l, r, std::move(inner));
        }
        if (name == QLatin1String("begin"))
            return parseMatrix(parseTextArg());
        const auto it = symbols().constFind(name);
        return makeSym(it != symbols().constEnd() ? it.value() : name);
    }
};

// A Sym's effective font: the row font, made roman for \text and bold for
// \textbf so those read as upright prose inside a formula.
QFont symFont(const Box &b, const QFont &f) {
    if (!b.upright && !b.bold)
        return f;
    QFont sf = f;
    if (b.upright)
        sf.setItalic(false);
    if (b.bold)
        sf.setBold(true);
    return sf;
}

// Matrix cell geometry, recomputed identically in layout() and paintBox().
struct Grid {
    std::vector<double> colW, rowAsc, rowDesc;
    double colGap = 0, rowGap = 0, w = 0, h = 0;
};
Grid gridMetrics(const Box &b, const QFontMetricsF &fm) {
    Grid g;
    g.colW.assign(b.cols, 0);
    g.rowAsc.assign(b.rows, 0);
    g.rowDesc.assign(b.rows, 0);
    for (int r = 0; r < b.rows; ++r)
        for (int c = 0; c < b.cols; ++c) {
            const Box &cell = *b.kids[r * b.cols + c];
            g.colW[c] = qMax(g.colW[c], cell.w);
            g.rowAsc[r] = qMax(g.rowAsc[r], cell.asc);
            g.rowDesc[r] = qMax(g.rowDesc[r], cell.desc);
        }
    g.colGap = fm.averageCharWidth() * 1.4;
    g.rowGap = fm.lineSpacing() * 0.35;
    for (double cw : g.colW)
        g.w += cw;
    if (b.cols > 0)
        g.w += g.colGap * (b.cols - 1);
    for (int r = 0; r < b.rows; ++r)
        g.h += g.rowAsc[r] + g.rowDesc[r];
    if (b.rows > 0)
        g.h += g.rowGap * (b.rows - 1);
    return g;
}

// --- Layout: fill w / asc / desc, depth-first --------------------------------
// `display` is true for $$…$$ blocks: there big operators set their scripts as
// stacked limits. It does not propagate into fractions (their parts are always
// textstyle, matching TeX).
void layout(Box &b, const QFont &f, bool display) {
    b.font = f;
    QFontMetricsF fm(f);
    switch (b.kind) {
    case Box::Sym: {
        const QFont sf = symFont(b, f);
        b.font = sf;
        QFontMetricsF sfm(sf);
        b.w = sfm.horizontalAdvance(b.sym);
        b.asc = sfm.ascent();
        b.desc = sfm.descent();
        break;
    }
    case Box::Row: {
        double w = 0, asc = 0, desc = 0;
        for (auto &k : b.kids) {
            layout(*k, f, display);
            w += k->w;
            asc = qMax(asc, k->asc);
            desc = qMax(desc, k->desc);
        }
        b.w = w;
        b.asc = asc;
        b.desc = desc;
        break;
    }
    case Box::Scripts: {
        QFont sf = f;
        sf.setPointSizeF(qMax(1.0, f.pointSizeF() * 0.7));
        b.limits = display && b.base && b.base->kind == Box::Sym &&
                   takesLimits(b.base->sym);
        if (b.limits) {
            // Enlarge the operator and stack the scripts centred over/under it.
            QFont bf = f;
            bf.setPointSizeF(f.pointSizeF() * 1.4);
            layout(*b.base, bf, false);
            const double gap = QFontMetricsF(bf).lineSpacing() * 0.1 + 1.0;
            double w = b.base->w, asc = b.base->asc, desc = b.base->desc;
            if (b.sup) {
                layout(*b.sup, sf, false);
                asc += gap + b.sup->asc + b.sup->desc;
                w = qMax(w, b.sup->w);
            }
            if (b.sub) {
                layout(*b.sub, sf, false);
                desc += gap + b.sub->asc + b.sub->desc;
                w = qMax(w, b.sub->w);
            }
            b.w = w;
            b.asc = asc;
            b.desc = desc;
            break;
        }
        layout(*b.base, f, display);
        double w = b.base->w, asc = b.base->asc, desc = b.base->desc;
        double scriptW = 0;
        if (b.sup) {
            layout(*b.sup, sf, false);
            asc = qMax(asc, b.base->asc * 0.5 + b.sup->asc + b.sup->desc);
            scriptW = qMax(scriptW, b.sup->w);
        }
        if (b.sub) {
            layout(*b.sub, sf, false);
            desc = qMax(desc, b.base->desc * 0.3 + b.sub->asc + b.sub->desc);
            scriptW = qMax(scriptW, b.sub->w);
        }
        b.w = w + scriptW;
        b.asc = asc;
        b.desc = desc;
        break;
    }
    case Box::Frac: {
        Box &num = *b.kids[0];
        Box &den = *b.kids[1];
        layout(num, f, b.fracDisplay);
        layout(den, f, b.fracDisplay);
        const double axis = fm.xHeight() * 0.5;       // math axis above baseline
        const double gap = fm.lineSpacing() * 0.1 + 1.0;
        b.w = qMax(num.w, den.w) + 4.0;
        b.asc = axis + gap + (num.asc + num.desc);
        b.desc = (den.asc + den.desc) + gap - axis;
        break;
    }
    case Box::Sqrt: {
        Box &rad = *b.kids[0];
        layout(rad, f, display);
        const double signW = fm.averageCharWidth() * 0.9 + 4.0;
        b.w = rad.w + signW + 2.0;
        b.asc = rad.asc + 3.0; // headroom for the vinculum
        b.desc = rad.desc;
        break;
    }
    case Box::Accent: {
        Box &base = *b.base;
        layout(base, f, display);
        const double gap = fm.ascent() * 0.08;
        const double h = (b.accent == Box::AcDot || b.accent == Box::AcDDot)
                             ? fm.ascent() * 0.12
                             : fm.ascent() * 0.18;
        b.w = base.w;
        b.asc = base.asc + gap + h;
        b.desc = base.desc;
        break;
    }
    case Box::Delim: {
        Box &inner = *b.kids[0];
        layout(inner, f, display);
        const double pad = fm.ascent() * 0.05;
        b.asc = inner.asc + pad;
        b.desc = inner.desc + pad;
        const double lw = b.sym.isEmpty() ? 0 : fm.horizontalAdvance(b.sym);
        const double rw = b.right.isEmpty() ? 0 : fm.horizontalAdvance(b.right);
        b.w = lw + inner.w + rw;
        break;
    }
    case Box::Matrix: {
        for (auto &k : b.kids)
            layout(*k, f, false);
        const Grid g = gridMetrics(b, fm);
        const double axis = fm.xHeight() * 0.5;
        b.w = g.w;
        b.asc = g.h / 2.0 + axis;
        b.desc = g.h / 2.0 - axis;
        break;
    }
    }
}

void drawDelim(QPainter &p, const QString &g, double x, double top, double bot,
               const QFont &font) {
    if (g.isEmpty())
        return;
    QFontMetricsF fm(font);
    const double glyphH = fm.ascent() + fm.descent();
    const double need = bot - top;
    const double sy = (need > 0 && glyphH > 0) ? need / glyphH : 1.0;
    p.save();
    p.setFont(font);
    p.translate(x, top); // stretch the glyph vertically to span [top, bot]
    p.scale(1.0, sy);
    p.drawText(QPointF(0, fm.ascent()), g);
    p.restore();
}

// --- Paint: draw at (x, baseline), depth-first -------------------------------
void paintBox(QPainter &p, const Box &b, double x, double baseline) {
    switch (b.kind) {
    case Box::Sym:
        if (!b.sym.isEmpty()) {
            p.setFont(b.font);
            p.drawText(QPointF(x, baseline), b.sym);
        }
        break;
    case Box::Row: {
        double cx = x;
        for (const auto &k : b.kids) {
            paintBox(p, *k, cx, baseline);
            cx += k->w;
        }
        break;
    }
    case Box::Scripts: {
        if (b.limits) {
            const double cx = x + b.w / 2.0; // centre axis
            const double gap = QFontMetricsF(b.base->font).lineSpacing() * 0.1 + 1.0;
            paintBox(p, *b.base, cx - b.base->w / 2.0, baseline);
            if (b.sup)
                paintBox(p, *b.sup, cx - b.sup->w / 2.0,
                         baseline - b.base->asc - gap - b.sup->desc);
            if (b.sub)
                paintBox(p, *b.sub, cx - b.sub->w / 2.0,
                         baseline + b.base->desc + gap + b.sub->asc);
            break;
        }
        paintBox(p, *b.base, x, baseline);
        const double sx = x + b.base->w;
        if (b.sup)
            paintBox(p, *b.sup, sx, baseline - b.base->asc * 0.5);
        if (b.sub)
            paintBox(p, *b.sub, sx, baseline + b.base->desc * 0.3 + b.sub->asc);
        break;
    }
    case Box::Frac: {
        const Box &num = *b.kids[0];
        const Box &den = *b.kids[1];
        QFontMetricsF fm(b.font);
        const double axis = fm.xHeight() * 0.5;
        const double gap = fm.lineSpacing() * 0.1 + 1.0;
        const double ruleY = baseline - axis;
        paintBox(p, num, x + (b.w - num.w) / 2.0, ruleY - gap - num.desc);
        paintBox(p, den, x + (b.w - den.w) / 2.0, ruleY + gap + den.asc);
        if (b.rule) {
            QPen pen = p.pen();
            pen.setWidthF(1.0);
            p.setPen(pen);
            p.drawLine(QPointF(x + 1.0, ruleY), QPointF(x + b.w - 1.0, ruleY));
        }
        break;
    }
    case Box::Sqrt: {
        const Box &rad = *b.kids[0];
        QFontMetricsF fm(b.font);
        const double signW = fm.averageCharWidth() * 0.9 + 4.0;
        const double top = baseline - b.asc;
        const double bot = baseline + rad.desc;
        QPen pen = p.pen();
        pen.setWidthF(1.2);
        p.setPen(pen);
        // A check-mark radical, then a vinculum across the radicand.
        const QList<QPointF> stroke{
            {x, baseline - rad.asc * 0.4},
            {x + signW * 0.35, bot},
            {x + signW * 0.6, top + 1.0},
            {x + b.w, top + 1.0}};
        p.drawPolyline(stroke);
        paintBox(p, rad, x + signW, baseline);
        break;
    }
    case Box::Accent: {
        const Box &base = *b.base;
        paintBox(p, base, x, baseline);
        QFontMetricsF fm(b.font);
        const double aw = base.w;
        const double cx = x + aw / 2.0;
        const double baseTop = baseline - base.asc;
        const double gap = fm.ascent() * 0.08;
        const double h = (b.accent == Box::AcDot || b.accent == Box::AcDDot)
                             ? fm.ascent() * 0.12
                             : fm.ascent() * 0.18;
        const double bot = baseTop - gap;       // bottom of the accent zone
        const double top = bot - h;             // top of it
        const double mid = (top + bot) / 2.0;
        QPen pen = p.pen();
        pen.setWidthF(1.2);
        p.setPen(pen);
        switch (b.accent) {
        case Box::AcBar:
            p.drawLine(QPointF(x, bot), QPointF(x + aw, bot));
            break;
        case Box::AcHat:
            p.drawPolyline(QList<QPointF>{
                {x + aw * 0.1, bot}, {cx, top}, {x + aw * 0.9, bot}});
            break;
        case Box::AcVec: {
            p.drawLine(QPointF(x, mid), QPointF(x + aw, mid));
            p.drawPolyline(QList<QPointF>{{x + aw - 3.5, mid - 2.5},
                                          {x + aw, mid},
                                          {x + aw - 3.5, mid + 2.5}});
            break;
        }
        case Box::AcTilde:
            p.drawPolyline(QList<QPointF>{{x, mid},
                                          {x + aw * 0.25, top},
                                          {x + aw * 0.5, mid},
                                          {x + aw * 0.75, bot},
                                          {x + aw, mid}});
            break;
        case Box::AcDot:
        case Box::AcDDot: {
            const double r = qMax(0.9, fm.ascent() * 0.05);
            p.save();
            p.setBrush(pen.color());
            if (b.accent == Box::AcDot)
                p.drawEllipse(QPointF(cx, mid), r, r);
            else {
                p.drawEllipse(QPointF(cx - 2.2, mid), r, r);
                p.drawEllipse(QPointF(cx + 2.2, mid), r, r);
            }
            p.restore();
            break;
        }
        }
        break;
    }
    case Box::Delim: {
        const Box &inner = *b.kids[0];
        QFontMetricsF fm(b.font);
        const double lw = b.sym.isEmpty() ? 0 : fm.horizontalAdvance(b.sym);
        const double top = baseline - b.asc, bot = baseline + b.desc;
        drawDelim(p, b.sym, x, top, bot, b.font);
        paintBox(p, inner, x + lw, baseline);
        drawDelim(p, b.right, x + lw + inner.w, top, bot, b.font);
        break;
    }
    case Box::Matrix: {
        QFontMetricsF fm(b.font);
        const Grid g = gridMetrics(b, fm);
        double y = baseline - b.asc;
        for (int r = 0; r < b.rows; ++r) {
            const double rowBaseline = y + g.rowAsc[r];
            double cx = x;
            for (int c = 0; c < b.cols; ++c) {
                const Box &cell = *b.kids[r * b.cols + c];
                paintBox(p, cell, cx + (g.colW[c] - cell.w) / 2.0, rowBaseline);
                cx += g.colW[c] + g.colGap;
            }
            y += g.rowAsc[r] + g.rowDesc[r] + g.rowGap;
        }
        break;
    }
    }
}

BoxPtr build(const QString &body) {
    Parser parser(body);
    return parser.parseRow(0);
}

QString bodyKey(const QString &body) {
    return QString::number(body.size()) + QLatin1Char(':') +
           QString::number(qHash(body, 0x8d12e7U), 16);
}

QString formulaKey(const QString &prefix, const QString &body, const QFont &font,
                   bool display) {
    return prefix + QLatin1Char(':') + bodyKey(body) + QLatin1Char(':') +
           font.toString() + QLatin1Char(':') + (display ? QLatin1Char('d')
                                                         : QLatin1Char('i'));
}

QSizeF measureUncached(const QString &body, const QFont &font, bool display) {
    BoxPtr tree = build(body);
    layout(*tree, font, display);
    return QSizeF(tree->w, tree->asc + tree->desc);
}

void paintUncached(QPainter &p, const QRectF &rect, const QString &body,
                   const QFont &font, const QColor &color, bool display,
                   MathRender::Align align) {
    BoxPtr tree = build(body);
    layout(*tree, font, display);
    const double natW = tree->w;
    const double natH = tree->asc + tree->desc;
    if (natW <= 0.0 || natH <= 0.0)
        return;

    const double scale =
        qMin(1.0, qMin(rect.width() / natW, rect.height() / natH));
    double x, baseline;
    if (align == MathRender::Align::Display) {
        // Centre the formula both ways in its block.
        x = rect.left() + (rect.width() - natW * scale) / 2.0;
        baseline =
            rect.top() + (rect.height() - natH * scale) / 2.0 + tree->asc * scale;
    } else {
        // Inline: sit on the surrounding text baseline, left-aligned, so any
        // reserved slack falls to the right and reads as ordinary spacing. A
        // formula taller than the line is top-aligned so it stays in the line
        // box rather than overlapping the line above.
        QFontMetricsF fm(font);
        const double scaledAsc = tree->asc * scale;
        x = rect.left();
        baseline = scaledAsc > fm.ascent() ? rect.top() + scaledAsc
                                           : rect.top() + fm.ascent();
    }
    p.save();
    p.setPen(color);
    p.translate(x, baseline);
    p.scale(scale, scale);
    paintBox(p, *tree, 0.0, 0.0); // baseline at y = 0
    p.restore();
}

} // namespace

namespace MathRender {

const QRegularExpression &pattern() {
    // A single-line $…$ whose body has no surrounding spaces (so "$5 and $10"
    // stays currency, not a formula) and which isn't part of a $$ fence.
    static const QRegularExpression re(
        QStringLiteral("(?<![\\\\$])\\$(?!\\$)(\\S(?:[^$\\n]*\\S)?)\\$(?!\\$)"));
    return re;
}

const QRegularExpression &displayPattern() {
    // A whole line that is $$ … $$ (with optional surrounding whitespace).
    static const QRegularExpression re(
        QStringLiteral("^\\s*\\$\\$\\s*(\\S(?:.*\\S)?)\\s*\\$\\$\\s*$"));
    return re;
}

bool opensBlock(const QString &line) {
    static const QRegularExpression re(QStringLiteral("^\\s*\\$\\$"));
    return re.match(line).hasMatch() && !displayPattern().match(line).hasMatch();
}

QString bodyAfterOpen(const QString &line) {
    const int p = line.indexOf(QStringLiteral("$$"));
    return p < 0 ? QString() : line.mid(p + 2);
}

QString bodyBeforeClose(const QString &line) {
    const int p = line.indexOf(QStringLiteral("$$"));
    return p < 0 ? line : line.left(p);
}

QFont mathFont(const QFont &base, bool display) {
    QFont f = base;
    f.setItalic(true);
    // Inline math sits a touch larger than the surrounding text so symbols read
    // clearly; display math is larger still so a $$ block reads as a centred
    // display rather than inline text.
    f.setPointSizeF(qMax(1.0, base.pointSizeF() * (display ? 1.3 : 1.12)));
    return f;
}

QVector<Span> spans(const QString &line) {
    QVector<Span> out;
    auto it = pattern().globalMatch(line);
    while (it.hasNext()) {
        const auto m = it.next();
        out.push_back({static_cast<int>(m.capturedStart(0)),
                       static_cast<int>(m.capturedLength(0)), m.captured(1)});
    }
    return out;
}

QSizeF measure(const QString &body, const QFont &font, bool display) {
    EMERALD_PROFILE_SCOPE("MathRender::measure");
    static QHash<QString, QSizeF> cache;
    static QStringList order;
    const QString key = formulaKey(QStringLiteral("measure"), body, font, display);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    const QSizeF size = measureUncached(body, font, display);
    cache.insert(key, size);
    order << key;
    constexpr int kMaxEntries = 512;
    while (order.size() > kMaxEntries)
        cache.remove(order.takeFirst());
    return size;
}

void paint(QPainter &p, const QRectF &rect, const QString &body,
           const QFont &font, const QColor &color, Align align) {
    EMERALD_PROFILE_SCOPE("MathRender::paint");
    const QSize pixelSize =
        (rect.size() * p.device()->devicePixelRatioF()).toSize();
    if (pixelSize.isEmpty())
        return;
    const bool display = align == Align::Display;
    const QString key = formulaKey(QStringLiteral("paint"), body, font, display) +
                        QLatin1Char(':') + color.name(QColor::HexArgb) +
                        QLatin1Char(':') + QString::number(pixelSize.width()) +
                        QLatin1Char('x') + QString::number(pixelSize.height()) +
                        QLatin1Char(':') +
                        QString::number(p.device()->devicePixelRatioF(), 'f', 2);
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) {
        p.drawPixmap(rect.topLeft(), cached);
        return;
    }

    QPixmap pm(pixelSize);
    pm.setDevicePixelRatio(p.device()->devicePixelRatioF());
    pm.fill(Qt::transparent);
    QPainter cachePainter(&pm);
    paintUncached(cachePainter, QRectF(QPointF(0, 0), rect.size()), body, font,
                  color, display, align);
    cachePainter.end();
    QPixmapCache::insert(key, pm);
    p.drawPixmap(rect.topLeft(), pm);
}

} // namespace MathRender
