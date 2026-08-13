#include "SpellChecker.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <hunspell.hxx>
#include <algorithm>
#include <stdexcept>

namespace {
constexpr int kMaximumCacheEntries = 4096;

const QJsonObject &dictionaryManifest() {
    static const QJsonObject manifest = [] {
        QFile file(QStringLiteral(":/dictionaries/manifest.json"));
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject{};
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject())
            return QJsonObject{};
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("schema")).toInt() != 1)
            return QJsonObject{};
        return object;
    }();
    return manifest;
}

QByteArray dictionarySourceVersion() {
    const QString revision =
        dictionaryManifest()
            .value(QStringLiteral("upstream"))
            .toObject()
            .value(QStringLiteral("commit"))
            .toString();
    static const QRegularExpression commitPattern(
        QStringLiteral("^[0-9a-f]{40}$"));
    return commitPattern.match(revision).hasMatch() ? revision.toLatin1()
                                                     : QByteArray{};
}

QByteArray dictionaryPackVersion() {
    const QString version = dictionaryManifest()
                                .value(QStringLiteral("packVersion"))
                                .toString();
    static const QRegularExpression versionPattern(
        QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return versionPattern.match(version).hasMatch() ? version.toLatin1()
                                                     : QByteArray{};
}

bool isValidPackBaseUrl(const QString &value) {
    const QUrl url(value);
    static const QRegularExpression pathPattern(QStringLiteral(
        "^/MinottiAlessandro/Emerald/releases/download/"
        "spell-dictionaries-v[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host() == QLatin1String("github.com") &&
           url.port() == -1 && url.userInfo().isEmpty() && url.query().isEmpty() &&
           url.fragment().isEmpty() && pathPattern.match(url.path()).hasMatch();
}

SpellPackFile packPart(const QJsonObject &object, const QString &baseUrl,
                       bool builtIn) {
    const QString name = object.value(QStringLiteral("name")).toString();
    const QByteArray hash =
        object.value(QStringLiteral("sha256")).toString().toLatin1();
    const qint64 maximum =
        object.value(QStringLiteral("maximumBytes")).toInteger();
    static const QRegularExpression safeName(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    static const QRegularExpression shaPattern(
        QStringLiteral("^[0-9a-f]{64}$"));
    if (!safeName.match(name).hasMatch() ||
        !shaPattern.match(QString::fromLatin1(hash)).hasMatch() ||
        maximum <= 0 || maximum > 10 * 1024 * 1024)
        return {};
    return {builtIn ? QString() : baseUrl + QLatin1Char('/') + name, hash,
            maximum};
}

QByteArray sha256(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool fileHasSha256(const QString &path, const QByteArray &expected) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return false;
    return hash.result().toHex() == expected;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("Could not write %1").arg(QFileInfo(path).fileName());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = QObject::tr("Could not finish writing %1")
                         .arg(QFileInfo(path).fileName());
        return false;
    }
    return true;
}

QByteArray resourceData(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Bundled dictionary resource is missing");
        return {};
    }
    return file.readAll();
}

void maskRange(QList<bool> &allowed, int start, int end, bool value = false) {
    for (int i = qMax(0, start); i < end && i < allowed.size(); ++i)
        allowed[i] = value;
}

void maskMatches(QList<bool> &allowed, const QString &text,
                 const QRegularExpression &expression) {
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        maskRange(allowed, match.capturedStart(), match.capturedEnd());
    }
}

bool verifyPackPart(const SpellPackFile &part, const QByteArray &data,
                    QString *error) {
    if (part.maximumBytes <= 0 || data.isEmpty() ||
        data.size() > part.maximumBytes) {
        if (error)
            *error = QObject::tr("Downloaded language file has an invalid size");
        return false;
    }
    if (sha256(data) != part.sha256) {
        if (error)
            *error = QObject::tr("Downloaded language file failed verification");
        return false;
    }
    return true;
}

QByteArray hunspellPath(const QString &path) {
#ifdef Q_OS_WIN
    // Hunspell accepts Unicode Windows paths only as UTF-8 long paths. AppData
    // commonly contains the user's name, so an ANSI QFile::encodeName path
    // would silently break dictionaries for many users.
    QString native =
        QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.startsWith(QStringLiteral("\\\\")))
        native = QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    else if (!native.startsWith(QStringLiteral("\\\\?\\")))
        native.prepend(QStringLiteral("\\\\?\\"));
    return native.toUtf8();
#else
    return QFile::encodeName(path);
#endif
}
} // namespace

