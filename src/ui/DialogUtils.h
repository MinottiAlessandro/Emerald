#pragma once

#include <QColorDialog>
#include <QDialog>
#include <QEventLoop>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

// QDialog::exec() marks a window modal before entering its nested event loop.
// With an XCB AppImage running through XWayland that modal path can become a
// compositor pointer grab. Keep the synchronous call-site semantics Emerald's
// actions rely on, but show a genuinely non-modal window so the pointer remains
// free to leave every dialog.
namespace DialogUtils {

inline QString visibleButtonText(const QString &text) {
    QString visible;
    visible.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('&')) {
            visible.append(text.at(i));
            continue;
        }
        if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&')) {
            visible.append(QLatin1Char('&'));
            ++i;
        }
    }
    return visible;
}

// QMessageBox can constrain its buttons to the theme's fixed minimum width.
// Size each one from the actual, translated label so actions such as
// "Install & Restart" never get clipped.
inline void ensureButtonTextFits(QPushButton *button) {
    if (!button)
        return;
    constexpr int kMinimumButtonWidth = 96;
    constexpr int kHorizontalChrome = 36;
    int contentWidth = button->fontMetrics().horizontalAdvance(
        visibleButtonText(button->text()));
    if (!button->icon().isNull())
        contentWidth += button->iconSize().width() + 8;
    button->setMinimumWidth(
        qMax(button->minimumWidth(),
             qMax(kMinimumButtonWidth, contentWidth + kHorizontalChrome)));
}

inline void ensureMessageBoxButtonsFit(QMessageBox &box) {
    const auto buttons = box.findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
        ensureButtonTextFits(button);
}

inline QDialog *deepestVisibleDialog(QWidget *root) {
    if (!root)
        return nullptr;
    QDialog *best = nullptr;
    int bestDepth = -1;
    if (auto *rootDialog = qobject_cast<QDialog *>(root);
        rootDialog && rootDialog->isVisible()) {
        best = rootDialog;
        bestDepth = 0;
    }
    const auto dialogs = root->findChildren<QDialog *>();
    for (QDialog *dialog : dialogs) {
        if (!dialog->isVisible())
            continue;
        int depth = 0;
        QObject *ancestor = dialog;
        while (ancestor && ancestor != root) {
            ++depth;
            ancestor = ancestor->parent();
        }
        if (ancestor == root && depth > bestDepth) {
            best = dialog;
            bestDepth = depth;
        }
    }
    return best;
}

inline int run(QDialog &dialog) {
    dialog.setModal(false);
    dialog.setWindowModality(Qt::NonModal);

    if (auto *messageBox = qobject_cast<QMessageBox *>(&dialog))
        ensureMessageBoxButtonsFit(*messageBox);

    QEventLoop loop;
    QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    dialog.show();
    dialog.raise();
    dialog.activateWindow();
    if (dialog.isVisible())
        loop.exec();
    return dialog.result();
}

inline QMessageBox::StandardButton
message(QMessageBox::Icon icon, QWidget *parent, const QString &title,
        const QString &text, QMessageBox::StandardButtons buttons,
        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton) {
    QMessageBox box(icon, title, text, buttons, parent);
    box.setObjectName(QStringLiteral("messageDialog"));
    box.setProperty("emeraldDialog", true);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);
    run(box);
    return box.standardButton(box.clickedButton());
}

inline QMessageBox::StandardButton
question(QWidget *parent, const QString &title, const QString &text,
         QMessageBox::StandardButtons buttons =
             QMessageBox::Yes | QMessageBox::No,
         QMessageBox::StandardButton defaultButton = QMessageBox::NoButton) {
    return message(QMessageBox::Question, parent, title, text, buttons,
                   defaultButton);
}

inline QMessageBox::StandardButton
information(QWidget *parent, const QString &title, const QString &text,
            QMessageBox::StandardButtons buttons = QMessageBox::Ok,
            QMessageBox::StandardButton defaultButton =
                QMessageBox::NoButton) {
    return message(QMessageBox::Information, parent, title, text, buttons,
                   defaultButton);
}

inline QMessageBox::StandardButton
warning(QWidget *parent, const QString &title, const QString &text,
        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton) {
    return message(QMessageBox::Warning, parent, title, text, buttons,
                   defaultButton);
}

inline QMessageBox::StandardButton
critical(QWidget *parent, const QString &title, const QString &text,
         QMessageBox::StandardButtons buttons = QMessageBox::Ok,
         QMessageBox::StandardButton defaultButton = QMessageBox::NoButton) {
    return message(QMessageBox::Critical, parent, title, text, buttons,
                   defaultButton);
}

inline QString getExistingDirectory(
    QWidget *parent, const QString &caption, const QString &directory,
    QFileDialog::Options options = QFileDialog::ShowDirsOnly) {
    QFileDialog dialog(parent, caption, directory);
    dialog.setOptions(options);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    if (run(dialog) != QDialog::Accepted)
        return {};
    return dialog.selectedFiles().value(0);
}

inline QString getOpenFileName(
    QWidget *parent, const QString &caption, const QString &directory,
    const QString &filter, QString *selectedFilter = nullptr,
    QFileDialog::Options options = {}) {
    QFileDialog dialog(parent, caption, directory, filter);
    dialog.setOptions(options);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    if (run(dialog) != QDialog::Accepted)
        return {};
    if (selectedFilter)
        *selectedFilter = dialog.selectedNameFilter();
    return dialog.selectedFiles().value(0);
}

inline QStringList getOpenFileNames(
    QWidget *parent, const QString &caption, const QString &directory,
    const QString &filter, QString *selectedFilter = nullptr,
    QFileDialog::Options options = {}) {
    QFileDialog dialog(parent, caption, directory, filter);
    dialog.setOptions(options);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    if (run(dialog) != QDialog::Accepted)
        return {};
    if (selectedFilter)
        *selectedFilter = dialog.selectedNameFilter();
    return dialog.selectedFiles();
}

inline QColor getColor(const QColor &initial, QWidget *parent,
                       const QString &title,
                       QColorDialog::ColorDialogOptions options = {}) {
    QColorDialog dialog(initial, parent);
    dialog.setWindowTitle(title);
    dialog.setOptions(options | QColorDialog::DontUseNativeDialog);
    return run(dialog) == QDialog::Accepted ? dialog.selectedColor() : QColor();
}

} // namespace DialogUtils
