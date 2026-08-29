#include "core/Perf.h"
#include "core/StandaloneFile.h"
#include "core/VaultSettings.h"
#include "ui/AppTheme.h"
#include "ui/DialogUtils.h"
#include "ui/GraphPage.h"
#include "ui/GraphView.h"
#include "ui/MainWindow.h"
#include "ui/MarkdownEditor.h"
#include "ui/SearchPopup.h"
#include "ui/UpdateProgressDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFontComboBox>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QSlider>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QWheelEvent>
#include <chrono>
#include <filesystem>
#include <system_error>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf.tests")

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
  if (condition)
    return;
  QTextStream(stderr) << "FAIL: " << message << '\n';
  ++failures;
}

bool widgetRendersOpaque(QWidget *widget) {
  if (!widget || widget->size().isEmpty())
    return false;
  const QImage image =
      widget->grab().toImage().convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x)
      if (qAlpha(line[x]) != 255)
        return false;
  }
  return true;
}

QDialog *activeTestDialog() {
  QDialog *best = nullptr;
  int bestDepth = -1;
  bool bestIsActive = false;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    auto *dialog = qobject_cast<QDialog *>(widget);
    if (!dialog || !dialog->isVisible())
      continue;
    int depth = 0;
    for (QObject *ancestor = dialog->parent(); ancestor;
         ancestor = ancestor->parent())
      ++depth;
    const bool isActive = dialog == QApplication::activeWindow();
    if (depth > bestDepth || (depth == bestDepth && isActive && !bestIsActive)) {
      best = dialog;
      bestDepth = depth;
      bestIsActive = isActive;
    }
  }
  return best;
}

bool writeFile(const QString &path, const QString &content) {
  QFile file(path);
  const QByteArray bytes = content.toUtf8();
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool setModificationTime(const QString &path, const QDateTime &time) {
  QFile file(path);
  return file.open(QIODevice::ReadWrite) &&
         file.setFileTime(time, QFileDevice::FileModificationTime);
}

bool setDirectoryModificationTime(const QString &path,
                                  const QDateTime &time) {
#ifdef Q_OS_WIN
  const std::filesystem::path nativePath(path.toStdWString());
#else
  const std::filesystem::path nativePath(path.toStdString());
#endif
  const auto systemTime = std::chrono::system_clock::time_point(
      std::chrono::milliseconds(time.toMSecsSinceEpoch()));
  std::error_code error;
  std::filesystem::last_write_time(
      nativePath, std::chrono::file_clock::from_sys(systemTime), error);
  return !error;
}

QStringList topLevelTreeLabels(const QTreeView *tree) {
  QStringList labels;
  if (!tree || !tree->model())
    return labels;
  const QAbstractItemModel *model = tree->model();
  for (int row = 0; row < model->rowCount(); ++row)
    labels << model->index(row, 0).data(Qt::DisplayRole).toString();
  return labels;
}

void sendKey(QWidget *receiver, QEvent::Type type, int key,
             Qt::KeyboardModifiers modifiers,
             const QString &text = QString(), bool autoRepeat = false) {
  QKeyEvent event(type, key, modifiers, text, autoRepeat, 1);
  QApplication::sendEvent(receiver, &event);
}

void sendWheel(QWidget *receiver, int angleY) {
  const QPoint center = receiver->rect().center();
  QWheelEvent event(QPointF(center),
                    QPointF(receiver->mapToGlobal(center)), QPoint(),
                    QPoint(0, angleY), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(receiver, &event);
}

void clickWidget(QWidget *receiver, const QPoint &position,
                 Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  if (!receiver)
    return;
  const QPoint global = receiver->mapToGlobal(position);
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(position),
                    QPointF(global), Qt::LeftButton, Qt::LeftButton,
                    modifiers);
  QApplication::sendEvent(receiver, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position),
                      QPointF(global), Qt::LeftButton, Qt::NoButton,
                      modifiers);
  QApplication::sendEvent(receiver, &release);
}

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return predicate();
}

