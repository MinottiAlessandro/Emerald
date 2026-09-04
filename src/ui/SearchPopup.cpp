#include "SearchPopup.h"

#include "core/SearchIndex.h"

#include <QApplication>
#include <QFileInfo>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kPositionRole = Qt::UserRole + 1;
constexpr int kLengthRole = Qt::UserRole + 2;
constexpr int kWidth = 560;
}

SearchPopup::SearchPopup(const SearchIndex *index, QWidget *parent)
    : QFrame(parent), m_index(index) {
    setObjectName(QStringLiteral("searchPopup"));
    setFrameShape(QFrame::StyledPanel);
    hide();

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("searchPopupHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(1, 0, 1, 0);
    headerLayout->setSpacing(8);
    m_title = new QLabel(tr("Search vault"), header);
    m_title->setObjectName(QStringLiteral("searchPopupTitle"));
    m_matchCounter = new QLabel(tr("0 / 0"), header);
    m_matchCounter->setObjectName(QStringLiteral("searchMatchCounter"));
    m_matchCounter->hide();
    auto *dismissHint = new QLabel(tr("Esc"), header);
    dismissHint->setObjectName(QStringLiteral("searchPopupDismissHint"));
    headerLayout->addWidget(m_title);
    headerLayout->addStretch();
    headerLayout->addWidget(m_matchCounter);
    headerLayout->addWidget(dismissHint);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("searchInput"));
    m_input->setPlaceholderText(tr("Search notes…"));
    m_input->setClearButtonEnabled(true);

    m_results = new QListWidget(this);
    m_results->setObjectName(QStringLiteral("searchResults"));
    m_results->setUniformItemSizes(false);

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(12, 10, 12, 12);
    col->setSpacing(7);
    col->addWidget(header);
    col->addWidget(m_input);
    col->addWidget(m_results);

    connect(m_input, &QLineEdit::textChanged, this, &SearchPopup::refresh);
    connect(m_results, &QListWidget::currentRowChanged, this,
            [this] { updateMatchCounter(); });
    connect(m_results, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *) { accept(); });

    // The input keeps focus while typing, so route its navigation keys to us.
    m_input->installEventFilter(this);
    if (parent)
        parent->installEventFilter(this);
}

void SearchPopup::setModeTitle(const QString &title) {
    if (m_title)
        m_title->setText(title);
}

void SearchPopup::showCentered(bool titlesOnly) {
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    m_vaultMode = false;
    m_templateMode = false;
    m_brokenLinkMode = false;
    m_headingMode = false;
    m_titlesOnly = titlesOnly;
    setModeTitle(titlesOnly ? tr("Go to note") : tr("Search vault"));
    m_input->setPlaceholderText(titlesOnly ? tr("Go to note…")
                                           : tr("Search notes…"));
    m_input->clear();
    m_results->clear();
    updateMatchCounter();
    reposition();
    show();
    raise();
    m_input->setFocus();
}

void SearchPopup::showVaults(const QStringList &dirs) {
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    m_vaultMode = true;
    m_templateMode = false;
    m_brokenLinkMode = false;
    m_headingMode = false;
    setModeTitle(tr("Switch vault"));
    m_vaultDirs = dirs;
    m_input->setPlaceholderText(tr("Switch vault…"));
    m_input->clear();
    refresh(QString()); // empty filter shows every vault straight away
    reposition();
    show();
    raise();
    m_input->setFocus();
}

void SearchPopup::showTemplates(const QStringList &files) {
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    m_vaultMode = false;
    m_templateMode = true;
    m_brokenLinkMode = false;
    m_headingMode = false;
    setModeTitle(tr("Insert template"));
    m_templateFiles = files;
    m_input->setPlaceholderText(tr("Insert template…"));
    m_input->clear();
    refresh(QString()); // empty filter lists every template straight away
    reposition();
    show();
    raise();
    m_input->setFocus();
}

