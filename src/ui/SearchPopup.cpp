#include "SearchPopup.h"

#include "core/SearchIndex.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kWidth = 560;
}

SearchPopup::SearchPopup(const SearchIndex *index, QWidget *parent)
    : QFrame(parent), m_index(index) {
    setObjectName(QStringLiteral("searchPopup"));
    setFrameShape(QFrame::StyledPanel);
    hide();

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("searchInput"));
    m_input->setPlaceholderText(tr("Search notes…"));
    m_input->setClearButtonEnabled(true);

    m_results = new QListWidget(this);
    m_results->setObjectName(QStringLiteral("searchResults"));
    m_results->setUniformItemSizes(false);

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(10, 10, 10, 10);
    col->setSpacing(8);
    col->addWidget(m_input);
    col->addWidget(m_results);

    connect(m_input, &QLineEdit::textChanged, this, &SearchPopup::refresh);
    connect(m_results, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *) { accept(); });

    // The input keeps focus while typing, so route its navigation keys to us.
    m_input->installEventFilter(this);
    if (parent)
        parent->installEventFilter(this);
}

void SearchPopup::showCentered(bool titlesOnly) {
    m_vaultMode = false;
    m_titlesOnly = titlesOnly;
    m_input->setPlaceholderText(titlesOnly ? tr("Go to note…")
                                           : tr("Search notes…"));
    m_input->clear();
    m_results->clear();
    reposition();
    show();
    raise();
    m_input->setFocus();
}

void SearchPopup::showVaults(const QStringList &dirs) {
    m_vaultMode = true;
    m_vaultDirs = dirs;
    m_input->setPlaceholderText(tr("Switch vault…"));
    m_input->clear();
    refresh(QString()); // empty filter shows every vault straight away
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

void SearchPopup::refresh(const QString &text) {
    m_results->clear();
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
        return;
    }
    if (!m_index || text.trimmed().isEmpty()) {
        adjustSize();
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
}

void SearchPopup::accept() {
    QListWidgetItem *item = m_results->currentItem();
    if (!item)
        return;
    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty())
        return;
    if (m_vaultMode)
        emit openVaultRequested(path);
    else
        emit openRequested(path, m_input->text());
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
