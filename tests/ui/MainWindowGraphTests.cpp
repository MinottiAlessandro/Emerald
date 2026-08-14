#include "core/Perf.h"
#include "ui/AppTheme.h"
#include "ui/GraphPage.h"
#include "ui/GraphView.h"
#include "ui/MainWindow.h"
#include "ui/MarkdownEditor.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontComboBox>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
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

bool writeFile(const QString &path, const QString &content) {
  QFile file(path);
  const QByteArray bytes = content.toUtf8();
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes) == bytes.size();
}

bool setModificationTime(const QString &path, const QDateTime &time) {
  QFile file(path);
  return file.open(QIODevice::ReadWrite) &&
         file.setFileTime(time, QFileDevice::FileModificationTime);
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
  auto *cheatsheet =
      window.findChild<QFrame *>(QStringLiteral("shortcutCheatsheet"));
  check(pages && graphPage && graphView && graphAction && localGraphAction &&
            backAction && forwardAction && title && sidebar && sideTitle &&
            splitter && gearMenu && settingsAction && editor && cheatsheet,
        QStringLiteral("graph page and navigation controls are discoverable"));
  if (!pages || !graphPage || !graphView || !graphAction || !localGraphAction ||
      !backAction || !forwardAction || !title || !sidebar || !sideTitle ||
      !splitter || !gearMenu || !settingsAction || !editor || !cheatsheet)
    return;

  check(!cheatsheet->isVisible(),
        QStringLiteral("shortcut cheatsheet starts hidden"));
  check(cheatsheet->findChildren<QLabel *>(
                           QStringLiteral("shortcutSectionTitle"))
                    .size() == 4 &&
            cheatsheet->findChildren<QLabel *>(QStringLiteral("shortcutKey"))
                    .size() >= 30,
        QStringLiteral("shortcut cheatsheet is grouped and comprehensive"));

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
  bool spellingLanguagePopupOpaque = false;
  bool spellingLanguagePopupFrameOpaque = false;
  bool fontPopupOpaque = false;
  bool spellingManagerSeen = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    auto *openGlobal = dialog ? dialog->findChild<QPushButton *>(
                                    QStringLiteral("settingsOpenGraph"))
                              : nullptr;
    auto *openLocal = dialog ? dialog->findChild<QPushButton *>(
                                   QStringLiteral("settingsOpenLocalGraph"))
                             : nullptr;
    auto *spellEnabled = dialog ? dialog->findChild<QCheckBox *>(
                                      QStringLiteral("spellCheckEnabled"))
                                : nullptr;
    auto *spellLanguage = dialog ? dialog->findChild<QComboBox *>(
                                       QStringLiteral("spellLanguage"))
                                 : nullptr;
    auto *manageLanguages = dialog ? dialog->findChild<QPushButton *>(
                                         QStringLiteral("manageSpellLanguages"))
                                   : nullptr;
    auto *fontFamily = dialog ? dialog->findChild<QFontComboBox *>() : nullptr;
    const QString settingsScreenshot =
        QString::fromLocal8Bit(qgetenv("EMERALD_TEST_SETTINGS_SCREENSHOT"));
    if (dialog && !settingsScreenshot.isEmpty()) {
      QApplication::processEvents();
      dialog->grab().save(settingsScreenshot);
    }
    settingsGraphControlsSeen = openGlobal && openLocal;
    spellingControlsSeen = spellEnabled && spellLanguage && manageLanguages &&
                           spellEnabled->isChecked() &&
                           spellLanguage->currentData() ==
                               QStringLiteral("en_US");
    if (spellLanguage) {
      // Exercise an unselected row: selected rows have their own fill and
      // could hide a transparent popup viewport regression.
      spellLanguage->addItem(QStringLiteral("Test language"),
                             QStringLiteral("test_TEST"));
      spellLanguage->showPopup();
      QApplication::processEvents();
      QAbstractItemView *popup = spellLanguage->view();
      QWidget *viewport = popup ? popup->viewport() : nullptr;
      spellingLanguagePopupFrameOpaque =
          popup && widgetRendersOpaque(popup->window());
      const QModelIndex testIndex =
          spellLanguage->model()->index(spellLanguage->count() - 1, 0);
      const QRect testRow = popup ? popup->visualRect(testIndex) : QRect();
      if (viewport && testRow.isValid() && !testRow.isEmpty()) {
        QImage rendered(viewport->size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        QPainter painter(&rendered);
        viewport->render(&painter);
        painter.end();
        const QPoint sample(qMax(testRow.left(), testRow.right() - 6),
                            testRow.center().y());
        if (rendered.rect().contains(sample)) {
          const QColor color = rendered.pixelColor(sample);
          spellingLanguagePopupOpaque =
              color.alpha() == 255 &&
              color == QColor(QStringLiteral("#121512"));
        }
      }
      spellLanguage->hidePopup();
      spellLanguage->removeItem(spellLanguage->count() - 1);
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
        auto *manager = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        const QString spellingScreenshot = QString::fromLocal8Bit(
            qgetenv("EMERALD_TEST_SPELLING_SCREENSHOT"));
        if (manager && !spellingScreenshot.isEmpty()) {
          QApplication::processEvents();
          manager->grab().save(spellingScreenshot);
        }
        spellingManagerSeen =
            manager && manager->objectName() ==
                           QStringLiteral("spellLanguageDialog") &&
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
  check(spellingControlsSeen,
        QStringLiteral("Settings exposes enabled bundled-English spelling and "
                       "the optional-language manager"));
  check(spellingLanguagePopupOpaque,
        QStringLiteral("the spelling language popup paints unselected rows "
                       "with an opaque background"));
  check(spellingLanguagePopupFrameOpaque,
        QStringLiteral("the spelling language popup paints an opaque frame"));
  check(fontPopupOpaque,
        QStringLiteral("the font-family popup is fully opaque"));
  check(spellingManagerSeen,
        QStringLiteral("language manager lists bundled and optional packs"));
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
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      auto *fullWidth = dialog ? dialog->findChild<QCheckBox *>(
                                     QStringLiteral("editorFullWidth"))
                               : nullptr;
      auto *pixelWidth = dialog ? dialog->findChild<QSpinBox *>(
                                      QStringLiteral("editorColumnWidth"))
                                : nullptr;
      controlsSeen = fullWidth && pixelWidth && !fullWidth->isChecked() &&
                     pixelWidth->isEnabled() && pixelWidth->value() == 520;
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
        auto *dialog =
            qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
      auto *dialog =
          qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
      auto *settingsDialog =
          qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
        auto *editorDialog =
            qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
              auto *activeEditor =
                  qobject_cast<QDialog *>(QApplication::activeModalWidget());
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
  testFullWidthEditorPreference();
  testFileTreeSortPreference();
  testThemePreference();
  testCustomThemes();
  if (failures == 0)
    QTextStream(stdout) << "All MainWindow graph tests passed.\n";
  return failures == 0 ? 0 : 1;
}
