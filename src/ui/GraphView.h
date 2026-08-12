#pragma once

#include "core/LinkGraphIndex.h"

#include <QPointF>
#include <QVector>
#include <QWidget>

class QThread;

// Dependency-free graph canvas: compact graph arrays are laid out off the UI
// thread and painted directly with QPainter. There is no per-node QObject or
// QGraphicsItem overhead, which keeps large vaults comparatively light.
class GraphView : public QWidget {
  Q_OBJECT
public:
  enum class Direction { Both, Outgoing, Incoming };

  explicit GraphView(QWidget *parent = nullptr);
  ~GraphView() override;

  void setSnapshot(const LinkGraphIndex::Snapshot &snapshot);
  void clearGraph();
  void setCurrentPath(const QString &path);
  void setLocalMode(bool local);
  void setLocalRoot(const QString &path);
  void setLocalDepth(int depth);
  void setShowOrphans(bool show);
  void setShowUnresolved(bool show);
  void setShowArrows(bool show);
  void setSearchText(const QString &text);
  void setFolderFilter(const QString &folder);
  void setDirection(Direction direction);

  bool localMode() const { return m_localMode; }
  QString localRoot() const { return m_localRoot; }
  int localDepth() const { return m_localDepth; }
  bool showOrphans() const { return m_showOrphans; }
  bool showUnresolved() const { return m_showUnresolved; }
  bool showArrows() const { return m_showArrows; }
  QString folderFilter() const { return m_folderFilter; }
  Direction direction() const { return m_direction; }
  int visibleNodeCount() const;
  int visibleEdgeCount() const;
  QPointF cameraCenter() const { return m_cameraCenter; }
  qreal zoomScale() const { return m_scale; }
  void setCamera(const QPointF &center, qreal scale);
  QString selectedPath() const;
  void selectPath(const QString &path);

public slots:
  void fitToView();
  void resetCamera();

signals:
  void noteActivated(const QString &path);
  void selectionChanged(const QString &title, int outgoing, int incoming,
                        bool unresolved);
  void visibleGraphChanged(int nodes, int edges);
  void searchRequested();
  void cameraChanged();
  void layoutSettled();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool event(QEvent *event) override;

private:
  QString nodeKey(int index) const;
  QPointF worldToScreen(const QPointF &point) const;
  QPointF screenToWorld(const QPointF &point) const;
  qreal nodeRadius(int index) const;
  int nodeAt(const QPointF &screenPoint) const;
  QColor nodeColor(int index) const;
  void rebuildVisibleSet();
  void selectNode(int index);
  void startLayout();
  void stopLayout();
  void applyLayout(int generation, const QVector<QPointF> &positions,
                   bool final);

  LinkGraphIndex::Snapshot m_graph;
  QVector<QPointF> m_positions;
  QVector<bool> m_visible;
  QString m_currentPath;
  QString m_localRoot;
  QString m_searchText;
  QString m_folderFilter = QStringLiteral("*");
  QPointF m_cameraCenter;
  qreal m_scale = 1.0;
  int m_localDepth = 1;
  int m_selected = -1;
  int m_hovered = -1;
  int m_draggedNode = -1;
  bool m_localMode = false;
  bool m_showOrphans = true;
  bool m_showUnresolved = false;
  bool m_showArrows = false;
  bool m_panning = false;
  bool m_movedSincePress = false;
  QPointF m_lastPointer;
  QPointF m_pressPointer;
  QThread *m_layoutThread = nullptr;
  int m_layoutGeneration = 0;
  bool m_hasFit = false;
  Direction m_direction = Direction::Both;
  bool m_fitAfterLayout = false;
  quint64 m_topologySignature = 0;
};
