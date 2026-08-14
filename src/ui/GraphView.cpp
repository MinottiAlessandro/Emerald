#include "GraphView.h"

#include "AppTheme.h"

#include <QApplication>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>
#include <QPointer>
#include <QQueue>
#include <QResizeEvent>
#include <QThread>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kMinScale = 0.06;
constexpr qreal kMaxScale = 5.0;

quint64 topologySignature(const LinkGraphIndex::Snapshot &graph) {
  quint64 signature = quint64(graph.nodes.size()) * 0x9e3779b97f4a7c15ULL ^
                      quint64(graph.edges.size());
  for (const auto &node : graph.nodes) {
    const QString key = node.path.isEmpty() ? QStringLiteral("missing:") +
                                                  node.title.toCaseFolded()
                                            : node.path;
    signature ^= quint64(qHash(key, 0x7f4a7c15U)) + 0x9e3779b97f4a7c15ULL +
                 (signature << 6) + (signature >> 2);
  }
  for (const auto &edge : graph.edges) {
    const quint64 packed = (quint64(quint32(edge.from)) << 32) ^
                           quint64(quint32(edge.to)) ^
                           (quint64(quint32(edge.occurrences)) << 17);
    signature ^=
        packed + 0x517cc1b727220a95ULL + (signature << 6) + (signature >> 2);
  }
  return signature == 0 ? 1 : signature;
}

struct Quad {
  QPointF center;
  QPointF massCenter;
  qreal half = 0.0;
  qreal mass = 0.0;
  int body = -1;
  int children[4] = {-1, -1, -1, -1};
};

int quadrant(const Quad &quad, const QPointF &point) {
  return (point.x() >= quad.center.x() ? 1 : 0) |
         (point.y() >= quad.center.y() ? 2 : 0);
}

int addChild(QVector<Quad> &tree, int parent, int which) {
  const Quad parentCopy = tree.at(parent);
  const qreal half = parentCopy.half * 0.5;
  const QPointF offset((which & 1) ? half : -half, (which & 2) ? half : -half);
  const int child = tree.size();
  tree.push_back({parentCopy.center + offset, {}, half});
  tree[parent].children[which] = child;
  return child;
}

void insertBody(QVector<Quad> &tree, int quadIndex, int body,
                const QVector<QPointF> &positions, int depth = 0) {
  const QPointF point = positions.at(body);
  Quad &quad = tree[quadIndex];
  const qreal oldMass = quad.mass;
  quad.mass += 1.0;
  quad.massCenter =
      oldMass <= 0.0 ? point : (quad.massCenter * oldMass + point) / quad.mass;

  const bool leaf = quad.children[0] < 0;
  if (leaf && quad.body < 0) {
    quad.body = body;
    return;
  }
  if (depth >= 18)
    return; // effectively coincident bodies are represented by aggregate mass

  if (leaf) {
    const int previous = quad.body;
    quad.body = -1;
    const int oldQuadrant = quadrant(quad, positions.at(previous));
    int child = addChild(tree, quadIndex, oldQuadrant);
    insertBody(tree, child, previous, positions, depth + 1);
  }

  const int which = quadrant(tree.at(quadIndex), point);
  int child = tree.at(quadIndex).children[which];
  if (child < 0)
    child = addChild(tree, quadIndex, which);
  insertBody(tree, child, body, positions, depth + 1);
}

