#include "SpellLanguageDialog.h"

#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

SpellLanguageDialog::SpellLanguageDialog(const QString &activeLanguage,
                                         QWidget *parent)
    : QDialog(parent), m_activeLanguage(activeLanguage),
      m_languages(SpellChecker::availableLanguages()),
      m_network(new QNetworkAccessManager(this)) {
    setObjectName(QStringLiteral("spellLanguageDialog"));
    setWindowTitle(tr("Spelling languages"));
    resize(560, 500);
    setMinimumSize(380, 400);
    buildUi();
    refreshRows();
}

void SpellLanguageDialog::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(12);

    auto *title = new QLabel(tr("Spelling languages"), this);
    title->setObjectName(QStringLiteral("settingsTitle"));
    auto *description = new QLabel(
        tr("English is included. Other dictionaries are downloaded only when "
           "you choose them and are stored outside your vault."),
        this);
    description->setObjectName(QStringLiteral("settingsSubtitle"));
    description->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(description);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *languagesLayout = new QVBoxLayout(content);
    languagesLayout->setContentsMargins(0, 0, 0, 0);
    languagesLayout->setSpacing(8);

    for (const SpellLanguage &language : m_languages) {
        auto *rowFrame = new QFrame(content);
        rowFrame->setObjectName(QStringLiteral("settingsSection"));
        auto *rowLayout = new QHBoxLayout(rowFrame);
        rowLayout->setContentsMargins(14, 11, 12, 11);
        rowLayout->setSpacing(12);

        auto *names = new QWidget(rowFrame);
        auto *namesLayout = new QVBoxLayout(names);
        namesLayout->setContentsMargins(0, 0, 0, 0);
        namesLayout->setSpacing(2);
        auto *name = new QLabel(language.name, names);
        name->setObjectName(QStringLiteral("settingsSectionTitle"));
        auto *license = new QLabel(tr("Dictionary license: %1")
                                       .arg(language.license),
                                   names);
        license->setObjectName(QStringLiteral("settingsSectionDescription"));
        namesLayout->addWidget(name);
        namesLayout->addWidget(license);
        rowLayout->addWidget(names, 1);

        auto *status = new QLabel(rowFrame);
        status->setObjectName(QStringLiteral("spellLanguageStatus"));
        auto *action = new QPushButton(rowFrame);
        action->setObjectName(QStringLiteral("spellLanguageAction_") +
                              language.locale);
        action->setMinimumWidth(92);
        rowLayout->addWidget(status);
        rowLayout->addWidget(action);
        m_rows.insert(language.locale, {status, action});

        connect(action, &QPushButton::clicked, this,
                [this, locale = language.locale] {
                    if (SpellChecker::isLanguageInstalled(locale))
                        removePack(locale);
                    else
                        startInstall(locale);
                });
        languagesLayout->addWidget(rowFrame);
    }
    languagesLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->hide();
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("settingsSectionDescription"));
    m_message->setWordWrap(true);
    m_message->hide();
    root->addWidget(m_progress);
    root->addWidget(m_message);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (auto *close = buttons->button(QDialogButtonBox::Close)) {
        close->setIcon(QIcon());
        close->setAutoDefault(false);
        close->setDefault(false);
    }
    connect(buttons, &QDialogButtonBox::rejected, this,
            &QDialog::reject);
    root->addWidget(buttons);
}

const SpellLanguage *SpellLanguageDialog::pack(const QString &locale) const {
    for (const SpellLanguage &language : m_languages)
        if (language.locale == locale)
            return &language;
    return nullptr;
}

void SpellLanguageDialog::refreshRows() {
    for (const SpellLanguage &language : m_languages) {
        Row row = m_rows.value(language.locale);
        const bool installed = SpellChecker::isLanguageInstalled(language.locale);
        const bool updateAvailable =
            SpellChecker::languageNeedsUpdate(language.locale);
        row.status->setText(language.builtIn
                                ? tr("Included")
                                : installed ? tr("Installed")
                                : updateAvailable ? tr("Update available")
                                                  : tr("Optional"));
        row.action->setVisible(!language.builtIn);
        row.action->setText(installed ? tr("Remove")
                                      : updateAvailable ? tr("Update")
                                                        : tr("Download"));
        const bool active = language.locale == m_activeLanguage;
        row.action->setEnabled(!active);
        row.action->setToolTip(active
                                   ? tr("Select another language before removing this one")
                                   : QString());
    }
}

void SpellLanguageDialog::setActionsEnabled(bool enabled) {
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        const bool active = it.key() == m_activeLanguage;
        it.value().action->setEnabled(enabled && !active);
    }
}

