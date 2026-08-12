#include "GraphPage.h"

#include "GraphView.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

GraphPage::GraphPage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("graphPage"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *header = new QWidget(this);
  header->setObjectName(QStringLiteral("graphHeader"));
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(18, 8, 18, 8);
  headerLayout->setSpacing(10);
  auto *title = new QLabel(tr("Graph"), header);
  title->setObjectName(QStringLiteral("graphTitle"));
  m_search = new QLineEdit(header);
  m_search->setObjectName(QStringLiteral("graphSearch"));
  m_search->setPlaceholderText(tr("Find a note…  (/)"));
  m_search->setClearButtonEnabled(true);
  m_search->setMinimumWidth(90);
  m_search->setMaximumWidth(280);
  m_search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  headerLayout->addWidget(title);
  headerLayout->addWidget(m_search, 1);

  auto *controls = new QHBoxLayout;
  controls->setSpacing(8);
  m_scope = new QComboBox(header);
  m_scope->setObjectName(QStringLiteral("graphScope"));
  m_scope->addItem(tr("Global"), false);
  m_scope->addItem(tr("Local"), true);
  m_depth = new QComboBox(header);
  m_depth->setObjectName(QStringLiteral("graphDepth"));
  for (int depth = 1; depth <= 3; ++depth)
    m_depth->addItem(tr("Depth %1").arg(depth), depth);
  m_depth->hide();
  m_folder = new QComboBox(header);
  m_folder->setObjectName(QStringLiteral("graphFolder"));
  m_folder->addItem(tr("All folders"), QStringLiteral("*"));
  m_direction = new QComboBox(header);
  m_direction->setObjectName(QStringLiteral("graphDirection"));
  m_direction->addItem(tr("Both directions"), int(GraphView::Direction::Both));
  m_direction->addItem(tr("Outgoing"), int(GraphView::Direction::Outgoing));
  m_direction->addItem(tr("Incoming"), int(GraphView::Direction::Incoming));
  m_direction->hide();
  m_orphans = new QCheckBox(tr("Orphans"), header);
  m_orphans->setChecked(true);
  m_unresolved = new QCheckBox(tr("Missing"), header);
  m_arrows = new QCheckBox(tr("Arrows"), header);
  m_filterButton = new QToolButton(header);
  m_filterButton->setObjectName(QStringLiteral("graphFilters"));
  m_filterButton->setText(tr("Filters"));
  m_filterButton->setPopupMode(QToolButton::InstantPopup);
  auto *filterMenu = new QMenu(m_filterButton);
  auto addFilterAction = [filterMenu](const QString &text, bool checked) {
    QAction *action = filterMenu->addAction(text);
    action->setCheckable(true);
    action->setChecked(checked);
    return action;
  };
  QAction *orphanAction = addFilterAction(tr("Show orphans"), true);
  QAction *missingAction = addFilterAction(tr("Show missing notes"), false);
  QAction *arrowAction = addFilterAction(tr("Show link arrows"), false);
  filterMenu->addSeparator();
  m_folderMenu = filterMenu->addMenu(tr("Folder"));
  m_directionMenu = filterMenu->addMenu(tr("Direction"));
  m_directionGroup = new QActionGroup(m_directionMenu);
  m_directionGroup->setExclusive(true);
  for (int i = 0; i < m_direction->count(); ++i) {
    QAction *action = m_directionMenu->addAction(m_direction->itemText(i));
    action->setCheckable(true);
    action->setData(m_direction->itemData(i));
    m_directionGroup->addAction(action);
    if (i == 0)
      action->setChecked(true);
    connect(action, &QAction::triggered, this,
            [this, i] { m_direction->setCurrentIndex(i); });
  }
  m_filterButton->setMenu(filterMenu);
  m_filterButton->hide();
  auto *fit = new QPushButton(tr("Fit"), header);
  fit->setObjectName(QStringLiteral("graphFit"));
  controls->addWidget(m_scope);
  controls->addWidget(m_depth);
  controls->addWidget(m_folder);
  controls->addWidget(m_direction);
  controls->addWidget(m_orphans);
  controls->addWidget(m_unresolved);
  controls->addWidget(m_arrows);
  controls->addWidget(m_filterButton);
  controls->addWidget(fit);
  headerLayout->addLayout(controls);
  layout->addWidget(header);

  // Float graph information over the canvas so it does not consume another
  // header row. Counts stay pinned to the bottom-right and selection details
  // have their own bottom-left label, so selecting a note never hides totals.
  auto *canvas = new QWidget(this);
  canvas->setObjectName(QStringLiteral("graphCanvasLayer"));
  auto *canvasLayout = new QGridLayout(canvas);
  canvasLayout->setContentsMargins(0, 0, 0, 0);
  canvasLayout->setSpacing(0);
  m_view = new GraphView(canvas);
  canvasLayout->addWidget(m_view, 0, 0);
  auto *statusOverlay = new QWidget(canvas);
  statusOverlay->setObjectName(QStringLiteral("graphStatusOverlay"));
  statusOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  auto *statusLayout = new QHBoxLayout(statusOverlay);
  statusLayout->setContentsMargins(14, 0, 14, 12);
  statusLayout->setSpacing(8);
  m_selectionStatus = new QLabel(statusOverlay);
  m_selectionStatus->setObjectName(QStringLiteral("graphSelectionStatus"));
  m_selectionStatus->hide();
  m_status = new QLabel(statusOverlay);
  m_status->setObjectName(QStringLiteral("graphStatus"));
  m_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  statusLayout->addWidget(m_selectionStatus);
  statusLayout->addStretch(1);
  statusLayout->addWidget(m_status);
  canvasLayout->addWidget(statusOverlay, 0, 0, Qt::AlignBottom);
  layout->addWidget(canvas, 1);

  connect(m_search, &QLineEdit::textChanged, m_view, &GraphView::setSearchText);
  connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int) {
    const bool local = m_scope->currentData().toBool();
    if (local) {
      const QString selected = m_view->selectedPath();
      if (!selected.isEmpty())
        m_view->setLocalRoot(selected);
    }
    updateCompactControls();
    m_view->setLocalMode(local);
  });
  connect(m_depth, &QComboBox::currentIndexChanged, this, [this](int) {
    m_view->setLocalDepth(m_depth->currentData().toInt());
  });
  connect(m_folder, &QComboBox::currentIndexChanged, this, [this](int) {
    m_requestedFolderFilter = m_folder->currentData().toString();
    m_view->setFolderFilter(m_requestedFolderFilter);
    if (m_folderMenu)
      for (QAction *action : m_folderMenu->actions())
        action->setChecked(action->data().toString() ==
                           m_requestedFolderFilter);
    scheduleStateSave();
  });
  connect(m_direction, &QComboBox::currentIndexChanged, this, [this](int) {
    m_view->setDirection(
        GraphView::Direction(m_direction->currentData().toInt()));
    if (m_directionMenu)
      for (QAction *action : m_directionMenu->actions())
        action->setChecked(action->data().toInt() ==
                           m_direction->currentData().toInt());
    scheduleStateSave();
  });
  connect(m_orphans, &QCheckBox::toggled, m_view, &GraphView::setShowOrphans);
  connect(m_unresolved, &QCheckBox::toggled, m_view,
          &GraphView::setShowUnresolved);
  connect(m_arrows, &QCheckBox::toggled, m_view, &GraphView::setShowArrows);
  connect(m_orphans, &QCheckBox::toggled, orphanAction, &QAction::setChecked);
  connect(m_unresolved, &QCheckBox::toggled, missingAction,
          &QAction::setChecked);
  connect(m_arrows, &QCheckBox::toggled, arrowAction, &QAction::setChecked);
  connect(orphanAction, &QAction::toggled, m_orphans, &QCheckBox::setChecked);
  connect(missingAction, &QAction::toggled, m_unresolved,
          &QCheckBox::setChecked);
  connect(arrowAction, &QAction::toggled, m_arrows, &QCheckBox::setChecked);
  connect(fit, &QPushButton::clicked, m_view, &GraphView::fitToView);
  connect(m_view, &GraphView::noteActivated, this, &GraphPage::noteActivated);
  connect(m_view, &GraphView::searchRequested, m_search,
          qOverload<>(&QWidget::setFocus));
  connect(m_view, &GraphView::visibleGraphChanged, this,
          [this](int nodes, int edges) {
            m_visibleNodes = nodes;
            m_visibleEdges = edges;
            updateStatus();
          });
  connect(m_view, &GraphView::selectionChanged, this,
          [this](const QString &title, int outgoing, int incoming,
                 bool unresolved) {
            if (title.isEmpty()) {
              m_selectedSummary.clear();
            } else if (unresolved) {
              m_selectedSummary = tr("%1 · missing note").arg(title);
            } else {
              m_selectedSummary = tr("%1 · %2 outgoing · %3 backlinks")
                                      .arg(title)
                                      .arg(outgoing)
                                      .arg(incoming);
            }
            updateStatus();
          });

  m_stateTimer = new QTimer(this);
  m_stateTimer->setSingleShot(true);
  m_stateTimer->setInterval(250);
  connect(m_stateTimer, &QTimer::timeout, this,
          [this] { emit stateChanged(savedState()); });
  connect(m_depth, &QComboBox::currentIndexChanged, this,
          [this](int) { scheduleStateSave(); });
  connect(m_orphans, &QCheckBox::toggled, this,
          [this](bool) { scheduleStateSave(); });
  connect(m_unresolved, &QCheckBox::toggled, this,
          [this](bool) { scheduleStateSave(); });
  connect(m_arrows, &QCheckBox::toggled, this,
          [this](bool) { scheduleStateSave(); });
  connect(m_view, &GraphView::cameraChanged, this,
          &GraphPage::scheduleStateSave);
  updateStatus();
}