QVector<QPointF>
runLayout(const LinkGraphIndex::Snapshot &graph, QVector<QPointF> positions,
          const QVector<bool> &visible,
          const std::function<bool()> &interrupted,
          const std::function<void(const QVector<QPointF> &, bool)> &publish) {
  const int count = graph.nodes.size();
  const int visibleCount =
      int(std::count(visible.cbegin(), visible.cend(), true));
  if (visibleCount <= 1)
    return positions;
  QVector<QPointF> velocity(count);
  QVector<QPointF> force(count);

  constexpr int iterations = 220;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    if (interrupted())
      return positions;

    bool firstVisible = true;
    qreal minX = 0.0;
    qreal maxX = 0.0;
    qreal minY = 0.0;
    qreal maxY = 0.0;
    for (int i = 0; i < positions.size(); ++i) {
      if (!visible.value(i))
        continue;
      const QPointF &point = positions.at(i);
      if (firstVisible) {
        minX = maxX = point.x();
        minY = maxY = point.y();
        firstVisible = false;
      }
      minX = qMin(minX, point.x());
      maxX = qMax(maxX, point.x());
      minY = qMin(minY, point.y());
      maxY = qMax(maxY, point.y());
    }
    const QPointF center((minX + maxX) * 0.5, (minY + maxY) * 0.5);
    const qreal half =
        qMax<qreal>(80.0, qMax(maxX - minX, maxY - minY) * 0.55 + 2.0);
    QVector<Quad> tree;
    tree.reserve(count * 2);
    tree.push_back({center, {}, half});
    for (int i = 0; i < count; ++i)
      if (visible.value(i))
        insertBody(tree, 0, i, positions);

    std::fill(force.begin(), force.end(), QPointF());
    for (int i = 0; i < count; ++i) {
      if (!visible.value(i))
        continue;
      QVector<int> stack{0};
      while (!stack.isEmpty()) {
        const int q = stack.takeLast();
        const Quad &quad = tree.at(q);
        if (quad.mass <= 0.0 || (quad.body == i && quad.mass <= 1.0))
          continue;
        QPointF delta = positions.at(i) - quad.massCenter;
        qreal distance2 = delta.x() * delta.x() + delta.y() * delta.y();
        if (distance2 < 0.05) {
          const uint hash = qHash(i * 2654435761U + q, 0);
          delta = QPointF((hash & 1) ? 0.2 : -0.2, (hash & 2) ? 0.2 : -0.2);
          distance2 = 0.08;
        }
        const qreal distance = std::sqrt(distance2);
        const bool leaf = quad.children[0] < 0;
        if (leaf || (quad.half * 2.0) / distance < 0.72) {
          const qreal strength =
              5200.0 * quad.mass / qMax<qreal>(distance2, 16.0);
          force[i] += delta / distance * strength;
        } else {
          for (int child : quad.children)
            if (child >= 0)
              stack.push_back(child);
        }
      }
      force[i] += positions.at(i) * -0.0035;
    }

    for (const LinkGraphIndex::Edge &edge : graph.edges) {
      if (edge.from < 0 || edge.to < 0 || edge.from == edge.to ||
          edge.from >= count || edge.to >= count || !visible.value(edge.from) ||
          !visible.value(edge.to))
        continue;
      QPointF delta = positions.at(edge.to) - positions.at(edge.from);
      const qreal distance = qMax<qreal>(0.1, std::hypot(delta.x(), delta.y()));
      const qreal rest = 78.0 + 12.0 / std::sqrt(qMax(1, edge.occurrences));
      const qreal strength = (distance - rest) * 0.015;
      const QPointF spring = delta / distance * strength;
      force[edge.from] += spring;
      force[edge.to] -= spring;
    }

    const qreal maxStep = 15.0 - 11.0 * qreal(iteration) / iterations;
    qreal energy = 0.0;
    for (int i = 0; i < count; ++i) {
      if (!visible.value(i))
        continue;
      velocity[i] = (velocity.at(i) + force.at(i) * 0.58) * 0.82;
      qreal speed = std::hypot(velocity.at(i).x(), velocity.at(i).y());
      if (speed > maxStep)
        velocity[i] *= maxStep / speed;
      positions[i] += velocity.at(i);
      energy += speed;
    }

    if ((iteration + 1) % 14 == 0)
      publish(positions, false);
    if (iteration > 70 && energy / visibleCount < 0.025)
      break;
  }
  publish(positions, true);
  return positions;
}

} // namespace

GraphView::GraphView(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("graphCanvas"));
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  grabGesture(Qt::PinchGesture);
}

GraphView::~GraphView() { stopLayout(); }