void testInPaneGraphNavigation(const QString &settingsRoot) {
  QTemporaryDir vault;
  check(vault.isValid(), QStringLiteral("main-window graph vault exists"));
  if (!vault.isValid())
    return;
  const QString alpha = vault.filePath(QStringLiteral("Alpha.md"));
  const QString beta = vault.filePath(QStringLiteral("Beta.md"));
  QDir().mkpath(vault.filePath(QStringLiteral("Projects")));
  const QString gamma = vault.filePath(QStringLiteral("Projects/Gamma.md"));
  const QString delta = vault.filePath(QStringLiteral("Delta.md"));
  check(writeFile(alpha,
                  QStringLiteral("Alpha links to [[Beta]].\n"
                                 "<!-- [[Commented Ghost]] -->\n")) &&
            writeFile(beta, QStringLiteral("Beta links to [[Alpha]].\n")) &&
            writeFile(gamma, QStringLiteral("An orphan note.\n")) &&
            writeFile(delta, QStringLiteral("An incoming [[Alpha]] link.\n")),
        QStringLiteral("graph navigation fixture is writable"));

  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("lastVault"), vault.path());
  settings.setValue(QStringLiteral("lastNote"), alpha);
  settings.sync();

  MainWindow window;
  window.resize(1200, 760);
  window.show();
  check(waitUntil([&window] { return window.isVisible(); }),
        QStringLiteral("main window becomes visible"));

  auto *pages =
      window.findChild<QStackedWidget *>(QStringLiteral("workspacePages"));
  auto *graphPage = window.findChild<GraphPage *>(QStringLiteral("graphPage"));
  auto *graphView =
      window.findChild<GraphView *>(QStringLiteral("graphCanvas"));
  auto *graphAction =
      window.findChild<QAction *>(QStringLiteral("graphViewAction"));
  auto *backAction = window.findChild<QAction *>(QStringLiteral("backAction"));
  auto *forwardAction =
      window.findChild<QAction *>(QStringLiteral("forwardAction"));
  auto *localGraphAction =
      window.findChild<QAction *>(QStringLiteral("localGraphAction"));
  auto *title = window.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
  auto *sidebar = window.findChild<QWidget *>(QStringLiteral("sidebar"));
  auto *sideTitle = window.findChild<QLabel *>(QStringLiteral("sideTitle"));
  auto *splitter =
      window.findChild<QSplitter *>(QStringLiteral("mainSplitter"));
  auto *gearMenu = window.findChild<QMenu *>(QStringLiteral("gearMenu"));
  auto *settingsAction =
      window.findChild<QAction *>(QStringLiteral("settingsAction"));
  auto *editor = window.findChild<MarkdownEditor *>(QStringLiteral("editor"));
  auto *findInput =
      window.findChild<QLineEdit *>(QStringLiteral("findInput"));
  auto *findCounter =
      window.findChild<QLabel *>(QStringLiteral("findMatchCounter"));
  auto *cheatsheet =
      window.findChild<QFrame *>(QStringLiteral("shortcutCheatsheet"));
  check(pages && graphPage && graphView && graphAction && localGraphAction &&
            backAction && forwardAction && title && sidebar && sideTitle &&
            splitter && gearMenu && settingsAction && editor && findInput &&
            findCounter && cheatsheet,
        QStringLiteral("graph page and navigation controls are discoverable"));
  if (!pages || !graphPage || !graphView || !graphAction || !localGraphAction ||
      !backAction || !forwardAction || !title || !sidebar || !sideTitle ||
      !splitter || !gearMenu || !settingsAction || !editor || !findInput ||
      !findCounter || !cheatsheet)
    return;

  check(!cheatsheet->isVisible(),
        QStringLiteral("shortcut cheatsheet starts hidden"));
  check(cheatsheet->findChildren<QLabel *>(
                           QStringLiteral("shortcutSectionTitle"))
                    .size() == 4 &&
            cheatsheet->findChildren<QLabel *>(QStringLiteral("shortcutKey"))
                    .size() >= 30,
        QStringLiteral("shortcut cheatsheet is grouped and comprehensive"));
  const QList<QLabel *> shortcutActions =
      cheatsheet->findChildren<QLabel *>(QStringLiteral("shortcutAction"));
  const QList<QLabel *> shortcutKeys =
      cheatsheet->findChildren<QLabel *>(QStringLiteral("shortcutKey"));
  check(!shortcutActions.isEmpty() && !shortcutKeys.isEmpty() &&
            shortcutActions.first()->font().pixelSize() >= 14 &&
            shortcutKeys.first()->font().pixelSize() >= 14,
        QStringLiteral("shortcut entries use comfortably sized text"));

  editor->setFocus();
  const QString sourceBeforeCheatsheet = editor->toPlainText();
  sendKey(editor, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  QApplication::processEvents();
  check(cheatsheet->isVisible() && editor->hasFocus() &&
            editor->toPlainText() == sourceBeforeCheatsheet,
        QStringLiteral("Alt+X shows the cheatsheet without focus or text edits"));
  check((cheatsheet->geometry().center() - window.rect().center())
                .manhattanLength() <= 2,
        QStringLiteral("shortcut cheatsheet is centered in the app window"));
  auto *cheatsheetScroll = cheatsheet->findChild<QScrollArea *>(
      QStringLiteral("shortcutCheatsheetScroll"));
  check(cheatsheet->width() > 920 && cheatsheet->height() > 680 &&
            cheatsheetScroll &&
            cheatsheetScroll->verticalScrollBar()->maximum() == 0,
        QStringLiteral("desktop cheatsheet grows to show every shortcut "
                       "without a scrollbar"));

  sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"), true);
  sendKey(editor, QEvent::KeyRelease, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"), true);
  QThread::msleep(220);
  QApplication::processEvents();
  check(cheatsheet->isVisible(),
        QStringLiteral("native repeat release/press pairs do not flash the "
                       "held shortcut cheatsheet"));
  sendKey(editor, QEvent::KeyRelease, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  check(waitUntil([cheatsheet] { return !cheatsheet->isVisible(); }, 200),
        QStringLiteral("releasing X closes the shortcut cheatsheet"));
  sendKey(editor, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);

  // Let Quick Jump become active first, then claim Alt with the cheatsheet.
  // Once X is released, Q must not open the first visible wiki link: the
  // application shortcut should have cancelled the link-hint mode completely.
  sendKey(editor, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  QThread::msleep(220);
  QApplication::processEvents();
  sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  QApplication::processEvents();
  check(cheatsheet->isVisible(),
        QStringLiteral("Alt+X replaces an already-visible Quick Jump overlay"));
  sendKey(editor, QEvent::KeyRelease, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  check(waitUntil([cheatsheet] { return !cheatsheet->isVisible(); }, 200),
        QStringLiteral("the replacement cheatsheet closes after X release"));
  sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier);
  QApplication::processEvents();
  check(title->text() == QStringLiteral("Alpha") &&
            editor->toPlainText() == sourceBeforeCheatsheet,
        QStringLiteral("Alt+X suppresses Quick Jump instead of leaving link "
                       "hints active behind the cheatsheet"));
  sendKey(editor, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);

  sendKey(editor, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  QApplication::processEvents();
  sendKey(editor, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  QApplication::processEvents();
  check(!cheatsheet->isVisible(),
        QStringLiteral("releasing Alt closes the shortcut cheatsheet"));
  sendKey(editor, QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier,
          QStringLiteral("x"));

  check(title->text() == QStringLiteral("Alpha"),
        QStringLiteral("fixture starts on Alpha"));
  const QString originalAlpha = editor->toPlainText();
  editor->setPlainText(QStringLiteral(
      "needle at the start\nsecond needle\nthird needle"));
  findInput->setText(QStringLiteral("needle"));
  QApplication::processEvents();
  check(findCounter->text() == QStringLiteral("1 / 3"),
        QStringLiteral("Find in Note shows the current and total matches"));
  sendKey(findInput, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::processEvents();
  check(findCounter->text() == QStringLiteral("2 / 3"),
        QStringLiteral("Find in Note counter follows forward navigation"));
  findInput->clear();
  editor->setPlainText(originalAlpha);
  check(!window.findChild<QToolButton *>(QStringLiteral("graphButton")),
        QStringLiteral("sidebar footer has no dedicated Graph button"));
  bool mobileGraphButton = false;
  for (auto *button :
       window.findChildren<QToolButton *>(QStringLiteral("mobileBarButton"))) {
    if (button->text() == QStringLiteral("Graph"))
      mobileGraphButton = true;
  }
  check(!mobileGraphButton,
        QStringLiteral("mobile toolbar has no dedicated Graph button"));
  check(!gearMenu->actions().contains(graphAction) &&
            !gearMenu->actions().contains(localGraphAction),
        QStringLiteral("Graph launchers are not loose gear-menu actions"));

  check(sidebar->sizePolicy().horizontalPolicy() != QSizePolicy::Ignored,
        QStringLiteral("sidebar preserves its header-derived minimum width"));
  const QList<int> originalSizes = splitter->sizes();
  const int splitterTotal = originalSizes.value(0) + originalSizes.value(1);
  splitter->setSizes(
      {qMax(1, sidebar->minimumSizeHint().width()), splitterTotal});
  QApplication::processEvents();
  const QList<QToolButton *> navButtons =
      window.findChildren<QToolButton *>(QStringLiteral("navButton"));
  check(navButtons.size() == 2,
        QStringLiteral("sidebar navigation arrows are present"));
  for (auto *button : navButtons) {
    check(sideTitle->geometry().right() < button->geometry().left(),
          QStringLiteral("sidebar arrows do not overlap the vault title"));
  }
  check(sideTitle->text().endsWith(QStringLiteral("...")),
        QStringLiteral("narrow sidebar elides its long vault title with ..."));
  splitter->setSizes({0, splitterTotal});
  QApplication::processEvents();
  check(splitter->sizes().value(0) == 0,
        QStringLiteral("sidebar still collapses completely"));
  splitter->setSizes(originalSizes);
  QApplication::processEvents();

  const int topLevels = QApplication::topLevelWidgets().size();
  bool settingsGraphControlsSeen = false;
  bool spellingControlsSeen = false;
  bool spellingLanguagePopupStaysOpen = false;
  bool spellingLanguagePopupOpaque = false;
  bool fontPopupOpaque = false;
  bool spellingManagerSeen = false;
  bool clearMascotsSettingSeen = false;
  bool settingsAllowsPointerOutside = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = activeTestDialog();
    auto *openGlobal = dialog ? dialog->findChild<QPushButton *>(
                                    QStringLiteral("settingsOpenGraph"))
                              : nullptr;
    auto *openLocal = dialog ? dialog->findChild<QPushButton *>(
                                   QStringLiteral("settingsOpenLocalGraph"))
                             : nullptr;
    auto *spellEnabled = dialog ? dialog->findChild<QCheckBox *>(
                                      QStringLiteral("spellCheckEnabled"))
                                : nullptr;
    auto *spellLanguage = dialog ? dialog->findChild<QToolButton *>(
                                       QStringLiteral("spellLanguage"))
                                 : nullptr;
    auto *manageLanguages = dialog ? dialog->findChild<QPushButton *>(
                                         QStringLiteral("manageSpellLanguages"))
                                   : nullptr;
    auto *clearMascots = dialog ? dialog->findChild<QPushButton *>(
                                      QStringLiteral("clearAllMascots"))
                                : nullptr;
    auto *fontFamily = dialog ? dialog->findChild<QFontComboBox *>() : nullptr;
    const QString settingsScreenshot =
        QString::fromLocal8Bit(qgetenv("EMERALD_TEST_SETTINGS_SCREENSHOT"));
    if (dialog && !settingsScreenshot.isEmpty()) {
      QApplication::processEvents();
      dialog->grab().save(settingsScreenshot);
    }
    settingsGraphControlsSeen = openGlobal && openLocal;
    settingsAllowsPointerOutside =
        dialog && !dialog->isModal() &&
        dialog->windowModality() == Qt::NonModal;
    clearMascotsSettingSeen = clearMascots && clearMascots->isEnabled();
    spellingControlsSeen = spellEnabled && spellLanguage && manageLanguages &&
                           spellEnabled->isChecked() &&
                           spellLanguage->property("selectedLanguages")
                               .toStringList()
                               .contains(QStringLiteral("en_US"));
    if (spellLanguage) {
      QMenu *popup = spellLanguage->menu();
      QAction *testLanguage = popup->addAction(QStringLiteral("Test language"));
      testLanguage->setData(QStringLiteral("test_TEST"));
      testLanguage->setCheckable(true);
      popup->popup(spellLanguage->mapToGlobal(
          QPoint(0, spellLanguage->height())));
      QApplication::processEvents();
      spellingLanguagePopupOpaque = widgetRendersOpaque(popup);
      const QRect testRow = popup ? popup->actionGeometry(testLanguage) : QRect();
      if (popup && testRow.isValid() && !testRow.isEmpty()) {
        clickWidget(popup, testRow.center());
        QApplication::processEvents();
        const bool firstToggleStayedOpen =
            popup->isVisible() && testLanguage->isChecked();
        clickWidget(popup, testRow.center());
        QApplication::processEvents();
        spellingLanguagePopupStaysOpen =
            firstToggleStayedOpen && popup->isVisible() &&
            !testLanguage->isChecked();
      }
      popup->hide();
      popup->removeAction(testLanguage);
      delete testLanguage;
    }
    if (fontFamily) {
      fontFamily->showPopup();
      QApplication::processEvents();
      QAbstractItemView *fontPopup = fontFamily->view();
      fontPopupOpaque = fontPopup && widgetRendersOpaque(fontPopup->window());
      fontFamily->hidePopup();
    }
    if (manageLanguages) {
      QTimer::singleShot(0, [&spellingManagerSeen] {
        auto *manager = activeTestDialog();
        const QString spellingScreenshot = QString::fromLocal8Bit(
            qgetenv("EMERALD_TEST_SPELLING_SCREENSHOT"));
        if (manager && !spellingScreenshot.isEmpty()) {
          QApplication::processEvents();
          manager->grab().save(spellingScreenshot);
        }
        spellingManagerSeen =
            manager && manager->objectName() ==
                           QStringLiteral("spellLanguageDialog") &&
            manager->findChild<QLabel *>(
                QStringLiteral("spellDownloadProgressPercent")) &&
            manager->findChildren<QPushButton *>(
                       QRegularExpression(QStringLiteral("spellLanguageAction_.*")))
                    .size() == 5;
        if (manager)
          manager->reject();
      });
      manageLanguages->click();
    }
    if (openGlobal)
      openGlobal->click();
    else if (dialog)
      dialog->reject();
  });
  settingsAction->trigger();
  check(settingsGraphControlsSeen,
        QStringLiteral("Settings > Vault contains Global and Local Graph "
                       "launchers"));
  check(settingsAllowsPointerOutside,
        QStringLiteral("Settings is non-modal and does not confine the pointer"));
  check(spellingControlsSeen,
        QStringLiteral("Settings exposes enabled bundled-English spelling and "
                       "the optional-language manager"));
  check(spellingLanguagePopupStaysOpen,
        QStringLiteral("selecting a spelling dictionary keeps the multi-select "
                       "popup open"));
  check(spellingLanguagePopupOpaque,
        QStringLiteral("the spelling-language popup is fully opaque"));
  check(fontPopupOpaque,
        QStringLiteral("the font-family popup is fully opaque"));
  check(spellingManagerSeen,
        QStringLiteral("language manager lists bundled and optional packs"));
  check(clearMascotsSettingSeen,
        QStringLiteral("Settings exposes vault-wide mascot clearing"));
  check(waitUntil(
            [pages, graphPage] { return pages->currentWidget() == graphPage; }),
        QStringLiteral("Settings can open the Graph View"));
  check(pages->currentWidget() == graphPage && graphPage->isVisible(),
        QStringLiteral("Graph View replaces the note in the central stack"));
  check(QApplication::topLevelWidgets().size() == topLevels,
        QStringLiteral("Graph View creates no overlapping top-level window"));
  auto *editorColumn =
      window.findChild<QWidget *>(QStringLiteral("editorColumn"));
  check(
      editorColumn && graphPage->width() > editorColumn->width(),
      QStringLiteral("Graph View uses more width than the capped note column"));
  check(waitUntil([graphView] { return graphView->visibleNodeCount() == 4; }),
        QStringLiteral(
            "background graph indexing reaches linked and orphan notes"));
  auto *graphTitle =
      graphPage->findChild<QLabel *>(QStringLiteral("graphTitle"));
  auto *graphSearch =
      graphPage->findChild<QLineEdit *>(QStringLiteral("graphSearch"));
  auto *graphScope =
      graphPage->findChild<QComboBox *>(QStringLiteral("graphScope"));
  auto *graphStatus =
      graphPage->findChild<QLabel *>(QStringLiteral("graphStatus"));
  check(graphTitle && graphSearch && graphScope && graphStatus,
        QStringLiteral("graph header and status controls are discoverable"));
  if (graphTitle && graphSearch && graphScope) {
    const QRect titleRect(graphTitle->mapTo(graphPage, QPoint()),
                          graphTitle->size());
    const QRect searchRect(graphSearch->mapTo(graphPage, QPoint()),
                           graphSearch->size());
    const QRect scopeRect(graphScope->mapTo(graphPage, QPoint()),
                          graphScope->size());
    check(qAbs(titleRect.center().y() - searchRect.center().y()) <= 3 &&
              qAbs(searchRect.center().y() - scopeRect.center().y()) <= 3,
          QStringLiteral(
              "Graph title, search, and controls share one header row"));
    check(searchRect.left() < scopeRect.left(),
          QStringLiteral("graph search remains left-aligned before controls"));
  }
  if (graphStatus) {
    const QRect statusRect(graphStatus->mapTo(graphPage, QPoint()),
                           graphStatus->size());
    check(statusRect.right() >= graphPage->width() - 20 &&
              statusRect.bottom() >= graphPage->height() - 20,
          QStringLiteral("note and link totals sit at the graph bottom-right"));
    check(graphStatus->text().contains(QStringLiteral("4 notes")) &&
              graphStatus->text().contains(QStringLiteral("3 links")),
          QStringLiteral("graph totals remain visible and accurate"));
  }
  check(splitter->handleWidth() <= 2,
        QStringLiteral("Graph View removes the transparent splitter gutter"));
  const int openGraphSeam =
      graphPage->mapTo(splitter, QPoint()).x() -
      (sidebar->mapTo(splitter, QPoint()).x() + sidebar->width());
  check(openGraphSeam <= 2,
        QStringLiteral("graph meets the open sidebar at the divider (seam=%1, "
                       "pageX=%2, sidebarX=%3, sidebarWidth=%4, stackX=%5, "
                       "pageLocalX=%6, frame=%7, handleGeometry=%8)")
            .arg(openGraphSeam)
            .arg(graphPage->mapTo(splitter, QPoint()).x())
            .arg(sidebar->mapTo(splitter, QPoint()).x())
            .arg(sidebar->width())
            .arg(pages->mapTo(splitter, QPoint()).x())
            .arg(graphPage->pos().x())
            .arg(pages->frameWidth())
            .arg(splitter->handle(1)->geometry().width()));
  const QList<int> graphOpenSizes = splitter->sizes();
  splitter->setSizes({0, graphOpenSizes.value(0) + graphOpenSizes.value(1)});
  QApplication::processEvents();
  const int collapsedGraphX = graphPage->mapTo(splitter, QPoint()).x();
  check(collapsedGraphX <= 2,
        QStringLiteral(
            "graph meets the window edge when the sidebar is collapsed "
            "(pageX=%1, handle=%2)")
            .arg(collapsedGraphX)
            .arg(splitter->handleWidth()));
  splitter->setSizes(graphOpenSizes);
  QApplication::processEvents();
  auto *folderFilter =
      graphPage->findChild<QComboBox *>(QStringLiteral("graphFolder"));
  check(folderFilter && folderFilter->findData(QStringLiteral("Projects")) >= 0,
        QStringLiteral("Graph View discovers top-level folders"));
  if (folderFilter) {
    folderFilter->setCurrentIndex(
        folderFilter->findData(QStringLiteral("Projects")));
    check(graphView->visibleNodeCount() == 1,
          QStringLiteral("folder filter isolates that folder's notes"));
    folderFilter->setCurrentIndex(folderFilter->findData(QStringLiteral("*")));
  }
  check(graphSearch, QStringLiteral("graph search is available"));
  if (graphSearch)
    graphSearch->setText(QStringLiteral("Alpha"));
  graphView->setCamera(QPointF(123.0, -45.0), 0.72);
  graphView->selectPath(beta);

  QMetaObject::invokeMethod(graphPage, "noteActivated", Qt::DirectConnection,
                            Q_ARG(QString, beta));
  QApplication::processEvents();
  check(pages->currentWidget()->objectName() == QStringLiteral("notePage") &&
            title->text() == QStringLiteral("Beta") &&
            splitter->handleWidth() == 11,
        QStringLiteral("activating a node opens its note in the same pane"));

  backAction->trigger();
  QApplication::processEvents();
  check(pages->currentWidget() == graphPage,
        QStringLiteral("Back returns from a node-opened note to Graph View"));
  check(splitter->handleWidth() <= 2,
        QStringLiteral("Back restores the gap-free graph divider"));
  check(
      graphSearch && graphSearch->text() == QStringLiteral("Alpha") &&
          qAbs(graphView->cameraCenter().x() - 123.0) < 0.01 &&
          qAbs(graphView->cameraCenter().y() + 45.0) < 0.01 &&
          qAbs(graphView->zoomScale() - 0.72) < 0.01 &&
          graphView->selectedPath() == beta,
      QStringLiteral("Back restores Graph View search, camera, and selection"));
  backAction->trigger();
  QApplication::processEvents();
  check(pages->currentWidget()->objectName() == QStringLiteral("notePage") &&
            title->text() == QStringLiteral("Alpha"),
        QStringLiteral("Back crosses Graph View and restores the prior note"));

  forwardAction->trigger();
  QApplication::processEvents();
  check(pages->currentWidget() == graphPage && graphSearch &&
            graphSearch->text() == QStringLiteral("Alpha"),
        QStringLiteral("Forward restores Graph View as a typed history page"));
  forwardAction->trigger();
  QApplication::processEvents();
  check(pages->currentWidget()->objectName() == QStringLiteral("notePage") &&
            title->text() == QStringLiteral("Beta"),
        QStringLiteral("Forward reaches the note opened from Graph View"));
  backAction->trigger();
  backAction->trigger();
  QApplication::processEvents();

  localGraphAction->trigger();
  QApplication::processEvents();
  check(pages->currentWidget() == graphPage && graphPage->isLocal() &&
            graphPage->localRoot() == alpha &&
            graphView->visibleNodeCount() == 3,
        QStringLiteral(
            "Local Graph uses the open note and one-hop neighborhood"));
  graphView->setDirection(GraphView::Direction::Outgoing);
  check(graphView->visibleNodeCount() == 2,
        QStringLiteral("Local Graph can restrict traversal to outgoing links"));
  graphView->setDirection(GraphView::Direction::Both);
  const QString screenshot =
      QString::fromLocal8Bit(qgetenv("EMERALD_TEST_SCREENSHOT"));
  if (!screenshot.isEmpty())
    window.grab().save(screenshot);
  window.resize(480, 720);
  QApplication::processEvents();
  auto *filters =
      graphPage->findChild<QToolButton *>(QStringLiteral("graphFilters"));
  check(filters && filters->isVisible(),
        QStringLiteral("narrow Graph View collapses filters into a menu "
                       "(window=%1, graph=%2, filter=%3)")
            .arg(window.width())
            .arg(graphPage->width())
            .arg(filters ? filters->isVisible() : false));
  sendKey(graphView, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  sendKey(graphView, QEvent::KeyPress, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  QApplication::processEvents();
  const QList<QWidget *> shortcutColumns =
      cheatsheet->findChildren<QWidget *>(QStringLiteral("shortcutColumn"));
  check(shortcutColumns.size() == 2 &&
            shortcutColumns.at(1)->geometry().top() >
                shortcutColumns.at(0)->geometry().bottom(),
        QStringLiteral("narrow cheatsheet stacks its columns for readable "
                       "shortcut labels"));
  sendKey(graphView, QEvent::KeyRelease, Qt::Key_X, Qt::AltModifier,
          QStringLiteral("x"));
  sendKey(graphView, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  const QString narrowScreenshot =
      QString::fromLocal8Bit(qgetenv("EMERALD_TEST_NARROW_SCREENSHOT"));
  if (!narrowScreenshot.isEmpty())
    window.grab().save(narrowScreenshot);

  window.close();
  QApplication::processEvents();
  QSettings().clear();
  Q_UNUSED(settingsRoot);
}

void testLastNotePerVault() {
  QSettings settings;
  settings.clear();
  QTemporaryDir parent;
  check(parent.isValid(), QStringLiteral("last-note vault parent exists"));
  if (!parent.isValid())
    return;

  const QString vaultA = parent.filePath(QStringLiteral("Vault A"));
  const QString vaultB = parent.filePath(QStringLiteral("Vault B"));
  check(QDir().mkpath(vaultA) && QDir().mkpath(vaultB),
        QStringLiteral("last-note vaults are writable"));
  const QString aFirst = QDir(vaultA).filePath(QStringLiteral("First A.md"));
  const QString aLast = QDir(vaultA).filePath(QStringLiteral("Last A.md"));
  const QString bFirst = QDir(vaultB).filePath(QStringLiteral("First B.md"));
  const QString bLast = QDir(vaultB).filePath(QStringLiteral("Last B.md"));
  check(writeFile(aFirst, QStringLiteral("first A\n")) &&
            writeFile(aLast, QStringLiteral("last A\n")) &&
            writeFile(bFirst, QStringLiteral("first B\n")) &&
            writeFile(bLast, QStringLiteral("last B\n")),
        QStringLiteral("last-note fixtures are writable"));
  VaultSettings::setValue(vaultA, QStringLiteral("lastNote"),
                          QStringLiteral("Last A.md"));
  VaultSettings::setValue(vaultB, QStringLiteral("lastNote"),
                          QStringLiteral("Last B.md"));

  settings.setValue(QStringLiteral("lastVault"), vaultA);
  {
    MainWindow window;
    window.show();
    auto *title =
        window.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
    check(waitUntil([title] {
            return title && title->text() == QStringLiteral("Last A");
          }),
          QStringLiteral("vault A restores its own last-open note"));
    window.close();
    QApplication::processEvents();
  }

  settings.setValue(QStringLiteral("lastVault"), vaultB);
  settings.setValue(QStringLiteral("lastClosedVault"), vaultB);
  {
    MainWindow window;
    window.show();
    auto *title =
        window.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
    check(waitUntil([title] {
            return title && title->text() == QStringLiteral("Last B");
          }),
          QStringLiteral("vault B restores its own last-open note"));
    window.close();
    QApplication::processEvents();
  }
  settings.clear();
}

void testLastClosedVault() {
  QSettings settings;
  settings.clear();
  QTemporaryDir parent;
  check(parent.isValid(), QStringLiteral("last-closed vault parent exists"));
  if (!parent.isValid())
    return;

  const QString vaultA = parent.filePath(QStringLiteral("Closed Vault"));
  const QString vaultB = parent.filePath(QStringLiteral("Opened Vault"));
  check(QDir().mkpath(vaultA) && QDir().mkpath(vaultB) &&
            writeFile(QDir(vaultA).filePath(QStringLiteral("Closed Note.md")),
                      QStringLiteral("closed session\n")) &&
            writeFile(QDir(vaultB).filePath(QStringLiteral("Opened Note.md")),
                      QStringLiteral("opened session\n")),
        QStringLiteral("last-closed vault fixtures are writable"));
  VaultSettings::setValue(vaultA, QStringLiteral("lastNote"),
                          QStringLiteral("Closed Note.md"));
  VaultSettings::setValue(vaultB, QStringLiteral("lastNote"),
                          QStringLiteral("Opened Note.md"));

  settings.setValue(QStringLiteral("lastClosedVault"), vaultA);
  settings.setValue(QStringLiteral("lastVault"), vaultB);
  {
    MainWindow window;
    window.show();
    auto *title =
        window.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
    auto *searchPopup = window.findChild<SearchPopup *>();
    check(waitUntil([title] {
            return title && title->text() == QStringLiteral("Closed Note");
          }),
          QStringLiteral("startup prefers the last closed vault over the "
                         "last opened vault"));

    check(searchPopup &&
              QMetaObject::invokeMethod(
                  searchPopup, "openVaultRequested", Qt::DirectConnection,
                  Q_ARG(QString, vaultB)) &&
              waitUntil([title] {
                return title &&
                       title->text() == QStringLiteral("Opened Note");
              }),
          QStringLiteral("the test can switch to a second vault"));
    check(settings.value(QStringLiteral("lastClosedVault")).toString() ==
              vaultA,
          QStringLiteral("switching vaults does not replace the last cleanly "
                         "closed vault"));

    window.close();
    QApplication::processEvents();
  }
  check(settings.value(QStringLiteral("lastClosedVault")).toString() ==
            vaultB,
        QStringLiteral("closing Emerald records the vault active at close"));

  // Simulate another vault having been opened without a subsequent clean
  // close. The next process should still restore the completed session above.
  settings.setValue(QStringLiteral("lastVault"), vaultA);
  {
    MainWindow restored;
    restored.show();
    auto *title =
        restored.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
    check(waitUntil([title] {
            return title && title->text() == QStringLiteral("Opened Note");
          }),
          QStringLiteral("a later startup still restores the last closed "
                         "vault"));
    restored.close();
    QApplication::processEvents();
  }
  settings.clear();
}

void testVaultSwitcherModifiedOrder() {
  QSettings settings;
  settings.clear();
  QTemporaryDir parent;
  check(parent.isValid(), QStringLiteral("vault-switcher parent exists"));
  if (!parent.isValid())
    return;

  const QString alpha = parent.filePath(QStringLiteral("Alpha Vault"));
  const QString bravo = parent.filePath(QStringLiteral("Bravo Vault"));
  const QString charlie = parent.filePath(QStringLiteral("Charlie Vault"));
  const QString nested = QDir(bravo).filePath(QStringLiteral("Projects"));
  check(QDir().mkpath(alpha) && QDir().mkpath(nested) &&
            QDir().mkpath(charlie),
        QStringLiteral("vault-switcher folders are writable"));
  const QString alphaNote =
      QDir(alpha).filePath(QStringLiteral("Alpha.md"));
  const QString bravoNote =
      QDir(nested).filePath(QStringLiteral("Nested.md"));
  const QString charlieNote =
      QDir(charlie).filePath(QStringLiteral("Charlie.md"));
  check(writeFile(alphaNote, QStringLiteral("oldest\n")) &&
            writeFile(bravoNote, QStringLiteral("newest nested note\n")) &&
            writeFile(charlieNote, QStringLiteral("middle\n")),
        QStringLiteral("vault-switcher notes are writable"));

  const QDateTime base =
      QDateTime::fromString(QStringLiteral("2026-01-01T12:00:00Z"),
                            Qt::ISODate);
  // The nested Bravo note is newer than everything else, but the vault folder
  // itself is oldest. The switcher must use the three folder timestamps only.
  check(setModificationTime(alphaNote, base.addDays(1)) &&
            setModificationTime(bravoNote, base.addDays(8)) &&
            setModificationTime(charlieNote, base.addDays(2)) &&
            setDirectoryModificationTime(alpha, base.addDays(6)) &&
            setDirectoryModificationTime(bravo, base.addDays(4)) &&
            setDirectoryModificationTime(charlie, base.addDays(5)),
        QStringLiteral("vault folder modification times are configurable"));

  settings.setValue(QStringLiteral("lastVault"), alpha);
  settings.setValue(QStringLiteral("lastClosedVault"), alpha);
  {
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    auto *switchAction = window.findChild<QAction *>(
        QStringLiteral("switchVaultAction"));
    auto *popup = window.findChild<SearchPopup *>();
    check(switchAction && popup,
          QStringLiteral("vault switcher controls are discoverable"));
    if (switchAction)
      switchAction->trigger();

    auto *results = popup ? popup->findChild<QListWidget *>(
                                QStringLiteral("searchResults"))
                          : nullptr;
    check(waitUntil([popup, results] {
            return popup && popup->isVisible() && results &&
                   results->count() == 3;
          }),
          QStringLiteral("vault switcher lists sibling vaults"));
    check(results && results->count() == 3 &&
              results->item(0)->text() ==
                         QStringLiteral("Alpha Vault") &&
              results->item(1)->text() ==
                  QStringLiteral("Charlie Vault") &&
              results->item(2)->text() == QStringLiteral("Bravo Vault"),
          QStringLiteral("vault switcher orders by the vault folders' own "
                         "timestamps without scanning nested files"));
    window.close();
    QApplication::processEvents();
  }
  settings.clear();
}

void testWikiHeadingNavigation() {
  QSettings settings;
  settings.clear();
  QTemporaryDir vault;
  check(vault.isValid(), QStringLiteral("wiki-heading vault exists"));
  if (!vault.isValid())
    return;

  const QString source = vault.filePath(QStringLiteral("Source.md"));
  const QString target = vault.filePath(QStringLiteral("Test.md"));
  QStringList targetRows{QStringLiteral("# Start"), QStringLiteral("```md"),
                         QStringLiteral("# Title2"), QStringLiteral("```")};
  for (int i = 0; i < 36; ++i)
    targetRows.append(QStringLiteral("context before target %1").arg(i));
  targetRows.append(QStringLiteral("# Title2"));
  for (int i = 0; i < 28; ++i)
    targetRows.append(QStringLiteral("context after target %1").arg(i));
  targetRows.append(QStringLiteral("## Local Section"));
  for (int i = 0; i < 16; ++i)
    targetRows.append(QStringLiteral("tail context %1").arg(i));
  check(writeFile(source, QStringLiteral("[[Test#Title2]]\n")) &&
            writeFile(target, targetRows.join(QLatin1Char('\n'))),
        QStringLiteral("wiki-heading fixtures are writable"));

  settings.setValue(QStringLiteral("lastVault"), vault.path());
  VaultSettings::setValue(vault.path(), QStringLiteral("lastNote"),
                          QStringLiteral("Source.md"));
  MainWindow window;
  window.resize(1000, 520);
  window.show();
  auto *editor =
      window.findChild<MarkdownEditor *>(QStringLiteral("editor"));
  auto *title = window.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
  check(waitUntil([title] {
          return title && title->text() == QStringLiteral("Source");
        }) &&
            editor,
        QStringLiteral("wiki-heading window opens its source note"));
  if (!editor) {
    window.close();
    settings.clear();
    return;
  }

  QApplication::processEvents();
  const QTextBlock sourceLinkBlock = editor->document()->firstBlock();
  QTextCursor sourceLink(sourceLinkBlock);
  sourceLink.setPosition(sourceLinkBlock.position() + 2);
  clickWidget(editor->viewport(), editor->cursorRect(sourceLink).center(),
              Qt::ControlModifier);
  check(waitUntil([title, editor] {
          return title && title->text() == QStringLiteral("Test") &&
                 editor->sourceTextCursor().block().text() ==
                     QStringLiteral("# Title2");
        }),
        QStringLiteral("[[Test#Title2]] opens Test at the real heading instead "
                       "of its fenced-code decoy"));
  QApplication::processEvents();
  check(qAbs(editor->cursorRect().center().y() -
             editor->viewport()->rect().center().y()) <=
            editor->fontMetrics().height(),
        QStringLiteral("wiki heading navigation centers the destination"));

  check(QMetaObject::invokeMethod(
            editor, "linkClicked", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("#Local Section"))) &&
            waitUntil([title, editor] {
              return title && title->text() == QStringLiteral("Test") &&
                     editor->sourceTextCursor().block().text() ==
                         QStringLiteral("## Local Section");
            }),
        QStringLiteral("local [[#heading]] navigation stays in the current note"));
  window.close();
  QApplication::processEvents();
  settings.clear();
}

void testFullWidthEditorPreference() {
  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("editorWidth"), 520);
  settings.setValue(QStringLiteral("editorFullWidth"), false);
  settings.sync();

  {
    MainWindow window;
    window.resize(1200, 700);
    window.show();
    check(waitUntil([&window] { return window.isVisible(); }),
          QStringLiteral("fixed-width settings test window becomes visible"));

    auto *editorColumn =
        window.findChild<QWidget *>(QStringLiteral("editorColumn"));
    auto *settingsAction =
        window.findChild<QAction *>(QStringLiteral("settingsAction"));
    check(editorColumn && settingsAction &&
              editorColumn->maximumWidth() == 520,
          QStringLiteral("saved pixel width constrains the editor by default"));

    bool controlsSeen = false;
    bool fullWidthPreviewSeen = false;
    bool settingsWheelScrollSeen = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = activeTestDialog();
      auto *fullWidth = dialog ? dialog->findChild<QCheckBox *>(
                                     QStringLiteral("editorFullWidth"))
                               : nullptr;
      auto *pixelWidth = dialog ? dialog->findChild<QSpinBox *>(
                                      QStringLiteral("editorColumnWidth"))
                                : nullptr;
      auto *settingsScroll =
          dialog ? dialog->findChild<QScrollArea *>() : nullptr;
      auto *fileOrder = dialog ? dialog->findChild<QComboBox *>(
                                     QStringLiteral("fileTreeSort"))
                               : nullptr;
      controlsSeen = fullWidth && pixelWidth && !fullWidth->isChecked() &&
                     pixelWidth->isEnabled() && pixelWidth->value() == 520;
      if (pixelWidth && fileOrder && settingsScroll) {
        QScrollBar *bar = settingsScroll->verticalScrollBar();
        const int valueBefore = pixelWidth->value();
        const int optionBefore = fileOrder->currentIndex();
        const int scrollBefore = bar->value();
        sendWheel(pixelWidth, -120);
        QApplication::processEvents();
        const int scrollAfterValue = bar->value();
        sendWheel(fileOrder, -120);
        QApplication::processEvents();
        settingsWheelScrollSeen =
            pixelWidth->value() == valueBefore &&
            fileOrder->currentIndex() == optionBefore &&
            scrollAfterValue > scrollBefore && bar->value() > scrollAfterValue;
      }
      if (fullWidth && pixelWidth) {
        fullWidth->setChecked(true);
        QApplication::processEvents();
        fullWidthPreviewSeen =
            editorColumn && !pixelWidth->isEnabled() &&
            editorColumn->maximumWidth() == QWIDGETSIZE_MAX &&
            editorColumn->width() > 520;
      }
      if (dialog)
        dialog->accept();
    });
    if (settingsAction)
      settingsAction->trigger();

    check(controlsSeen,
          QStringLiteral("Settings exposes Full width beside the pixel width"));
    check(fullWidthPreviewSeen,
          QStringLiteral("Full width disables the pixel limit and expands the "
                         "editor immediately"));
    check(settingsWheelScrollSeen,
          QStringLiteral("wheel input over a Settings value scrolls the page "
                         "without changing that value"));
    check(settings.value(QStringLiteral("editorFullWidth")).toBool() &&
              editorColumn &&
              editorColumn->maximumWidth() == QWIDGETSIZE_MAX,
          QStringLiteral("accepting Settings persists the full-width editor"));
    window.close();
    QApplication::processEvents();
  }

  {
    MainWindow restored;
    restored.resize(1200, 700);
    restored.show();
    check(waitUntil([&restored] { return restored.isVisible(); }),
          QStringLiteral("restored full-width window becomes visible"));
    auto *editorColumn =
        restored.findChild<QWidget *>(QStringLiteral("editorColumn"));
    check(editorColumn &&
              editorColumn->maximumWidth() == QWIDGETSIZE_MAX &&
              editorColumn->width() > 520,
          QStringLiteral("full-width editor preference is restored on startup"));
    restored.close();
    QApplication::processEvents();
  }

  settings.clear();
}

void testFileTreeSortPreference() {
  QTemporaryDir vault;
  check(vault.isValid(), QStringLiteral("file-tree sort vault exists"));
  if (!vault.isValid())
    return;

  const QString alpha = vault.filePath(QStringLiteral("Alpha.md"));
  const QString bravo = vault.filePath(QStringLiteral("Bravo.md"));
  const QString charlie = vault.filePath(QStringLiteral("Charlie.md"));
  check(writeFile(alpha, QStringLiteral("alpha\n")) &&
            writeFile(bravo, QStringLiteral("bravo\n")) &&
            writeFile(charlie, QStringLiteral("charlie\n")),
        QStringLiteral("file-tree sort fixture is writable"));
  const QDateTime base = QDateTime::currentDateTimeUtc().addDays(-7);
  check(setModificationTime(alpha, base.addDays(3)) &&
            setModificationTime(bravo, base.addDays(1)) &&
            setModificationTime(charlie, base.addDays(2)),
        QStringLiteral("file-tree fixture modification times are writable"));

  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("lastVault"), vault.path());
  settings.setValue(QStringLiteral("lastNote"), bravo);
  settings.sync();

  {
    MainWindow window;
    window.resize(1000, 680);
    window.show();
    check(waitUntil([&window] { return window.isVisible(); }),
          QStringLiteral("file-tree sort window becomes visible"));
    auto *tree =
        window.findChild<QTreeView *>(QStringLiteral("noteTree"));
    auto *settingsAction =
        window.findChild<QAction *>(QStringLiteral("settingsAction"));
    check(tree && settingsAction &&
              topLevelTreeLabels(tree) ==
                  QStringList({QStringLiteral("Alpha"),
                               QStringLiteral("Bravo"),
                               QStringLiteral("Charlie")}),
          QStringLiteral("file tree defaults to ascending name order"));

    struct SortCase {
      QString key;
      QStringList expected;
    };
    const QList<SortCase> cases = {
        {QStringLiteral("nameDesc"),
         {QStringLiteral("Charlie"), QStringLiteral("Bravo"),
          QStringLiteral("Alpha")}},
        {QStringLiteral("modifiedNewest"),
         {QStringLiteral("Alpha"), QStringLiteral("Charlie"),
          QStringLiteral("Bravo")}},
        {QStringLiteral("modifiedOldest"),
         {QStringLiteral("Bravo"), QStringLiteral("Charlie"),
          QStringLiteral("Alpha")}},
        {QStringLiteral("nameAsc"),
         {QStringLiteral("Alpha"), QStringLiteral("Bravo"),
          QStringLiteral("Charlie")}},
    };
    bool selectorComplete = false;
    for (const SortCase &sortCase : cases) {
      QTimer::singleShot(0, [&] {
        auto *dialog = activeTestDialog();
        auto *sort = dialog ? dialog->findChild<QComboBox *>(
                                  QStringLiteral("fileTreeSort"))
                            : nullptr;
        selectorComplete = selectorComplete ||
                           (sort && sort->count() == 4);
        if (sort)
          sort->setCurrentIndex(sort->findData(sortCase.key));
        if (dialog)
          dialog->accept();
      });
      if (settingsAction)
        settingsAction->trigger();
      QApplication::processEvents();
      check(settings.value(QStringLiteral("fileTreeSort")).toString() ==
                    sortCase.key &&
                topLevelTreeLabels(tree) == sortCase.expected,
            QStringLiteral("file tree applies and persists sort mode %1")
                .arg(sortCase.key));
    }
    check(selectorComplete,
          QStringLiteral("Settings exposes all four file-tree sort orders"));

    QTimer::singleShot(0, [&] {
      auto *dialog = activeTestDialog();
      auto *sort = dialog ? dialog->findChild<QComboBox *>(
                                QStringLiteral("fileTreeSort"))
                          : nullptr;
      if (sort)
        sort->setCurrentIndex(
            sort->findData(QStringLiteral("modifiedNewest")));
      if (dialog)
        dialog->accept();
    });
    if (settingsAction)
      settingsAction->trigger();
    auto *editor = window.findChild<MarkdownEditor *>(QStringLiteral("editor"));
    if (editor) {
      QTextCursor cursor = editor->sourceTextCursor();
      cursor.movePosition(QTextCursor::End);
      cursor.insertText(QStringLiteral("updated\n"));
      editor->setSourceTextCursor(cursor);
    }
    check(editor &&
              waitUntil([tree] {
                return topLevelTreeLabels(tree) ==
                       QStringList({QStringLiteral("Bravo"),
                                    QStringLiteral("Alpha"),
                                    QStringLiteral("Charlie")});
              }),
          QStringLiteral("modified ordering follows an autosaved active note "
                         "without rebuilding the full tree"));
    window.close();
    QApplication::processEvents();
  }

  settings.setValue(QStringLiteral("fileTreeSort"),
                    QStringLiteral("modifiedOldest"));
  settings.sync();
  check(setModificationTime(bravo, base.addDays(1)),
        QStringLiteral("file-tree restore fixture time is reset"));
  {
    MainWindow restored;
    restored.show();
    check(waitUntil([&restored] { return restored.isVisible(); }),
          QStringLiteral("restored file-tree sort window becomes visible"));
    auto *tree =
        restored.findChild<QTreeView *>(QStringLiteral("noteTree"));
    check(topLevelTreeLabels(tree) ==
              QStringList({QStringLiteral("Bravo"),
                           QStringLiteral("Charlie"),
                           QStringLiteral("Alpha")}),
          QStringLiteral("saved file-tree order is restored on startup"));
    restored.close();
    QApplication::processEvents();
  }

  settings.clear();
}

void testThemePreference() {
  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("editorFontFamily"),
                    QStringLiteral("DejaVu Sans Mono"));
  settings.setValue(QStringLiteral("editorFontSize"), 18);
  settings.setValue(QStringLiteral("editorWidth"), 560);
  settings.setValue(QStringLiteral("editorFullWidth"), false);
  settings.setValue(QStringLiteral("lineSpacing"), 170);
  settings.sync();
  AppTheme::apply(*qApp, AppTheme::Id::Dark);

  {
    MainWindow window;
    window.resize(1000, 680);
    window.show();
    check(waitUntil([&window] { return window.isVisible(); }),
          QStringLiteral("theme settings test window becomes visible"));
    auto *settingsAction =
        window.findChild<QAction *>(QStringLiteral("settingsAction"));

    bool selectorSeen = false;
    bool lightPreviewSeen = false;
    bool editorStylePreviewSeen = false;
    QString previewFamily;
    int previewSize = -1;
    int previewSpacing = -1;
    int previewWidth = -1;
    QTimer::singleShot(0, [&] {
      auto *dialog = activeTestDialog();
      auto *theme = dialog ? dialog->findChild<QComboBox *>(
                                 QStringLiteral("appTheme"))
                           : nullptr;
      selectorSeen = theme && theme->count() == 2 &&
                     theme->currentData() == QStringLiteral("dark");
      if (theme) {
        theme->setCurrentIndex(theme->findData(QStringLiteral("light")));
        QApplication::processEvents();
        lightPreviewSeen =
            AppTheme::current() == AppTheme::Id::Light &&
            qApp->palette().color(QPalette::Base) ==
                QColor(QStringLiteral("#fbfcfb")) &&
            qApp->styleSheet().contains(QStringLiteral("#fbfcfb"));
        auto *editor = window.findChild<MarkdownEditor *>(
            QStringLiteral("editor"));
        auto *editorColumn = window.findChild<QWidget *>(
            QStringLiteral("editorColumn"));
        previewFamily = editor ? editor->font().family() : QString();
        previewSize = editor ? editor->font().pointSize() : -1;
        previewSpacing = editor ? editor->lineSpacing() : -1;
        previewWidth = editorColumn ? editorColumn->maximumWidth() : -1;
        editorStylePreviewSeen =
            previewSize == 18 &&
            previewFamily == QStringLiteral("DejaVu Sans Mono") &&
            previewSpacing == 170 && previewWidth == 560;
        const QString screenshot = QString::fromLocal8Bit(
            qgetenv("EMERALD_TEST_LIGHT_THEME_SCREENSHOT"));
        if (dialog && !screenshot.isEmpty())
          dialog->grab().save(screenshot);
      }
      if (dialog)
        dialog->reject();
    });
    if (settingsAction)
      settingsAction->trigger();

    check(selectorSeen,
          QStringLiteral("Settings exposes the two bundled themes"));
    check(lightPreviewSeen,
          QStringLiteral("selecting Emerald Light previews the complete app "
                         "palette immediately"));
    check(editorStylePreviewSeen,
          QStringLiteral("theme preview retains the saved editor font, size, "
                         "spacing, and column width (actual %1 %2pt, %3%%, "
                         "%4px)")
              .arg(previewFamily)
              .arg(previewSize)
              .arg(previewSpacing)
              .arg(previewWidth));
    check(AppTheme::current() == AppTheme::Id::Dark &&
              !settings.contains(QStringLiteral("theme")),
          QStringLiteral("cancelling Settings restores the original theme "
                         "without persisting the preview"));

    QTimer::singleShot(0, [&] {
      auto *dialog = activeTestDialog();
      auto *theme = dialog ? dialog->findChild<QComboBox *>(
                                 QStringLiteral("appTheme"))
                           : nullptr;
      if (theme)
        theme->setCurrentIndex(theme->findData(QStringLiteral("light")));
      if (dialog)
        dialog->accept();
    });
    if (settingsAction)
      settingsAction->trigger();
    check(settings.value(QStringLiteral("theme")).toString() ==
                  QStringLiteral("light") &&
              AppTheme::current() == AppTheme::Id::Light,
          QStringLiteral("accepting Settings persists Emerald Light"));
    window.close();
    QApplication::processEvents();
  }

  // Prove restoration comes from QSettings rather than retained process state.
  AppTheme::apply(*qApp, AppTheme::Id::Dark);
  {
    MainWindow restored;
    restored.show();
    check(waitUntil([&restored] { return restored.isVisible(); }) &&
              AppTheme::current() == AppTheme::Id::Light,
          QStringLiteral("the saved theme is restored when a window starts"));
    restored.close();
    QApplication::processEvents();
  }

  settings.clear();
  AppTheme::apply(*qApp, AppTheme::Id::Dark);
}

void testReleaseChannelPreference() {
  QSettings settings;
  settings.clear();

  MainWindow window;
  window.resize(900, 650);
  window.show();
  check(waitUntil([&window] { return window.isVisible(); }),
        QStringLiteral("release-channel settings window becomes visible"));
  auto *settingsAction =
      window.findChild<QAction *>(QStringLiteral("settingsAction"));

  bool stableDefaultSeen = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = activeTestDialog();
    auto *channel = dialog ? dialog->findChild<QComboBox *>(
                                 QStringLiteral("releaseChannel"))
                           : nullptr;
    stableDefaultSeen = channel && channel->count() == 2 &&
                        channel->currentData() == QStringLiteral("stable");
    if (channel)
      channel->setCurrentIndex(
          channel->findData(QStringLiteral("development")));
    if (dialog)
      dialog->accept();
  });
  if (settingsAction)
    settingsAction->trigger();

  check(stableDefaultSeen,
        QStringLiteral("Settings defaults existing users to Stable"));
  check(settings.value(QStringLiteral("updateChannel")).toString() ==
            QStringLiteral("development"),
        QStringLiteral("accepting Settings persists the Development channel"));

  bool developmentRestored = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = activeTestDialog();
    auto *channel = dialog ? dialog->findChild<QComboBox *>(
                                 QStringLiteral("releaseChannel"))
                           : nullptr;
    developmentRestored =
        channel && channel->currentData() == QStringLiteral("development");
    if (dialog)
      dialog->reject();
  });
  if (settingsAction)
    settingsAction->trigger();
  check(developmentRestored,
        QStringLiteral("Settings restores the selected release channel"));

  window.close();
  QApplication::processEvents();
  settings.clear();
}