void GraphPage::setSnapshot(const LinkGraphIndex::Snapshot &snapshot) {
  QSet<QString> folderSet;
  bool hasRootNotes = false;
  for (const LinkGraphIndex::Node &node : snapshot.nodes) {
    if (node.unresolved)
      continue;
    if (node.folder.isEmpty())
      hasRootNotes = true;
    else
      folderSet.insert(node.folder);
  }
  QStringList folders(folderSet.cbegin(), folderSet.cend());
  folders.sort(Qt::CaseInsensitive);
  m_folder->blockSignals(true);
  m_folder->clear();
  m_folder->addItem(tr("All folders"), QStringLiteral("*"));
  if (hasRootNotes)
    m_folder->addItem(tr("Vault root"), QString());
  for (const QString &folder : folders)
    m_folder->addItem(folder, folder);
  int folderIndex = m_folder->findData(m_requestedFolderFilter);
  if (folderIndex < 0) {
    m_requestedFolderFilter = QStringLiteral("*");
    folderIndex = 0;
  }
  m_folder->setCurrentIndex(folderIndex);
  m_folder->blockSignals(false);

  delete m_folderGroup;
  m_folderGroup = nullptr;
  m_folderMenu->clear();
  m_folderGroup = new QActionGroup(m_folderMenu);
  m_folderGroup->setExclusive(true);
  for (int i = 0; i < m_folder->count(); ++i) {
    QAction *action = m_folderMenu->addAction(m_folder->itemText(i));
    action->setCheckable(true);
    action->setData(m_folder->itemData(i));
    action->setChecked(i == folderIndex);
    m_folderGroup->addAction(action);
    connect(action, &QAction::triggered, this,
            [this, i] { m_folder->setCurrentIndex(i); });
  }
  m_view->setFolderFilter(m_requestedFolderFilter);
  m_view->setSnapshot(snapshot);
}