void SearchPopup::showBrokenLinks(const QList<BrokenLinkItem> &items) {
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    m_vaultMode = false;
    m_templateMode = false;
    m_brokenLinkMode = true;
    m_headingMode = false;
    setModeTitle(tr("Broken links"));
    m_brokenLinkItems = items;
    m_input->setPlaceholderText(tr("Filter broken links…"));
    m_input->clear();
    refresh(QString());
    reposition();
    show();
    raise();
    m_input->setFocus();
}

void SearchPopup::showHeadings(const QList<HeadingItem> &items) {
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    m_vaultMode = false;
    m_templateMode = false;
    m_brokenLinkMode = false;
    m_headingMode = true;
    m_titlesOnly = false;
    setModeTitle(tr("Note index"));
    m_headingItems = items;
    m_input->setPlaceholderText(tr("Filter headings…"));
    m_input->clear();
    refresh(QString());
    reposition();
    show();
    raise();
    m_input->setFocus();
}

void SearchPopup::reposition() {
    QWidget *p = parentWidget();
    if (!p)
        return;
    const int w = qMin(kWidth, p->width() - 40);
    setFixedWidth(w);
    adjustSize();
    move((p->width() - width()) / 2, qMax(40, p->height() / 8));
}

void SearchPopup::updateMatchCounter() {
    if (!m_matchCounter)
        return;
    const bool showCounter =
        m_headingMode || (!m_titlesOnly && !m_vaultMode &&
                          !m_templateMode && !m_brokenLinkMode);
    m_matchCounter->setVisible(showCounter);
    if (!showCounter)
        return;
    int total = 0;
    int current = 0;
    if (m_results) {
        for (int row = 0; row < m_results->count(); ++row) {
            const QListWidgetItem *item = m_results->item(row);
            if (!item || !(item->flags() & Qt::ItemIsEnabled))
                continue;
            ++total;
            if (row == m_results->currentRow())
                current = total;
        }
    }
    m_matchCounter->setText(tr("%1 / %2").arg(current).arg(total));
}

void SearchPopup::refresh(const QString &text) {
    m_results->clear();
    if (m_headingMode) {
        const QString needle = text.trimmed();
        for (const HeadingItem &heading : std::as_const(m_headingItems)) {
            if (!needle.isEmpty() &&
                !heading.text.contains(needle, Qt::CaseInsensitive))
                continue;
            const QString indent(qMax(0, heading.level - 1), QChar(0x2003));
            auto *item = new QListWidgetItem(indent + heading.text, m_results);
            item->setData(kPositionRole, heading.position);
        }
        if (m_headingItems.isEmpty()) {
            auto *empty = new QListWidgetItem(tr("No headings in this note"),
                                              m_results);
            empty->setFlags(Qt::NoItemFlags);
        } else if (m_results->count() == 0) {
            auto *empty = new QListWidgetItem(tr("No matching headings"),
                                              m_results);
            empty->setFlags(Qt::NoItemFlags);
        } else {
            m_results->setCurrentRow(0);
        }
        adjustSize();
        reposition();
        updateMatchCounter();
        return;
    }
    if (m_brokenLinkMode) {
        const QString needle = text.trimmed();
        for (const BrokenLinkItem &entry : std::as_const(m_brokenLinkItems)) {
            if (!needle.isEmpty() &&
                !entry.label.contains(needle, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(entry.label, m_results);
            item->setData(kPathRole, entry.path);
            item->setData(kPositionRole, entry.position);
            item->setData(kLengthRole, entry.length);
        }
        if (m_brokenLinkItems.isEmpty()) {
            auto *empty = new QListWidgetItem(tr("No broken links found"),
                                              m_results);
            empty->setFlags(Qt::NoItemFlags);
        } else if (m_results->count() == 0) {
            auto *empty = new QListWidgetItem(tr("No matching broken links"),
                                              m_results);
            empty->setFlags(Qt::NoItemFlags);
        } else {
            m_results->setCurrentRow(0);
        }
        adjustSize();
        reposition();
        updateMatchCounter();
        return;
    }
    if (m_templateMode) {
        const QString needle = text.trimmed();
        for (const QString &file : m_templateFiles) {
            const QString name = QFileInfo(file).completeBaseName();
            if (needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive)) {
                auto *item = new QListWidgetItem(name, m_results);
                item->setData(kPathRole, file);
            }
        }
        if (m_results->count())
            m_results->setCurrentRow(0);
        adjustSize();
        reposition();
        updateMatchCounter();
        return;
    }
    if (m_vaultMode) {
        const QString needle = text.trimmed();
        for (const QString &dir : m_vaultDirs) {
            const QString name = QFileInfo(dir).fileName();
            if (needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive)) {
                auto *item = new QListWidgetItem(name, m_results);
                item->setData(kPathRole, dir);
            }
        }
        if (m_results->count())
            m_results->setCurrentRow(0);
        adjustSize();
        reposition();
        updateMatchCounter();
        return;
    }
    if (!m_index || text.trimmed().isEmpty()) {
        adjustSize();
        updateMatchCounter();
        return;
    }
    const QList<SearchIndex::Result> results =
        m_titlesOnly ? m_index->searchTitles(text, 30)
                     : m_index->search(text, 30);
    for (const SearchIndex::Result &r : results) {
        const QString label =
            r.snippet.isEmpty() ? r.title : r.title + QLatin1Char('\n') + r.snippet;
        auto *item = new QListWidgetItem(label, m_results);
        item->setData(kPathRole, r.path);
    }
    if (!results.isEmpty())
        m_results->setCurrentRow(0);
    adjustSize();
    reposition();
    updateMatchCounter();
}

