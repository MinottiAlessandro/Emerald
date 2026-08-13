#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <string>

class Hunspell;

struct SpellPackFile {
    QString url;
    QByteArray sha256;
    qint64 maximumBytes = 0;
};

struct SpellLanguage {
    QString locale;
    QString name;
    QString license;
    bool builtIn = false;
    SpellPackFile affix;
    SpellPackFile dictionary;
    SpellPackFile notice;
};

// A small Qt-facing wrapper around Hunspell. It owns exactly one active
// dictionary, keeps bounded word-result caches, persists a UTF-8 personal word
// list per language, and exposes Markdown-aware word ranges to the editor.
class SpellChecker : public QObject {
public:
    struct WordRange {
        int start = 0;
        int length = 0;
        QString word;
    };

    explicit SpellChecker(QObject *parent = nullptr);
    ~SpellChecker() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool isReady() const { return m_engine != nullptr; }

    bool setLanguage(const QString &locale, QString *error = nullptr);
    QString language() const { return m_language; }
    void setOptions(bool ignoreWordsWithNumbers, bool ignoreAllCaps);

    bool isCorrect(const QString &word) const;
    QStringList suggestions(const QString &word, int limit = 7) const;
    bool addToPersonalDictionary(const QString &word, QString *error = nullptr);
    void ignoreForSession(const QString &word);

    static QList<WordRange> wordsInMarkdown(const QString &text);
    static QList<SpellLanguage> availableLanguages();
    static QStringList installedLanguages();
    static bool isLanguageInstalled(const QString &locale);
    static bool languageNeedsUpdate(const QString &locale);
    static bool ensureBundledEnglish(QString *error = nullptr);
    static QString dictionaryRoot();
    static bool installLanguage(const QString &locale, const QByteArray &affix,
                                const QByteArray &dictionary,
                                const QByteArray &notice,
                                QString *error = nullptr);
    static bool removeLanguage(const QString &locale,
                               QString *error = nullptr);

private:
    static const SpellLanguage *languageInfo(const QString &locale);
    static QString languageDirectory(const QString &locale);
    QByteArray encode(const QString &word) const;
    QString decode(const std::string &word) const;
    void loadPersonalDictionary();
    QString personalDictionaryPath() const;
    void clearCache() const;

    std::unique_ptr<Hunspell> m_engine;
    QString m_language;
    QByteArray m_encoding;
    QSet<QString> m_personalWords;
    QSet<QString> m_sessionIgnored;
    mutable QHash<QString, bool> m_cache;
    bool m_enabled = true;
    bool m_ignoreWordsWithNumbers = true;
    bool m_ignoreAllCaps = true;
};
