#include "core/SearchIndex.h"
#include "ui/SearchPopup.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTextStream>

// Broken-link mode never queries the full-text index. These two definitions
// satisfy SearchPopup's normal search branches without pulling the entire Vault
// implementation into this focused widget regression test.
QList<SearchIndex::Result> SearchIndex::search(const QString &query, int) const {
    if (query.isEmpty())
        return {};
    return {{QStringLiteral("/vault/One.md"), QStringLiteral("One"), {}, 3},
            {QStringLiteral("/vault/Two.md"), QStringLiteral("Two"), {}, 2},
            {QStringLiteral("/vault/Three.md"), QStringLiteral("Three"), {},
             1}};
}

QList<SearchIndex::Result> SearchIndex::searchTitles(const QString &, int) const {
    return {};
}

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget parent;
    parent.resize(900, 700);
    parent.show();

    SearchIndex index;
    SearchPopup popup(&index, &parent);
    const QList<SearchPopup::BrokenLinkItem> items{
        {QStringLiteral("[[Missing]] — Missing note\nFrom Source.md · Line 2"),
         QStringLiteral("/vault/Source.md"), 10, 11},
        {QStringLiteral("[[Blank]] — Empty note\nFrom Other.md · Line 8"),
         QStringLiteral("/vault/Other.md"), 42, 9}};
    popup.showBrokenLinks(items);
    QApplication::processEvents();

    auto *input = popup.findChild<QLineEdit *>(QStringLiteral("searchInput"));
    auto *title =
        popup.findChild<QLabel *>(QStringLiteral("searchPopupTitle"));
    auto *counter =
        popup.findChild<QLabel *>(QStringLiteral("searchMatchCounter"));
    auto *results =
        popup.findChild<QListWidget *>(QStringLiteral("searchResults"));
    check(popup.isVisible(), QStringLiteral("broken-link popup is visible"));
    check(input && results && title && counter,
          QStringLiteral("popup exposes its compact command-palette controls"));
    check(title && title->text() == QStringLiteral("Broken links"),
          QStringLiteral("popup identifies the active command mode"));
    if (input && results) {
        check(results->count() == 2,
              QStringLiteral("all broken links are initially listed"));
        input->setText(QStringLiteral("empty"));
        QApplication::processEvents();
        check(results->count() == 1 &&
                  results->item(0)->text().contains(QStringLiteral("Blank")),
              QStringLiteral("broken links filter by their displayed details"));

        QString openedPath;
        int openedPosition = -1;
        int openedLength = -1;
        QObject::connect(
            &popup, &SearchPopup::brokenLinkRequested,
            [&](const QString &path, int position, int length) {
                openedPath = path;
                openedPosition = position;
                openedLength = length;
            });
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(input, &enter);
        check(openedPath == QStringLiteral("/vault/Other.md") &&
                  openedPosition == 42 && openedLength == 9,
              QStringLiteral("Enter emits the exact source occurrence"));
        check(!popup.isVisible(),
              QStringLiteral("accepting a broken link dismisses the popup"));

        popup.showCentered(false);
        input->setText(QStringLiteral("needle"));
        QApplication::processEvents();
        check(results->count() == 3 && counter && counter->isVisible() &&
                  counter->text() == QStringLiteral("1 / 3"),
              QStringLiteral("vault search shows the selected match and total"));
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(input, &down);
        check(counter && counter->text() == QStringLiteral("2 / 3"),
              QStringLiteral("vault search counter follows result navigation"));

        popup.showCentered(true);
        QApplication::processEvents();
        check(title && title->text() == QStringLiteral("Go to note"),
              QStringLiteral("quick-open updates the command-palette title"));

        popup.showBrokenLinks({});
        QApplication::processEvents();
        check(results->count() == 1 &&
                  results->item(0)->text() ==
                      QStringLiteral("No broken links found") &&
                  results->item(0)->flags() == Qt::NoItemFlags,
              QStringLiteral("an issue-free vault shows a non-actionable state"));
    }

    if (failures == 0)
        QTextStream(stdout) << "All search popup tests passed.\n";
    return failures == 0 ? 0 : 1;
}