QString GraphView::nodeKey(int index) const {
  if (index < 0 || index >= m_graph.nodes.size())
    return {};
  const auto &node = m_graph.nodes.at(index);
  return node.path.isEmpty()
             ? QStringLiteral("missing:") + node.title.toCaseFolded()
             : node.path;
}

void GraphView::setSnapshot(const LinkGraphIndex::Snapshot &snapshot) {
  const bool firstCamera = !m_hasFit;
  const quint64 signature = topologySignature(snapshot);
  if (signature == m_topologySignature &&
      snapshot.nodes.size() == m_positions.size()) {
    m_graph = snapshot;
    rebuildVisibleSet();
    if (m_selected >= 0 && m_selected < m_graph.nodes.size()) {
      const auto &node = m_graph.nodes.at(m_selected);
      emit selectionChanged(node.title, node.outgoing, node.incoming,
                            node.unresolved);
    }
    update();
    return;
  }
  QHash<QString, QPointF> oldPositions;
  oldPositions.reserve(m_positions.size());
  for (int i = 0; i < m_positions.size() && i < m_graph.nodes.size(); ++i)
    oldPositions.insert(nodeKey(i), m_positions.at(i));

  const QString selectedKey = nodeKey(m_selected);
  m_graph = snapshot;
  m_topologySignature = signature;
  m_positions.resize(m_graph.nodes.size());
  const qreal spread =
      qMax<qreal>(120.0, std::sqrt(m_graph.nodes.size()) * 38.0);
  for (int i = 0; i < m_graph.nodes.size(); ++i) {
    const QString key = nodeKey(i);
    const auto existing = oldPositions.constFind(key);
    if (existing != oldPositions.constEnd()) {
      m_positions[i] = existing.value();
      continue;
    }
    const uint hx = qHash(key, 0x72f6a9b1U);
    const uint hy = qHash(key, 0xa3c59ac3U);
    m_positions[i] = QPointF((qreal(hx % 20001) / 10000.0 - 1.0) * spread,
                             (qreal(hy % 20001) / 10000.0 - 1.0) * spread);
  }
  m_selected = -1;
  if (!selectedKey.isEmpty())
    for (int i = 0; i < m_graph.nodes.size(); ++i)
      if (nodeKey(i) == selectedKey) {
        m_selected = i;
        break;
      }
  m_hovered = -1;
  rebuildVisibleSet();
  m_fitAfterLayout = firstCamera;
  startLayout();
  if (!m_hasFit)
    fitToView();
  update();
}

void GraphView::clearGraph() {
  stopLayout();
  m_graph = {};
  m_topologySignature = 0;
  m_positions.clear();
  m_visible.clear();
  m_selected = -1;
  m_hovered = -1;
  m_currentPath.clear();
  m_localRoot.clear();
  m_hasFit = false;
  emit visibleGraphChanged(0, 0);
  update();
}

void GraphView::setCurrentPath(const QString &path) {
  m_currentPath = path;
  update();
}

void GraphView::setLocalMode(bool local) {
  if (m_localMode == local)
    return;
  m_localMode = local;
  rebuildVisibleSet();
  m_fitAfterLayout = true;
  startLayout();
  fitToView();
}

void GraphView::setLocalRoot(const QString &path) {
  if (m_localRoot == path)
    return;
  m_localRoot = path;
  if (m_localMode) {
    rebuildVisibleSet();
    m_fitAfterLayout = true;
    startLayout();
    fitToView();
  }
}

void GraphView::setLocalDepth(int depth) {
  depth = qBound(1, depth, 3);
  if (m_localDepth == depth)
    return;
  m_localDepth = depth;
  if (m_localMode) {
    rebuildVisibleSet();
    m_fitAfterLayout = true;
    startLayout();
    fitToView();
  }
}

void GraphView::setShowOrphans(bool show) {
  if (m_showOrphans == show)
    return;
  m_showOrphans = show;
  rebuildVisibleSet();
  m_fitAfterLayout = true;
  startLayout();
  update();
}