void GraphPage::clearGraph() {
  if (m_stateTimer)
    m_stateTimer->stop();
  m_selectedSummary.clear();
  m_search->clear();
  m_view->clearGraph();
}

void GraphPage::openGlobal(const QString &currentPath) {
  m_view->setCurrentPath(currentPath);
  m_scope->setCurrentIndex(0);
  m_view->setLocalMode(false);
  focusGraph();
}

void GraphPage::openLocal(const QString &rootPath) {
  m_view->setCurrentPath(rootPath);
  m_scope->setCurrentIndex(1);
  m_view->setLocalRoot(rootPath);
  m_view->setLocalMode(true);
  focusGraph();
}

void GraphPage::setCurrentPath(const QString &path) {
  m_view->setCurrentPath(path);
  if (!m_view->localMode() && !path.isEmpty())
    m_view->setLocalRoot(path);
}

void GraphPage::focusGraph() { m_view->setFocus(Qt::OtherFocusReason); }

bool GraphPage::isLocal() const { return m_view->localMode(); }

QString GraphPage::localRoot() const { return m_view->localRoot(); }

QString GraphPage::savedState() const {
  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("localDepth"), m_depth->currentData().toInt());
  root.insert(QStringLiteral("folder"), m_requestedFolderFilter);
  root.insert(QStringLiteral("direction"), m_direction->currentData().toInt());
  root.insert(QStringLiteral("showOrphans"), m_orphans->isChecked());
  root.insert(QStringLiteral("showUnresolved"), m_unresolved->isChecked());
  root.insert(QStringLiteral("showArrows"), m_arrows->isChecked());
  root.insert(QStringLiteral("cameraX"), m_view->cameraCenter().x());
  root.insert(QStringLiteral("cameraY"), m_view->cameraCenter().y());
  root.insert(QStringLiteral("scale"), m_view->zoomScale());
  root.insert(QStringLiteral("hasCamera"), true);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void GraphPage::restoreState(const QString &state) {
  const QJsonDocument document = QJsonDocument::fromJson(state.toUtf8());
  if (!document.isObject())
    return;
  const QJsonObject root = document.object();
  m_restoringState = true;
  const int depth = qBound(1,
                           root.value(QStringLiteral("localDepth"))
                               .toInt(m_depth->currentData().toInt()),
                           3);
  m_depth->setCurrentIndex(m_depth->findData(depth));
  m_requestedFolderFilter =
      root.value(QStringLiteral("folder")).toString(QStringLiteral("*"));
  m_view->setFolderFilter(m_requestedFolderFilter);
  const int direction = qBound(int(GraphView::Direction::Both),
                               root.value(QStringLiteral("direction"))
                                   .toInt(int(GraphView::Direction::Both)),
                               int(GraphView::Direction::Incoming));
  const int directionIndex = m_direction->findData(direction);
  if (directionIndex >= 0)
    m_direction->setCurrentIndex(directionIndex);
  m_orphans->setChecked(root.value(QStringLiteral("showOrphans")).toBool(true));
  m_unresolved->setChecked(
      root.value(QStringLiteral("showUnresolved")).toBool(false));
  m_arrows->setChecked(root.value(QStringLiteral("showArrows")).toBool(false));
  if (root.value(QStringLiteral("hasCamera")).toBool(false))
    m_view->setCamera(QPointF(root.value(QStringLiteral("cameraX")).toDouble(),
                              root.value(QStringLiteral("cameraY")).toDouble()),
                      root.value(QStringLiteral("scale")).toDouble(1.0));
  m_restoringState = false;
}

