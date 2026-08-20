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
#include <vector>

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

// A small Qt-facing wrapper around Hunspell. Active dictionaries are stacked:
// a word is accepted when any selected language recognizes it. The checker
// keeps one bounded combined-result cache, persists a UTF-8 personal word list
// per language, and exposes Markdown-aware word ranges to the editor.
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
    bool isReady() const { return !m_dictionaries.empty(); }

    bool setLanguages(const QStringList &locales, QString *error = nullptr);
    QStringList languages() const { return m_languages; }
    // Singular compatibility helpers for callers that intentionally need one
    // dictionary (for example pack smoke tests).
    bool setLanguage(const QString &locale, QString *error = nullptr);
    QString language() const { return m_languages.value(0); }
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
    struct Dictionary;
    static const SpellLanguage *languageInfo(const QString &locale);
    static QString languageDirectory(const QString &locale);
    static QByteArray encode(const QString &word, const QByteArray &encoding);
    static QString decode(const std::string &word,
                          const QByteArray &encoding);
    void loadPersonalDictionary(Dictionary &dictionary);
    static QString personalDictionaryPath(const QString &locale);
    void clearCache() const;

    std::vector<std::unique_ptr<Dictionary>> m_dictionaries;
    QStringList m_languages;
    QSet<QString> m_personalWords;
    QSet<QString> m_sessionIgnored;
    mutable QHash<QString, bool> m_cache;
    bool m_enabled = true;
    bool m_ignoreWordsWithNumbers = true;
    bool m_ignoreAllCaps = true;
};