SpellChecker::SpellChecker(QObject *parent) : QObject(parent) {}

SpellChecker::~SpellChecker() = default;

void SpellChecker::setEnabled(bool enabled) { m_enabled = enabled; }

void SpellChecker::setOptions(bool ignoreWordsWithNumbers, bool ignoreAllCaps) {
    if (m_ignoreWordsWithNumbers == ignoreWordsWithNumbers &&
        m_ignoreAllCaps == ignoreAllCaps)
        return;
    m_ignoreWordsWithNumbers = ignoreWordsWithNumbers;
    m_ignoreAllCaps = ignoreAllCaps;
    clearCache();
}

bool SpellChecker::setLanguage(const QString &locale, QString *error) {
    if (error)
        error->clear();
    const SpellLanguage *info = languageInfo(locale);
    if (!info) {
        if (error)
            *error = tr("Unknown spelling language: %1").arg(locale);
        return false;
    }
    if (info->builtIn && !ensureBundledEnglish(error))
        return false;
    if (!isLanguageInstalled(locale)) {
        if (error)
            *error = tr("The %1 dictionary is not installed").arg(info->name);
        return false;
    }

    const QString dir = languageDirectory(locale);
    const QByteArray aff = hunspellPath(dir + QLatin1Char('/') + locale +
                                       QStringLiteral(".aff"));
    const QByteArray dic = hunspellPath(dir + QLatin1Char('/') + locale +
                                       QStringLiteral(".dic"));
    try {
        auto engine = std::make_unique<Hunspell>(aff.constData(), dic.constData());
        const QByteArray encoding =
            QByteArray::fromStdString(engine->get_dict_encoding()).toUpper();
        if (encoding.isEmpty())
            throw std::runtime_error("dictionary encoding is missing");
        m_engine = std::move(engine);
        m_language = locale;
        m_encoding = encoding;
        m_sessionIgnored.clear();
        clearCache();
        loadPersonalDictionary();
        return true;
    } catch (const std::exception &exception) {
        if (error)
            *error = tr("Could not load %1: %2").arg(info->name,
                                                       QString::fromUtf8(exception.what()));
        return false;
    }
}

QByteArray SpellChecker::encode(const QString &word) const {
    QByteArray normalized = m_encoding;
    normalized.replace('-', QByteArrayView());
    if (normalized == "ISO88591" || normalized == "LATIN1")
        return word.toLatin1();
    return word.toUtf8();
}

QString SpellChecker::decode(const std::string &word) const {
    QByteArray normalized = m_encoding;
    normalized.replace('-', QByteArrayView());
    const QByteArray bytes = QByteArray::fromStdString(word);
    if (normalized == "ISO88591" || normalized == "LATIN1")
        return QString::fromLatin1(bytes);
    return QString::fromUtf8(bytes);
}

bool SpellChecker::isCorrect(const QString &word) const {
    if (!m_enabled || !m_engine || word.isEmpty())
        return true;
    const QString key = word.normalized(QString::NormalizationForm_C);
    if (m_personalWords.contains(key.toCaseFolded()) ||
        m_sessionIgnored.contains(key.toCaseFolded()))
        return true;
    if (m_ignoreWordsWithNumbers) {
        for (const QChar ch : key)
            if (ch.isDigit())
                return true;
    }
    if (m_ignoreAllCaps && key.size() > 1 && key == key.toUpper())
        return true;
    // Mixed-case identifiers such as MarkdownEditor are overwhelmingly code or
    // application names rather than prose mistakes. A normal capitalized word
    // has no uppercase character after its first letter and is still checked.
    for (int i = 1; i < key.size(); ++i)
        if (key.at(i).isUpper())
            return true;

    if (const auto found = m_cache.constFind(key); found != m_cache.constEnd())
        return found.value();
    const bool correct = m_engine->spell(encode(key).toStdString());
    if (m_cache.size() >= kMaximumCacheEntries)
        m_cache.clear();
    m_cache.insert(key, correct);
    return correct;
}