void GraphView::setShowUnresolved(bool show) {
  if (m_showUnresolved == show)
    return;
  m_showUnresolved = show;
  rebuildVisibleSet();
  m_fitAfterLayout = true;
  startLayout();
  update();
}

void GraphView::setShowArrows(bool show) {
  m_showArrows = show;
  update();
}

void GraphView::setSearchText(const QString &text) {
  m_searchText = text.trimmed();
  update();
}

void GraphView::setFolderFilter(const QString &folder) {
  if (m_folderFilter == folder)
    return;
  m_folderFilter = folder;
  rebuildVisibleSet();
  m_fitAfterLayout = true;
  startLayout();
  fitToView();
}

void GraphView::setDirection(Direction direction) {
  if (m_direction == direction)
    return;
  m_direction = direction;
  if (m_localMode) {
    rebuildVisibleSet();
    m_fitAfterLayout = true;
    startLayout();
    fitToView();
  }
}

void GraphView::setCamera(const QPointF &center, qreal scale) {
  m_cameraCenter = center;
  m_scale = qBound(kMinScale, scale, kMaxScale);
  m_hasFit = true;
  m_fitAfterLayout = false;
  update();
}

QString GraphView::selectedPath() const {
  return m_selected >= 0 && m_selected < m_graph.nodes.size()
             ? m_graph.nodes.at(m_selected).path
             : QString();
}

void GraphView::selectPath(const QString &path) {
  selectNode(m_graph.nodeByPath.value(path, -1));
}

int GraphView::visibleNodeCount() const {
  return int(std::count(m_visible.cbegin(), m_visible.cend(), true));
}

int GraphView::visibleEdgeCount() const {
  int count = 0;
  for (const auto &edge : m_graph.edges)
    if (edge.from != edge.to && edge.from >= 0 && edge.to >= 0 &&
        edge.from < m_visible.size() && edge.to < m_visible.size() &&
        m_visible.at(edge.from) && m_visible.at(edge.to))
      ++count;
  return count;
}

void GraphView::rebuildVisibleSet() {
  m_visible.fill(false, m_graph.nodes.size());
  if (m_graph.nodes.isEmpty()) {
    emit visibleGraphChanged(0, 0);
    return;
  }

  if (m_localMode) {
    const int root = m_graph.nodeByPath.value(m_localRoot, -1);
    if (root >= 0) {
      QVector<QVector<int>> neighbors(m_graph.nodes.size());
      for (const auto &edge : m_graph.edges) {
        if (edge.from < 0 || edge.to < 0 || edge.from == edge.to ||
            edge.from >= neighbors.size() || edge.to >= neighbors.size())
          continue;
        if (m_direction != Direction::Incoming)
          neighbors[edge.from].push_back(edge.to);
        if (m_direction != Direction::Outgoing)
          neighbors[edge.to].push_back(edge.from);
      }
      QVector<int> distance(m_graph.nodes.size(), -1);
      QQueue<int> queue;
      distance[root] = 0;
      queue.enqueue(root);
      while (!queue.isEmpty()) {
        const int node = queue.dequeue();
        if (distance.at(node) >= m_localDepth)
          continue;
        for (int next : neighbors.at(node)) {
          if (distance.at(next) >= 0)
            continue;
          distance[next] = distance.at(node) + 1;
          queue.enqueue(next);
        }
      }
      for (int i = 0; i < distance.size(); ++i)
        m_visible[i] = distance.at(i) >= 0;
    }
  } else {
    m_visible.fill(true);
  }

  for (int i = 0; i < m_graph.nodes.size(); ++i) {
    const auto &node = m_graph.nodes.at(i);
    if (node.unresolved && !m_showUnresolved)
      m_visible[i] = false;
    if (!m_localMode && !m_showOrphans && !node.unresolved &&
        node.incoming + node.outgoing == 0)
      m_visible[i] = false;
    if (!m_localMode && m_folderFilter != QStringLiteral("*") &&
        (node.unresolved || node.folder != m_folderFilter))
      m_visible[i] = false;
  }
  if (m_selected >= 0 &&
      (m_selected >= m_visible.size() || !m_visible.at(m_selected)))
    selectNode(-1);
  emit visibleGraphChanged(visibleNodeCount(), visibleEdgeCount());
}

