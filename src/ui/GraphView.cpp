#include "GraphView.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <numbers>

namespace {
const QColor kEdge("#2a2e42");
const QColor kResolved("#7aa2f7");
const QColor kUnresolved("#3b4261");
const QColor kCurrent("#bb9af7");
const QColor kLabel("#a9b1d6");
const QColor kLabelCurrent("#c0caf5");
} // namespace

GraphView::GraphView(QWidget *parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    // GPU-accelerated canvas: the scene is composited by OpenGL, so the
    // layout animation and pan/zoom stay smooth even with many nodes.
    setViewport(new QOpenGLWidget);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor("#16161e"));
    setFrameStyle(QFrame::NoFrame);

    m_timer = new QTimer(this);
    m_timer->setInterval(16); // ~60 fps
    connect(m_timer, &QTimer::timeout, this, [this] {
        stepLayout();
        applyPositions();
        if (++m_iter > 600 || m_temp < 0.05) {
            m_timer->stop();
            if (!m_fitted) {
                fitInView(m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40),
                          Qt::KeepAspectRatio);
                m_fitted = true;
            }
        }
    });
}

void GraphView::setGraph(const QList<QPair<QString, bool>> &nodes,
                         const QList<QPair<QString, QString>> &edges,
                         const QString &current) {
    m_timer->stop();
    m_scene->clear(); // deletes all items
    m_nodes.clear();
    m_idx.clear();
    m_edges.clear();
    m_edgeItems.clear();
    m_current = current;
    m_iter = 0;
    m_fitted = false;

    const int n = nodes.size();
    for (int i = 0; i < n; ++i) {
        Node node;
        node.title = nodes[i].first;
        node.resolved = nodes[i].second;
        const double angle = (2.0 * std::numbers::pi * i) / std::max(1, n);
        const double radius = 30.0 * std::sqrt(double(n)) + 40.0;
        node.pos = QPointF(radius * std::cos(angle), radius * std::sin(angle));
        m_idx.insert(node.title.toLower(), i);
        m_nodes.push_back(node);
    }

    for (const auto &e : edges) {
        const int a = m_idx.value(e.first.toLower(), -1);
        const int b = m_idx.value(e.second.toLower(), -1);
        if (a < 0 || b < 0 || a == b)
            continue;
        m_edges.push_back({a, b});
        m_nodes[a].degree++;
        m_nodes[b].degree++;
    }

    for (int i = 0; i < m_edges.size(); ++i) {
        auto *line = m_scene->addLine(QLineF(), QPen(kEdge, 1.2));
        line->setZValue(-1);
        m_edgeItems.push_back(line);
    }

    for (int i = 0; i < n; ++i) {
        const double r = 5.0 + std::min(9, m_nodes[i].degree * 2);
        auto *dot = m_scene->addEllipse(-r, -r, 2 * r, 2 * r, QPen(Qt::NoPen),
                                        QBrush(kResolved));
        dot->setZValue(1);
        dot->setData(0, i);
        m_nodes[i].dot = dot;

        auto *label = m_scene->addSimpleText(m_nodes[i].title);
        QFont font = label->font();
        font.setPointSizeF(8.0);
        label->setFont(font);
        label->setBrush(kLabel);
        label->setZValue(2);
        label->setData(0, i);
        m_nodes[i].label = label;

        colorNode(i);
    }

    m_temp = 10.0 * std::sqrt(double(std::max(1, n)));
    applyPositions();
    if (n > 0)
        m_timer->start();
}

void GraphView::stepLayout() {
    const int n = m_nodes.size();
    if (n == 0)
        return;

    const double area = 280000.0;
    const double k = std::sqrt(area / n);

    for (Node &node : m_nodes)
        node.disp = QPointF(0, 0);

    // Repulsion between every pair of nodes (O(n^2) — fine for a vault).
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            QPointF delta = m_nodes[i].pos - m_nodes[j].pos;
            double dist = std::hypot(delta.x(), delta.y());
            if (dist < 0.01) {
                delta = QPointF(0.01, 0.0);
                dist = 0.01;
            }
            const QPointF dir = delta / dist;
            const double force = (k * k) / dist;
            m_nodes[i].disp += dir * force;
            m_nodes[j].disp -= dir * force;
        }
    }

    // Attraction along edges.
    for (const auto &e : m_edges) {
        QPointF delta = m_nodes[e.first].pos - m_nodes[e.second].pos;
        double dist = std::hypot(delta.x(), delta.y());
        if (dist < 0.01)
            dist = 0.01;
        const QPointF dir = delta / dist;
        const double force = (dist * dist) / k;
        m_nodes[e.first].disp -= dir * force;
        m_nodes[e.second].disp += dir * force;
    }

    // Move each node, capped by the cooling temperature, then recenter.
    QPointF center(0, 0);
    for (Node &node : m_nodes) {
        const double len = std::hypot(node.disp.x(), node.disp.y());
        if (len > 0)
            node.pos += (node.disp / len) * std::min(len, m_temp);
        center += node.pos;
    }
    center /= n;
    for (Node &node : m_nodes)
        node.pos -= center;

    m_temp *= 0.97;
}

void GraphView::applyPositions() {
    for (const Node &node : m_nodes) {
        node.dot->setPos(node.pos);
        const QRectF box = node.label->boundingRect();
        node.label->setPos(node.pos.x() - box.width() / 2.0, node.pos.y() + 7.0);
    }
    for (int i = 0; i < m_edges.size(); ++i) {
        m_edgeItems[i]->setLine(
            QLineF(m_nodes[m_edges[i].first].pos, m_nodes[m_edges[i].second].pos));
    }
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
}

void GraphView::colorNode(int i) {
    const Node &node = m_nodes[i];
    const bool current = node.title.compare(m_current, Qt::CaseInsensitive) == 0;
    QColor fill = node.resolved ? kResolved : kUnresolved;
    if (current)
        fill = kCurrent;
    node.dot->setBrush(fill);
    node.dot->setPen(current ? QPen(kLabelCurrent, 2) : QPen(Qt::NoPen));
    node.label->setBrush(current ? kLabelCurrent : kLabel);
}

void GraphView::setCurrent(const QString &title) {
    m_current = title;
    for (int i = 0; i < m_nodes.size(); ++i)
        colorNode(i);
}

void GraphView::wheelEvent(QWheelEvent *event) {
    const double factor = std::pow(1.0015, event->angleDelta().y());
    scale(factor, factor);
}

void GraphView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (QGraphicsItem *item = itemAt(event->pos())) {
        bool ok = false;
        const int idx = item->data(0).toInt(&ok);
        if (ok && idx >= 0 && idx < m_nodes.size()) {
            emit noteActivated(m_nodes[idx].title);
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}