QStringList SpellChecker::suggestions(const QString &word, int limit) const {
    QStringList result;
    if (!m_enabled || !m_engine || word.isEmpty() || limit <= 0)
        return result;
    const std::vector<std::string> raw =
        m_engine->suggest(encode(word).toStdString());
    for (const std::string &entry : raw) {
        const QString decoded = decode(entry);
        if (!decoded.isEmpty() && !result.contains(decoded))
            result.append(decoded);
        if (result.size() >= limit)
            break;
    }
    return result;
}

bool SpellChecker::addToPersonalDictionary(const QString &word, QString *error) {
    if (error)
        error->clear();
    if (!m_engine || word.trimmed().isEmpty()) {
        if (error)
            *error = tr("The spell checker is not ready for that word");
        return false;
    }
    const QString clean = word.trimmed().normalized(QString::NormalizationForm_C);
    const QString folded = clean.toCaseFolded();
    QSet<QString> updated = m_personalWords;
    updated.insert(folded);
    QStringList words(updated.cbegin(), updated.cend());
    std::sort(words.begin(), words.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    QDir().mkpath(QFileInfo(personalDictionaryPath()).absolutePath());
    if (!writeFile(personalDictionaryPath(),
                   words.join(QLatin1Char('\n')).toUtf8() + '\n', error))
        return false;
    m_personalWords = std::move(updated);
    m_engine->add(encode(clean).toStdString());
    clearCache();
    return true;
}

void SpellChecker::ignoreForSession(const QString &word) {
    if (word.isEmpty())
        return;
    m_sessionIgnored.insert(word.normalized(QString::NormalizationForm_C)
                                .toCaseFolded());
    clearCache();
}

void SpellChecker::loadPersonalDictionary() {
    m_personalWords.clear();
    QFile file(personalDictionaryPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
        file.size() > 2 * 1024 * 1024)
        return;
    const QString content = QString::fromUtf8(file.readAll());
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        const QString word = line.trimmed();
        if (word.isEmpty())
            continue;
        m_personalWords.insert(word.toCaseFolded());
        m_engine->add(encode(word).toStdString());
    }
}

QString SpellChecker::personalDictionaryPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/spelling/personal-") + m_language +
           QStringLiteral(".txt");
}

void SpellChecker::clearCache() const { m_cache.clear(); }