QPointF GraphView::worldToScreen(const QPointF &point) const {
  return QPointF(width() * 0.5, height() * 0.5) +
         (point - m_cameraCenter) * m_scale;
}

QPointF GraphView::screenToWorld(const QPointF &point) const {
  return m_cameraCenter +
         (point - QPointF(width() * 0.5, height() * 0.5)) / m_scale;
}

qreal GraphView::nodeRadius(int index) const {
  const auto &node = m_graph.nodes.at(index);
  return 4.2 + qMin<qreal>(5.8, std::log2(node.incoming + node.outgoing + 1.0) *
                                    1.45);
}

int GraphView::nodeAt(const QPointF &screenPoint) const {
  for (int i = m_graph.nodes.size() - 1; i >= 0; --i) {
    if (i >= m_visible.size() || !m_visible.at(i))
      continue;
    const QPointF delta = screenPoint - worldToScreen(m_positions.at(i));
    const qreal hitRadius = qMax<qreal>(8.0, nodeRadius(i) * m_scale + 4.0);
    if (delta.x() * delta.x() + delta.y() * delta.y() <= hitRadius * hitRadius)
      return i;
  }
  return -1;
}

QColor GraphView::nodeColor(int index) const {
  const auto &node = m_graph.nodes.at(index);
  if (node.unresolved)
    return AppTheme::color(QColor(QStringLiteral("#b18172")));
  if (node.folder.isEmpty())
    return AppTheme::color(QColor(QStringLiteral("#2bbf74")));
  const int hue = int(qHash(node.folder.toCaseFolded(), 0) % 360);
  return QColor::fromHsv(hue, 125, 205);
}

