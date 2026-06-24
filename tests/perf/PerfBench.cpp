#include "core/SearchIndex.h"
#include "core/Vault.h"
#include "ui/MarkdownEditor.h"
#include "ui/Mascot.h"
#include "ui/MathRender.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtGlobal>

#if defined(Q_OS_LINUX) || defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <sys/resource.h>
#endif

#include <algorithm>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf")

namespace {

struct Sample {
    QString name;
    double ms = 0.0;
};

qint64 peakRssKb() {
#if defined(Q_OS_LINUX) || defined(__linux__)
    QFile f(QStringLiteral("/proc/self/status"));
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            if (line.startsWith("VmHWM:")) {
                const QList<QByteArray> parts = line.simplified().split(' ');
                if (parts.size() >= 2)
                    return parts.at(1).toLongLong();
            }
        }
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return -1;
    return usage.ru_maxrss;
#elif defined(Q_OS_MACOS)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return -1;
    return usage.ru_maxrss;
#else
    return -1;
#endif
}

QString noteBody(int index, int wordsPerNote) {
    static const QStringList words{
        QStringLiteral("emerald"), QStringLiteral("vault"), QStringLiteral("markdown"),
        QStringLiteral("search"), QStringLiteral("index"), QStringLiteral("render"),
        QStringLiteral("formula"), QStringLiteral("canvas"), QStringLiteral("link"),
        QStringLiteral("folder"), QStringLiteral("performance"), QStringLiteral("cache"),
        QStringLiteral("latency"), QStringLiteral("memory"), QStringLiteral("profile"),
        QStringLiteral("delta"), QStringLiteral("syntax"), QStringLiteral("editor")};

    QString body;
    QTextStream out(&body);
    if (index % 7 == 0)
        out << "<!-- mascot: " << quint64(index + 1) * 2654435761ULL << " -->\n";
    out << "# Synthetic Note " << index << "\n\n";
    out << "Linked to [[Note " << ((index + 1) % 997) << "]] and [[Note "
        << ((index + 17) % 997) << "|alias]].\n\n";
    if (index % 5 == 0)
        out << "Inline math $x_" << index % 13 << "^2 + \\frac{a}{b}$ appears here.\n\n";
    if (index % 11 == 0)
        out << "```cpp\nint value = " << index << ";\nreturn value;\n```\n\n";
    if (index % 13 == 0)
        out << "$$\\sum_{i=1}^{n} \\frac{i^2}{n}$$\n\n";

    for (int i = 0; i < wordsPerNote; ++i) {
        const QString w = words.at((index * 31 + i * 7) % words.size());
        if (i % 17 == 0)
            out << "\n- [ ] task " << i << " ";
        out << w << ' ';
        if (i % 29 == 0)
            out << "**bold** ==highlight== ~~strike~~ ";
    }
    out << '\n';
    return body;
}

void makeVault(const QString &root, int notes, int wordsPerNote) {
    QDir dir(root);
    dir.mkpath(QStringLiteral("area-a"));
    dir.mkpath(QStringLiteral("area-b/nested"));
    for (int i = 0; i < notes; ++i) {
        QString folder;
        if (i % 3 == 1)
            folder = QStringLiteral("area-a");
        else if (i % 3 == 2)
            folder = QStringLiteral("area-b/nested");
        const QString title = QStringLiteral("Note %1").arg(i, 5, 10, QLatin1Char('0'));
        QFile f(folder.isEmpty() ? dir.filePath(title + QStringLiteral(".md"))
                                 : dir.filePath(folder + QLatin1Char('/') + title +
                                                QStringLiteral(".md")));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            continue;
        f.write(noteBody(i, wordsPerNote).toUtf8());
    }
}

template <typename Fn>
double timeMs(Fn fn) {
    QElapsedTimer timer;
    timer.start();
    fn();
    return double(timer.nsecsElapsed()) / 1000000.0;
}