QList<SpellChecker::WordRange>
SpellChecker::wordsInMarkdown(const QString &text) {
    QList<bool> allowed(text.size(), true);

    // Exclusive inline constructs. Wiki aliases remain prose, while the target
    // and an unaliased wiki link are note identifiers and are deliberately
    // ignored. Markdown link labels remain prose; only their destination is
    // masked.
    static const QRegularExpression wiki(
        QStringLiteral("\\[\\[([^\\]\\n]+)\\]\\]"));
    auto wikiMatches = wiki.globalMatch(text);
    while (wikiMatches.hasNext()) {
        const auto match = wikiMatches.next();
        maskRange(allowed, match.capturedStart(), match.capturedEnd());
        const QString inner = match.captured(1);
        const int pipe = inner.indexOf(QLatin1Char('|'));
        if (pipe >= 0)
            maskRange(allowed, match.capturedStart(1) + pipe + 1,
                      match.capturedEnd(1), true);
    }

    static const QRegularExpression markdownLink(
        QStringLiteral("!?\\[([^\\]\\n]*)\\]\\((?:<[^>\\n]+>|[^)\\n]+)\\)"));
    auto linkMatches = markdownLink.globalMatch(text);
    while (linkMatches.hasNext()) {
        const auto match = linkMatches.next();
        const int labelStart = match.capturedStart(1);
        const int labelEnd = match.capturedEnd(1);
        maskRange(allowed, match.capturedStart(), match.capturedEnd());
        if (!text.mid(match.capturedStart(), 1).startsWith(QLatin1Char('!')))
            maskRange(allowed, labelStart, labelEnd, true);
    }

    static const QRegularExpression code(QStringLiteral("`+[^`\\n]*`+"));
    static const QRegularExpression math(QStringLiteral("\\${1,2}[^$\\n]+\\${1,2}"));
    static const QRegularExpression html(QStringLiteral("<[^>\\n]*>"));
    static const QRegularExpression url(QStringLiteral(
        "(?i)\\b(?:https?://|www\\.)[^\\s<>()]+"));
    static const QRegularExpression email(QStringLiteral(
        "(?i)\\b[\\p{L}\\p{N}._%+-]+@[\\p{L}\\p{N}.-]+\\.[\\p{L}]{2,}\\b"));
    static const QRegularExpression callout(QStringLiteral("\\[![\\p{L}-]+\\]"));
    maskMatches(allowed, text, code);
    maskMatches(allowed, text, math);
    maskMatches(allowed, text, html);
    maskMatches(allowed, text, url);
    maskMatches(allowed, text, email);
    maskMatches(allowed, text, callout);

    static const QRegularExpression word(QStringLiteral(
        "(?<![\\p{L}\\p{M}\\p{N}_])"
        "([\\p{L}\\p{M}]+(?:[’'\\-][\\p{L}\\p{M}]+)*)"
        "(?![\\p{L}\\p{M}\\p{N}_])"));
    QList<WordRange> result;
    auto words = word.globalMatch(text);
    while (words.hasNext()) {
        const auto match = words.next();
        const int start = match.capturedStart(1);
        const int end = match.capturedEnd(1);
        bool entirelyAllowed = start >= 0;
        for (int i = start; entirelyAllowed && i < end; ++i)
            entirelyAllowed = i < allowed.size() && allowed.at(i);
        if (entirelyAllowed)
            result.append({start, end - start, match.captured(1)});
    }
    return result;
}

QList<SpellLanguage> SpellChecker::availableLanguages() {
    struct Metadata {
        const char *locale;
        QString name;
        const char *license;
        bool builtIn;
    };
    const QList<Metadata> metadata = {
        {"en_US", tr("English (US)"), "SCOWL/Ispell permissive licenses", true},
        {"it_IT", tr("Italian"), "GPL-3.0", false},
        {"de_DE", tr("German (Germany)"), "GPL-2.0/GPL-3.0", false},
        {"fr_FR", tr("French (France)"), "MPL-2.0", false},
        {"es_ES", tr("Spanish (Spain)"), "MPL-1.1/GPL-3+/LGPL-3+", false}};

    const QJsonObject manifest = dictionaryManifest();
    const QString baseUrl =
        manifest.value(QStringLiteral("releaseBaseUrl")).toString();
    if (!isValidPackBaseUrl(baseUrl) || dictionarySourceVersion().isEmpty())
        return {};

    QHash<QString, QJsonObject> catalog;
    for (const QJsonValue &value :
         manifest.value(QStringLiteral("languages")).toArray()) {
        const QJsonObject object = value.toObject();
        const QString locale = object.value(QStringLiteral("locale")).toString();
        if (!locale.isEmpty() && !catalog.contains(locale))
            catalog.insert(locale, object);
    }

    QList<SpellLanguage> result;
    for (const Metadata &entry : metadata) {
        const QString locale = QString::fromLatin1(entry.locale);
        const QJsonObject object = catalog.value(locale);
        if (object.isEmpty() ||
            object.value(QStringLiteral("builtIn")).toBool() != entry.builtIn)
            continue;
        const QJsonObject files = object.value(QStringLiteral("files")).toObject();
        const SpellPackFile affix =
            packPart(files.value(QStringLiteral("affix")).toObject(), baseUrl,
                     entry.builtIn);
        const SpellPackFile dictionary =
            packPart(files.value(QStringLiteral("dictionary")).toObject(), baseUrl,
                     entry.builtIn);
        const SpellPackFile notice =
            packPart(files.value(QStringLiteral("notice")).toObject(), baseUrl,
                     entry.builtIn);
        if (affix.sha256.isEmpty() || dictionary.sha256.isEmpty() ||
            notice.sha256.isEmpty())
            continue;
        result.append({locale, entry.name, QString::fromLatin1(entry.license),
                       entry.builtIn, affix, dictionary, notice});
    }
    return result;
}

