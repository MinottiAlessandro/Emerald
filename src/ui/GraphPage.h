#pragma once

#include "core/LinkGraphIndex.h"

#include <QWidget>

class GraphView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTimer;
class QMenu;
class QActionGroup;

class GraphPage : public QWidget {
  Q_OBJECT
public:
  explicit GraphPage(QWidget *parent = nullptr);

  void setSnapshot(const LinkGraphIndex::Snapshot &snapshot);
  void clearGraph();
  void openGlobal(const QString &currentPath);
  void openLocal(const QString &rootPath);
  void setCurrentPath(const QString &path);
  void focusGraph();

  bool isLocal() const;
  QString localRoot() const;
  QString savedState() const;
  void restoreState(const QString &state);
  QString sessionState() const;
  void restoreSessionState(const QString &state);

signals:
  void noteActivated(const QString &path);
  void stateChanged(const QString &state);

protected:
    void resizeEvent(QResizeEvent *event) override;
    QSize minimumSizeHint() const override;

private:
  void updateStatus(int nodes = -1, int edges = -1);
  void scheduleStateSave();
  void updateCompactControls();

  GraphView *m_view = nullptr;
  QLineEdit *m_search = nullptr;
  QComboBox *m_scope = nullptr;
  QComboBox *m_depth = nullptr;
  QComboBox *m_folder = nullptr;
  QComboBox *m_direction = nullptr;
  QCheckBox *m_orphans = nullptr;
  QCheckBox *m_unresolved = nullptr;
  QCheckBox *m_arrows = nullptr;
  QLabel *m_selectionStatus = nullptr;
  QLabel *m_status = nullptr;
  QToolButton *m_filterButton = nullptr;
  QTimer *m_stateTimer = nullptr;
  QMenu *m_folderMenu = nullptr;
  QMenu *m_directionMenu = nullptr;
  QActionGroup *m_folderGroup = nullptr;
  QActionGroup *m_directionGroup = nullptr;
  QString m_selectedSummary;
  int m_visibleNodes = 0;
  int m_visibleEdges = 0;
  bool m_restoringState = false;
  QString m_requestedFolderFilter = QStringLiteral("*");
};
