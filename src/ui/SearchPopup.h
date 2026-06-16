#pragma once

#include <QFrame>

class QLineEdit;
class QListWidget;
class SearchIndex;

// A Telescope-style centred search overlay: a text field over a live result
// list, floating in the upper-middle of the window. Type to filter, Up/Down to
// move, Enter to open, Esc to dismiss. It reads the shared SearchIndex and asks
// the window to open the chosen note via openRequested().
class SearchPopup : public QFrame {
    Q_OBJECT
public:
    SearchPopup(const SearchIndex *index, QWidget *parent);

    void showCentered();

signals:
    void openRequested(const QString &path, const QString &query);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void refresh(const QString &text);
    void accept();      // open the highlighted result
    void reposition();  // keep centred over the parent

    const SearchIndex *m_index = nullptr;
    QLineEdit *m_input = nullptr;
    QListWidget *m_results = nullptr;
};