const SpellLanguage *SpellChecker::languageInfo(const QString &locale) {
    static const QList<SpellLanguage> languages = availableLanguages();
    for (const SpellLanguage &language : languages)
        if (language.locale == locale)
            return &language;
    return nullptr;
}

QString SpellChecker::dictionaryRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/dictionaries");
}

QString SpellChecker::languageDirectory(const QString &locale) {
    return dictionaryRoot() + QLatin1Char('/') + locale;
}

bool SpellChecker::ensureBundledEnglish(QString *error) {
    if (error)
        error->clear();
    const SpellLanguage *english = languageInfo(QStringLiteral("en_US"));
    const QByteArray sourceVersion = dictionarySourceVersion();
    if (!english || !english->builtIn || sourceVersion.isEmpty()) {
        if (error)
            *error = tr("Bundled dictionary manifest is invalid");
        return false;
    }
    const QString dir = languageDirectory(QStringLiteral("en_US"));
    const QString marker = dir + QStringLiteral("/.source-version");
    QFile markerFile(marker);
    if (fileHasSha256(dir + QStringLiteral("/en_US.aff"),
                      english->affix.sha256) &&
        fileHasSha256(dir + QStringLiteral("/en_US.dic"),
                      english->dictionary.sha256) &&
        fileHasSha256(dir + QStringLiteral("/NOTICE.txt"),
                      english->notice.sha256) &&
        markerFile.open(QIODevice::ReadOnly) &&
        markerFile.readAll().trimmed() == sourceVersion)
        return true;

    if (!QDir().mkpath(dir)) {
        if (error)
            *error = tr("Could not create the spelling dictionary folder");
        return false;
    }
    const QByteArray aff =
        resourceData(QStringLiteral(":/dictionaries/en_US/en_US.aff"), error);
    const QByteArray dic =
        resourceData(QStringLiteral(":/dictionaries/en_US/en_US.dic"), error);
    const QByteArray notice = resourceData(
        QStringLiteral(":/dictionaries/en_US/NOTICE.txt"), error);
    if (!verifyPackPart(english->affix, aff, error) ||
        !verifyPackPart(english->dictionary, dic, error) ||
        !verifyPackPart(english->notice, notice, error))
        return false;
    return writeFile(dir + QStringLiteral("/en_US.aff"), aff, error) &&
           writeFile(dir + QStringLiteral("/en_US.dic"), dic, error) &&
           writeFile(dir + QStringLiteral("/NOTICE.txt"), notice, error) &&
           writeFile(marker, sourceVersion + '\n', error);
}

bool SpellChecker::isLanguageInstalled(const QString &locale) {
    const SpellLanguage *info = languageInfo(locale);
    if (!info)
        return false;
    if (info->builtIn)
        return ensureBundledEnglish();
    const QString dir = languageDirectory(locale);
    return fileHasSha256(dir + QLatin1Char('/') + locale +
                             QStringLiteral(".aff"),
                         info->affix.sha256) &&
           fileHasSha256(dir + QLatin1Char('/') + locale +
                             QStringLiteral(".dic"),
                         info->dictionary.sha256) &&
           fileHasSha256(dir + QStringLiteral("/NOTICE.txt"),
                         info->notice.sha256);
}

bool SpellChecker::languageNeedsUpdate(const QString &locale) {
    const SpellLanguage *info = languageInfo(locale);
    if (!info || info->builtIn || isLanguageInstalled(locale))
        return false;
    const QString dir = languageDirectory(locale);
    return QFileInfo::exists(dir + QLatin1Char('/') + locale +
                             QStringLiteral(".aff")) ||
           QFileInfo::exists(dir + QLatin1Char('/') + locale +
                             QStringLiteral(".dic")) ||
           QFileInfo::exists(dir + QStringLiteral("/NOTICE.txt"));
}

