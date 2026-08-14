#pragma once

#include "AppTheme.h"

#include <QDialog>
#include <QHash>

class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class QTextBrowser;

// A focused theme workshop. Draft colors are confined to its representative
// test note until the user explicitly saves the named theme.
class ThemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditorDialog(const QString &basedOnTheme,
                               QWidget *parent = nullptr);

    AppTheme::CustomTheme theme() const { return m_theme; }

private:
    void selectRole(const QString &role);
    void setRoleColor(const QColor &color);
    void updateColorControls();
    void updateSwatches();
    void schedulePreview();
    void applyPreview();
    void validateName();

    AppTheme::CustomTheme m_theme;
    QString m_selectedRole;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_hexEdit = nullptr;
    QLabel *m_selectedColorLabel = nullptr;
    QLabel *m_nameError = nullptr;
    QSlider *m_hueSlider = nullptr;
    QSlider *m_saturationSlider = nullptr;
    QSlider *m_lightnessSlider = nullptr;
    QLabel *m_hueValue = nullptr;
    QLabel *m_saturationValue = nullptr;
    QLabel *m_lightnessValue = nullptr;
    QPushButton *m_saveButton = nullptr;
    QTextBrowser *m_previewNote = nullptr;
    QHash<QString, QPushButton *> m_swatchButtons;
    QTimer *m_previewTimer = nullptr;
    bool m_updatingControls = false;
};