double median(QVector<double> values) {
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

double percentile(QVector<double> values, double p) {
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const int idx = qBound(0, int((values.size() - 1) * p), values.size() - 1);
    return values.at(idx);
}

struct ChunkedIndexResult {
    double firstChunkMs = 0.0;
    double totalMs = 0.0;
    int chunks = 0;
};

ChunkedIndexResult chunkedIndex(const Vault &vault) {
    SearchIndex index;
    ChunkedIndexResult result;
    int pos = 0;
    QElapsedTimer total;
    total.start();
    while (pos < vault.notes().size()) {
        QElapsedTimer chunk;
        chunk.start();
        int indexed = 0;
        while (pos < vault.notes().size()) {
            const Note note = vault.notes().at(pos++);
            index.updateNote(note.path, note.title, vault.read(note.path));
            ++indexed;
            if (indexed >= 24 || chunk.elapsed() >= 8)
                break;
        }
        const double chunkMs = double(chunk.nsecsElapsed()) / 1000000.0;
        if (result.chunks == 0)
            result.firstChunkMs = chunkMs;
        ++result.chunks;
    }
    result.totalMs = double(total.nsecsElapsed()) / 1000000.0;
    return result;
}

void addMetric(QJsonArray &metrics, const QString &name, double value,
               const QString &unit) {
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("value"), value);
    o.insert(QStringLiteral("unit"), unit);
    metrics.append(o);
}

} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("emerald_perf_tests"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Emerald development performance benchmarks"));
    parser.addHelpOption();
    QCommandLineOption notesOpt(QStringLiteral("notes"), QStringLiteral("Synthetic note count."),
                                QStringLiteral("count"), QStringLiteral("2500"));
    QCommandLineOption wordsOpt(QStringLiteral("words"), QStringLiteral("Words per synthetic note."),
                                QStringLiteral("count"), QStringLiteral("220"));
    QCommandLineOption queriesOpt(QStringLiteral("queries"), QStringLiteral("Search query repetitions."),
                                  QStringLiteral("count"), QStringLiteral("80"));
    QCommandLineOption jsonOpt(QStringLiteral("json"), QStringLiteral("Write JSON metrics to file."),
                               QStringLiteral("path"));
    parser.addOption(notesOpt);
    parser.addOption(wordsOpt);
    parser.addOption(queriesOpt);
    parser.addOption(jsonOpt);
    parser.process(app);

    const int notes = parser.value(notesOpt).toInt();
    const int words = parser.value(wordsOpt).toInt();
    const int queryRuns = parser.value(queriesOpt).toInt();

    QJsonArray metrics;
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 2;

    addMetric(metrics, QStringLiteral("generate_vault"),
              timeMs([&] { makeVault(tmp.path(), notes, words); }), QStringLiteral("ms"));

    Vault vault(tmp.path());
    addMetric(metrics, QStringLiteral("vault_scan"),
              timeMs([&] { vault.scan(); }), QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("notes_indexed"), vault.notes().size(),
              QStringLiteral("count"));

    SearchIndex index;
    addMetric(metrics, QStringLiteral("search_rebuild"),
              timeMs([&] { index.rebuild(vault); }), QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("rss_after_rebuild"), peakRssKb(),
              QStringLiteral("KiB"));

    const ChunkedIndexResult chunked = chunkedIndex(vault);
    addMetric(metrics, QStringLiteral("startup_index_first_chunk"),
              chunked.firstChunkMs, QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("startup_index_complete"),
              chunked.totalMs, QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("startup_index_chunks"),
              chunked.chunks, QStringLiteral("count"));

    const QStringList queries{
        QStringLiteral("emerald"), QStringLiteral("markdown search"),
        QStringLiteral("performance cache"), QStringLiteral("formula render"),
        QStringLiteral("vault link")};
    QVector<double> searchTimes;
    for (int i = 0; i < queryRuns; ++i) {
        const QString q = queries.at(i % queries.size());
        searchTimes << timeMs([&] { volatile int n = index.search(q, 30).size(); Q_UNUSED(n); });
    }
    addMetric(metrics, QStringLiteral("search_p50"), median(searchTimes), QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("search_p95"), percentile(searchTimes, 0.95), QStringLiteral("ms"));

    const Note updateNote = vault.notes().at(qMin(10, vault.notes().size() - 1));
    QString updated = vault.read(updateNote.path);
    updated += QStringLiteral("\nperformance regression sentinel unique-token\n");
    addMetric(metrics, QStringLiteral("search_update_note"),
              timeMs([&] { index.updateNote(updateNote.path, updateNote.title, updated); }),
              QStringLiteral("ms"));

    MarkdownEditor editor;
    editor.resize(820, 720);
    const QString bigDoc = noteBody(4242, qMax(words * 8, 2000));
    addMetric(metrics, QStringLiteral("editor_set_plain_text"),
              timeMs([&] {
                  editor.setPlainText(bigDoc);
                  app.processEvents();
              }),
              QStringLiteral("ms"));
    QImage editorImage(editor.size(), QImage::Format_ARGB32_Premultiplied);
    addMetric(metrics, QStringLiteral("editor_render_viewport"),
              timeMs([&] {
                  editorImage.fill(Qt::transparent);
                  QPainter p(&editorImage);
                  editor.render(&p);
              }),
              QStringLiteral("ms"));

    const QStringList formulas{
        QStringLiteral("\\frac{a+b}{\\sqrt{x^2+y^2}}"),
        QStringLiteral("\\sum_{i=1}^{n} \\frac{i^2}{n}"),
        QStringLiteral("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}")};
    QImage mathImage(640, 120, QImage::Format_ARGB32_Premultiplied);
    QFont mathBase = editor.font();
    addMetric(metrics, QStringLiteral("math_measure_and_paint"),
              timeMs([&] {
                  for (int i = 0; i < 300; ++i) {
                      const QString f = formulas.at(i % formulas.size());
                      MathRender::measure(f, MathRender::mathFont(mathBase, true), true);
                      mathImage.fill(Qt::transparent);
                      QPainter p(&mathImage);
                      MathRender::paint(p, QRectF(0, 0, 640, 120), f,
                                        MathRender::mathFont(mathBase, true),
                                        QColor(0x6f, 0xcf, 0xc0),
                                        MathRender::Align::Display);
                  }
              }),
              QStringLiteral("ms"));

    addMetric(metrics, QStringLiteral("mascot_render_unique_500"),
              timeMs([&] {
                  for (int i = 0; i < 500; ++i)
                      Mascot::renderPixmap(quint64(i + 1) * 11400714819323198485ULL,
                                           QString(), QSize(176, 196));
              }),
              QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("mascot_render_repeated_500"),
              timeMs([&] {
                  const quint64 seed = 11400714819323198485ULL;
                  for (int i = 0; i < 500; ++i)
                      Mascot::renderPixmap(seed, QString(), QSize(176, 196));
              }),
              QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("rss_final"), peakRssKb(), QStringLiteral("KiB"));

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("emerald-perf-v1"));
    root.insert(QStringLiteral("version"), QStringLiteral(EMERALD_VERSION));
    root.insert(QStringLiteral("notes"), notes);
    root.insert(QStringLiteral("words_per_note"), words);
    root.insert(QStringLiteral("metrics"), metrics);

    const QJsonDocument doc(root);
    if (parser.isSet(jsonOpt)) {
        QFile out(parser.value(jsonOpt));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        out.write(doc.toJson(QJsonDocument::Indented));
    }

    QTextStream(stdout) << doc.toJson(QJsonDocument::Indented);
    return 0;
}
