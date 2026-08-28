#pragma once

#include "core/SpellChecker.h"

#include <QDialog>
#include <QHash>
#include <QList>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

// Installs optional Hunspell pairs directly from a pinned, hash-verified
// catalog. Only one small file is buffered at a time and the completed pack is
// handed to SpellChecker for staging, validation, and atomic activation.
class SpellLanguageDialog : public QDialog {
    Q_OBJECT
public:
    explicit SpellLanguageDialog(const QStringList &activeLanguages,
                                 QWidget *parent = nullptr);

signals:
    void languagesChanged();

private:
    struct Row {
        QLabel *status = nullptr;
        QPushButton *action = nullptr;
    };

    void buildUi();
    void refreshRows();
    void startInstall(const QString &locale);
    void requestNextPart();
    void finishInstall();
    void failInstall(const QString &message);
    void removePack(const QString &locale);
    const SpellLanguage *pack(const QString &locale) const;
    const SpellPackFile *currentPart() const;
    void setActionsEnabled(bool enabled);

    QStringList m_activeLanguages;
    QList<SpellLanguage> m_languages;
    QHash<QString, Row> m_rows;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressPercent = nullptr;
    QLabel *m_message = nullptr;
    QString m_installingLocale;
    int m_partIndex = 0;
    QByteArray m_partBuffer;
    QList<QByteArray> m_downloadedParts;
    bool m_sizeRejected = false;
};