QStringList SpellChecker::installedLanguages() {
    QStringList result;
    for (const SpellLanguage &language : availableLanguages())
        if (isLanguageInstalled(language.locale))
            result.append(language.locale);
    return result;
}

bool SpellChecker::installLanguage(const QString &locale,
                                   const QByteArray &affix,
                                   const QByteArray &dictionary,
                                   const QByteArray &notice, QString *error) {
    if (error)
        error->clear();
    const SpellLanguage *info = languageInfo(locale);
    if (!info || info->builtIn) {
        if (error)
            *error = tr("This language pack cannot be installed separately");
        return false;
    }
    if (!verifyPackPart(info->affix, affix, error) ||
        !verifyPackPart(info->dictionary, dictionary, error) ||
        !verifyPackPart(info->notice, notice, error))
        return false;
    if (isLanguageInstalled(locale)) {
        if (error)
            *error = tr("This language is already installed");
        return false;
    }
    if (!QDir().mkpath(dictionaryRoot())) {
        if (error)
            *error = tr("Could not create the spelling dictionary folder");
        return false;
    }

    QTemporaryDir staging(dictionaryRoot() + QStringLiteral("/.install-") +
                          locale + QStringLiteral("-XXXXXX"));
    if (!staging.isValid()) {
        if (error)
            *error = tr("Could not create a safe download staging folder");
        return false;
    }
    const QString affPath = staging.path() + QLatin1Char('/') + locale +
                            QStringLiteral(".aff");
    const QString dicPath = staging.path() + QLatin1Char('/') + locale +
                            QStringLiteral(".dic");
    const QByteArray packVersion = dictionaryPackVersion();
    if (packVersion.isEmpty()) {
        if (error)
            *error = tr("Bundled dictionary manifest is invalid");
        return false;
    }
    if (!writeFile(affPath, affix, error) ||
        !writeFile(dicPath, dictionary, error) ||
        !writeFile(staging.path() + QStringLiteral("/NOTICE.txt"), notice,
                   error) ||
        !writeFile(staging.path() + QStringLiteral("/.pack-version"),
                   packVersion + '\n', error))
        return false;

    try {
        const QByteArray affName = hunspellPath(affPath);
        const QByteArray dicName = hunspellPath(dicPath);
        Hunspell validation(affName.constData(), dicName.constData());
        if (validation.get_dict_encoding().empty())
            throw std::runtime_error("missing encoding");
    } catch (const std::exception &) {
        if (error)
            *error = tr("The downloaded dictionary could not be loaded");
        return false;
    }

    const QString finalPath = languageDirectory(locale);
    QString backupPath;
    if (QFileInfo::exists(finalPath)) {
        backupPath = dictionaryRoot() + QStringLiteral("/.previous-") + locale +
                     QLatin1Char('-') +
                     QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QDir().rename(finalPath, backupPath)) {
            if (error)
                *error = tr("Could not prepare the existing language for update");
            return false;
        }
    }
    staging.setAutoRemove(false);
    if (!QDir().rename(staging.path(), finalPath)) {
        staging.setAutoRemove(true);
        if (!backupPath.isEmpty())
            QDir().rename(backupPath, finalPath);
        if (error)
            *error = tr("Could not activate the downloaded dictionary");
        return false;
    }
    if (!backupPath.isEmpty())
        QDir(backupPath).removeRecursively();
    return true;
}

bool SpellChecker::removeLanguage(const QString &locale, QString *error) {
    if (error)
        error->clear();
    const SpellLanguage *info = languageInfo(locale);
    if (!info || info->builtIn) {
        if (error)
            *error = tr("The built-in English dictionary cannot be removed");
        return false;
    }
    const QString path = QDir::cleanPath(languageDirectory(locale));
    const QString root = QDir::cleanPath(dictionaryRoot());
    if (!path.startsWith(root + QLatin1Char('/')) ||
        QFileInfo(path).fileName() != locale) {
        if (error)
            *error = tr("Refusing an invalid dictionary path");
        return false;
    }
    if (!QFileInfo::exists(path))
        return true;
    if (!QDir(path).removeRecursively()) {
        if (error)
            *error = tr("Could not remove the language pack");
        return false;
    }
    return true;
}
