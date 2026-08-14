#include "ThemeEditorDialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QIcon colorIcon(const QColor &color) {
    QPixmap swatch(28, 20);
    swatch.fill(color);
    return QIcon(swatch);
}

QString previewHtml(const AppTheme::CustomTheme &theme) {
    auto role = [&theme](const char *key) {
        const QColor color =
            theme.colors.value(QString::fromLatin1(key), QColor(Qt::black));
        return color.name(QColor::HexRgb);
    };
    QString html = QStringLiteral(R"HTML(
<!doctype html><html><head><style>
body { background: {{background}}; color: {{text}}; font-family: sans-serif;
       font-size: 14px; line-height: 1.42; margin: 22px; }
h1 { color: {{text}}; font-size: 27px; margin: 0 0 14px 0; }
h2 { color: {{text}}; font-size: 21px; margin: 18px 0 8px 0; }
p { margin: 7px 0; }
a, .accent { color: {{accent}}; text-decoration: underline; }
.muted { color: {{muted}}; }
.selection { background: {{selection}}; color: {{text}}; padding: 1px 4px; }
code { background: {{field}}; color: {{code}}; border: 1px solid {{border}};
       padding: 1px 4px; }
pre { background: {{sidebar}}; color: {{code}}; border: 1px solid {{border}};
      padding: 10px; white-space: pre-wrap; }
blockquote { color: {{quote}}; border-left: 3px solid {{accent}};
             margin: 12px 0; padding: 2px 10px; }
.callout { background: {{surface}}; border: 1px solid {{border}};
           padding: 8px 10px; margin: 10px 0; }
.callout-title { color: {{accent}}; font-weight: bold; }
.warning { color: {{warning}}; font-weight: bold; }
.error { color: {{error}}; font-weight: bold; }
.done { color: {{muted}}; text-decoration: line-through; }
table { border-collapse: collapse; margin: 12px 0; }
th, td { border: 1px solid {{border}}; padding: 5px 9px; }
th { background: {{surface}}; color: {{text}}; }
hr { border: 0; border-top: 1px solid {{border}}; margin: 16px 0; }
.chrome { background: {{surface}}; border: 1px solid {{border}};
          padding: 8px; margin-bottom: 14px; }
.sidebar { background: {{sidebar}}; color: {{muted}}; padding: 6px 9px; }
.field { background: {{field}}; color: {{text}}; border: 1px solid {{border}};
         padding: 5px 9px; }
</style></head><body>
<h1>Theme Preview</h1>
<div class="chrome"><span class="sidebar">Sidebar</span>&nbsp;
<span class="field">Search field</span>&nbsp;
<span class="accent">Active action</span></div>
<h2>A complete test note</h2>
<p>This paragraph has <b>bold text</b>, <i>italic text</i>,
<span class="selection">highlighted text</span>, an
<a href="https://example.com">external link</a>, and a
<span class="accent">Wiki link</span>.</p>
<blockquote>Quotes show the secondary reading color and accent border.</blockquote>
<div class="callout"><div class="callout-title">&#x1F4DD; Callout title</div>
Panels, borders, main text, and accent colors appear together here.</div>
<div class="callout"><span class="warning">&#x26A0; Warning callout</span><br>
Attention colors are visible here.</div>
<div class="callout"><span class="error">&#x26D4; Error callout</span><br>
Error colors are visible here.</div>
<p>&#x2610; An open task<br><span class="done">&#x2611; A completed task</span><br>
&#x2022; A normal list item</p>
<p>Use <code>inline code</code> inside a sentence.</p>
<pre>int main() {
    return 0;
}</pre>
<table><tr><th>Element</th><th>Example</th></tr>
<tr><td>Table</td><td class="muted">Muted value</td></tr></table>
<hr><p class="muted">Muted metadata and secondary information.</p>
</body></html>)HTML");
    static const char *roleKeys[] = {
        "background", "sidebar", "surface", "field",   "border",
        "text",       "muted",   "accent",  "selection", "code",
        "quote",      "warning", "error"};
    for (const char *key : roleKeys)
        html.replace(QStringLiteral("{{") + QString::fromLatin1(key) +
                         QStringLiteral("}}"),
                     role(key));
    return html;
}
} // namespace