void GraphView::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(
      rect(), AppTheme::color(QColor(QStringLiteral("#141619"))));
  if (m_graph.nodes.isEmpty()) {
    painter.setPen(AppTheme::color(QColor(QStringLiteral("#6d8e7c"))));
    painter.drawText(rect(), Qt::AlignCenter, tr("No notes in this vault yet"));
    return;
  }

  const int emphasis = m_hovered >= 0 ? m_hovered : m_selected;
  QVector<bool> adjacent(m_graph.nodes.size(), false);
  if (emphasis >= 0) {
    adjacent[emphasis] = true;
    for (const auto &edge : m_graph.edges) {
      if (edge.from == emphasis && edge.to >= 0 && edge.to < adjacent.size())
        adjacent[edge.to] = true;
      if (edge.to == emphasis && edge.from >= 0 && edge.from < adjacent.size())
        adjacent[edge.from] = true;
    }
  }

  painter.setRenderHint(QPainter::Antialiasing, false);
  const QRectF viewportBounds = QRectF(rect()).adjusted(-30, -30, 30, 30);
  for (const auto &edge : m_graph.edges) {
    if (edge.from < 0 || edge.to < 0 || edge.from == edge.to ||
        edge.from >= m_visible.size() || edge.to >= m_visible.size() ||
        !m_visible.at(edge.from) || !m_visible.at(edge.to))
      continue;
    const bool highlighted =
        emphasis >= 0 && (edge.from == emphasis || edge.to == emphasis);
    QColor color = AppTheme::color(
        highlighted ? QColor(QStringLiteral("#56d995"))
                    : QColor(QStringLiteral("#496558")));
    color.setAlpha(highlighted ? 215 : (emphasis >= 0 ? 42 : 120));
    QPen pen(color);
    pen.setWidthF(
        (highlighted ? 1.6 : 0.95) +
        qMin<qreal>(1.4, std::log2(qMax(1, edge.occurrences)) * 0.35));
    painter.setPen(pen);
    const QPointF from = worldToScreen(m_positions.at(edge.from));
    const QPointF to = worldToScreen(m_positions.at(edge.to));
    if (!viewportBounds.intersects(
            QRectF(from, to).normalized().adjusted(-2, -2, 2, 2)))
      continue;
    painter.drawLine(from, to);
    if (m_showArrows && (to - from).manhattanLength() > 16.0) {
      const QPointF delta = to - from;
      const qreal length = std::hypot(delta.x(), delta.y());
      const QPointF unit = delta / length;
      const QPointF normal(-unit.y(), unit.x());
      const QPointF tip = to - unit * (nodeRadius(edge.to) * m_scale + 2.0);
      QPainterPath arrow;
      arrow.moveTo(tip);
      arrow.lineTo(tip - unit * 7.0 + normal * 3.2);
      arrow.lineTo(tip - unit * 7.0 - normal * 3.2);
      arrow.closeSubpath();
      painter.fillPath(arrow, color);
    }
  }

  painter.setRenderHint(QPainter::Antialiasing, true);
  const QFont baseFont = painter.font();
  const int visibleCount = visibleNodeCount();
  for (int i = 0; i < m_graph.nodes.size(); ++i) {
    if (!m_visible.at(i))
      continue;
    const auto &node = m_graph.nodes.at(i);
    const QPointF point = worldToScreen(m_positions.at(i));
    if (!viewportBounds.contains(point))
      continue;
    const qreal radius = qMax<qreal>(2.2, nodeRadius(i) * std::sqrt(m_scale));
    const bool selected = i == m_selected;
    const bool hovered = i == m_hovered;
    const bool current = !node.path.isEmpty() && node.path == m_currentPath;
    const bool localRoot = m_localMode && node.path == m_localRoot;
    const bool searchMatch =
        !m_searchText.isEmpty() &&
        node.title.contains(m_searchText, Qt::CaseInsensitive);
    QColor color = nodeColor(i);
    if (emphasis >= 0 && !adjacent.at(i))
      color.setAlpha(55);
    else if (!m_searchText.isEmpty() && !searchMatch)
      color.setAlpha(65);

    QPen outline(color.lighter(selected || hovered ? 145 : 110));
    outline.setWidthF(selected || hovered || current || localRoot ? 2.2 : 1.0);
    if (node.unresolved)
      outline.setStyle(Qt::DashLine);
    painter.setPen(outline);
    painter.setBrush(node.unresolved ? Qt::NoBrush : QBrush(color));
    painter.drawEllipse(point, radius, radius);
    if (current || localRoot) {
      QPen ring(AppTheme::color(QColor(QStringLiteral("#d7eee2"))));
      ring.setWidthF(1.3);
      painter.setPen(ring);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(point, radius + 3.4, radius + 3.4);
    }

    const bool label =
        selected || hovered || searchMatch || visibleCount <= 70 ||
        (m_scale >= 0.85 && node.incoming + node.outgoing >= 2) ||
        m_scale >= 1.65;
    if (!label)
      continue;
    QFont font = baseFont;
    font.setPointSizeF(qBound<qreal>(8.0, 9.5 * std::sqrt(m_scale), 12.0));
    font.setBold(selected || hovered || localRoot);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const QString text = metrics.elidedText(node.title, Qt::ElideRight, 190.0);
    QRectF labelRect = metrics.boundingRect(text);
    labelRect.moveCenter(QPointF(
        point.x(), point.y() + radius + labelRect.height() * 0.75 + 3.0));
    if (selected || hovered) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(AppTheme::color(QColor(16, 17, 19, 220)));
      painter.drawRoundedRect(labelRect.adjusted(-5, -2, 5, 2), 4, 4);
    }
    QColor textColor =
        AppTheme::color(QColor(QStringLiteral("#cfe8dc")));
    if (emphasis >= 0 && !adjacent.at(i))
      textColor.setAlpha(75);
    painter.setPen(textColor);
    painter.drawText(labelRect, Qt::AlignCenter, text);
  }
}