void testCustomThemes() {
  QSettings settings;
  settings.clear();
  AppTheme::apply(*qApp, AppTheme::Id::Dark);

  AppTheme::CustomTheme stored = AppTheme::makeCustomTheme(
      QStringLiteral("Ocean Ink"), QStringLiteral("dark"));
  stored.colors[QStringLiteral("background")] = QColor("#182638");
  stored.colors[QStringLiteral("accent")] = QColor("#48a9e6");
  stored.colors[QStringLiteral("text")] = QColor("#e8f2ff");
  AppTheme::saveCustomTheme(stored);
  check(AppTheme::customThemes().size() == 1 &&
            AppTheme::isAvailable(stored.key) &&
            AppTheme::displayName(stored.key) == QStringLiteral("Ocean Ink"),
        QStringLiteral("named custom themes persist in application settings"));

  AppTheme::apply(*qApp, stored.key);
  check(AppTheme::currentKey() == stored.key &&
            qApp->palette().color(QPalette::Base) == QColor("#182638") &&
            AppTheme::color(QColor("#2bbf74")) == QColor("#48a9e6") &&
            qApp->styleSheet().contains(QStringLiteral("#182638")),
        QStringLiteral("custom semantic colors reach the palette, stylesheet, "
                       "and custom painters"));

  AppTheme::apply(*qApp, AppTheme::Id::Dark);
  bool editorSeen = false;
  bool slidersSeen = false;
  bool isolatedPreviewSeen = false;
  bool customAddedToSelector = false;
  QString createdKey;
  {
    MainWindow window;
    window.resize(1100, 760);
    window.show();
    check(waitUntil([&window] { return window.isVisible(); }),
          QStringLiteral("custom-theme test window becomes visible"));
    auto *settingsAction =
        window.findChild<QAction *>(QStringLiteral("settingsAction"));

    QTimer::singleShot(0, [&] {
      auto *settingsDialog = activeTestDialog();
      auto *themeBox = settingsDialog
                           ? settingsDialog->findChild<QComboBox *>(
                                 QStringLiteral("appTheme"))
                           : nullptr;
      auto *create = settingsDialog
                         ? settingsDialog->findChild<QPushButton *>(
                               QStringLiteral("createCustomTheme"))
                         : nullptr;
      auto *remove = settingsDialog
                         ? settingsDialog->findChild<QPushButton *>(
                               QStringLiteral("deleteCustomTheme"))
                         : nullptr;
      check(themeBox && themeBox->count() == 3 && create && remove &&
                !remove->isEnabled(),
            QStringLiteral("Settings lists stored custom themes and protects "
                           "bundled themes from deletion"));
      if (!create) {
        if (settingsDialog)
          settingsDialog->reject();
        return;
      }

      QTimer::singleShot(0, [&] {
        auto *editorDialog = activeTestDialog();
        auto *name = editorDialog
                         ? editorDialog->findChild<QLineEdit *>(
                               QStringLiteral("customThemeName"))
                         : nullptr;
        auto *hue = editorDialog
                        ? editorDialog->findChild<QSlider *>(
                              QStringLiteral("themeHue"))
                        : nullptr;
        auto *saturation = editorDialog
                               ? editorDialog->findChild<QSlider *>(
                                     QStringLiteral("themeSaturation"))
                               : nullptr;
        auto *lightness = editorDialog
                              ? editorDialog->findChild<QSlider *>(
                                    QStringLiteral("themeLightness"))
                              : nullptr;
        auto *preview = editorDialog
                            ? editorDialog->findChild<QTextBrowser *>(
                                  QStringLiteral("themePreviewNote"))
                            : nullptr;
        editorSeen = editorDialog &&
                     editorDialog->objectName() ==
                         QStringLiteral("themeEditorDialog") &&
                     preview &&
                     preview->toPlainText().contains(
                         QStringLiteral("external link"));
        slidersSeen = hue && saturation && lightness;
        const QString applicationThemeBefore = AppTheme::currentKey();
        const QColor applicationBaseBefore =
            qApp->palette().color(QPalette::Base);
        const QString applicationStyleBefore = qApp->styleSheet();
        const QString previewStyleBefore =
            preview ? preview->styleSheet() : QString();
        const QString screenshot = QString::fromLocal8Bit(
            qgetenv("EMERALD_TEST_CUSTOM_THEME_SCREENSHOT"));
        if (editorDialog && !screenshot.isEmpty())
          editorDialog->grab().save(screenshot);
        if (name)
          name->setText(QStringLiteral("Sunset Draft"));
        if (hue)
          hue->setValue((hue->value() + 45) % 360);

        QTimer::singleShot(
            60, [&, applicationThemeBefore, applicationBaseBefore,
                 applicationStyleBefore, previewStyleBefore] {
              auto *activeEditor = activeTestDialog();
              auto *activePreview =
                  activeEditor
                      ? activeEditor->findChild<QTextBrowser *>(
                            QStringLiteral("themePreviewNote"))
                      : nullptr;
              isolatedPreviewSeen =
                  activePreview &&
                  activePreview->styleSheet() != previewStyleBefore &&
                  AppTheme::currentKey() == applicationThemeBefore &&
                  qApp->palette().color(QPalette::Base) ==
                      applicationBaseBefore &&
                  qApp->styleSheet() == applicationStyleBefore;
              if (auto *save =
                      activeEditor
                          ? activeEditor->findChild<QPushButton *>(
                                QStringLiteral("saveCustomTheme"))
                          : nullptr)
                save->click();
              else if (activeEditor)
                activeEditor->reject();
            });
      });
      create->click();

      createdKey = themeBox ? themeBox->currentData().toString() : QString();
      customAddedToSelector =
          themeBox && themeBox->count() == 4 &&
          themeBox->currentText() == QStringLiteral("Sunset Draft") &&
          createdKey.startsWith(QStringLiteral("custom:")) &&
          createdKey != stored.key && remove->isEnabled();
      if (settingsDialog)
        settingsDialog->accept();
    });
    if (settingsAction)
      settingsAction->trigger();

    check(editorSeen,
          QStringLiteral("Create your theme opens a representative test note"));
    check(slidersSeen,
          QStringLiteral("theme editor exposes live hue, saturation, and "
                         "lightness controls"));
    check(isolatedPreviewSeen,
          QStringLiteral("theme slider changes stay inside the test note until "
                         "the theme is saved"));
    check(customAddedToSelector &&
              settings.value(QStringLiteral("theme")).toString() == createdKey,
          QStringLiteral("saving a named theme adds and selects it in Settings"));
    window.close();
    QApplication::processEvents();
  }

  AppTheme::apply(*qApp, AppTheme::Id::Dark);
  {
    MainWindow restored;
    restored.show();
    check(waitUntil([&restored] { return restored.isVisible(); }) &&
              AppTheme::currentKey() == createdKey,
          QStringLiteral("a selected custom theme is restored on startup"));
    restored.close();
    QApplication::processEvents();
  }

  check(AppTheme::deleteCustomTheme(createdKey) &&
            !AppTheme::isAvailable(createdKey) &&
            settings.value(QStringLiteral("theme")).toString() ==
                QStringLiteral("dark"),
        QStringLiteral("custom themes can be deleted and a stale selection "
                       "falls back safely"));
  AppTheme::deleteCustomTheme(stored.key);
  settings.clear();
  AppTheme::apply(*qApp, AppTheme::Id::Dark);
}