ThemeEditorDialog::ThemeEditorDialog(const QString &basedOnTheme,
                                     QWidget *parent)
    : QDialog(parent),
      m_theme(AppTheme::makeCustomTheme(tr("My theme"), basedOnTheme)) {
    setObjectName(QStringLiteral("themeEditorDialog"));
    setProperty("emeraldDialog", true);
    setWindowTitle(tr("Create your theme"));
    setMinimumSize(760, 580);
    resize(980, 700);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto *title = new QLabel(tr("Create your theme"), this);
    title->setObjectName(QStringLiteral("settingsTitle"));
    auto *subtitle = new QLabel(
        tr("Tune each color while only the test note updates live. Your theme "
           "is stored locally only when you save it."),
        this);
    subtitle->setObjectName(QStringLiteral("settingsSubtitle"));
    subtitle->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(subtitle);

    auto *nameRow = new QWidget(this);
    auto *nameLayout = new QHBoxLayout(nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(9);
    auto *nameLabel = new QLabel(tr("Theme name"), nameRow);
    nameLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    m_nameEdit = new QLineEdit(m_theme.name, nameRow);
    m_nameEdit->setObjectName(QStringLiteral("customThemeName"));
    m_nameEdit->selectAll();
    m_nameError = new QLabel(nameRow);
    m_nameError->setObjectName(QStringLiteral("themeNameError"));
    m_nameError->setWordWrap(true);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameEdit, 1);
    nameLayout->addWidget(m_nameError);
    root->addWidget(nameRow);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("themeEditorSplitter"));

    auto *controlsPane = new QWidget(splitter);
    auto *controlsPaneLayout = new QVBoxLayout(controlsPane);
    controlsPaneLayout->setContentsMargins(0, 0, 8, 0);
    controlsPaneLayout->setSpacing(8);

    auto *colorsTitle = new QLabel(tr("Theme colors"), controlsPane);
    colorsTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
    auto *colorsHelp = new QLabel(
        tr("Choose an element, then use the sliders or enter a hex color."),
        controlsPane);
    colorsHelp->setObjectName(QStringLiteral("settingsSectionDescription"));
    colorsHelp->setWordWrap(true);
    controlsPaneLayout->addWidget(colorsTitle);
    controlsPaneLayout->addWidget(colorsHelp);

    auto *roleScroll = new QScrollArea(controlsPane);
    roleScroll->setObjectName(QStringLiteral("themeColorRoleScroll"));
    roleScroll->setWidgetResizable(true);
    roleScroll->setFrameShape(QFrame::NoFrame);
    auto *roleContent = new QWidget(roleScroll);
    auto *roleLayout = new QVBoxLayout(roleContent);
    roleLayout->setContentsMargins(0, 0, 3, 0);
    roleLayout->setSpacing(7);

    for (const AppTheme::ColorRole &role : AppTheme::colorRoles()) {
        auto *button = new QPushButton(role.label, roleContent);
        button->setObjectName(QStringLiteral("themeColor_") + role.key);
        button->setProperty("themeColorRole", true);
        button->setIcon(colorIcon(m_theme.colors.value(role.key)));
        button->setIconSize(QSize(28, 20));
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(button, &QPushButton::clicked, this,
                [this, role] { selectRole(role.key); });
        m_swatchButtons.insert(role.key, button);
        roleLayout->addWidget(button);
    }
    roleLayout->addStretch();
    roleScroll->setWidget(roleContent);
    controlsPaneLayout->addWidget(roleScroll, 1);

    auto *fineTune = new QFrame(controlsPane);
    fineTune->setObjectName(QStringLiteral("settingsSection"));
    auto *fineLayout = new QVBoxLayout(fineTune);
    fineLayout->setContentsMargins(12, 11, 12, 11);
    fineLayout->setSpacing(8);
    m_selectedColorLabel = new QLabel(fineTune);
    m_selectedColorLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
    fineLayout->addWidget(m_selectedColorLabel);

    auto addSlider = [fineTune, fineLayout](const QString &labelText, int maximum,
                                            const QString &objectName,
                                            QSlider **slider, QLabel **value) {
        auto *row = new QWidget(fineTune);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *label = new QLabel(labelText, row);
        label->setObjectName(QStringLiteral("settingsFieldLabel"));
        label->setMinimumWidth(68);
        *slider = new QSlider(Qt::Horizontal, row);
        (*slider)->setObjectName(objectName);
        (*slider)->setRange(0, maximum);
        *value = new QLabel(row);
        (*value)->setMinimumWidth(34);
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(label);
        layout->addWidget(*slider, 1);
        layout->addWidget(*value);
        fineLayout->addWidget(row);
    };
    addSlider(tr("Hue"), 359, QStringLiteral("themeHue"), &m_hueSlider,
              &m_hueValue);
    addSlider(tr("Saturation"), 100, QStringLiteral("themeSaturation"),
              &m_saturationSlider, &m_saturationValue);
    addSlider(tr("Lightness"), 100, QStringLiteral("themeLightness"),
              &m_lightnessSlider, &m_lightnessValue);

    auto *directRow = new QWidget(fineTune);
    auto *directLayout = new QHBoxLayout(directRow);
    directLayout->setContentsMargins(0, 0, 0, 0);
    directLayout->setSpacing(8);
    m_hexEdit = new QLineEdit(directRow);
    m_hexEdit->setObjectName(QStringLiteral("themeHexColor"));
    m_hexEdit->setMaxLength(7);
    auto *pickButton = new QPushButton(tr("Pick…"), directRow);
    pickButton->setObjectName(QStringLiteral("themePickColor"));
    directLayout->addWidget(m_hexEdit, 1);
    directLayout->addWidget(pickButton);
    fineLayout->addWidget(directRow);
    controlsPaneLayout->addWidget(fineTune);

    auto *previewPane = new QWidget(splitter);
    auto *previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(8, 0, 0, 0);
    previewLayout->setSpacing(8);
    auto *previewHeader = new QWidget(previewPane);
    auto *previewHeaderLayout = new QHBoxLayout(previewHeader);
    previewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    auto *previewLabel = new QLabel(tr("Test note"), previewHeader);
    previewLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
    previewHeaderLayout->addWidget(previewLabel);
    previewHeaderLayout->addStretch();
    previewLayout->addWidget(previewHeader);
    m_previewNote = new QTextBrowser(previewPane);
    m_previewNote->setObjectName(QStringLiteral("themePreviewNote"));
    m_previewNote->setOpenExternalLinks(false);
    m_previewNote->setFocusPolicy(Qt::NoFocus);
    previewLayout->addWidget(m_previewNote, 1);

    splitter->addWidget(controlsPane);
    splitter->addWidget(previewPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 620});
    root->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                             QDialogButtonBox::Cancel,
                                         this);
    buttons->setObjectName(QStringLiteral("themeEditorButtons"));
    m_saveButton = buttons->button(QDialogButtonBox::Save);
    if (m_saveButton) {
        m_saveButton->setText(tr("Save theme"));
        m_saveButton->setObjectName(QStringLiteral("saveCustomTheme"));
        m_saveButton->setProperty("dialogRole", QStringLiteral("primary"));
        m_saveButton->setAutoDefault(false);
        m_saveButton->setDefault(false);
    }
    if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setProperty("dialogRole", QStringLiteral("secondary"));
        cancel->setAutoDefault(false);
        cancel->setDefault(false);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(20);
    connect(m_previewTimer, &QTimer::timeout, this,
            &ThemeEditorDialog::applyPreview);

    connect(m_nameEdit, &QLineEdit::textChanged, this,
            [this](const QString &name) {
                m_theme.name = name.trimmed();
                validateName();
            });
    auto sliderChanged = [this] {
        if (m_updatingControls)
            return;
        setRoleColor(QColor::fromHsl(
            m_hueSlider->value(), m_saturationSlider->value() * 255 / 100,
            m_lightnessSlider->value() * 255 / 100));
    };
    connect(m_hueSlider, &QSlider::valueChanged, this, sliderChanged);
    connect(m_saturationSlider, &QSlider::valueChanged, this, sliderChanged);
    connect(m_lightnessSlider, &QSlider::valueChanged, this, sliderChanged);
    connect(m_hexEdit, &QLineEdit::editingFinished, this, [this] {
        const QColor color(m_hexEdit->text().trimmed());
        if (color.isValid())
            setRoleColor(color);
        else
            updateColorControls();
    });
    connect(pickButton, &QPushButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(
            m_theme.colors.value(m_selectedRole), this, tr("Choose color"));
        if (selected.isValid())
            setRoleColor(selected);
    });
    selectRole(AppTheme::colorRoles().first().key);
    validateName();
    applyPreview();
}