void SpellLanguageDialog::startInstall(const QString &locale) {
    const SpellLanguage *language = pack(locale);
    if (!language || language->builtIn || !m_installingLocale.isEmpty())
        return;
    m_installingLocale = locale;
    m_partIndex = 0;
    m_downloadedParts.clear();
    m_partBuffer.clear();
    m_sizeRejected = false;
    m_message->setText(tr("Downloading %1…").arg(language->name));
    m_message->show();
    m_progress->setValue(0);
    m_progress->show();
    setActionsEnabled(false);
    requestNextPart();
}

const SpellPackFile *SpellLanguageDialog::currentPart() const {
    const SpellLanguage *language = pack(m_installingLocale);
    if (!language)
        return nullptr;
    switch (m_partIndex) {
    case 0: return &language->affix;
    case 1: return &language->dictionary;
    case 2: return &language->notice;
    default: return nullptr;
    }
}

void SpellLanguageDialog::requestNextPart() {
    const SpellPackFile *part = currentPart();
    if (!part) {
        finishInstall();
        return;
    }
    m_partBuffer.clear();
    m_sizeRejected = false;
    QNetworkRequest request(QUrl(part->url));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QByteArrayLiteral("Emerald-Spelling/2"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    m_reply->setReadBufferSize(part->maximumBytes + 1);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, [this] {
        const SpellPackFile *part = currentPart();
        if (!part || !m_reply)
            return;
        const qint64 declared =
            m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (declared > part->maximumBytes) {
            m_sizeRejected = true;
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const SpellPackFile *part = currentPart();
        if (!part || !m_reply)
            return;
        m_partBuffer += m_reply->readAll();
        if (m_partBuffer.size() > part->maximumBytes) {
            m_sizeRejected = true;
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                const SpellPackFile *part = currentPart();
                if (!part)
                    return;
                const qint64 denominator =
                    total > 0 ? qMin(total, part->maximumBytes)
                              : part->maximumBytes;
                const int withinPart = denominator > 0
                                           ? int(qBound(qint64(0),
                                                        received * 100 /
                                                            denominator,
                                                        qint64(100)))
                                           : 0;
                m_progress->setValue((m_partIndex * 100 + withinPart) / 3);
            });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_partBuffer += reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (m_sizeRejected) {
            failInstall(tr("The server returned a language file that was too large."));
            return;
        }
        if (error != QNetworkReply::NoError) {
            failInstall(tr("Download failed: %1").arg(errorText));
            return;
        }
        const SpellPackFile *part = currentPart();
        if (!part || m_partBuffer.isEmpty() ||
            m_partBuffer.size() > part->maximumBytes ||
            QCryptographicHash::hash(m_partBuffer,
                                     QCryptographicHash::Sha256)
                    .toHex() != part->sha256) {
            failInstall(tr("The downloaded language file failed verification."));
            return;
        }
        m_downloadedParts.append(m_partBuffer);
        ++m_partIndex;
        requestNextPart();
    });
}

void SpellLanguageDialog::finishInstall() {
    QString error;
    const bool ok = m_downloadedParts.size() == 3 &&
                    SpellChecker::installLanguage(
                        m_installingLocale, m_downloadedParts.at(0),
                        m_downloadedParts.at(1), m_downloadedParts.at(2),
                        &error);
    if (!ok) {
        failInstall(error.isEmpty() ? tr("Could not install the language pack.")
                                    : error);
        return;
    }
    const SpellLanguage *language = pack(m_installingLocale);
    m_message->setText(tr("%1 is ready to use.")
                           .arg(language ? language->name : m_installingLocale));
    m_progress->setValue(100);
    m_progress->hide();
    m_installingLocale.clear();
    m_downloadedParts.clear();
    refreshRows();
    emit languagesChanged();
}

void SpellLanguageDialog::failInstall(const QString &message) {
    m_message->setText(message);
    m_progress->hide();
    m_installingLocale.clear();
    m_downloadedParts.clear();
    setActionsEnabled(true);
    refreshRows();
}

void SpellLanguageDialog::removePack(const QString &locale) {
    const SpellLanguage *language = pack(locale);
    if (!language || language->builtIn || locale == m_activeLanguage)
        return;
    if (QMessageBox::question(
            this, tr("Remove language"),
            tr("Remove the downloaded %1 dictionary?").arg(language->name),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    QString error;
    if (!SpellChecker::removeLanguage(locale, &error)) {
        m_message->setText(error);
        m_message->show();
        return;
    }
    m_message->setText(tr("%1 was removed.").arg(language->name));
    m_message->show();
    refreshRows();
    emit languagesChanged();
}