void testStandaloneFileSession() {
  QTemporaryDir previousVault;
  QTemporaryDir files;
  check(previousVault.isValid() && files.isValid(),
        QStringLiteral("standalone fixture directories exist"));
  if (!previousVault.isValid() || !files.isValid())
    return;

  const QString previousNote =
      previousVault.filePath(QStringLiteral("Vault Note.md"));
  const QString standalone = files.filePath(QStringLiteral("Single.md"));
  const QString sibling = files.filePath(QStringLiteral("Sibling.md"));
  check(writeFile(previousNote, QStringLiteral("vault content\n")) &&
            writeFile(sibling, QStringLiteral("sibling must stay untouched\n")),
        QStringLiteral("standalone neighboring fixtures are writable"));
  QFile standaloneFile(standalone);
  const QByteArray original =
      QByteArray::fromHex("EFBBBF") +
      QByteArrayLiteral("Intro\r\n\r\n## Target Heading\r\nBody\r\n");
  check(standaloneFile.open(QIODevice::WriteOnly) &&
            standaloneFile.write(original) == original.size(),
        QStringLiteral("UTF-8 BOM/CRLF standalone fixture is writable"));
  standaloneFile.close();

  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("lastVault"), previousVault.path());
  settings.setValue(QStringLiteral("lastClosedVault"), previousVault.path());
  settings.sync();

  MainWindow window(standalone);
  window.resize(1100, 720);
  window.show();
  check(waitUntil([&window] { return window.isVisible(); }),
        QStringLiteral("standalone window becomes visible"));

  auto *editor = window.findChild<MarkdownEditor *>(QStringLiteral("editor"));
  auto *tree = window.findChild<QTreeView *>(QStringLiteral("noteTree"));
  auto *sideTitle = window.findChild<QLabel *>(QStringLiteral("sideTitle"));
  auto *title = window.findChild<QLineEdit *>(QStringLiteral("noteTitle"));
  auto *watcher = window.findChild<QFileSystemWatcher *>();
  auto action = [&window](const char *name) {
    return window.findChild<QAction *>(QString::fromLatin1(name));
  };
  check(editor && tree && sideTitle && title && watcher &&
            action("saveAction") && action("renameAction") &&
            action("readModeAction") && action("findAction") &&
            action("newNoteAction") && action("searchVaultAction") &&
            action("graphViewAction") && action("deleteNoteAction") &&
            action("insertImageAction"),
        QStringLiteral("standalone controls are discoverable"));
  if (!editor || !tree || !sideTitle || !title || !watcher ||
      !action("saveAction") || !action("renameAction") ||
      !action("readModeAction") || !action("findAction") ||
      !action("newNoteAction") || !action("searchVaultAction") ||
      !action("graphViewAction") || !action("deleteNoteAction") ||
      !action("insertImageAction"))
    return;

  check(window.isStandaloneFile() &&
            window.standalonePath() == QFileInfo(standalone).canonicalFilePath(),
        QStringLiteral("the requested file opens as a standalone session"));
  check(editor->toPlainText() ==
            QStringLiteral("Intro\n\n## Target Heading\nBody\n"),
        QStringLiteral("standalone content is decoded and normalized in memory"));
  check(topLevelTreeLabels(tree) == QStringList{QStringLiteral("Single")},
        QStringLiteral("the sidebar exposes only the explicitly opened file"));
  check(sideTitle->text() == QStringLiteral("Single.md"),
        QStringLiteral("the sidebar identifies the standalone document"));
  check(watcher->directories().isEmpty() &&
            watcher->files() == QStringList{window.standalonePath()},
        QStringLiteral("standalone mode watches the file but not its directory"));
  check(settings.value(QStringLiteral("lastVault")).toString() ==
                previousVault.path() &&
            settings.value(QStringLiteral("lastClosedVault")).toString() ==
                previousVault.path(),
        QStringLiteral("opening a standalone file leaves vault restoration untouched"));

  check(action("saveAction")->isEnabled() &&
            action("renameAction")->isEnabled() &&
            action("readModeAction")->isEnabled() &&
            action("findAction")->isEnabled(),
        QStringLiteral("single-document editing actions stay available"));
  check(!action("newNoteAction")->isEnabled() &&
            !action("searchVaultAction")->isEnabled() &&
            !action("graphViewAction")->isEnabled() &&
            !action("deleteNoteAction")->isEnabled() &&
            !action("insertImageAction")->isEnabled(),
        QStringLiteral("vault-wide and attachment actions are disabled"));

  const QImage pasted(12, 12, QImage::Format_ARGB32_Premultiplied);
  QMetaObject::invokeMethod(editor, "imagePasted", Qt::DirectConnection,
                            Q_ARG(QImage, pasted));
  check(!QDir(files.filePath(QStringLiteral("_attachments"))).exists(),
        QStringLiteral("pasting an image cannot create standalone attachments"));

  const bool headingInvoked = QMetaObject::invokeMethod(
      editor, "linkClicked", Qt::DirectConnection,
      Q_ARG(QString, QStringLiteral("#Target Heading")));
  check(headingInvoked &&
            editor->sourceTextCursor().block().text().contains(
                QStringLiteral("Target Heading")),
        QStringLiteral("a standalone self-heading wiki link still navigates"));
  QMetaObject::invokeMethod(editor, "linkClicked", Qt::DirectConnection,
                            Q_ARG(QString, QStringLiteral("Sibling")));
  check(readBytes(sibling) == QByteArrayLiteral("sibling must stay untouched\n"),
        QStringLiteral("a standalone wiki link cannot open or alter a sibling"));

  QTextCursor cursor = editor->sourceTextCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("Changed\n"));
  editor->setSourceTextCursor(cursor);
  action("saveAction")->trigger();
  QApplication::processEvents();
  QByteArray saved = readBytes(standalone);
  check(saved.startsWith(QByteArray::fromHex("EFBBBF")),
        QStringLiteral("standalone save preserves the UTF-8 BOM"));
  QByteArray withoutCrLf = saved;
  withoutCrLf.replace(QByteArrayLiteral("\r\n"), QByteArray());
  check(saved.contains(QByteArrayLiteral("Changed\r\n")) &&
            !withoutCrLf.contains('\n') && !withoutCrLf.contains('\r'),
        QStringLiteral("standalone save preserves CRLF line endings"));
  check(readBytes(sibling) == QByteArrayLiteral("sibling must stay untouched\n"),
        QStringLiteral("saving the document does not touch neighboring files"));

  title->setText(QStringLiteral("Renamed"));
  QMetaObject::invokeMethod(title, "editingFinished", Qt::DirectConnection);
  const QString renamed = files.filePath(QStringLiteral("Renamed.md"));
  check(!QFileInfo::exists(standalone) && QFileInfo::exists(renamed) &&
            window.standalonePath() == QFileInfo(renamed).canonicalFilePath(),
        QStringLiteral("standalone title editing renames only the open file"));
  check(readBytes(renamed).startsWith(QByteArray::fromHex("EFBBBF")) &&
            readBytes(sibling) == QByteArrayLiteral("sibling must stay untouched\n"),
        QStringLiteral("rename preserves content and leaves the sibling alone"));

  StandaloneFile::Document invalidDocument;
  const QString invalid = files.filePath(QStringLiteral("Invalid.md"));
  QFile invalidFile(invalid);
  check(invalidFile.open(QIODevice::WriteOnly) &&
            invalidFile.write(QByteArray::fromHex("FFFE")) == 2,
        QStringLiteral("invalid UTF-8 standalone fixture is writable"));
  invalidFile.close();
  QString invalidError;
  check(!StandaloneFile::load(invalid, &invalidDocument, &invalidError) &&
            !invalidError.isEmpty(),
        QStringLiteral("invalid UTF-8 is rejected instead of being corrupted"));

  window.close();
  QApplication::processEvents();
  check(settings.value(QStringLiteral("lastClosedVault")).toString() ==
            previousVault.path(),
        QStringLiteral("closing a standalone window does not replace the last vault"));
}