void ThemeEditorDialog::selectRole(const QString &role) {
    m_selectedRole = role;
    for (auto it = m_swatchButtons.begin(); it != m_swatchButtons.end(); ++it)
        it.value()->setChecked(it.key() == role);
    updateColorControls();
}

void ThemeEditorDialog::setRoleColor(const QColor &color) {
    if (!color.isValid() || m_selectedRole.isEmpty())
        return;
    m_theme.colors.insert(m_selectedRole, color);
    updateColorControls();
    updateSwatches();
    schedulePreview();
}

void ThemeEditorDialog::updateColorControls() {
    const QColor color = m_theme.colors.value(m_selectedRole);
    if (!color.isValid())
        return;
    QString label = m_selectedRole;
    for (const AppTheme::ColorRole &role : AppTheme::colorRoles())
        if (role.key == m_selectedRole) {
            label = role.label;
            break;
        }
    m_selectedColorLabel->setText(label);

    int hue = 0;
    int saturation = 0;
    int lightness = 0;
    color.getHsl(&hue, &saturation, &lightness);
    if (hue < 0)
        hue = m_hueSlider->value();
    m_updatingControls = true;
    m_hueSlider->setValue(hue);
    m_saturationSlider->setValue(qRound(saturation * 100.0 / 255.0));
    m_lightnessSlider->setValue(qRound(lightness * 100.0 / 255.0));
    m_hueValue->setText(QString::number(m_hueSlider->value()) +
                        QChar(0x00b0));
    m_saturationValue->setText(QString::number(m_saturationSlider->value()) +
                               QLatin1Char('%'));
    m_lightnessValue->setText(QString::number(m_lightnessSlider->value()) +
                              QLatin1Char('%'));
    {
        const QSignalBlocker blocker(m_hexEdit);
        m_hexEdit->setText(color.name(QColor::HexRgb));
    }
    m_updatingControls = false;
}

