#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

class QGraphicsScene;
class QGraphicsEllipseItem;
class QGraphicsSimpleTextItem;
class QGraphicsLineItem;
class QTimer;

// The vault as a force-directed graph: notes are nodes, [[links]] are edges.
// Rendered through an OpenGL viewport so panning, zooming and the layout
// animation are handled by the GPU. Double-click a node to open the note.
class GraphView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphView(QWidget *parent = nullptr);

    // (title, resolved) for each node; (source, target) for each edge.
    void setGraph(const QList<QPair<QString, bool>> &nodes,
                  const QList<QPair<QString, QString>> &edges,
                  const QString &current);
    // Re-highlight the node for `title` without re-running the layout.
    void setCurrent(const QString &title);

signals:
    void noteActivated(const QString &title);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void stepLayout();
    void applyPositions();
    void colorNode(int i);

    struct Node {
        QString title;
        bool resolved = true;
        int degree = 0;
        QPointF pos;
        QPointF disp;
        QGraphicsEllipseItem *dot = nullptr;
        QGraphicsSimpleTextItem *label = nullptr;
    };

    QGraphicsScene *m_scene = nullptr;
    QTimer *m_timer = nullptr;
    QVector<Node> m_nodes;
    QHash<QString, int> m_idx; // lower title -> node index
    QVector<QPair<int, int>> m_edges;
    QVector<QGraphicsLineItem *> m_edgeItems;
    QString m_current;
    double m_temp = 0.0;
    int m_iter = 0;
    bool m_fitted = false;
};