void testWhatsNewAfterUpdate() {
  QSettings settings;
  settings.clear();
  const QString originalVersion = QCoreApplication::applicationVersion();
  const QString currentVersion = QStringLiteral(EMERALD_VERSION);
  QCoreApplication::setApplicationVersion(currentVersion);

  // A new installation establishes its baseline silently.
  {
    MainWindow freshInstall;
    freshInstall.resize(1000, 700);
    freshInstall.show();
    QApplication::processEvents();
    check(!freshInstall.findChild<QDialog *>(
              QStringLiteral("whatsNewDialog"), Qt::FindDirectChildrenOnly) &&
              settings.value(QStringLiteral("lastRunVersion")).toString() ==
                  currentVersion,
          QStringLiteral("a fresh install records its version without an "
                         "update notification"));
    freshInstall.close();
    QApplication::processEvents();
  }

  settings.setValue(QStringLiteral("lastRunVersion"),
                    QStringLiteral("0.0.0"));
  settings.sync();
  {
    MainWindow updated;
    updated.resize(1000, 700);
    updated.show();
    QDialog *automatic = nullptr;
    check(waitUntil([&] {
            automatic = updated.findChild<QDialog *>(
                QStringLiteral("whatsNewDialog"),
                Qt::FindDirectChildrenOnly);
            return automatic && automatic->isVisible();
          }),
          QStringLiteral("an upgraded installation opens What's New"));
    check(automatic && !automatic->isModal() &&
              automatic->windowModality() == Qt::NonModal,
          QStringLiteral("What's New does not confine desktop pointer input"));

    auto *content = automatic
                        ? automatic->findChild<QTextBrowser *>(
                              QStringLiteral("whatsNewContent"))
                        : nullptr;
    auto *action = updated.findChild<QAction *>(
        QStringLiteral("whatsNewAction"));
    check(content &&
              content->toPlainText().contains(
                  QStringLiteral("Emerald %1").arg(currentVersion),
                  Qt::CaseInsensitive) &&
              settings.value(QStringLiteral("lastRunVersion")).toString() ==
                  currentVersion &&
              action,
          QStringLiteral("What's New uses bundled release notes, records the "
                         "version, and remains available from the menu"));

    if (automatic) {
      automatic->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    if (action)
      action->trigger();
    QDialog *reopened = nullptr;
    check(waitUntil([&] {
            reopened = updated.findChild<QDialog *>(
                QStringLiteral("whatsNewDialog"),
                Qt::FindDirectChildrenOnly);
            return reopened && reopened->isVisible();
          }),
          QStringLiteral("the What's New menu action reopens the panel"));
    if (reopened)
      reopened->close();
    updated.close();
    QApplication::processEvents();
  }

  // Reopening the same release must not repeat the automatic notification.
  {
    MainWindow sameVersion;
    sameVersion.show();
    QApplication::processEvents();
    check(!sameVersion.findChild<QDialog *>(
              QStringLiteral("whatsNewDialog"), Qt::FindDirectChildrenOnly),
          QStringLiteral("What's New is shown automatically only once per "
                         "installed version"));
    sameVersion.close();
    QApplication::processEvents();
  }

  QCoreApplication::setApplicationVersion(originalVersion);
  settings.clear();
}

void testNonModalDialogsAndUpdateProgress() {
  QWidget parent;
  parent.resize(640, 480);
  parent.show();

  QDialog dialog(&parent);
  bool plainDialogNonModal = false;
  QTimer::singleShot(0, [&] {
    plainDialogNonModal = dialog.isVisible() && !dialog.isModal() &&
                          dialog.windowModality() == Qt::NonModal;
    dialog.accept();
  });
  check(DialogUtils::run(dialog) == QDialog::Accepted && plainDialogNonModal,
        QStringLiteral("shared dialog runner preserves synchronous results "
                       "without modal pointer confinement"));

  bool messageNonModal = false;
  QTimer::singleShot(0, [&] {
    auto *box = qobject_cast<QMessageBox *>(activeTestDialog());
    messageNonModal = box && box->isVisible() && !box->isModal() &&
                      box->windowModality() == Qt::NonModal;
    if (box) {
      if (auto *cancel = box->button(QMessageBox::Cancel))
        cancel->click();
      else
        box->reject();
    }
  });
  const auto answer = DialogUtils::question(
      &parent, QStringLiteral("Question"), QStringLiteral("Continue?"),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  check(messageNonModal && answer == QMessageBox::Cancel,
        QStringLiteral("message boxes use the non-modal runner and return the "
                       "clicked standard button"));

  UpdateProgressDialog progress(QStringLiteral("2.2.2-test"), &parent);
  progress.setPercentage(73);
  progress.show();
  QApplication::processEvents();
  auto *bar = progress.findChild<QProgressBar *>(
      QStringLiteral("updateProgressBar"));
  auto *percentage = progress.findChild<QLabel *>(
      QStringLiteral("updateProgressPercentage"));
  check(bar && percentage && !bar->isTextVisible() && bar->value() == 73 &&
            percentage->text() == QStringLiteral("73%") &&
            !progress.isModal() &&
            progress.windowModality() == Qt::NonModal,
        QStringLiteral("update download percentage is a separate label outside "
                       "the non-modal progress bar"));
  progress.close();
  parent.close();
}
} // namespace

int main(int argc, char **argv) {
  QTemporaryDir settingsDir;
  QTemporaryDir applicationData;
  if (!settingsDir.isValid() || !applicationData.isValid())
    return 2;
  qputenv("XDG_DATA_HOME", applicationData.path().toUtf8());
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("EmeraldTests"));
  QCoreApplication::setApplicationName(QStringLiteral("MainWindowGraphTests"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settingsDir.path());
  AppTheme::apply(app, AppTheme::Id::Dark);

  testInPaneGraphNavigation(settingsDir.path());
  testLastNotePerVault();
  testLastClosedVault();
  testVaultSwitcherModifiedOrder();
  testWikiHeadingNavigation();
  testFullWidthEditorPreference();
  testFileTreeSortPreference();
  testReleaseChannelPreference();
  testThemePreference();
  testCustomThemes();
  testStandaloneFileSession();
  testWhatsNewAfterUpdate();
  testNonModalDialogsAndUpdateProgress();
  if (failures == 0)
    QTextStream(stdout) << "All MainWindow graph tests passed.\n";
  return failures == 0 ? 0 : 1;
}
