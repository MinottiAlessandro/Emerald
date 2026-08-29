#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

// A non-modal updater progress surface with a separately readable numeric
// percentage. QProgressDialog draws that text over its bar, where Emerald's
// palette can leave it without enough contrast.
class UpdateProgressDialog final : public QDialog {
public:
    explicit UpdateProgressDialog(const QString &version,
                                  QWidget *parent = nullptr)
        : QDialog(parent) {
        setObjectName(QStringLiteral("updateProgressDialog"));
        setProperty("emeraldDialog", true);
        setWindowTitle(tr("Emerald Update"));
        setWindowModality(Qt::NonModal);
        setModal(false);
        setMinimumWidth(420);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(12);

        auto *message =
            new QLabel(tr("Downloading Emerald v%1…").arg(version), this);
        message->setObjectName(QStringLiteral("updateProgressMessage"));
        root->addWidget(message);

        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        m_bar = new QProgressBar(this);
        m_bar->setObjectName(QStringLiteral("updateProgressBar"));
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        m_bar->setTextVisible(false);
        row->addWidget(m_bar, 1);

        m_percentage = new QLabel(tr("0%"), this);
        m_percentage->setObjectName(
            QStringLiteral("updateProgressPercentage"));
        m_percentage->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_percentage->setMinimumWidth(
            m_percentage->fontMetrics().horizontalAdvance(
                QStringLiteral("100%")));
        row->addWidget(m_percentage);
        root->addLayout(row);

        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) {
            cancel->setAutoDefault(false);
            cancel->setDefault(false);
            cancel->setProperty("dialogRole", QStringLiteral("secondary"));
        }
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

    void setPercentage(int percentage) {
        const int bounded = qBound(0, percentage, 100);
        m_bar->setValue(bounded);
        m_percentage->setText(tr("%1%").arg(bounded));
    }

private:
    QProgressBar *m_bar = nullptr;
    QLabel *m_percentage = nullptr;
};