void GraphView::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (!m_hasFit && !m_graph.nodes.isEmpty())
    fitToView();
}

void GraphView::mousePressEvent(QMouseEvent *event) {
  setFocus(Qt::MouseFocusReason);
  m_lastPointer = event->position();
  m_pressPointer = event->position();
  m_movedSincePress = false;
  if (event->button() == Qt::LeftButton) {
    m_draggedNode = nodeAt(event->position());
    if (m_draggedNode >= 0)
      stopLayout();
    m_panning = m_draggedNode < 0;
    setCursor(m_panning ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void GraphView::mouseMoveEvent(QMouseEvent *event) {
  const QPointF delta = event->position() - m_lastPointer;
  if ((event->buttons() & Qt::LeftButton) &&
      (m_panning || m_draggedNode >= 0)) {
    if ((event->position() - m_pressPointer).manhattanLength() > 3.0)
      m_movedSincePress = true;
    if (m_panning)
      m_cameraCenter -= delta / m_scale;
    else if (m_draggedNode < m_positions.size())
      m_positions[m_draggedNode] = screenToWorld(event->position());
    m_lastPointer = event->position();
    update();
    event->accept();
    return;
  }

  const int hovered = nodeAt(event->position());
  if (hovered != m_hovered) {
    m_hovered = hovered;
    setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (hovered >= 0)
      setToolTip(m_graph.nodes.at(hovered).title);
    else
      setToolTip(QString());
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void GraphView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    const int releasedNode = m_draggedNode;
    m_draggedNode = -1;
    m_panning = false;
    unsetCursor();
    if (!m_movedSincePress)
      selectNode(releasedNode);
    else if (releasedNode < 0)
      emit cameraChanged();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void GraphView::mouseDoubleClickEvent(QMouseEvent *event) {
  const int node = nodeAt(event->position());
  if (node >= 0 && !m_graph.nodes.at(node).path.isEmpty()) {
    selectNode(node);
    emit noteActivated(m_graph.nodes.at(node).path);
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void GraphView::wheelEvent(QWheelEvent *event) {
  const QPointF before = screenToWorld(event->position());
  const qreal steps = event->angleDelta().y() / 120.0;
  const qreal factor = std::pow(1.16, steps);
  m_scale = qBound(kMinScale, m_scale * factor, kMaxScale);
  const QPointF after = screenToWorld(event->position());
  m_cameraCenter += before - after;
  m_hasFit = true;
  m_fitAfterLayout = false;
  update();
  emit cameraChanged();
  event->accept();
}

void GraphView::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    selectNode(-1);
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (m_selected >= 0 && !m_graph.nodes.at(m_selected).path.isEmpty())
      emit noteActivated(m_graph.nodes.at(m_selected).path);
    return;
  }
  if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier) {
    fitToView();
    return;
  }
  if (event->key() == Qt::Key_0 && event->modifiers() == Qt::NoModifier) {
    resetCamera();
    return;
  }
  if (event->key() == Qt::Key_Slash ||
      (event->key() == Qt::Key_F &&
       event->modifiers().testFlag(Qt::ControlModifier))) {
    emit searchRequested();
    return;
  }
  QWidget::keyPressEvent(event);
}

bool GraphView::event(QEvent *event) {
  if (event->type() == QEvent::Gesture) {
    auto *gestureEvent = static_cast<QGestureEvent *>(event);
    if (auto *pinch = static_cast<QPinchGesture *>(
            gestureEvent->gesture(Qt::PinchGesture))) {
      if (pinch->changeFlags().testFlag(QPinchGesture::ScaleFactorChanged)) {
        const QPointF center = pinch->centerPoint();
        const QPointF before = screenToWorld(center);
        m_scale = qBound(kMinScale, m_scale * pinch->scaleFactor(), kMaxScale);
        m_cameraCenter += before - screenToWorld(center);
        m_hasFit = true;
        m_fitAfterLayout = false;
        update();
        emit cameraChanged();
      }
      gestureEvent->accept(pinch);
      return true;
    }
  }
  return QWidget::event(event);
}

void GraphView::selectNode(int index) {
  if (index < 0 || index >= m_graph.nodes.size() ||
      (index < m_visible.size() && !m_visible.at(index)))
    index = -1;
  if (m_selected == index)
    return;
  m_selected = index;
  if (index < 0) {
    emit selectionChanged({}, 0, 0, false);
  } else {
    const auto &node = m_graph.nodes.at(index);
    emit selectionChanged(node.title, node.outgoing, node.incoming,
                          node.unresolved);
  }
  update();
}

void GraphView::fitToView() {
  bool first = true;
  qreal minX = 0.0;
  qreal maxX = 0.0;
  qreal minY = 0.0;
  qreal maxY = 0.0;
  for (int i = 0; i < m_positions.size(); ++i) {
    if (i >= m_visible.size() || !m_visible.at(i))
      continue;
    const QPointF point = m_positions.at(i);
    if (first) {
      minX = maxX = point.x();
      minY = maxY = point.y();
      first = false;
    } else {
      minX = qMin(minX, point.x());
      maxX = qMax(maxX, point.x());
      minY = qMin(minY, point.y());
      maxY = qMax(maxY, point.y());
    }
  }
  if (first)
    return;
  const QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
  m_cameraCenter = bounds.center();
  const qreal usableWidth = qMax(80, width() - 100);
  const qreal usableHeight = qMax(80, height() - 100);
  const qreal sx = usableWidth / qMax<qreal>(bounds.width(), 80.0);
  const qreal sy = usableHeight / qMax<qreal>(bounds.height(), 80.0);
  m_scale = qBound(kMinScale, qMin(sx, sy), qMin<qreal>(1.45, kMaxScale));
  m_hasFit = true;
  m_fitAfterLayout = true;
  update();
  emit cameraChanged();
}

void GraphView::resetCamera() {
  m_cameraCenter = {};
  m_scale = 1.0;
  m_hasFit = true;
  m_fitAfterLayout = false;
  update();
  emit cameraChanged();
}

void GraphView::stopLayout() {
  ++m_layoutGeneration;
  if (!m_layoutThread)
    return;
  m_layoutThread->requestInterruption();
  m_layoutThread->wait();
  delete m_layoutThread;
  m_layoutThread = nullptr;
}

void GraphView::startLayout() {
  stopLayout();
  if (m_graph.nodes.size() < 2)
    return;
  const int generation = ++m_layoutGeneration;
  const LinkGraphIndex::Snapshot graph = m_graph;
  const QVector<QPointF> initial = m_positions;
  const QVector<bool> visible = m_visible;
  const QPointer<GraphView> guard(this);
  QThread *thread =
      QThread::create([guard, graph, initial, visible, generation] {
        const auto interrupted = [] {
          return QThread::currentThread()->isInterruptionRequested();
        };
        const auto publish =
            [guard, generation](const QVector<QPointF> &positions, bool final) {
              if (!guard)
                return;
              QMetaObject::invokeMethod(
                  guard,
                  [guard, generation, positions, final] {
                    if (guard)
                      guard->applyLayout(generation, positions, final);
                  },
                  Qt::QueuedConnection);
            };
        runLayout(graph, initial, visible, interrupted, publish);
      });
  m_layoutThread = thread;
  connect(thread, &QThread::finished, this, [this, thread] {
    if (m_layoutThread != thread)
      return;
    delete m_layoutThread;
    m_layoutThread = nullptr;
  });
  thread->start();
}

void GraphView::applyLayout(int generation, const QVector<QPointF> &positions,
                            bool final) {
  if (generation != m_layoutGeneration ||
      positions.size() != m_positions.size())
    return;
  m_positions = positions;
  if (final && m_fitAfterLayout) {
    m_fitAfterLayout = false;
    fitToView();
    m_fitAfterLayout = false;
  }
  if (final)
    emit layoutSettled();
  update();
}
