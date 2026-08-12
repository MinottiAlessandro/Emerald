#include "core/LinkGraphIndex.h"
#include "core/SearchIndex.h"
#include "core/Vault.h"
#include "ui/GraphView.h"
#include "ui/MarkdownEditor.h"
#include "ui/Mascot.h"
#include "ui/MathRender.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QtGlobal>

#if defined(Q_OS_LINUX) || defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

#include <algorithm>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf")

namespace {

struct Sample {
    QString name;
    double ms = 0.0;
};

enum class VaultProfile {
    Classic,
    Light,
    Mixed,
    Heavy,
};

VaultProfile parseProfile(const QString &value, bool *ok) {
    const QString normalized = value.toLower();
    if (normalized == QStringLiteral("classic")) {
        *ok = true;
        return VaultProfile::Classic;
    }
    if (normalized == QStringLiteral("light")) {
        *ok = true;
        return VaultProfile::Light;
    }
    if (normalized == QStringLiteral("mixed")) {
        *ok = true;
        return VaultProfile::Mixed;
    }
    if (normalized == QStringLiteral("heavy")) {
        *ok = true;
        return VaultProfile::Heavy;
    }
    *ok = false;
    return VaultProfile::Mixed;
}

QString profileName(VaultProfile profile) {
    switch (profile) {
    case VaultProfile::Classic:
        return QStringLiteral("classic");
    case VaultProfile::Light:
        return QStringLiteral("light");
    case VaultProfile::Mixed:
        return QStringLiteral("mixed");
    case VaultProfile::Heavy:
        return QStringLiteral("heavy");
    }
    return QStringLiteral("mixed");
}

bool richProfile(VaultProfile profile) {
    return profile == VaultProfile::Mixed || profile == VaultProfile::Heavy;
}

bool heavyProfile(VaultProfile profile) {
    return profile == VaultProfile::Heavy;
}

int attachmentCount(VaultProfile profile) {
    switch (profile) {
    case VaultProfile::Classic:
        return 0;
    case VaultProfile::Light:
        return 0;
    case VaultProfile::Mixed:
        return 12;
    case VaultProfile::Heavy:
        return 48;
    }
    return 0;
}

qint64 linuxStatusKb(const char *field) {
#if defined(Q_OS_LINUX) || defined(__linux__)
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray prefix = QByteArray(field) + ':';
    while (!f.atEnd()) {
        const QList<QByteArray> parts = f.readLine().simplified().split(' ');
        if (parts.size() >= 2 && parts.at(0) == prefix)
            return parts.at(1).toLongLong();
    }
#else
    Q_UNUSED(field);
#endif
    return -1;
}

qint64 currentRssKb();

qint64 peakRssKb() {
#if defined(Q_OS_LINUX) || defined(__linux__)
    const qint64 hwm = linuxStatusKb("VmHWM");
    const qint64 rss = currentRssKb();
    if (hwm >= 0 && rss >= 0)
        return qMax(hwm, rss);
    if (hwm >= 0)
        return hwm;
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

qint64 currentRssKb() {
#if defined(Q_OS_LINUX) || defined(__linux__)
    QFile f(QStringLiteral("/proc/self/statm"));
    if (!f.open(QIODevice::ReadOnly))
        return linuxStatusKb("VmRSS");
    const QList<QByteArray> parts = f.readAll().simplified().split(' ');
    if (parts.size() < 2)
        return linuxStatusKb("VmRSS");
    const qint64 residentPages = parts.at(1).toLongLong();
    const long pageSize = sysconf(_SC_PAGESIZE);
    return pageSize > 0 ? residentPages * qint64(pageSize) / 1024
                        : linuxStatusKb("VmRSS");
#elif defined(Q_OS_MACOS)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return static_cast<qint64>(info.resident_size / 1024);
#else
    return -1;
#endif
}

QString folderForNote(int index, VaultProfile profile) {
    if (profile == VaultProfile::Light)
        return index % 8 == 0 ? QStringLiteral("reference") : QString();
    if (profile == VaultProfile::Classic || profile == VaultProfile::Mixed) {
        if (index % 3 == 1)
            return QStringLiteral("area-a");
        if (index % 3 == 2)
            return QStringLiteral("area-b/nested");
        return QString();
    }

    switch (index % 8) {
    case 0:
        return QString();
    case 1:
        return QStringLiteral("projects/alpha");
    case 2:
        return QStringLiteral("projects/beta/research");
    case 3:
        return QStringLiteral("daily/2026/q3/week-01");
    case 4:
        return QStringLiteral("resources/books/technical");
    case 5:
        return QStringLiteral("resources/images/contact-sheets");
    case 6:
        return QStringLiteral("archive/clients/acme/meetings");
    default:
        return QStringLiteral("archive/clients/zephyr/notes/deep");
    }
}

QString attachmentRef(const QString &folder, int index) {
    QString prefix;
    if (!folder.isEmpty()) {
        const int depth = folder.count(QLatin1Char('/')) + 1;
        for (int i = 0; i < depth; ++i)
            prefix += QStringLiteral("../");
    }
    return prefix + QStringLiteral("_attachments/image-%1.png")
                        .arg(index, 2, 10, QLatin1Char('0'));
}

void makeAttachments(const QString &root, VaultProfile profile) {
    const int count = attachmentCount(profile);
    if (count == 0)
        return;

    QDir dir(root);
    dir.mkpath(QStringLiteral("_attachments"));
    const QSize size = heavyProfile(profile) ? QSize(960, 540) : QSize(480, 270);
    for (int i = 0; i < count; ++i) {
        QImage image(size, QImage::Format_RGB32);
        QPainter painter(&image);
        const QColor base = QColor::fromHsv((i * 37) % 360, 130, 48);
        const QColor accent = QColor::fromHsv((i * 37 + 120) % 360, 170, 210);
        painter.fillRect(image.rect(), base);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(QRect(size.width() / 8, size.height() / 8,
                                  size.width() / 2, size.height() / 2));
        painter.setBrush(QColor(245, 245, 245, 190));
        painter.drawRect(QRect(size.width() / 2, size.height() / 3,
                               size.width() / 3, size.height() / 3));
        painter.setPen(QColor(20, 28, 25));
        painter.drawText(image.rect().adjusted(24, 24, -24, -24),
                         Qt::AlignBottom | Qt::AlignRight,
                         QStringLiteral("attachment %1").arg(i));
        painter.end();
        image.save(dir.filePath(QStringLiteral("_attachments/image-%1.png")
                                    .arg(i, 2, 10, QLatin1Char('0'))),
                   "PNG");
    }
}

QString noteBody(int index, int wordsPerNote, int noteCount,
                 VaultProfile profile, const QString &folder) {
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

    if (richProfile(profile)) {
        out << "---\n";
        out << "title: Synthetic Note " << index << "\n";
        out << "created: 2026-07-"
            << QStringLiteral("%1").arg(index % 28 + 1, 2, 10, QLatin1Char('0'))
            << "\n";
        out << "tags: [emerald, perf, vault-" << index % 17
            << ", area-" << index % 9 << "]\n";
        out << "aliases: [Synthetic " << index << ", Benchmark " << index % 101
            << "]\n";
        if (heavyProfile(profile)) {
            const char *status = index % 4 == 0
                                     ? "draft"
                                     : index % 4 == 1
                                           ? "active"
                                           : index % 4 == 2 ? "waiting" : "done";
            out << "reviewers: [alice, bob, casey]\n";
            out << "status: " << status << "\n";
            out << "unique_key: entity" << index << " checkpoint" << index * 13
                << "\n";
        }
        out << "---\n\n";
    }

    out << "# Synthetic Note " << index << "\n\n";
    const int targetBase =
        profile == VaultProfile::Classic ? 997 : qMax(1, noteCount);
    out << "Linked to [[Note " << ((index + 1) % targetBase)
        << "]] and [[Note " << ((index + 17) % targetBase) << "|alias]].\n";
    if (heavyProfile(profile)) {
        for (int i = 0; i < 6; ++i)
            out << "Cross reference [[Note "
                << ((index + 37 + i * 19) % targetBase) << "#heading-" << i
                << "|ref " << i << "]].\n";
    }
    out << "\n";

    if (richProfile(profile))
        out << "#tag" << index % 41 << " #project/" << index % 13 << " [[MOC "
            << index % 23 << "]]\n\n";

    if (index % 5 == 0 || heavyProfile(profile))
        out << "Inline math $x_" << index % 13 << "^2 + \\frac{a}{b}$ appears here.\n\n";
    if (index % 11 == 0 || (heavyProfile(profile) && index % 2 == 0))
        out << "```cpp\nint value = " << index << ";\nreturn value;\n```\n\n";
    if (index % 13 == 0 || heavyProfile(profile))
        out << "$$\\sum_{i=1}^{n} \\frac{i^2}{n}$$\n\n";

    if (richProfile(profile) && index % 9 == 0) {
        const int imageCount = qMax(1, attachmentCount(profile));
        out << "![Synthetic attachment]("
            << attachmentRef(folder, index % imageCount) << ")\n\n";
    }

    if (heavyProfile(profile) && index % 3 == 0) {
        out << "| Field | Value | Notes |\n";
        out << "| --- | ---: | --- |\n";
        out << "| score | " << index % 100 << " | benchmark table |\n";
        out << "| links | " << index % 17 << " | repeated metadata |\n\n";
    }

    const int adjustedWords = heavyProfile(profile) && index % 19 == 0
                                  ? wordsPerNote * 2
                                  : wordsPerNote;
    for (int i = 0; i < wordsPerNote; ++i) {
        const QString w = words.at((index * 31 + i * 7) % words.size());
        if (i % 17 == 0)
            out << "\n- [ ] task " << i << " ";
        out << w << ' ';
        if (i % 29 == 0)
            out << "**bold** ==highlight== ~~strike~~ ";
        if (heavyProfile(profile) && i % 43 == 0)
            out << "entity" << index << "topic" << i << ' ';
    }
    for (int i = wordsPerNote; i < adjustedWords; ++i)
        out << words.at((index + i) % words.size()) << ' ';
    out << '\n';
    return body;
}

void makeVault(const QString &root, int notes, int wordsPerNote,
               VaultProfile profile) {
    QDir dir(root);
    makeAttachments(root, profile);
    for (int i = 0; i < notes; ++i) {
        const QString folder = folderForNote(i, profile);
        if (!folder.isEmpty())
            dir.mkpath(folder);
        const QString title = QStringLiteral("Note %1").arg(i, 5, 10, QLatin1Char('0'));
        QFile f(folder.isEmpty() ? dir.filePath(title + QStringLiteral(".md"))
                                 : dir.filePath(folder + QLatin1Char('/') + title +
                                                QStringLiteral(".md")));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            continue;
        f.write(noteBody(i, wordsPerNote, notes, profile, folder).toUtf8());
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
    QCommandLineOption profileOpt(QStringLiteral("profile"),
                                  QStringLiteral("Synthetic vault profile: classic, light, mixed, or heavy."),
                                  QStringLiteral("name"), QStringLiteral("classic"));
    QCommandLineOption queriesOpt(QStringLiteral("queries"), QStringLiteral("Search query repetitions."),
                                  QStringLiteral("count"), QStringLiteral("80"));
    QCommandLineOption jsonOpt(QStringLiteral("json"), QStringLiteral("Write JSON metrics to file."),
                               QStringLiteral("path"));
    QCommandLineOption generateOnlyOpt(
        QStringLiteral("generate-only"),
        QStringLiteral("Generate the synthetic vault at path and exit."),
        QStringLiteral("path"));
    parser.addOption(notesOpt);
    parser.addOption(wordsOpt);
    parser.addOption(profileOpt);
    parser.addOption(queriesOpt);
    parser.addOption(jsonOpt);
    parser.addOption(generateOnlyOpt);
    parser.process(app);

    const int notes = parser.value(notesOpt).toInt();
    const int words = parser.value(wordsOpt).toInt();
    bool profileOk = false;
    const VaultProfile profile = parseProfile(parser.value(profileOpt), &profileOk);
    if (!profileOk) {
        QTextStream(stderr)
            << "Unknown --profile value. Use classic, light, mixed, or heavy.\n";
        return 2;
    }
    const int queryRuns = parser.value(queriesOpt).toInt();

    if (parser.isSet(generateOnlyOpt)) {
        makeVault(parser.value(generateOnlyOpt), notes, words, profile);
        return 0;
    }

    QJsonArray metrics;
    qint64 observedPeakRss = -1;
    auto addCurrentRssMetric = [&](const QString &name) {
        const qint64 rss = currentRssKb();
        if (rss >= 0)
            observedPeakRss = qMax(observedPeakRss, rss);
        addMetric(metrics, name, rss, QStringLiteral("KiB"));
        return rss;
    };
    auto addPeakRssMetric = [&](const QString &name) {
        const qint64 sampled = currentRssKb();
        if (sampled >= 0)
            observedPeakRss = qMax(observedPeakRss, sampled);
        const qint64 platformPeak = peakRssKb();
        const qint64 peak = qMax(platformPeak, observedPeakRss);
        addMetric(metrics, name, peak, QStringLiteral("KiB"));
        return peak;
    };

    addCurrentRssMetric(QStringLiteral("rss_start_current"));

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 2;

    bool generated = false;
    addMetric(metrics, QStringLiteral("generate_vault"),
              timeMs([&] {
                  const QStringList args{
                      QStringLiteral("--generate-only"), tmp.path(),
                      QStringLiteral("--notes"), QString::number(notes),
                      QStringLiteral("--words"), QString::number(words),
                      QStringLiteral("--profile"), profileName(profile),
                      QStringLiteral("--queries"), QString::number(queryRuns)};
                  generated =
                      QProcess::execute(QCoreApplication::applicationFilePath(),
                                        args) == 0;
              }),
              QStringLiteral("ms"));
    if (!generated)
        return 4;

    Vault vault(tmp.path());
    addMetric(metrics, QStringLiteral("vault_scan"),
              timeMs([&] { vault.scan(); }), QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("notes_indexed"), vault.notes().size(),
              QStringLiteral("count"));
    addCurrentRssMetric(QStringLiteral("rss_after_scan_current"));

    SearchIndex index;
    const qint64 rssBeforeSearch = currentRssKb();
    addMetric(metrics, QStringLiteral("search_rebuild"),
              timeMs([&] { index.rebuild(vault); }), QStringLiteral("ms"));
    const qint64 rssAfterSearch = currentRssKb();
    if (rssAfterSearch >= 0)
        observedPeakRss = qMax(observedPeakRss, rssAfterSearch);
    addPeakRssMetric(QStringLiteral("rss_after_rebuild"));
    addMetric(metrics, QStringLiteral("rss_after_rebuild_current"), rssAfterSearch,
              QStringLiteral("KiB"));
    addMetric(metrics, QStringLiteral("rss_search_rebuild_delta"),
              (rssBeforeSearch >= 0 && rssAfterSearch >= 0)
                  ? rssAfterSearch - rssBeforeSearch
                  : -1,
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
    addCurrentRssMetric(QStringLiteral("rss_after_search_current"));

    const Note updateNote = vault.notes().at(qMin(10, vault.notes().size() - 1));
    QString updated = vault.read(updateNote.path);
    updated += QStringLiteral("\nperformance regression sentinel unique-token\n");
    addMetric(metrics, QStringLiteral("search_update_note"),
              timeMs([&] { index.updateNote(updateNote.path, updateNote.title, updated); }),
              QStringLiteral("ms"));

    LinkGraphIndex graphIndex;
    const qint64 rssBeforeGraph = currentRssKb();
    addMetric(metrics, QStringLiteral("graph_index_rebuild"),
              timeMs([&] {
                  graphIndex.setNotes(vault.root(), vault.notes());
                  for (const Note &note : vault.notes())
                      graphIndex.updateNote(note.path, note.title,
                                            vault.read(note.path));
              }),
              QStringLiteral("ms"));
    LinkGraphIndex::Snapshot graphSnapshot;
    addMetric(metrics, QStringLiteral("graph_snapshot"),
              timeMs([&] { graphSnapshot = graphIndex.snapshot(); }),
              QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("graph_update_note"),
              timeMs([&] {
                  graphIndex.updateNote(updateNote.path, updateNote.title,
                                        updated);
              }),
              QStringLiteral("ms"));
    const qint64 rssAfterGraph = currentRssKb();
    addMetric(metrics, QStringLiteral("rss_graph_index_delta"),
              (rssBeforeGraph >= 0 && rssAfterGraph >= 0)
                  ? rssAfterGraph - rssBeforeGraph
                  : -1,
              QStringLiteral("KiB"));

    GraphView graphView;
    graphView.resize(1100, 720);
    QElapsedTimer graphLayoutTimer;
    graphLayoutTimer.start();
    QEventLoop graphLayoutLoop;
    QTimer graphLayoutTimeout;
    graphLayoutTimeout.setSingleShot(true);
    graphLayoutTimeout.setInterval(30000);
    QObject::connect(&graphView, &GraphView::layoutSettled, &graphLayoutLoop,
                     &QEventLoop::quit);
    QObject::connect(&graphLayoutTimeout, &QTimer::timeout, &graphLayoutLoop,
                     &QEventLoop::quit);
    graphLayoutTimeout.start();
    graphView.setSnapshot(graphSnapshot);
    graphLayoutLoop.exec();
    addMetric(metrics, QStringLiteral("graph_initial_layout"),
              graphLayoutTimer.nsecsElapsed() / 1000000.0,
              QStringLiteral("ms"));
    QImage graphImage(graphView.size(), QImage::Format_ARGB32_Premultiplied);
    addMetric(metrics, QStringLiteral("graph_render_viewport"),
              timeMs([&] {
                  graphImage.fill(Qt::transparent);
                  QPainter painter(&graphImage);
                  graphView.render(&painter);
              }),
              QStringLiteral("ms"));
    addCurrentRssMetric(QStringLiteral("rss_after_graph_current"));

    MarkdownEditor editor;
    editor.resize(820, 720);
    editor.setImagePaths(tmp.path(), tmp.path());
    QString bigDoc = noteBody(4242, qMax(words * 8, 2000),
                              qMax(notes, 4243), profile, QString());
    if (richProfile(profile)) {
        bigDoc.prepend(QStringLiteral("![Editor preview](_attachments/image-00.png)\n\n"));
        if (heavyProfile(profile))
            bigDoc.prepend(QStringLiteral("![Second preview](_attachments/image-01.png)\n\n"));
    }
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
    addMetric(metrics, QStringLiteral("editor_build_read_document"),
              timeMs([&] {
                  editor.setReadMode(true);
                  app.processEvents();
              }),
              QStringLiteral("ms"));
    addMetric(metrics, QStringLiteral("editor_render_read_viewport"),
              timeMs([&] {
                  editorImage.fill(Qt::transparent);
                  QPainter p(&editorImage);
                  editor.render(&p);
              }),
              QStringLiteral("ms"));
    const QString nextReadDoc = noteBody(
        4243, qMax(words * 8, 2000), qMax(notes, 4244), profile, QString());
    addMetric(metrics, QStringLiteral("editor_switch_read_note"),
              timeMs([&] {
                  editor.setPlainText(nextReadDoc);
                  app.processEvents();
              }),
              QStringLiteral("ms"));
    QString wikiDenseReadDoc;
    wikiDenseReadDoc.reserve(140000);
    for (int i = 0; i < 2000; ++i) {
        wikiDenseReadDoc +=
            QStringLiteral("Navigate through [[Note %1|linked note %1]] and "
                           "continue reading.\n")
                .arg(i);
    }
    addMetric(metrics, QStringLiteral("editor_switch_read_wiki_dense_note"),
              timeMs([&] {
                  editor.setPlainText(wikiDenseReadDoc);
                  app.processEvents();
              }),
              QStringLiteral("ms"));
    editor.setReadMode(false);
    app.processEvents();
    addCurrentRssMetric(QStringLiteral("rss_after_editor_current"));

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
    addCurrentRssMetric(QStringLiteral("rss_after_mascot_current"));
    addPeakRssMetric(QStringLiteral("rss_final"));

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("emerald-perf-v1"));
    root.insert(QStringLiteral("version"), QStringLiteral(EMERALD_VERSION));
    root.insert(QStringLiteral("profile"), profileName(profile));
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