void SearchPopup::accept() {
    QListWidgetItem *item = m_results->currentItem();
    if (!item)
        return;
    if (m_headingMode) {
        const int position = item->data(kPositionRole).toInt();
        if (position >= 0) {
            emit headingRequested(position);
            hide();
        }
        return;
    }
    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty())
        return;
    if (m_brokenLinkMode)
        emit brokenLinkRequested(path, item->data(kPositionRole).toInt(),
                                 item->data(kLengthRole).toInt());
    else if (m_vaultMode)
        emit openVaultRequested(path);
    else if (m_templateMode)
        emit templateRequested(path);
    else
        // Titles-only quick-open (Ctrl+P) carries no body query, so pass none:
        // the caller would otherwise try to jump to the typed title inside the
        // body, fail, and land at the top — defeating the restored caret. Only
        // a full-text search (Ctrl+Shift+F) jumps to its match.
        emit openRequested(path, m_titlesOnly ? QString() : m_input->text());
    hide();
}

bool SearchPopup::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        if (isVisible())
            reposition();
        return false;
    }
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Down:
            m_results->setCurrentRow(
                qMin(m_results->currentRow() + 1, m_results->count() - 1));
            return true;
        case Qt::Key_Up:
            m_results->setCurrentRow(qMax(m_results->currentRow() - 1, 0));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void SearchPopup::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QFrame::keyPressEvent(event);
}

void SearchPopup::hideEvent(QHideEvent *event) {
    if (m_brokenLinkMode) {
        // A report can contain many occurrences. Keep it only for the lifetime
        // of the visible utility instead of turning it into a second vault-wide
        // cache beside SearchIndex.
        m_brokenLinkMode = false;
        m_brokenLinkItems.clear();
        m_results->clear();
    }
    if (m_headingMode) {
        m_headingMode = false;
        m_headingItems.clear();
        m_results->clear();
    }
    const QPointer<QWidget> previousFocus = m_previousFocus;
    m_previousFocus.clear();
    QTimer::singleShot(0, this, [previousFocus] {
        if (previousFocus && previousFocus->isVisible() &&
            previousFocus->isEnabled())
            previousFocus->setFocus();
    });
    QFrame::hideEvent(event);
}