QString GraphPage::sessionState() const {
  QJsonObject root = QJsonDocument::fromJson(savedState().toUtf8()).object();
  root.insert(QStringLiteral("search"), m_search->text());
  root.insert(QStringLiteral("selectedPath"), m_view->selectedPath());
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void GraphPage::restoreSessionState(const QString &state) {
  restoreState(state);
  const QJsonObject root = QJsonDocument::fromJson(state.toUtf8()).object();
  m_search->setText(root.value(QStringLiteral("search")).toString());
  m_view->selectPath(root.value(QStringLiteral("selectedPath")).toString());
}

void GraphPage::scheduleStateSave() {
  if (!m_restoringState && m_stateTimer)
    m_stateTimer->start();
}

void GraphPage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateCompactControls();
}

QSize GraphPage::minimumSizeHint() const { return QSize(260, 180); }

void GraphPage::updateCompactControls() {
  const bool compact = width() > 0 && width() < 620;
  const bool local = m_scope->currentData().toBool();
  m_depth->setVisible(local);
  m_folder->setVisible(!compact && !local);
  m_direction->setVisible(!compact && local);
  m_orphans->setVisible(!compact);
  m_unresolved->setVisible(!compact);
  m_arrows->setVisible(!compact);
  m_filterButton->setVisible(compact);
  m_folderMenu->setEnabled(!local);
  m_directionMenu->setEnabled(local);
}

void GraphPage::updateStatus(int, int) {
  m_selectionStatus->setText(m_selectedSummary);
  m_selectionStatus->setVisible(!m_selectedSummary.isEmpty());
  m_status->setText(
      tr("%1 notes · %2 links").arg(m_visibleNodes).arg(m_visibleEdges));
}