void ThemeEditorDialog::updateSwatches() {
    for (auto it = m_swatchButtons.begin(); it != m_swatchButtons.end(); ++it)
        it.value()->setIcon(colorIcon(m_theme.colors.value(it.key())));
}

void ThemeEditorDialog::schedulePreview() { m_previewTimer->start(); }

void ThemeEditorDialog::applyPreview() {
    const QColor background =
        m_theme.colors.value(QStringLiteral("background"));
    const QColor text = m_theme.colors.value(QStringLiteral("text"));
    const QColor border = m_theme.colors.value(QStringLiteral("border"));
    QPalette previewPalette = m_previewNote->palette();
    previewPalette.setColor(QPalette::Base, background);
    previewPalette.setColor(QPalette::Text, text);
    m_previewNote->setPalette(previewPalette);
    m_previewNote->setStyleSheet(
        QStringLiteral("QTextBrowser#themePreviewNote { background: %1; color: "
                       "%2; border: 1px solid %3; }")
            .arg(background.name(QColor::HexRgb), text.name(QColor::HexRgb),
                 border.name(QColor::HexRgb)));
    m_previewNote->setHtml(previewHtml(m_theme));
    updateSwatches();
}

void ThemeEditorDialog::validateName() {
    const QString name = m_nameEdit->text().trimmed();
    bool duplicate = false;
    for (const AppTheme::CustomTheme &theme : AppTheme::customThemes())
        if (theme.key != m_theme.key &&
            theme.name.compare(name, Qt::CaseInsensitive) == 0) {
            duplicate = true;
            break;
        }
    if (name.isEmpty())
        m_nameError->setText(tr("Enter a name"));
    else if (duplicate)
        m_nameError->setText(tr("Name already used"));
    else
        m_nameError->clear();
    if (m_saveButton)
        m_saveButton->setEnabled(!name.isEmpty() && !duplicate);
}
