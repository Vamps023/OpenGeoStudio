// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include <QtWidgets>
#include <QStackedLayout>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QLineEdit>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <cmath>

#include <CGAL/exceptions.h>

#include "main_widget.h"

// ============================================================
// LmStyleServer — minimal HTTP server for MapLibre style JSON
// (same implementation as MapViewportWidget)
// ============================================================
LmStyleServer::LmStyleServer(const QByteArray& styleJson, QObject* parent)
    : QObject(parent), m_styleJson(styleJson)
{
    if (m_server.listen(QHostAddress::LocalHost)) {
        m_port = m_server.serverPort();
        connect(&m_server, &QTcpServer::newConnection, this, &LmStyleServer::onNewConnection);
    }
}

QString LmStyleServer::styleUrl() const {
    return QString("http://127.0.0.1:%1/style.json").arg(m_port);
}

void LmStyleServer::onNewConnection() {
    while (auto* sock = m_server.nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, &LmStyleServer::onReadyRead);
    }
}

void LmStyleServer::onReadyRead() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    sock->readAll();
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Content-Length: " + QByteArray::number(m_styleJson.size()) + "\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(m_styleJson);
    sock->write(response);
    sock->disconnectFromHost();
}

#include "main_widget.h"
#include "map_view_gl.h"
#include "main_window.h"
#include "action_manager.h"
#include "road_drawing.h"
#include "change_tracker.h"
#include "LaneConfigWidget.h"
#include "DrawOptionDialog.h"
#include "RoadTypes.hpp"
#include "sign_system.h"
#include "marking_graphics.h"
#include "cross_section_extender.h"
#include "junction.h"
#include "Geometries/Line.h"
#include <QComboBox>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QShortcut>
#include <functional>

// Helper: find a road as shared_ptr from the World by road ID
static std::shared_ptr<LM::Road> findRoadShared(const std::string& roadID)
{
    auto* world = World::Instance();
    if (!world) return nullptr;
    for (auto& road : world->allRoads)
    {
        if (road && road->ID() == roadID)
            return road;
    }
    return nullptr;
}

// ============================================================
// Persistent custom graphics management
// ============================================================
// These methods manage the lifetime of MarkingGraphics, SignGraphics,
// and FurnitureGraphics objects. They MUST be stored as unique_ptr
// in maps — if created as temporaries, the destructor calls Clear()
// which removes all geometry from the viewport immediately.
// ============================================================

void MainWidget::refreshCustomGraphics(const std::string& roadID)
{
    auto road = findRoadShared(roadID);
    if (!road) return;

    // Destroy old graphics (Clear() removes geometry from viewport)
    m_markingGraphics.erase(roadID);
    m_signGraphics.erase(roadID);
    m_furnitureGraphics.erase(roadID);

    // Create new graphics (constructor adds geometry to viewport)
    // The objects persist in the maps until explicitly destroyed.
    m_markingGraphics[roadID] = std::make_unique<LM::MarkingGraphics>(road);
    m_signGraphics[roadID] = std::make_unique<LM::SignGraphics>(road);
    m_furnitureGraphics[roadID] = std::make_unique<LM::FurnitureGraphics>(road);
}

void MainWidget::refreshAllCustomGraphics()
{
    auto* world = World::Instance();
    if (!world) return;

    // Remove graphics for roads that no longer exist
    std::set<std::string> activeRoadIDs;
    for (auto& road : world->allRoads)
    {
        if (road) activeRoadIDs.insert(road->ID());
    }

    for (auto it = m_markingGraphics.begin(); it != m_markingGraphics.end();)
    {
        if (activeRoadIDs.find(it->first) == activeRoadIDs.end())
            it = m_markingGraphics.erase(it);
        else
            ++it;
    }
    for (auto it = m_signGraphics.begin(); it != m_signGraphics.end();)
    {
        if (activeRoadIDs.find(it->first) == activeRoadIDs.end())
            it = m_signGraphics.erase(it);
        else
            ++it;
    }
    for (auto it = m_furnitureGraphics.begin(); it != m_furnitureGraphics.end();)
    {
        if (activeRoadIDs.find(it->first) == activeRoadIDs.end())
            it = m_furnitureGraphics.erase(it);
        else
            ++it;
    }

    // Re-render all active roads
    for (auto& road : world->allRoads)
    {
        if (!road) continue;
        const auto& roadID = road->ID();
        m_markingGraphics.erase(roadID);
        m_signGraphics.erase(roadID);
        m_furnitureGraphics.erase(roadID);
        m_markingGraphics[roadID] = std::make_unique<LM::MarkingGraphics>(road);
        m_signGraphics[roadID] = std::make_unique<LM::SignGraphics>(road);
        m_furnitureGraphics[roadID] = std::make_unique<LM::FurnitureGraphics>(road);
    }
}

void MainWidget::clearCustomGraphics(const std::string& roadID)
{
    m_markingGraphics.erase(roadID);
    m_signGraphics.erase(roadID);
    m_furnitureGraphics.erase(roadID);
}

void MainWidget::clearAllCustomGraphics()
{
    m_markingGraphics.clear();
    m_signGraphics.clear();
    m_furnitureGraphics.clear();
}

// ============================================================
// CollapsibleSection — right-panel section with a toggle header.
// Content is shown/hidden by clicking the header; no Q_OBJECT
// needed (no custom signals).
// ============================================================
class CollapsibleSection : public QWidget {
public:
    explicit CollapsibleSection(const QString& title, bool expanded = true,
                                QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        headerButton = new QToolButton(this);
        headerButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        headerButton->setText(title);
        headerButton->setCursor(Qt::PointingHandCursor);
        headerButton->setStyleSheet(
            "QToolButton { background: transparent; border: none; font-size: 11px;"
            " font-weight: bold; color: #e6edf3; padding: 4px 2px; text-align: left; }"
            "QToolButton:hover { color: #79c0ff; }");

        m_content = new QWidget(this);
        m_contentLayout = new QVBoxLayout(m_content);
        m_contentLayout->setContentsMargins(2, 0, 2, 0);
        m_contentLayout->setSpacing(4);

        layout->addWidget(headerButton);
        layout->addWidget(m_content);
        setExpanded(expanded);
        connect(headerButton, &QAbstractButton::clicked, this,
                [this]() { setExpanded(!m_expanded); });
    }

    QVBoxLayout* content() { return m_contentLayout; }
    bool isExpanded() const { return m_expanded; }

    void setExpanded(bool on)
    {
        m_expanded = on;
        m_content->setVisible(on);
        headerButton->setIcon(style()->standardIcon(
            on ? QStyle::SP_ArrowDown : QStyle::SP_ArrowRight));
    }

private:
    QToolButton* headerButton = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    bool m_expanded = true;
};

MainWidget* MainWidget::instance = nullptr;

MainWidget::MainWidget(QWidget* parent)
    : QFrame(parent), laneConfig(new LaneConfigWidget),
    drawOptionDialog(new DrawOptionDialog(g_mainWindow))
{
    instance = this;
    setFrameStyle(NoFrame | Plain);

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(16);
    format.setAlphaBufferSize(8); // needed for transparent background in 2D mode

    mapViewGL = new LM::MapViewGL;
    mapViewGL->setFormat(format);
    mapViewGL->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mapViewGL->setFocusPolicy(Qt::ClickFocus);
    mapViewGL->setMouseTracking(true);

    int size = style()->pixelMetric(QStyle::PM_ToolBarIconSize);
    QSize iconSize(size, size);

    // Helper: load SVG icon from our road_studio resource prefix
    auto loadSvgIcon = [](const QString& name) -> QIcon {
        return QIcon(QString(":/rs/svg/%1.svg").arg(name));
    };

    // ============================================================
    // Left tool palette — grouped CREATE / EDIT / ANNOTATE
    // Text-under-icon labels + keyboard shortcuts so every tool is
    // self-explanatory; utility tools (snap) stay at the bottom.
    // ============================================================
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("roadSidebar");
    sidebar->setFixedWidth(84);
    sidebar->setStyleSheet(
        "QWidget#roadSidebar { background-color: #0d1117; border-right: 1px solid #21262d; }"
        "QToolButton { background: transparent; border: none; border-radius: 8px; "
        "  padding: 4px 2px; margin: 1px 2px; font-size: 10px; color: #e6edf3; }"
        "QToolButton:hover { background-color: #21262d; }"
        "QToolButton:checked { background-color: #1f6feb33; border: 1px solid #1f6feb; "
        "  border-radius: 8px; color: #79c0ff; }"
        "QLabel#sidebarCaption { color: #6e7681; font-size: 8px; font-weight: bold; "
        "  letter-spacing: 1px; padding: 6px 4px 0 4px; }");
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(3, 8, 3, 8);
    sidebarLayout->setSpacing(1);
    sidebarLayout->setAlignment(Qt::AlignTop);

    QSize paletteIconSize = iconSize * 1.4;

    // Group caption + separator helper
    auto addCaption = [this, sidebarLayout](const char* text) {
        auto* caption = new QLabel(QString(text).toUpper(), this);
        caption->setObjectName("sidebarCaption");
        sidebarLayout->addWidget(caption);
    };

    // Mode button factory: icon + label + tooltip (shortcuts are wired
    // separately below so they never hijack text input or camera keys)
    auto makeModeButton = [&](QToolButton*& btn, const QString& iconName,
                              const QString& label, const QString& tip) {
        btn = new QToolButton;
        btn->setIcon(loadSvgIcon(iconName));
        btn->setIconSize(paletteIconSize);
        btn->setText(label);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setChecked(false);
    };

    // ── CREATE ──
    addCaption("Create");
    makeModeButton(createModeButton, "road_mode", tr("Road"),
                   tr("Draw new roads"));

    // ── NAVIGATE ──
    addCaption("Navigate");
    makeModeButton(dragModeButton, "view_mode", tr("View"),
                   tr("Select / pan / zoom"));

    pointerModeGroup = new QButtonGroup(this);
    pointerModeGroup->setExclusive(true);
    pointerModeGroup->addButton(createModeButton);
    pointerModeGroup->addButton(dragModeButton);

    // Keyboard shortcuts — single letters while the canvas has focus.
    // QToolButton::setShortcut would fire while typing in text fields and
    // steal keys from the GL canvas, so wire guarded QShortcuts instead.
    // Keys avoid the canvas camera bindings (W/A/S/D/Q/E).
    auto addModeShortcut = [this](const char* key, QToolButton* btn,
                                  const QString& tip) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        connect(sc, &QShortcut::activated, this, [this, btn, key, tip]() {
            if (qobject_cast<QLineEdit*>(focusWidget()))
                return;  // typing in a text field — don't switch modes
            if (!btn->isChecked())
                btn->click();
        });
        btn->setToolTip(QString("%1 (%2)").arg(tip, key));
    };
    addModeShortcut("R", createModeButton,      tr("Draw new roads"));
    addModeShortcut("V", dragModeButton,        tr("Select / pan / zoom"));

    sidebarLayout->addWidget(createModeButton);
    sidebarLayout->addWidget(dragModeButton);

    // Esc always returns to the safe View mode (but not while typing)
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (qobject_cast<QLineEdit*>(focusWidget()))
            return;
        if (!dragModeButton->isChecked())
            dragModeButton->click();
    });

    sidebarLayout->addStretch();

    // LaneConfigWidget — placed at bottom of sidebar
    QSizePolicy sp_retain = laneConfig->sizePolicy();
    sp_retain.setRetainSizeWhenHidden(true);
    laneConfig->setSizePolicy(sp_retain);
    laneConfig->hide();
    g_laneConfig = laneConfig;
    sidebarLayout->addWidget(laneConfig);

    // ============================================================
    // Top toolbar — file ops, undo/redo, draw options, view mode
    // OpenGeoStudio-native: clean flat buttons with SVG icons
    // ============================================================
    auto* topBar = new QWidget(this);
    topBar->setObjectName("roadTopBar");
    topBar->setFixedHeight(40);
    topBar->setStyleSheet(
        "QWidget#roadTopBar { background-color: #0d1117; border-bottom: 1px solid #21262d; }"
        "QToolButton { background: transparent; border: none; border-radius: 6px; "
        "  padding: 6px; margin: 2px; }"
        "QToolButton:hover { background-color: #21262d; }"
        "QToolButton:checked { background-color: #1f6feb33; border: 1px solid #1f6feb; "
        "  border-radius: 6px; }"
        "QLabel { color: #7d8590; font-size: 11px; padding: 0 4px; }");
    auto* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(8, 4, 8, 4);
    topBarLayout->setSpacing(2);

    auto makeToolbarBtn = [&](const QString& iconName, const QString& tip) {
        auto* btn = new QToolButton;
        btn->setIcon(loadSvgIcon(iconName));
        btn->setIconSize(iconSize);
        btn->setToolTip(tip);
        return btn;
    };

    auto* loadButton = makeToolbarBtn("open", tr("Open file"));
    auto* saveButton = makeToolbarBtn("save", tr("Save file"));
    topBarLayout->addWidget(loadButton);
    topBarLayout->addWidget(saveButton);

    auto* sep1 = new QFrame;
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("color: #21262d;");
    topBarLayout->addWidget(sep1);

    auto* undoButton = makeToolbarBtn("undo", tr("Undo"));
    auto* redoButton = makeToolbarBtn("redo", tr("Redo"));
    topBarLayout->addWidget(undoButton);
    topBarLayout->addWidget(redoButton);

    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color: #21262d;");
    topBarLayout->addWidget(sep2);

    auto* drawOptionButton = makeToolbarBtn("draw_option", tr("Draw options"));
    topBarLayout->addWidget(drawOptionButton);

    auto* sep3 = new QFrame;
    sep3->setFrameShape(QFrame::VLine);
    sep3->setStyleSheet("color: #21262d;");
    topBarLayout->addWidget(sep3);

    // Profile selector is now in the LaneConfigWidget (Cross-Section Studio)
    // No top-bar profile combo needed anymore.

    auto* sep3b = new QFrame;
    sep3b->setFrameShape(QFrame::VLine);
    sep3b->setStyleSheet("color: #21262d;");
    topBarLayout->addWidget(sep3b);

    // Zoom buttons — SVG icons
    zoomInButton = makeToolbarBtn("zoom_in", tr("Zoom in"));
    zoomInButton->setShortcut(QKeySequence::ZoomIn);
    topBarLayout->addWidget(zoomInButton);

    zoomOutButton = makeToolbarBtn("zoom_out", tr("Zoom out"));
    zoomOutButton->setShortcut(QKeySequence::ZoomOut);
    topBarLayout->addWidget(zoomOutButton);

    fitButton = makeToolbarBtn("fit", tr("Reset camera to default view"));
    topBarLayout->addWidget(fitButton);

    auto* sep4 = new QFrame;
    sep4->setFrameShape(QFrame::VLine);
    sep4->setStyleSheet("color: #21262d;");
    topBarLayout->addWidget(sep4);

    // Search bar — geocode via Nominatim, fly to location
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("Search location...");
    searchEdit->setMinimumWidth(180);
    searchEdit->setMaximumWidth(280);
    searchEdit->setStyleSheet(
        "QLineEdit { background: #161b22; border: 1px solid #21262d; border-radius: 6px;"
        "padding: 6px 10px 6px 32px; color: #e6edf3; font-size: 12px; }"
        "QLineEdit:focus { border-color: #1f6feb; }"
        "QLineEdit::placeholder { color: #484f58; }");
    // Add search icon as a leading indicator via action
    auto* searchAction = new QAction(loadSvgIcon("search"), tr("Search"), searchEdit);
    searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
    searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(searchEdit);

    topBarLayout->addStretch();

    // 2D/3D view mode toggle — SVG icon + text
    viewModeButton = new QToolButton(this);
    viewModeButton->setIcon(loadSvgIcon("view_3d"));
    viewModeButton->setText("3D");
    viewModeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    viewModeButton->setToolTip(tr("Toggle 2D/3D view"));
    viewModeButton->setCheckable(true);
    viewModeButton->setChecked(false);
    topBarLayout->addWidget(viewModeButton);

    // Map toggle button — SVG icon + text
    loadMapButton = new QToolButton(this);
    loadMapButton->setIcon(loadSvgIcon("satellite"));
    loadMapButton->setText("Satellite");
    loadMapButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    loadMapButton->setToolTip(tr("Toggle satellite map background"));
    loadMapButton->setCheckable(true);
    topBarLayout->addWidget(loadMapButton);

    // Right panel toggle — collapse for maximum viewport space
    panelToggleButton = new QToolButton(this);
    panelToggleButton->setText("Panel");
    panelToggleButton->setToolTip(tr("Show/hide the properties panel"));
    panelToggleButton->setCheckable(true);
    panelToggleButton->setChecked(true);

    topBarLayout->addWidget(panelToggleButton);

    // === Viewport container — OpenGL widget ===
    auto* viewportContainer = new QWidget(this);
    viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* containerLayout = new QVBoxLayout(viewportContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

#ifdef HAVE_MAPLIBRE
    setupMapBackground();
    if (m_mapWidget)
    {
        m_mapWidget->setParent(viewportContainer);
        m_mapWidget->hide();
    }
#endif

    mapViewGL->setParent(viewportContainer);
    mapViewGL->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    containerLayout->addWidget(mapViewGL);

    // === Main layout: top bar + (sidebar | viewport | right panel) ===
    auto* contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(viewportContainer);

    // ─── Right panel: collapsible Inspector / Cross Section / Object Tree / Validation ───
    rightPanel = new QWidget(this);
    rightPanel->setFixedWidth(300);
    rightPanel->setStyleSheet("QWidget { background: #0d1117; color: #e6edf3; } "
        "QGroupBox { border: 1px solid #30363d; border-radius: 4px; margin-top: 8px; padding-top: 8px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; } "
        "QTreeWidget { background: #161b22; border: 1px solid #30363d; color: #e6edf3; } "
        "QLabel { color: #e6edf3; } "
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; } "
        "QPushButton { background: #21262d; color: #e6edf3; border: 1px solid #30363d; padding: 4px 12px; } "
        "QPushButton:hover { background: #30363d; }");

    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(6, 6, 6, 6);
    rightLayout->setSpacing(4);

    // ── Inspector section — contextual: placeholder when nothing selected ──
    inspectorSection = new CollapsibleSection(tr("Road Properties"), true, this);
    auto* inspectorArea = inspectorSection->content();

    inspectorPlaceholder = new QLabel(tr("Select an object to view properties"));
    inspectorPlaceholder->setWordWrap(true);
    inspectorPlaceholder->setStyleSheet(
        "QLabel { color: #6e7681; font-size: 11px; padding: 12px 4px; }");
    inspectorArea->addWidget(inspectorPlaceholder);

    inspectorGroup = new QGroupBox;
    inspectorForm = new QFormLayout(inspectorGroup);
    inspectorRoadName = new QLineEdit;
    inspectorRoadId = new QLineEdit;
    inspectorRoadId->setReadOnly(true);
    inspectorRoadLength = new QDoubleSpinBox;
    inspectorRoadLength->setReadOnly(true);
    inspectorRoadLength->setSuffix(" m");
    inspectorLaneCount = new QSpinBox;
    inspectorLaneCount->setReadOnly(true);
    inspectorLaneWidth = new QDoubleSpinBox;
    inspectorLaneWidth->setRange(2.0, 5.0);
    inspectorLaneWidth->setSuffix(" m");
    inspectorSpeedLimit = new QDoubleSpinBox;
    inspectorSpeedLimit->setRange(0, 200);
    inspectorSpeedLimit->setSuffix(" km/h");
    inspectorRoadType = new QComboBox;
    inspectorRoadType->addItems({"City", "Highway", "Residential", "Industrial", "Ramp"});
    inspectorTrafficDir = new QComboBox;
    inspectorTrafficDir->addItems({"Two-way", "One-way forward", "One-way reverse"});

    inspectorForm->addRow("Name:", inspectorRoadName);
    inspectorForm->addRow("ID:", inspectorRoadId);
    inspectorForm->addRow("Length:", inspectorRoadLength);
    inspectorForm->addRow("Lanes:", inspectorLaneCount);
    inspectorForm->addRow("Width:", inspectorLaneWidth);
    inspectorForm->addRow("Speed:", inspectorSpeedLimit);
    inspectorForm->addRow("Type:", inspectorRoadType);
    inspectorForm->addRow("Direction:", inspectorTrafficDir);
    inspectorArea->addWidget(inspectorGroup);

    // Road operation buttons — enabled only with a selection
    auto* roadOpsLayout = new QHBoxLayout;
    splitRoadButton = new QPushButton("Split");
    mergeRoadsButton = new QPushButton("Merge");
    reverseRoadButton = new QPushButton("Reverse");
    roadOpsLayout->addWidget(splitRoadButton);
    roadOpsLayout->addWidget(mergeRoadsButton);
    roadOpsLayout->addWidget(reverseRoadButton);
    inspectorArea->addLayout(roadOpsLayout);
    rightLayout->addWidget(inspectorSection);

    // ── Cross Section section (advanced — collapsed by default) ──
    crossSectionSection = new CollapsibleSection(tr("Cross Section"), false, this);
    auto* csArea = crossSectionSection->content();
    crossSectionGroup = new QGroupBox;
    auto* csLayout = new QFormLayout(crossSectionGroup);
    csLeftLanes = new QSpinBox;
    csLeftLanes->setRange(0, 6);
    csRightLanes = new QSpinBox;
    csRightLanes->setRange(0, 6);
    csLeftOffset = new QDoubleSpinBox;
    csLeftOffset->setRange(0, 20);
    csLeftOffset->setSuffix(" m");
    csRightOffset = new QDoubleSpinBox;
    csRightOffset->setRange(0, 20);
    csRightOffset->setSuffix(" m");
    csLaneWidth = new QDoubleSpinBox;
    csLaneWidth->setRange(2.0, 5.0);
    csLaneWidth->setValue(3.5);
    csLaneWidth->setSuffix(" m");
    csHasSidewalk = new QCheckBox("Sidewalk");
    csHasCurb = new QCheckBox("Curb");
    csHasShoulder = new QCheckBox("Shoulder");
    csHasMedian = new QCheckBox("Median");
    csApplyButton = new QPushButton("Apply Cross Section");

    csLayout->addRow("Left Lanes:", csLeftLanes);
    csLayout->addRow("Right Lanes:", csRightLanes);
    csLayout->addRow("Left Offset:", csLeftOffset);
    csLayout->addRow("Right Offset:", csRightOffset);
    csLayout->addRow("Lane Width:", csLaneWidth);
    csLayout->addRow("", csHasSidewalk);
    csLayout->addRow("", csHasCurb);
    csLayout->addRow("", csHasShoulder);
    csLayout->addRow("", csHasMedian);
    csLayout->addRow("", csApplyButton);
    csArea->addWidget(crossSectionGroup);
    rightLayout->addWidget(crossSectionSection);

    // ── Object Tree section with search filter ──
    objectTreeSection = new CollapsibleSection(tr("Object Tree"), true, this);
    auto* treeArea = objectTreeSection->content();
    treeFilterEdit = new QLineEdit;
    treeFilterEdit->setPlaceholderText(tr("Filter objects..."));
    treeFilterEdit->setClearButtonEnabled(true);
    treeArea->addWidget(treeFilterEdit);
    objectTree = new QTreeWidget;
    objectTree->setHeaderHidden(true);
    objectTree->setColumnWidth(0, 180);
    treeArea->addWidget(objectTree, 1);
    rightLayout->addWidget(objectTreeSection, 1);

    // ── Validation section — compact summary, expandable details ──
    validationSection = new CollapsibleSection(tr("Validation"), false, this);
    auto* valArea = validationSection->content();
    auto* valSummaryRow = new QHBoxLayout;
    validationSummary = new QLabel(tr("Not run"));
    validationSummary->setStyleSheet("QLabel { color: #6e7681; font-size: 11px; }");
    validateButton = new QPushButton(tr("Run"));
    validateButton->setFixedWidth(56);
    valSummaryRow->addWidget(validationSummary, 1);
    valSummaryRow->addWidget(validateButton);
    valArea->addLayout(valSummaryRow);
    validationTree = new QTreeWidget;
    validationTree->setHeaderLabels({"Issue", "Severity"});
    validationTree->setMaximumHeight(140);
    valArea->addWidget(validationTree);
    rightLayout->addWidget(validationSection);

    contentLayout->addWidget(rightPanel);

    // ─── Status bar ───
    auto* statusBar = new QWidget(this);
    statusBar->setFixedHeight(24);
    statusBar->setStyleSheet("QWidget { background: #0d1117; border-top: 1px solid #30363d; } "
        "QLabel { color: #8b949e; padding: 2px 8px; font-size: 11px; }");
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);
    statusTool = new QLabel("Tool: View");
    statusCoords = new QLabel("X: 0  Y: 0");
    statusStation = new QLabel("S: —");
    statusValidation = new QLabel("Validation: OK");
    statusSnap = new QLabel("Snap: Off");
    statusLayout->addWidget(statusTool);
    statusLayout->addStretch();
    statusLayout->addWidget(statusCoords);
    statusLayout->addWidget(statusStation);
    statusLayout->addWidget(statusSnap);
    statusLayout->addWidget(statusValidation);

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(topBar);
    mainLayout->addLayout(contentLayout);
    mainLayout->addWidget(statusBar);
    setLayout(mainLayout);

    connect(createModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoCreateRoadMode);
    connect(dragModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoDragMode);
    connect(mapViewGL, &LM::MapViewGL::MousePerformedAction, this, &MainWidget::OnMouseAction);
    connect(mapViewGL, &LM::MapViewGL::KeyPerformedAction, this, &MainWidget::OnKeyPress);

    connect(saveButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::saveToFile);
    connect(loadButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::loadFromFile);
    connect(undoButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::undo);
    connect(redoButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::redo);
    connect(drawOptionButton, &QAbstractButton::clicked, drawOptionDialog, &QDialog::open);
    connect(viewModeButton, &QAbstractButton::toggled, this, &MainWidget::toggleViewMode);
    // Profile combo is now inside LaneConfigWidget — wire its signal
    connect(laneConfig, &LaneConfigWidget::ProfileChanged, this, [this](const QString& key) {
        if (mapViewGL) mapViewGL->update();
    });
    connect(laneConfig, &LaneConfigWidget::RoadMetadataChanged, this, [this](double, bool, bool) {
        if (mapViewGL) mapViewGL->update();
    });
    connect(loadMapButton, &QAbstractButton::toggled, this, [this](bool checked) {
        if (checked) {
            loadMapBackground();
        } else {
            mapViewGL->ClearMapBackground();
        }
    });

    // Zoom buttons — adjust camera Z (2D) or dolly (3D)
    connect(zoomInButton, &QToolButton::clicked, this, [this]() {
        mapViewGL->ZoomIn();
    });

    connect(zoomOutButton, &QToolButton::clicked, this, [this]() {
        mapViewGL->ZoomOut();
    });

    connect(fitButton, &QToolButton::clicked, this, [this]() {
        mapViewGL->ResetCamera();
        mapViewGL->update();
    });

    // Object tree / validation / inspector connections
    connect(panelToggleButton, &QAbstractButton::toggled, this, [this](bool on) {
        rightPanel->setVisible(on);
    });
    // P toggles the panel — guarded so typing in text fields is not hijacked
    auto* panelShortcut = new QShortcut(QKeySequence("P"), this);
    connect(panelShortcut, &QShortcut::activated, this, [this]() {
        if (qobject_cast<QLineEdit*>(focusWidget()))
            return;
        panelToggleButton->click();
    });
    connect(treeFilterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString filter = text.trimmed();
        // Recursive: keep items that match or contain a matching descendant
        std::function<bool(QTreeWidgetItem*)> applyFilter =
            [&](QTreeWidgetItem* item) -> bool {
            bool childMatch = false;
            for (int i = 0; i < item->childCount(); ++i)
                childMatch = applyFilter(item->child(i)) || childMatch;
            const bool selfMatch = item->text(0).contains(filter, Qt::CaseInsensitive);
            const bool visible = filter.isEmpty() || selfMatch || childMatch;
            item->setHidden(!visible);
            if (!filter.isEmpty() && childMatch)
                item->setExpanded(true);
            return visible;
        };
        for (int i = 0; i < objectTree->topLevelItemCount(); ++i)
            applyFilter(objectTree->topLevelItem(i));
    });
    connect(objectTree, &QTreeWidget::itemClicked, this, &MainWidget::onObjectTreeItemClicked);
    objectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objectTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = objectTree->itemAt(pos);
        if (!item) return;
        showObjectTreeContextMenu(item, objectTree->viewport()->mapToGlobal(pos));
    });
    connect(validateButton, &QPushButton::clicked, this, &MainWidget::runValidation);
    connect(validationTree, &QTreeWidget::itemClicked, this, &MainWidget::onValidationItemClicked);
    connect(splitRoadButton, &QPushButton::clicked, this, &MainWidget::onSplitRoad);
    connect(mergeRoadsButton, &QPushButton::clicked, this, &MainWidget::onMergeRoads);
    connect(reverseRoadButton, &QPushButton::clicked, this, &MainWidget::onReverseRoad);
    connect(csApplyButton, &QPushButton::clicked, this, &MainWidget::onApplyCrossSection);
    // Live preview of cross-section changes
    connect(csLeftLanes, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWidget::onCrossSectionChanged);
    connect(csRightLanes, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWidget::onCrossSectionChanged);
    connect(csHasSidewalk, &QCheckBox::toggled, this, &MainWidget::onCrossSectionChanged);
    connect(csHasCurb, &QCheckBox::toggled, this, &MainWidget::onCrossSectionChanged);
    connect(csHasShoulder, &QCheckBox::toggled, this, &MainWidget::onCrossSectionChanged);
    connect(csHasMedian, &QCheckBox::toggled, this, &MainWidget::onCrossSectionChanged);
    connect(inspectorLaneWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        onInspectorPropertyChanged();
    });
    connect(inspectorSpeedLimit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        onInspectorPropertyChanged();
    });
    connect(inspectorRoadType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        onInspectorPropertyChanged();
    });
    connect(inspectorTrafficDir, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        onInspectorPropertyChanged();
    });

    // Search bar — geocode via Nominatim, fly to location
    auto* searchNetMgr = new QNetworkAccessManager(this);
    connect(searchEdit, &QLineEdit::returnPressed, this, [this, searchNetMgr]() {
        QString query = searchEdit->text().trimmed();
        if (query.length() < 3) return;

        QUrl url("https://nominatim.openstreetmap.org/search");
        QUrlQuery q;
        q.addQueryItem("q", query);
        q.addQueryItem("format", "json");
        q.addQueryItem("limit", "1");
        url.setQuery(q);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
        request.setRawHeader("Accept", "application/json");

        QNetworkReply* reply = searchNetMgr->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;

            auto doc = QJsonDocument::fromJson(reply->readAll());
            auto arr = doc.array();
            if (arr.isEmpty()) return;

            auto obj = arr.first().toObject();
            double lat = obj.value("lat").toString().toDouble();
            double lon = obj.value("lon").toString().toDouble();

            // Fly to location — set map center and reset camera
            mapViewGL->SetMapCenter(lat, lon);
            mapViewGL->ResetCamera();
            mapViewGL->update();

            searchEdit->setText(obj.value("display_name").toString());
        });
    });

    Reset();
}

MainWidget* MainWidget::Instance()
{
    return instance;
}

void MainWidget::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
#ifdef HAVE_MAPLIBRE
    // Keep map widget sized to match the OpenGL viewport
    if (m_mapWidget && m_2dMode && mapViewGL)
    {
        m_mapWidget->setGeometry(mapViewGL->geometry());
    }
#endif
}

void MainWidget::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    // Update the global pointer to point to THIS widget's MapViewGL.
    // This is needed because TrainStudioWidget also creates a MainWindow/MapViewGL,
    // and the global g_mapViewGL would otherwise point to the wrong instance.
    if (mapViewGL) {
        LM::g_mapViewGL = mapViewGL;
    }
}

void MainWidget::gotoCreateRoadMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_Create);
    LM::ActionManager::Instance()->Record(LM::Mode_Create);
    laneConfig->GotoRoadMode();
}

void MainWidget::gotoStraightLineMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_StraightLine);
    LM::ActionManager::Instance()->Record(LM::Mode_Create);
    laneConfig->GotoRoadMode();
}

void MainWidget::SetRailMode(bool railMode)
{
    if (railMode)
    {
        // Switch LaneConfigWidget to rail mode (populates rail profiles internally)
        if (laneConfig)
        {
            laneConfig->GotoRailMode();
            laneConfig->PopulateRailProfiles();
            laneConfig->LoadProfile("single_standard");
        }
    }
    else
    {
        // Restore road profiles in LaneConfigWidget
        if (laneConfig)
        {
            laneConfig->GotoRoadMode();
            laneConfig->PopulateRoadProfiles();
            laneConfig->LoadProfile("city_2x1");
        }
    }
}

bool MainWidget::IsRailMode() const
{
    return laneConfig && laneConfig->RailMode();
}

void MainWidget::UseSharedSatelliteView(double lat, double lon, double zoom)
{
    // Keep Road Studio and Train Studio visually aligned with Terrain Studio:
    // Esri World Imagery, the same geographic center, and the same zoom.
    if (!viewModeButton->isChecked()) {
        viewModeButton->setChecked(true);
    } else {
        mapViewGL->SetViewMode(LM::MapViewGL::ViewMode::TopDown2D);
    }
    if (!loadMapButton->isChecked()) {
        loadMapButton->setChecked(true);
    }
    mapViewGL->SetMapCenter(lat, lon);
    mapViewGL->SetMapZoom(zoom);
    mapViewGL->update();
}

void MainWidget::gotoCreateLaneMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_CreateLanes);
    LM::ActionManager::Instance()->Record(LM::Mode_CreateLanes);
    laneConfig->GotoLaneMode();
}

void MainWidget::gotoDestroyMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_Destroy);
    LM::ActionManager::Instance()->Record(LM::Mode_Destroy);
    laneConfig->hide();
}

void MainWidget::gotoModifyMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_Modify);
    LM::ActionManager::Instance()->Record(LM::Mode_Modify);
    laneConfig->GotoRoadMode();
}

void MainWidget::gotoFlipLaneMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_FlipLane);
    LM::ActionManager::Instance()->Record(LM::Mode_FlipLane);
    laneConfig->hide();
}

void MainWidget::gotoDragMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_None);
    LM::ActionManager::Instance()->Record(LM::Mode_None);
    laneConfig->hide();
    statusTool->setText("Tool: View");
}

void MainWidget::gotoPlaceSignMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_PlaceSign);
    laneConfig->hide();
    statusTool->setText("Tool: Sign");

    auto* reg = LM::SignRegistry::Instance();
    QStringList names;
    for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        names << it.value().displayName;
    bool ok = false;
    QString selected = QInputDialog::getItem(this, tr("Place Traffic Sign"),
        tr("Select sign type:"), names, 0, false, &ok);
    if (ok && !selected.isEmpty())
        m_selectedSignType = selected;
    else
        dragModeButton->setChecked(true);
}

void MainWidget::gotoPlaceMarkingMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_PlaceMarking);
    laneConfig->hide();
    statusTool->setText("Tool: Marking");

    auto* reg = LM::MarkingRegistry::Instance();
    QStringList names;
    for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        names << it.value().displayName;
    bool ok = false;
    QString selected = QInputDialog::getItem(this, tr("Place Road Marking"),
        tr("Select marking type:"), names, 0, false, &ok);
    if (ok && !selected.isEmpty())
        m_selectedMarkingType = selected;
    else
        dragModeButton->setChecked(true);
}

void MainWidget::gotoCreateRoundaboutMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_CreateRoundabout);
    laneConfig->hide();
    statusTool->setText("Tool: Roundabout");

    QDialog dlg(this);
    dlg.setWindowTitle("Create Roundabout");
    dlg.setStyleSheet("QDialog { background: #0d1117; } QLabel { color: #e6edf3; } "
        "QDoubleSpinBox, QSpinBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; }");

    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* radiusSpin = new QDoubleSpinBox;
    radiusSpin->setRange(5.0, 100.0);
    radiusSpin->setValue(20.0);
    radiusSpin->setSuffix(" m");
    form->addRow("Outer Radius:", radiusSpin);
    auto* laneSpin = new QSpinBox;
    laneSpin->setRange(1, 3);
    laneSpin->setValue(1);
    form->addRow("Circulatory Lanes:", laneSpin);
    auto* widthSpin = new QDoubleSpinBox;
    widthSpin->setRange(2.5, 5.0);
    widthSpin->setValue(3.5);
    widthSpin->setSuffix(" m");
    form->addRow("Lane Width:", widthSpin);
    auto* entrySpin = new QSpinBox;
    entrySpin->setRange(2, 6);
    entrySpin->setValue(4);
    form->addRow("Entry Count:", entrySpin);
    auto* sidewalkCheck = new QCheckBox("Sidewalk");
    sidewalkCheck->setChecked(true);
    form->addRow("Sidewalk:", sidewalkCheck);
    layout->addLayout(form);
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        m_roundaboutRadius = radiusSpin->value();
        m_roundaboutLanes = laneSpin->value();
        m_roundaboutLaneWidth = widthSpin->value();
        m_roundaboutEntries = entrySpin->value();
        m_roundaboutSidewalk = sidewalkCheck->isChecked();
    }
    else
        dragModeButton->setChecked(true);
}

void MainWidget::gotoPlaceFurnitureMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_PlaceFurniture);
    laneConfig->hide();
    statusTool->setText("Tool: Furniture");

    auto* reg = LM::FurnitureRegistry::Instance();
    QStringList names = reg->typeNames();
    bool ok = false;
    QString selected = QInputDialog::getItem(this, tr("Place Road Furniture"),
        tr("Select furniture type:"), names, 0, false, &ok);
    if (ok && !selected.isEmpty())
        m_selectedFurnitureType = selected;
    else
        dragModeButton->setChecked(true);
}

void MainWidget::gotoMeasureMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_Measure);
    laneConfig->hide();
    statusTool->setText("Tool: Measure");
    m_measurePoints.clear();
    statusCoords->setText("Measure: click points (Esc to clear)");
}

void MainWidget::toggleSnapSettings()
{
    auto* snap = LM::SnapSettings::Instance();
    QDialog dlg(this);
    dlg.setWindowTitle("Snap Settings");
    dlg.setStyleSheet("QDialog { background: #0d1117; } QLabel { color: #e6edf3; } "
        "QCheckBox { color: #e6edf3; } QDoubleSpinBox { background: #161b22; color: #e6edf3; }");

    auto* layout = new QVBoxLayout(&dlg);
    auto* label = new QLabel("Enable snapping categories:");
    layout->addWidget(label);

    std::map<LM::SnapCategory, QCheckBox*> checks;
    for (int i = 0; i <= static_cast<int>(LM::SnapCategory::Grid); i++)
    {
        auto cat = static_cast<LM::SnapCategory>(i);
        auto* cb = new QCheckBox(LM::SnapSettings::categoryToString(cat));
        cb->setChecked(snap->isEnabled(cat));
        layout->addWidget(cb);
        checks[cat] = cb;
    }

    layout->addSpacing(10);
    auto* radiusLabel = new QLabel("Snap Radius (m):");
    layout->addWidget(radiusLabel);
    auto* radiusSpin = new QDoubleSpinBox;
    radiusSpin->setRange(0.1, 50.0);
    radiusSpin->setValue(snap->snapRadius);
    radiusSpin->setSuffix(" m");
    layout->addWidget(radiusSpin);

    auto* gridLabel = new QLabel("Grid Size (m):");
    layout->addWidget(gridLabel);
    auto* gridSpin = new QDoubleSpinBox;
    gridSpin->setRange(0.5, 200.0);
    gridSpin->setValue(snap->gridSize);
    gridSpin->setSuffix(" m");
    layout->addWidget(gridSpin);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        for (auto& [cat, cb] : checks)
            snap->setEnabled(cat, cb->isChecked());
        snap->snapRadius = radiusSpin->value();
        snap->gridSize = gridSpin->value();
        statusSnap->setText(QString("Snap: %1 categories, r=%2m")
            .arg(snap->enabledCategories().size()).arg(snap->snapRadius, 0, 'f', 1));
    }
}

// ============================================================
// Property Inspector + Object Tree + Status Bar slots
// ============================================================

void MainWidget::refreshObjectTree()
{
    if (!objectTree) return;
    QSignalBlocker block(objectTree);
    objectTree->clear();

    auto* rootItem = new QTreeWidgetItem(objectTree, QStringList() << "Road Network" << "");
    rootItem->setExpanded(true);

    auto* roadsItem = new QTreeWidgetItem(rootItem, QStringList() << "Roads" << "");
    roadsItem->setExpanded(true);

    auto* world = World::Instance();
    if (world)
    {
        for (auto& road : world->allRoads)
        {
            if (!road) continue;
            QString roadId = QString::fromStdString(road->ID());
            QString roadLabel = QString("Road %1").arg(roadId);
            auto* roadItem = new QTreeWidgetItem(roadsItem, QStringList() << roadLabel << "Road");
            roadItem->setData(0, Qt::UserRole, roadId);

            auto leftPlan = road->generated.rr_profile.ProfileAt(0, 1);
            auto rightPlan = road->generated.rr_profile.ProfileAt(0, -1);
            int totalLanes = static_cast<int>(leftPlan.laneCount + rightPlan.laneCount);

            auto* laneInfo = new QTreeWidgetItem(roadItem,
                QStringList() << QString("Lanes (%1)").arg(totalLanes) << "");
            laneInfo->setExpanded(false);

            for (const auto& [s0, laneSection] : road->generated.s_to_lanesection)
            {
                for (const auto& [laneId, lane] : laneSection.id_to_lane)
                {
                    auto* laneItem = new QTreeWidgetItem(laneInfo,
                        QStringList() << QString("Lane %1").arg(laneId) << QString::fromStdString(lane.type));
                    laneItem->setData(0, Qt::UserRole, roadId);
                }
                break;
            }

            new QTreeWidgetItem(roadItem,
                QStringList() << QString("Length: %1m").arg(road->Length(), 0, 'f', 1) << "");

            // Markings for this road
            auto* markReg = LM::MarkingRegistry::Instance();
            auto roadMarkings = markReg->markingsForRoad(road->ID());
            if (!roadMarkings.empty())
            {
                auto* marksItem = new QTreeWidgetItem(roadItem,
                    QStringList() << QString("Markings (%1)").arg(roadMarkings.size()) << "");
                for (const auto* mark : roadMarkings)
                {
                    const auto* def = markReg->get(mark->type);
                    QString markName = def ? def->displayName : "Unknown";
                    auto* markItem = new QTreeWidgetItem(marksItem,
                        QStringList() << QString::fromStdString(mark->id) << markName);
                    markItem->setData(0, Qt::UserRole, roadId);
                }
            }

            // Signs for this road
            auto* signReg = LM::SignRegistry::Instance();
            auto roadSigns = signReg->signsForRoad(road->ID());
            if (!roadSigns.empty())
            {
                auto* signsItem = new QTreeWidgetItem(roadItem,
                    QStringList() << QString("Signs (%1)").arg(roadSigns.size()) << "");
                for (const auto* sign : roadSigns)
                {
                    const auto* def = signReg->get(sign->signType);
                    QString signName = def ? def->displayName : sign->signType;
                    auto* signItem = new QTreeWidgetItem(signsItem,
                        QStringList() << QString::fromStdString(sign->id) << signName);
                    signItem->setData(0, Qt::UserRole, roadId);
                }
            }

            // Furniture for this road
            auto* furnReg = LM::FurnitureRegistry::Instance();
            auto roadFurniture = furnReg->furnitureForRoad(road->ID());
            if (!roadFurniture.empty())
            {
                auto* furnItem = new QTreeWidgetItem(roadItem,
                    QStringList() << QString("Objects (%1)").arg(roadFurniture.size()) << "");
                for (const auto* furn : roadFurniture)
                {
                    const auto* def = furnReg->get(furn->furnitureType);
                    QString furnName = def ? def->displayName : furn->furnitureType;
                    auto* fItem = new QTreeWidgetItem(furnItem,
                        QStringList() << QString::fromStdString(furn->id) << furnName);
                    fItem->setData(0, Qt::UserRole, roadId);
                }
            }
        }
    }

    // Junctions
    auto* junctionsItem = new QTreeWidgetItem(rootItem, QStringList() << "Junctions" << "");
    auto& junctionGen = IDGenerator::ForType(IDType::Junction);
    if (junctionGen)
    {
        for (const auto& entry : junctionGen->PeekChanges())
        {
            auto* junction = static_cast<LM::AbstractJunction*>(entry.second);
            if (junction)
            {
                QString juncId = QString::fromStdString(junction->ID());
                QString juncType = (dynamic_cast<LM::DirectJunction*>(junction) != nullptr) ? "Direct" : "Common";
                auto* juncItem = new QTreeWidgetItem(junctionsItem,
                    QStringList() << QString("Junction %1").arg(juncId) << juncType);
                juncItem->setData(0, Qt::UserRole, juncId);
            }
        }
    }

    // Top-level Signs
    auto* signsItem = new QTreeWidgetItem(rootItem, QStringList() << "Signs" << "");
    auto* signReg = LM::SignRegistry::Instance();
    for (const auto& sign : signReg->placedSigns())
    {
        const auto* def = signReg->get(sign.signType);
        QString signName = def ? def->displayName : sign.signType;
        auto* signItem = new QTreeWidgetItem(signsItem,
            QStringList() << QString::fromStdString(sign.id) << signName);
        signItem->setData(0, Qt::UserRole, QString::fromStdString(sign.roadID));
    }

    // Top-level Markings
    auto* marksItem = new QTreeWidgetItem(rootItem, QStringList() << "Markings" << "");
    auto* markReg = LM::MarkingRegistry::Instance();
    for (const auto& mark : markReg->placedMarkings())
    {
        const auto* def = markReg->get(mark.type);
        QString markName = def ? def->displayName : "Unknown";
        auto* markItem = new QTreeWidgetItem(marksItem,
            QStringList() << QString::fromStdString(mark.id) << markName);
        markItem->setData(0, Qt::UserRole, QString::fromStdString(mark.roadID));
    }

    // Road Objects (furniture)
    auto* furnItem = new QTreeWidgetItem(rootItem, QStringList() << "Road Objects" << "");
    auto* furnReg = LM::FurnitureRegistry::Instance();
    for (const auto& furn : furnReg->placedFurniture())
    {
        const auto* def = furnReg->get(furn.furnitureType);
        QString furnName = def ? def->displayName : "Unknown";
        auto* fItem = new QTreeWidgetItem(furnItem,
            QStringList() << QString::fromStdString(furn.id) << furnName);
        fItem->setData(0, Qt::UserRole, QString::fromStdString(furn.roadID));
    }
}

void MainWidget::refreshInspector()
{
    onSelectionChanged();
}

void MainWidget::onSelectionChanged()
{
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road)
    {
        // Contextual inspector: nothing selected → placeholder only
        inspectorPlaceholder->setVisible(true);
        inspectorGroup->setVisible(false);
        splitRoadButton->setEnabled(false);
        mergeRoadsButton->setEnabled(false);
        reverseRoadButton->setEnabled(false);
        inspectorRoadName->setText("");
        inspectorRoadId->setText("");
        inspectorRoadLength->setValue(0);
        inspectorLaneCount->setValue(0);
        inspectorLaneWidth->setValue(LM::LaneWidth);
        inspectorSpeedLimit->setValue(0);
        statusStation->setText("S: —");
        return;
    }

    inspectorPlaceholder->setVisible(false);
    inspectorGroup->setVisible(true);
    splitRoadButton->setEnabled(true);
    mergeRoadsButton->setEnabled(true);
    reverseRoadButton->setEnabled(true);

    QSignalBlocker blockName(inspectorRoadName);
    QSignalBlocker blockCount(inspectorLaneCount);
    QSignalBlocker blockWidth(inspectorLaneWidth);
    QSignalBlocker blockSpeed(inspectorSpeedLimit);

    inspectorRoadId->setText(QString::fromStdString(g_road->ID()));
    inspectorRoadLength->setValue(g_road->Length());
    auto leftPlan = g_road->generated.rr_profile.ProfileAt(0, 1);
    auto rightPlan = g_road->generated.rr_profile.ProfileAt(0, -1);
    int totalLanes = static_cast<int>(leftPlan.laneCount + rightPlan.laneCount);
    inspectorLaneCount->setValue(totalLanes);
    inspectorLaneWidth->setValue(LM::LaneWidth);

    auto csConfig = LM::CrossSectionExtender::Extract(g_road);
    QSignalBlocker blockCSLeft(csLeftLanes);
    QSignalBlocker blockCSRight(csRightLanes);
    QSignalBlocker blockCSLO(csLeftOffset);
    QSignalBlocker blockCSRO(csRightOffset);
    QSignalBlocker blockCSW(csLaneWidth);
    QSignalBlocker blockSW(csHasSidewalk);
    QSignalBlocker blockCurb(csHasCurb);
    QSignalBlocker blockShoulder(csHasShoulder);
    QSignalBlocker blockMedian(csHasMedian);
    csLeftLanes->setValue(leftPlan.laneCount);
    csRightLanes->setValue(rightPlan.laneCount);
    csLeftOffset->setValue(leftPlan.offsetx2 / 2.0);
    csRightOffset->setValue(rightPlan.offsetx2 / 2.0);
    csLaneWidth->setValue(LM::LaneWidth);
    csHasSidewalk->setChecked(csConfig.leftHasSidewalk);
    csHasCurb->setChecked(csConfig.leftHasCurb);
    csHasShoulder->setChecked(csConfig.leftHasShoulder);
    csHasMedian->setChecked(csConfig.hasMedian);

    statusStation->setText(QString("S: %1m").arg(LM::g_PointerRoadS, 0, 'f', 1));
}

void MainWidget::onInspectorPropertyChanged()
{
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road) return;

    // Apply lane width change to the selected road
    double newWidth = inspectorLaneWidth->value();
    if (std::abs(newWidth - LM::LaneWidth) > 1e-6)
    {
        LM::LaneWidth = newWidth;
        // Re-generate the road with the new lane width
        try {
            g_road->Generate(false);
            g_road->GenerateAllSectionGraphics();
            refreshCustomGraphics(g_road->ID());
        } catch (...) {
            spdlog::warn("Failed to regenerate road after lane width change");
        }
    }

    mapViewGL->update();
}

void MainWidget::onObjectTreeItemClicked(QTreeWidgetItem* item, int column)
{
    if (!item) return;
    QString roadId = item->data(0, Qt::UserRole).toString();
    if (roadId.isEmpty()) return;
    auto g_road = findRoadShared(roadId.toStdString());
    if (g_road)
    {
        g_road->EnableHighlight(true);
        LM::g_PointerRoadID = roadId.toStdString();
        onSelectionChanged();
    }
}

void MainWidget::runValidation()
{
    if (!validationTree) return;
    QSignalBlocker block(validationTree);
    validationTree->clear();
    int errors = 0, warnings = 0;
    auto* world = World::Instance();
    if (!world) return;

    // Build a set of valid road IDs for orphan checking
    std::set<std::string> validRoadIDs;
    for (auto& road : world->allRoads)
    {
        if (road) validRoadIDs.insert(road->ID());
    }

    for (auto& road : world->allRoads)
    {
        if (!road) continue;
        if (road->Length() < 1.0)
        {
            auto* item = new QTreeWidgetItem(validationTree,
                QStringList() << QString("Road %1 too short").arg(QString::fromStdString(road->ID())) << "Error");
            item->setForeground(1, QColor("#f85149"));
            errors++;
        }
        auto leftPlan = road->generated.rr_profile.ProfileAt(0, 1);
        auto rightPlan = road->generated.rr_profile.ProfileAt(0, -1);
        if (leftPlan.laneCount == 0 && rightPlan.laneCount == 0)
        {
            auto* item = new QTreeWidgetItem(validationTree,
                QStringList() << QString("Road %1 has no lanes").arg(QString::fromStdString(road->ID())) << "Error");
            item->setForeground(1, QColor("#f85149"));
            errors++;
        }
    }

    // Check for orphaned signs (referencing non-existent roads)
    auto* signReg = LM::SignRegistry::Instance();
    for (const auto& sign : signReg->placedSigns())
    {
        if (validRoadIDs.find(sign.roadID) == validRoadIDs.end())
        {
            auto* item = new QTreeWidgetItem(validationTree,
                QStringList() << QString("Sign %1 references missing road %2")
                    .arg(QString::fromStdString(sign.id))
                    .arg(QString::fromStdString(sign.roadID)) << "Warning");
            item->setForeground(1, QColor("#d29922"));
            warnings++;
        }
    }

    // Check for orphaned markings
    auto* markReg = LM::MarkingRegistry::Instance();
    for (const auto& marking : markReg->placedMarkings())
    {
        if (validRoadIDs.find(marking.roadID) == validRoadIDs.end())
        {
            auto* item = new QTreeWidgetItem(validationTree,
                QStringList() << QString("Marking %1 references missing road %2")
                    .arg(QString::fromStdString(marking.id))
                    .arg(QString::fromStdString(marking.roadID)) << "Warning");
            item->setForeground(1, QColor("#d29922"));
            warnings++;
        }
    }

    // Check for orphaned furniture
    auto* furnReg = LM::FurnitureRegistry::Instance();
    for (const auto& furn : furnReg->placedFurniture())
    {
        if (validRoadIDs.find(furn.roadID) == validRoadIDs.end())
        {
            auto* item = new QTreeWidgetItem(validationTree,
                QStringList() << QString("Furniture %1 references missing road %2")
                    .arg(QString::fromStdString(furn.id))
                    .arg(QString::fromStdString(furn.roadID)) << "Warning");
            item->setForeground(1, QColor("#d29922"));
            warnings++;
        }
    }

    if (errors == 0 && warnings == 0)
    {
        auto* item = new QTreeWidgetItem(validationTree, QStringList() << "No issues found" << "OK");
        item->setForeground(1, QColor("#3fb950"));
    }
    statusValidation->setText(QString("Validation: %1 err, %2 warn").arg(errors).arg(warnings));
    statusValidation->setStyleSheet(errors > 0 ? "QLabel { color: #f85149; }" : "QLabel { color: #3fb950; }");

    // Compact panel summary; auto-expand only when something is wrong
    if (validationSummary)
    {
        if (errors == 0 && warnings == 0)
        {
            validationSummary->setText(tr("OK"));
            validationSummary->setStyleSheet("QLabel { color: #3fb950; font-size: 11px; }");
        }
        else
        {
            validationSummary->setText(tr("%1 error%2, %3 warning%4")
                .arg(errors).arg(errors == 1 ? "" : "s")
                .arg(warnings).arg(warnings == 1 ? "" : "s"));
            validationSummary->setStyleSheet("QLabel { color: #f85149; font-size: 11px; }");
            validationSection->setExpanded(true);
        }
    }
}

void MainWidget::onValidationItemClicked(QTreeWidgetItem* item, int column)
{
    // Future: focus the road with the issue
}

void MainWidget::onSplitRoad()
{
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road) return;
    try
    {
        std::string origRoadID = g_road->ID();
        double splitS = g_road->Length() / 2.0;
        auto part2 = LM::Road::SplitRoad(g_road, splitS);
        // Transfer markings/signs/furniture that are beyond the split point
        // to the new road (part2), adjusting their station values.
        if (part2)
        {
            std::string part2ID = part2->ID();
            auto* signReg = LM::SignRegistry::Instance();
            auto* markReg = LM::MarkingRegistry::Instance();
            auto* furnReg = LM::FurnitureRegistry::Instance();
            // Signs: adjust station for signs beyond splitS
            // Collect copies to avoid iterator invalidation during update
            std::vector<LM::PlacedSign> signsCopy;
            for (const auto* s : signReg->signsForRoad(origRoadID)) signsCopy.push_back(*s);
            for (const auto& s : signsCopy)
            {
                if (s.s >= splitS)
                {
                    LM::PlacedSign updated = s;
                    updated.roadID = part2ID;
                    updated.s -= splitS;
                    signReg->updateSign(updated);
                }
            }
            // Markings: adjust station for markings beyond splitS
            std::vector<LM::PlacedMarking> marksCopy;
            for (const auto* m : markReg->markingsForRoad(origRoadID)) marksCopy.push_back(*m);
            for (const auto& m : marksCopy)
            {
                if (m.sStart >= splitS)
                {
                    LM::PlacedMarking updated = m;
                    updated.roadID = part2ID;
                    updated.sStart -= splitS;
                    updated.sEnd -= splitS;
                    markReg->updateMarking(updated);
                }
            }
            // Furniture: adjust station for furniture beyond splitS
            std::vector<LM::PlacedFurniture> furnsCopy;
            for (const auto* f : furnReg->furnitureForRoad(origRoadID)) furnsCopy.push_back(*f);
            for (const auto& f : furnsCopy)
            {
                if (f.sStart >= splitS)
                {
                    LM::PlacedFurniture updated = f;
                    updated.roadID = part2ID;
                    updated.sStart -= splitS;
                    updated.sEnd -= splitS;
                    furnReg->updateFurniture(updated);
                }
            }
        }
        refreshAllCustomGraphics();
        mapViewGL->update();
        refreshObjectTree();
        statusValidation->setText("Validation: Road split");
        statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
    }
    catch (...)
    {
        statusValidation->setText("Validation: Split failed");
        statusValidation->setStyleSheet("QLabel { color: #f85149; }");
    }
}

void MainWidget::onMergeRoads()
{
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road) return;
    bool merged = false;
    std::string deletedRoadID;
    double road1Length = g_road->Length();
    if (!g_road->generated.successor.id.empty())
    {
        auto nextRoad = findRoadShared(g_road->generated.successor.id);
        if (nextRoad)
        {
            deletedRoadID = nextRoad->ID();
            try { LM::Road::JoinRoads(g_road, g_road->generated.successor.contact_point, nextRoad, nextRoad->generated.predecessor.contact_point); merged = true; }
            catch (const std::exception& e) { spdlog::warn("Merge failed: {}", e.what()); }
            catch (...) { spdlog::warn("Merge failed: unknown exception"); }
        }
    }
    if (merged)
    {
        // Transfer markings/signs/furniture from the deleted road to the
        // merged road, adjusting their station values by road1Length.
        std::string mergedRoadID = g_road->ID();
        auto* signReg = LM::SignRegistry::Instance();
        auto* markReg = LM::MarkingRegistry::Instance();
        auto* furnReg = LM::FurnitureRegistry::Instance();
        // Collect copies to avoid iterator invalidation during update
        std::vector<LM::PlacedSign> signsCopy;
        for (const auto* s : signReg->signsForRoad(deletedRoadID)) signsCopy.push_back(*s);
        for (const auto& s : signsCopy)
        {
            LM::PlacedSign updated = s;
            updated.roadID = mergedRoadID;
            updated.s += road1Length;
            signReg->updateSign(updated);
        }
        std::vector<LM::PlacedMarking> marksCopy;
        for (const auto* m : markReg->markingsForRoad(deletedRoadID)) marksCopy.push_back(*m);
        for (const auto& m : marksCopy)
        {
            LM::PlacedMarking updated = m;
            updated.roadID = mergedRoadID;
            updated.sStart += road1Length;
            updated.sEnd += road1Length;
            markReg->updateMarking(updated);
        }
        std::vector<LM::PlacedFurniture> furnsCopy;
        for (const auto* f : furnReg->furnitureForRoad(deletedRoadID)) furnsCopy.push_back(*f);
        for (const auto& f : furnsCopy)
        {
            LM::PlacedFurniture updated = f;
            updated.roadID = mergedRoadID;
            updated.sStart += road1Length;
            updated.sEnd += road1Length;
            furnReg->updateFurniture(updated);
        }
        refreshAllCustomGraphics();
        mapViewGL->update();
        refreshObjectTree();
        statusValidation->setText("Validation: Roads merged");
        statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
    }
    else
        QMessageBox::warning(this, tr("Merge Roads"), tr("Cannot merge: no connected road found."));
}

void MainWidget::onReverseRoad()
{
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road) return;
    if (g_road->predecessorJunction || g_road->successorJunction)
    {
        QMessageBox::warning(this, tr("Reverse Road"), tr("Cannot reverse: road connected to junction."));
        return;
    }
    std::string roadID = g_road->ID();
    double roadLength = g_road->Length();
    auto leftPlan = g_road->generated.rr_profile.ProfileAt(0, 1);
    auto rightPlan = g_road->generated.rr_profile.ProfileAt(0, -1);
    g_road->ModifyProfile(0, g_road->Length(), rightPlan, leftPlan);
    // Adjust markings/signs/furniture station values after reversal:
    // new_s = roadLength - old_s
    auto* signReg = LM::SignRegistry::Instance();
    auto* markReg = LM::MarkingRegistry::Instance();
    auto* furnReg = LM::FurnitureRegistry::Instance();
    // Collect copies to avoid iterator invalidation during update
    std::vector<LM::PlacedSign> signsCopy;
    for (const auto* s : signReg->signsForRoad(roadID)) signsCopy.push_back(*s);
    for (const auto& s : signsCopy)
    {
        LM::PlacedSign updated = s;
        updated.s = roadLength - updated.s;
        updated.tOffset = -updated.tOffset;
        signReg->updateSign(updated);
    }
    std::vector<LM::PlacedMarking> marksCopy;
    for (const auto* m : markReg->markingsForRoad(roadID)) marksCopy.push_back(*m);
    for (const auto& m : marksCopy)
    {
        LM::PlacedMarking updated = m;
        double newStart = roadLength - m.sEnd;
        double newEnd = roadLength - m.sStart;
        updated.sStart = newStart;
        updated.sEnd = newEnd;
        updated.tOffset = -updated.tOffset;
        markReg->updateMarking(updated);
    }
    std::vector<LM::PlacedFurniture> furnsCopy;
    for (const auto* f : furnReg->furnitureForRoad(roadID)) furnsCopy.push_back(*f);
    for (const auto& f : furnsCopy)
    {
        LM::PlacedFurniture updated = f;
        updated.sStart = roadLength - f.sEnd;
        updated.sEnd = roadLength - f.sStart;
        updated.tOffset = -updated.tOffset;
        furnReg->updateFurniture(updated);
    }
    refreshCustomGraphics(roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Road reversed");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::onCrossSectionChanged()
{
    // Update the cross-section preview when any cross-section control changes.
    // This provides live feedback as the user adjusts lane counts, widths,
    // and sidewalk/curb/shoulder/median checkboxes.
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road) return;
    // Just update the status bar to show the current configuration
    int totalLanes = csLeftLanes->value() + csRightLanes->value();
    QString msg = QString("Cross-section: %1+%2 lanes (%3 total)")
        .arg(csLeftLanes->value())
        .arg(csRightLanes->value())
        .arg(totalLanes);
    if (csHasSidewalk->isChecked()) msg += " +sidewalk";
    if (csHasCurb->isChecked()) msg += " +curb";
    if (csHasShoulder->isChecked()) msg += " +shoulder";
    if (csHasMedian->isChecked()) msg += " +median";
    statusValidation->setText("Validation: " + msg);
    statusValidation->setStyleSheet("QLabel { color: #58a6ff; }");
}

void MainWidget::onApplyCrossSection()
{
    auto g_road = RoadDrawingSession::GetPointerRoad();
    if (!g_road) return;
    int leftLanes = csLeftLanes->value();
    int rightLanes = csRightLanes->value();
    double leftOffset = csLeftOffset->value();
    double rightOffset = csRightOffset->value();
    double laneWidth = csLaneWidth->value();

    LM::LanePlan leftPlan, rightPlan;
    leftPlan.laneCount = leftLanes;
    leftPlan.offsetx2 = static_cast<LM::type_t>(leftOffset * 2);
    rightPlan.laneCount = rightLanes;
    rightPlan.offsetx2 = static_cast<LM::type_t>(rightOffset * 2);
    g_road->ModifyProfile(0, g_road->Length(), leftPlan, rightPlan);
    LM::LaneWidth = laneWidth;

    LM::CrossSectionConfig csConfig;
    csConfig.rightHasSidewalk = csHasSidewalk->isChecked();
    csConfig.rightHasCurb = csHasCurb->isChecked();
    csConfig.rightHasShoulder = csHasShoulder->isChecked();
    csConfig.hasMedian = csHasMedian->isChecked();
    csConfig.leftHasSidewalk = csHasSidewalk->isChecked();
    csConfig.leftHasCurb = csHasCurb->isChecked();
    csConfig.leftHasShoulder = csHasShoulder->isChecked();

    auto roadPtr = g_road;
    LM::CrossSectionExtender::Apply(roadPtr, csConfig);
    g_road->GenerateAllSectionGraphics();
    refreshCustomGraphics(g_road->ID());
    mapViewGL->update();
    refreshObjectTree();
    onSelectionChanged();
    statusValidation->setText("Validation: Cross section applied");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

// ============================================================
// Object Tree Context Menu — Marking/Sign/Furniture operations
// ============================================================

void MainWidget::showObjectTreeContextMenu(QTreeWidgetItem* item, QPoint globalPos)
{
    if (!item) return;
    QString itemId = item->text(0);
    bool isMarking = itemId.startsWith("mark_");
    bool isSign = itemId.startsWith("sign_");
    bool isFurniture = itemId.startsWith("furn_");
    if (!isMarking && !isSign && !isFurniture) return;

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #161b22; color: #e6edf3; border: 1px solid #30363d; }");

    if (isMarking)
    {
        auto* editAction = menu.addAction("Edit Marking...");
        auto* moveAction = menu.addAction("Move Marking...");
        auto* dupAction = menu.addAction("Duplicate Marking");
        menu.addSeparator();
        auto* revAction = menu.addAction("Reverse Marking");
        auto* mirrorAction = menu.addAction("Mirror Marking");
        menu.addSeparator();
        auto* delAction = menu.addAction("Delete Marking");
        auto id = itemId.toStdString();
        connect(editAction, &QAction::triggered, this, [this, id]() { editMarking(id); });
        connect(moveAction, &QAction::triggered, this, [this, id]() { moveMarking(id); });
        connect(dupAction, &QAction::triggered, this, [this, id]() { duplicateMarking(id); });
        connect(revAction, &QAction::triggered, this, [this, id]() { reverseMarking(id); });
        connect(mirrorAction, &QAction::triggered, this, [this, id]() { mirrorMarking(id); });
        connect(delAction, &QAction::triggered, this, [this, id]() { deleteMarking(id); });
    }
    else if (isSign)
    {
        auto* editAction = menu.addAction("Edit Sign...");
        auto* dupAction = menu.addAction("Duplicate Sign");
        menu.addSeparator();
        auto* delAction = menu.addAction("Delete Sign");
        auto id = itemId.toStdString();
        connect(editAction, &QAction::triggered, this, [this, id]() { editSign(id); });
        connect(dupAction, &QAction::triggered, this, [this, id]() { duplicateSign(id); });
        connect(delAction, &QAction::triggered, this, [this, id]() { deleteSign(id); });
    }
    else if (isFurniture)
    {
        auto* editAction = menu.addAction("Edit Furniture...");
        menu.addSeparator();
        auto* delAction = menu.addAction("Delete Furniture");
        auto id = itemId.toStdString();
        connect(editAction, &QAction::triggered, this, [this, id]() { editFurniture(id); });
        connect(delAction, &QAction::triggered, this, [this, id]() { deleteFurniture(id); });
    }
    menu.exec(globalPos);
}

void MainWidget::editMarking(const std::string& markingId)
{
    auto* reg = LM::MarkingRegistry::Instance();
    auto* marking = reg->findMarking(markingId);
    if (!marking) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Edit Marking %1").arg(QString::fromStdString(markingId)));
    dlg.setStyleSheet("QDialog { background: #0d1117; } QLabel { color: #e6edf3; } "
        "QDoubleSpinBox, QComboBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; }");
    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* typeCombo = new QComboBox;
    for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        typeCombo->addItem(it.value().displayName);
    const auto* def = reg->get(marking->type);
    if (def) typeCombo->setCurrentText(def->displayName);
    form->addRow("Type:", typeCombo);
    auto* sStartSpin = new QDoubleSpinBox; sStartSpin->setRange(0, 99999); sStartSpin->setValue(marking->sStart); sStartSpin->setSuffix(" m");
    form->addRow("Start Station:", sStartSpin);
    auto* sEndSpin = new QDoubleSpinBox; sEndSpin->setRange(0, 99999); sEndSpin->setValue(marking->sEnd); sEndSpin->setSuffix(" m");
    form->addRow("End Station:", sEndSpin);
    auto* tOffsetSpin = new QDoubleSpinBox; tOffsetSpin->setRange(-50, 50); tOffsetSpin->setValue(marking->tOffset); tOffsetSpin->setSuffix(" m");
    form->addRow("Lateral Offset:", tOffsetSpin);
    auto* widthSpin = new QDoubleSpinBox; widthSpin->setRange(0.01, 5.0); widthSpin->setValue(marking->width); widthSpin->setSuffix(" m");
    form->addRow("Width:", widthSpin);
    auto* colorEdit = new QLineEdit(marking->color);
    form->addRow("Color:", colorEdit);
    layout->addLayout(form);
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        marking->type = LM::MarkingRegistry::stringToType(typeCombo->currentText());
        marking->sStart = sStartSpin->value();
        marking->sEnd = sEndSpin->value();
        marking->tOffset = tOffsetSpin->value();
        marking->width = widthSpin->value();
        marking->color = colorEdit->text();
        const auto* newDef = reg->get(marking->type);
        if (newDef) { marking->pattern = newDef->pattern; marking->material = newDef->material; }
        reg->updateMarking(*marking);
        refreshCustomGraphics(marking->roadID);
        mapViewGL->update();
        refreshObjectTree();
        statusValidation->setText("Validation: Marking updated");
        statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
    }
}

void MainWidget::editSign(const std::string& signId)
{
    auto* reg = LM::SignRegistry::Instance();
    auto* sign = reg->findSign(signId);
    if (!sign) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Edit Sign %1").arg(QString::fromStdString(signId)));
    dlg.setStyleSheet("QDialog { background: #0d1117; } QLabel { color: #e6edf3; } "
        "QDoubleSpinBox, QComboBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; }");
    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* typeCombo = new QComboBox;
    for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        typeCombo->addItem(it.value().displayName);
    const auto* def = reg->get(sign->signType);
    if (def) typeCombo->setCurrentText(def->displayName);
    form->addRow("Type:", typeCombo);
    auto* sSpin = new QDoubleSpinBox; sSpin->setRange(0, 99999); sSpin->setValue(sign->s); sSpin->setSuffix(" m");
    form->addRow("Station:", sSpin);
    auto* tOffsetSpin = new QDoubleSpinBox; tOffsetSpin->setRange(-50, 50); tOffsetSpin->setValue(sign->tOffset); tOffsetSpin->setSuffix(" m");
    form->addRow("Lateral Offset:", tOffsetSpin);
    auto* heightSpin = new QDoubleSpinBox; heightSpin->setRange(0.5, 20.0); heightSpin->setValue(sign->height); heightSpin->setSuffix(" m");
    form->addRow("Height:", heightSpin);
    auto* rotSpin = new QDoubleSpinBox; rotSpin->setRange(-360, 360); rotSpin->setValue(sign->rotation); rotSpin->setSuffix(" deg");
    form->addRow("Rotation:", rotSpin);
    auto* sideCombo = new QComboBox;
    sideCombo->addItem("Left", static_cast<int>(LM::SignSide::Left));
    sideCombo->addItem("Right", static_cast<int>(LM::SignSide::Right));
    sideCombo->addItem("Overhead", static_cast<int>(LM::SignSide::Overhead));
    sideCombo->setCurrentIndex(static_cast<int>(sign->side));
    form->addRow("Side:", sideCombo);
    layout->addLayout(form);
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        {
            if (it.value().displayName == typeCombo->currentText())
            {
                sign->signType = it.value().id;
                break;
            }
        }
        sign->s = sSpin->value();
        sign->tOffset = tOffsetSpin->value();
        sign->height = heightSpin->value();
        sign->rotation = rotSpin->value();
        sign->side = static_cast<LM::SignSide>(sideCombo->currentData().toInt());
        auto g_road = findRoadShared(sign->roadID);
        if (g_road)
        {
            double hdg = g_road->generated.ref_line.get_hdg(sign->s);
            sign->rotation = hdg * 180.0 / M_PI;
            reg->updateSign(*sign);
            refreshCustomGraphics(sign->roadID);
            mapViewGL->update();
        }
        refreshObjectTree();
        statusValidation->setText("Validation: Sign updated");
        statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
    }
}

void MainWidget::editFurniture(const std::string& furnitureId)
{
    auto* reg = LM::FurnitureRegistry::Instance();
    auto* furniture = reg->findFurniture(furnitureId);
    if (!furniture) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Edit Furniture %1").arg(QString::fromStdString(furnitureId)));
    dlg.setStyleSheet("QDialog { background: #0d1117; } QLabel { color: #e6edf3; } "
        "QDoubleSpinBox, QComboBox, QSpinBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; }");
    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* typeCombo = new QComboBox;
    for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        typeCombo->addItem(it.value().displayName);
    const auto* def = reg->get(furniture->furnitureType);
    if (def) typeCombo->setCurrentText(def->displayName);
    form->addRow("Type:", typeCombo);
    auto* sStartSpin = new QDoubleSpinBox; sStartSpin->setRange(0, 99999); sStartSpin->setValue(furniture->sStart); sStartSpin->setSuffix(" m");
    form->addRow("Start Station:", sStartSpin);
    auto* sEndSpin = new QDoubleSpinBox; sEndSpin->setRange(0, 99999); sEndSpin->setValue(furniture->sEnd); sEndSpin->setSuffix(" m");
    form->addRow("End Station:", sEndSpin);
    auto* tOffsetSpin = new QDoubleSpinBox; tOffsetSpin->setRange(-50, 50); tOffsetSpin->setValue(furniture->tOffset); tOffsetSpin->setSuffix(" m");
    form->addRow("Lateral Offset:", tOffsetSpin);
    auto* heightSpin = new QDoubleSpinBox; heightSpin->setRange(0.1, 20.0); heightSpin->setValue(furniture->height); heightSpin->setSuffix(" m");
    form->addRow("Height:", heightSpin);
    auto* repeatSpin = new QSpinBox; repeatSpin->setRange(1, 1000); repeatSpin->setValue(furniture->repeatCount);
    form->addRow("Repeat Count:", repeatSpin);
    auto* spacingSpin = new QDoubleSpinBox; spacingSpin->setRange(0.5, 200); spacingSpin->setValue(furniture->repeatSpacing); spacingSpin->setSuffix(" m");
    form->addRow("Repeat Spacing:", spacingSpin);
    layout->addLayout(form);
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
        {
            if (it.value().displayName == typeCombo->currentText())
            {
                furniture->furnitureType = it.value().id;
                break;
            }
        }
        furniture->sStart = sStartSpin->value();
        furniture->sEnd = sEndSpin->value();
        furniture->tOffset = tOffsetSpin->value();
        furniture->height = heightSpin->value();
        furniture->repeatCount = repeatSpin->value();
        furniture->repeatSpacing = spacingSpin->value();
        reg->updateFurniture(*furniture);
        refreshCustomGraphics(furniture->roadID);
        mapViewGL->update();
        refreshObjectTree();
        statusValidation->setText("Validation: Furniture updated");
        statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
    }
}

void MainWidget::deleteMarking(const std::string& markingId)
{
    auto* reg = LM::MarkingRegistry::Instance();
    auto* marking = reg->findMarking(markingId);
    if (!marking) return;
    std::string roadID = marking->roadID;
    reg->removeMarking(markingId);
    refreshCustomGraphics(roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Marking deleted");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::deleteSign(const std::string& signId)
{
    auto* reg = LM::SignRegistry::Instance();
    auto* sign = reg->findSign(signId);
    if (!sign) return;
    std::string roadID = sign->roadID;
    reg->removeSign(signId);
    refreshCustomGraphics(roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Sign deleted");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::deleteFurniture(const std::string& furnitureId)
{
    auto* reg = LM::FurnitureRegistry::Instance();
    auto* furniture = reg->findFurniture(furnitureId);
    if (!furniture) return;
    std::string roadID = furniture->roadID;
    reg->removeFurniture(furnitureId);
    refreshCustomGraphics(roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Furniture deleted");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::duplicateMarking(const std::string& markingId)
{
    auto* reg = LM::MarkingRegistry::Instance();
    auto* marking = reg->findMarking(markingId);
    if (!marking) return;
    LM::PlacedMarking copy = *marking;
    copy.id = "mark_" + std::to_string(reg->placedMarkings().size() + 1);
    copy.sStart += 5.0;
    copy.sEnd += 5.0;
    reg->addMarking(copy);
    refreshCustomGraphics(copy.roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Marking duplicated");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::duplicateSign(const std::string& signId)
{
    auto* reg = LM::SignRegistry::Instance();
    auto* sign = reg->findSign(signId);
    if (!sign) return;
    LM::PlacedSign copy = *sign;
    copy.id = "sign_" + std::to_string(reg->placedSigns().size() + 1);
    copy.s += 10.0;
    reg->addSign(copy);
    refreshCustomGraphics(copy.roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Sign duplicated");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::moveMarking(const std::string& markingId)
{
    auto* reg = LM::MarkingRegistry::Instance();
    auto* marking = reg->findMarking(markingId);
    if (!marking) return;
    bool ok = false;
    double newS = QInputDialog::getDouble(this, tr("Move Marking"),
        tr("New start station (m):"), marking->sStart, 0, 99999, 2, &ok);
    if (!ok) return;
    double delta = newS - marking->sStart;
    marking->sStart = newS;
    marking->sEnd += delta;
    reg->updateMarking(*marking);
    refreshCustomGraphics(marking->roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Marking moved");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::reverseMarking(const std::string& markingId)
{
    auto* reg = LM::MarkingRegistry::Instance();
    auto* marking = reg->findMarking(markingId);
    if (!marking) return;
    std::swap(marking->sStart, marking->sEnd);
    marking->tOffset = -marking->tOffset;
    reg->updateMarking(*marking);
    refreshCustomGraphics(marking->roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Marking reversed");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::mirrorMarking(const std::string& markingId)
{
    auto* reg = LM::MarkingRegistry::Instance();
    auto* marking = reg->findMarking(markingId);
    if (!marking) return;
    marking->tOffset = -marking->tOffset;
    reg->updateMarking(*marking);
    refreshCustomGraphics(marking->roadID);
    mapViewGL->update();
    refreshObjectTree();
    statusValidation->setText("Validation: Marking mirrored");
    statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
}

void MainWidget::toggleViewMode(bool checked)
{
    m_2dMode = checked;
    if (checked)
    {
        // Switch to 2D top-down view
        viewModeButton->setText("2D");
        mapViewGL->SetViewMode(LM::MapViewGL::ViewMode::TopDown2D);
        // Auto-load satellite map for 2D mode
        loadMapButton->setChecked(true);
        loadMapBackground();
    }
    else
    {
        // Switch back to 3D perspective view
        viewModeButton->setText("3D");
        mapViewGL->SetViewMode(LM::MapViewGL::ViewMode::Perspective3D);
        loadMapButton->setChecked(false);
        mapViewGL->ClearMapBackground();
    }
}

void MainWidget::loadMapBackground()
{
    // Set the map center — the dynamic tile loader in MapViewGL will
    // automatically fetch visible tiles at the appropriate zoom level.
    const double lat = 18.52;  // Pune, India
    const double lon = 73.85;
    mapViewGL->SetMapCenter(lat, lon);}

void MainWidget::setupMapBackground()
{
#ifdef HAVE_MAPLIBRE
    // Esri World Imagery raster style JSON (same as MapViewportWidget)
    static constexpr const char* kEsriImageryStyle = R"({
        "version": 8,
        "sources": {
            "esri-imagery": {
                "type": "raster",
                "tiles": [
                    "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
                ],
                "tileSize": 256,
                "maxzoom": 19,
                "attribution": "Esri"
            }
        },
        "layers": [
            {
                "id": "background",
                "type": "background",
                "paint": { "background-color": "#000000" }
            },
            {
                "id": "esri-imagery",
                "type": "raster",
                "source": "esri-imagery",
                "minzoom": 0,
                "maxzoom": 22
            }
        ]
    })";

    m_styleServer = new LmStyleServer(QByteArray(kEsriImageryStyle), this);
    const QString styleUrl = m_styleServer->styleUrl();
    qDebug() << "[MainWidget] Map style server URL:" << styleUrl;

    QMapLibre::Styles styles;
    styles.emplace_back(styleUrl, "Esri World Imagery");

    QMapLibre::Settings settings;
    settings.setStyles(styles);
    settings.setDefaultCoordinate(QMapLibre::Coordinate(18.52, 73.85));
    settings.setDefaultZoom(15.0);

    m_mapWidget = new QMapLibre::MapWidget(settings);
    m_mapWidget->setMinimumSize(100, 100);

    // Connect map change signals
    QTimer::singleShot(100, this, [this]() {
        if (m_mapWidget && m_mapWidget->map()) {
            connect(m_mapWidget->map(), &QMapLibre::Map::mapChanged,
                    this, [this](QMapLibre::Map::MapChange change) {
                if (change == QMapLibre::Map::MapChangeRegionDidChange) {
                    onMapMoved();
                }
            });
        }
    });
#endif
}

void MainWidget::onMapMoved()
{
#ifdef HAVE_MAPLIBRE
    // When the MapLibre map moves, we could sync the LaneMaker camera.
    // For now, the map is the background and user interacts with it directly.
    // LaneMaker's OpenGL overlay stays at fixed world coordinates.
#endif
}

void MainWidget::syncMapToCamera()
{
#ifdef HAVE_MAPLIBRE
    if (!m_mapWidget || !m_mapWidget->map()) return;
    // Center the map on the default location (Pune, India)
    // In a full implementation, this would convert LaneMaker world coords
    // to lat/lon and sync the map camera.
    auto* map = m_mapWidget->map();
    map->setCoordinate(QMapLibre::Coordinate(18.52, 73.85));
    map->setZoom(15.0);
#endif
}

void MainWidget::OnMouseAction(LM::MouseAction evt)
{
    // Don't process drawing actions if GL isn't initialized — would crash
    if (!mapViewGL || !mapViewGL->isGLInitialized()) return;

    // Update status bar with coordinates
    if (statusCoords)
        statusCoords->setText(QString("X: %1  Y: %2").arg(LM::g_PointerOnGround[0], 0, 'f', 1).arg(LM::g_PointerOnGround[1], 0, 'f', 1));
    if (statusStation && !LM::g_PointerRoadID.empty())
        statusStation->setText(QString("S: %1m").arg(LM::g_PointerRoadS, 0, 'f', 1));

    // Handle sign placement mode
    if (editMode == LM::Mode_PlaceSign &&
        evt.button == Qt::LeftButton &&
        evt.type == QEvent::Type::MouseButtonPress &&
        !LM::g_PointerRoadID.empty())
    {
        auto g_road = RoadDrawingSession::GetPointerRoad();
        if (g_road)
        {
            auto* reg = LM::SignRegistry::Instance();
            QString signTypeId;
            for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
            {
                if (it.value().displayName == m_selectedSignType)
                {
                    signTypeId = it.value().id;
                    break;
                }
            }
            if (!signTypeId.isEmpty())
            {
                LM::PlacedSign sign;
                sign.id = "sign_" + std::to_string(reg->placedSigns().size() + 1);
                sign.signType = signTypeId;
                sign.roadID = LM::g_PointerRoadID;
                sign.s = LM::g_PointerRoadS;
                // Validate station is within road bounds
                if (sign.s < 0.0) sign.s = 0.0;
                if (g_road && sign.s > g_road->Length()) sign.s = g_road->Length();
                sign.tOffset = -3.0;
                sign.rotation = 0.0;
                sign.height = 2.5;
                sign.side = LM::SignSide::Right;
                // Snap rotation to road direction
                double hdg = g_road->generated.ref_line.get_hdg(sign.s);
                sign.rotation = hdg * 180.0 / M_PI;
                reg->addSign(sign);
                refreshCustomGraphics(sign.roadID);
                mapViewGL->update();
                refreshObjectTree();
                statusValidation->setText("Validation: Sign placed");
                statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
            }
        }
    }

    // Handle marking placement mode
    if (editMode == LM::Mode_PlaceMarking &&
        evt.button == Qt::LeftButton &&
        evt.type == QEvent::Type::MouseButtonPress &&
        !LM::g_PointerRoadID.empty())
    {
        auto g_road = RoadDrawingSession::GetPointerRoad();
        if (g_road)
        {
            auto* registry = LM::MarkingRegistry::Instance();
            LM::MarkingType markType = LM::MarkingType::SolidLine;
            for (auto it = registry->all().begin(); it != registry->all().end(); ++it)
            {
                if (it.value().displayName == m_selectedMarkingType)
                {
                    markType = it.value().type;
                    break;
                }
            }
            LM::PlacedMarking marking;
            marking.id = "mark_" + std::to_string(registry->placedMarkings().size() + 1);
            marking.type = markType;
            marking.roadID = LM::g_PointerRoadID;
            marking.sStart = LM::g_PointerRoadS;
            marking.sEnd = LM::g_PointerRoadS + 10.0;
            marking.tOffset = 0.0;
            marking.width = 0.15;
            marking.color = "white";
            const auto* def = registry->get(markType);
            if (def) { marking.pattern = def->pattern; marking.material = def->material; }
            registry->addMarking(marking);
            refreshCustomGraphics(marking.roadID);
            mapViewGL->update();
            refreshObjectTree();
            statusValidation->setText("Validation: Marking placed");
            statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
        }
    }

    // Handle furniture placement mode
    if (editMode == LM::Mode_PlaceFurniture &&
        evt.button == Qt::LeftButton &&
        evt.type == QEvent::Type::MouseButtonPress &&
        !LM::g_PointerRoadID.empty())
    {
        auto g_road = RoadDrawingSession::GetPointerRoad();
        if (g_road)
        {
            auto* reg = LM::FurnitureRegistry::Instance();
            QString furnitureId;
            for (auto it = reg->all().begin(); it != reg->all().end(); ++it)
            {
                if (it.value().displayName == m_selectedFurnitureType)
                {
                    furnitureId = it.value().id;
                    break;
                }
            }
            if (!furnitureId.isEmpty())
            {
                const auto* def = reg->get(furnitureId);
                LM::PlacedFurniture furniture;
                furniture.id = "furn_" + std::to_string(reg->placedFurniture().size() + 1);
                furniture.furnitureType = furnitureId;
                furniture.roadID = LM::g_PointerRoadID;
                furniture.sStart = LM::g_PointerRoadS;
                furniture.sEnd = LM::g_PointerRoadS + (def ? def->defaultLength : 1.0);
                furniture.tOffset = -3.0;
                furniture.height = def ? def->defaultHeight : 1.0;
                furniture.side = LM::SignSide::Right;
                furniture.repeatCount = 1;
                furniture.repeatSpacing = 10.0;
                reg->addFurniture(furniture);
                refreshCustomGraphics(furniture.roadID);
                mapViewGL->update();
                refreshObjectTree();
                statusValidation->setText("Validation: Furniture placed");
                statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
            }
        }
    }

    // Handle measurement mode
    if (editMode == LM::Mode_Measure &&
        evt.button == Qt::LeftButton &&
        evt.type == QEvent::Type::MouseButtonPress)
    {
        double x = LM::g_PointerOnGround[0];
        double y = LM::g_PointerOnGround[1];
        m_measurePoints.push_back({x, y, 0.0});
        auto* ms = LM::MeasurementSystem::Instance();
        if (m_measurePoints.size() == 1)
        {
            auto r = ms->measureCoordinate(x, y, 0.0);
            statusCoords->setText(r.description);
        }
        else if (m_measurePoints.size() == 2)
        {
            auto& p1 = m_measurePoints[0];
            auto& p2 = m_measurePoints[1];
            auto r = ms->measureDistance(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2]);
            statusCoords->setText(r.description);
        }
        else if (m_measurePoints.size() == 3)
        {
            auto& p1 = m_measurePoints[0];
            auto& p2 = m_measurePoints[1];
            auto& p3 = m_measurePoints[2];
            auto r = ms->measureAngle(p1[0], p1[1], p2[0], p2[1], p3[0], p3[1]);
            auto r2 = ms->measureRadius(p1[0], p1[1], p2[0], p2[1], p3[0], p3[1]);
            statusCoords->setText(r.description + "  |  " + r2.description);
            m_measurePoints.clear();
        }
        if (!LM::g_PointerRoadID.empty())
        {
            auto r = ms->measureStation(LM::g_PointerRoadID, LM::g_PointerRoadS);
            statusStation->setText(r.description);
        }
    }

    // Handle roundabout creation mode
    if (editMode == LM::Mode_CreateRoundabout &&
        evt.button == Qt::LeftButton &&
        evt.type == QEvent::Type::MouseButtonPress)
    {
        double cx = LM::g_PointerOnGround[0];
        double cy = LM::g_PointerOnGround[1];
        double radius = m_roundaboutRadius;
        int numPoints = 32;
        // Validate parameters to prevent crashes and invalid geometry
        if (radius <= 0.0)
        {
            statusValidation->setText("Validation: Roundabout radius must be positive");
            statusValidation->setStyleSheet("QLabel { color: #f85149; }");
            dragModeButton->setChecked(true);
            return;
        }
        if (m_roundaboutLanes <= 0)
        {
            statusValidation->setText("Validation: Roundabout needs at least 1 lane");
            statusValidation->setStyleSheet("QLabel { color: #f85149; }");
            dragModeButton->setChecked(true);
            return;
        }
        try
        {
            double circumference = 2.0 * M_PI * radius;
            odr::RefLine refLine("roundabout_temp", circumference);
            for (int i = 0; i < numPoints; ++i)
            {
                double angle1 = 2.0 * M_PI * i / numPoints;
                double s1 = circumference * i / numPoints;
                double x1 = cx + radius * cos(angle1);
                double y1 = cy + radius * sin(angle1);
                double hdg = angle1 + M_PI / 2.0;
                double segLen = circumference / numPoints;
                auto lineGeo = std::make_unique<odr::Line>(s1, x1, y1, hdg, segLen);
                refLine.s0_to_geometry[s1] = std::move(lineGeo);
            }
            LM::LaneProfile profile(0, 0, m_roundaboutLanes, 0);
            auto newRoad = std::make_shared<LM::Road>(profile, refLine);
            World::Instance()->allRoads.insert(newRoad);
            refreshAllCustomGraphics();
            refreshObjectTree();
            statusValidation->setText("Validation: Roundabout created");
            statusValidation->setStyleSheet("QLabel { color: #3fb950; }");
            mapViewGL->update();
        }
        catch (const std::exception& e)
        {
            spdlog::warn("Roundabout creation failed: {}", e.what());
            statusValidation->setText("Validation: Roundabout creation failed");
            statusValidation->setStyleSheet("QLabel { color: #f85149; }");
        }
        catch (...)
        {
            spdlog::warn("Roundabout creation failed: unknown exception");
            statusValidation->setText("Validation: Roundabout creation failed");
            statusValidation->setStyleSheet("QLabel { color: #f85149; }");
        }
        dragModeButton->setChecked(true);
    }

#ifndef _DEBUG
    try
    {
#endif
        if (drawingSession != nullptr)
        {
            if (!drawingSession->Update(evt))
            {
                confirmEdit();
            }
        }
#ifndef _DEBUG
    }
    catch (CGAL::Failure_exception e)
    {
        spdlog::warn(e.what());
    }
    catch (std::exception e)
    {
        elegantlyHandleException(e);
    }
#endif
}

void MainWidget::OnKeyPress(LM::KeyPressAction evt)
{
    if (drawingSession == nullptr)
    {
        if (evt.key == Qt::Key_Escape)
        {
            if (editMode == LM::Mode_Measure)
            {
                m_measurePoints.clear();
                statusCoords->setText("Measure: cleared");
            }
            dragModeButton->setChecked(true);
        }
        return;
    }
#ifndef _DEBUG
    try
    {
#endif
        switch (evt.key)
        {
        case Qt::Key_Escape:
            if (!drawingSession->Cancel())
            {
                quitEdit();
            }
            break;
        case Qt::Key_Space:
            {
                confirmEdit();
            }
            break;
        default:
            drawingSession->Update(evt);
            break;
        }

#ifndef _DEBUG
    }
    catch (CGAL::Failure_exception e)
    {
        spdlog::warn(e.what());
    }
    catch (std::exception e)
    {
        elegantlyHandleException(e);
    }
#endif
}

void MainWidget::confirmEdit()
{
    LM::ChangeTracker::Instance()->StartRecordEdit();
    bool cleanState = drawingSession->Complete();
    LM::ChangeTracker::Instance()->FinishRecordEdit(!cleanState);
    quitEdit();

    // After any road edit (create/modify/destroy), refresh the UI and
    // re-render all custom graphics so they stay in sync with the model.
    refreshAllCustomGraphics();
    refreshObjectTree();
    if (mapViewGL) mapViewGL->update();
}

void MainWidget::quitEdit()
{
    SetEditMode(editMode);
}

void MainWidget::toggleAntialiasing(bool enabled)
{
    auto fmt = mapViewGL->format();
    fmt.setSamples(enabled ? 4 : 1);
    mapViewGL->setFormat(fmt);
}

void MainWidget::Painted()
{
    auto t = QDateTime::currentMSecsSinceEpoch();
    nRepaints++;
    auto deltaMS = t - lastUpdateFPSMS;
    if (deltaMS > 1000)
    {
        auto fps = static_cast<double>(nRepaints) * 1000 / deltaMS;
        auto fpsInt = static_cast<int>(fps);
        auto displayStr = QString::fromStdString(std::to_string(fpsInt)) + tr(" FPS");
        emit FPSChanged(displayStr);

        lastUpdateFPSMS = t;
        nRepaints = 0;
    }
}

void MainWidget::Reset()
{
    laneConfig->Reset();
    pointerModeGroup->setExclusive(false);
    for (auto btn : pointerModeGroup->buttons())
    {
        btn->setChecked(false);
    }
    pointerModeGroup->setExclusive(true);
    // goto drag mode, but don't record
    SetEditMode(LM::Mode_None);
    laneConfig->hide();
    // Clear all custom graphics and registries on reset
    clearAllCustomGraphics();
    LM::SignRegistry::Instance()->clearPlaced();
    LM::MarkingRegistry::Instance()->clearPlaced();
    LM::FurnitureRegistry::Instance()->clearPlaced();
    m_measurePoints.clear();
    if (objectTree) objectTree->clear();
    if (validationTree) validationTree->clear();
}

void MainWidget::SetModeFromReplay(int mode)
{
    switch (mode)
    {
    case LM::Mode_Create:
        createModeButton->setChecked(true);
        break;
    default:
        dragModeButton->setChecked(true);
        break;
    }
}

void MainWidget::GoToSimulationMode(bool enabled)
{
    if (enabled)
    {
        dragModeButton->setChecked(true);
        //gotoDragMode();
    }
    for (auto btn : pointerModeGroup->buttons())
    {
        btn->setEnabled(!enabled);
    }
}

void MainWidget::SetEditMode(LM::EditMode aMode)
{
    // Don't enter drawing modes if GL isn't initialized — would crash
    if (!mapViewGL || !mapViewGL->isGLInitialized()) {
        editMode = LM::Mode_None;
        return;
    }
    editMode = aMode;

    // Keep the status bar's tool label in sync with every mode change
    if (statusTool)
    {
        switch (aMode)
        {
        case LM::Mode_Create:          statusTool->setText("Tool: Road"); break;
        case LM::Mode_StraightLine:    statusTool->setText("Tool: Line"); break;
        case LM::Mode_CreateLanes:     statusTool->setText("Tool: Lane"); break;
        case LM::Mode_Modify:          statusTool->setText("Tool: Modify"); break;
        case LM::Mode_Destroy:         statusTool->setText("Tool: Delete"); break;
        case LM::Mode_FlipLane:        statusTool->setText("Tool: Flip"); break;
        case LM::Mode_PlaceSign:       statusTool->setText("Tool: Sign"); break;
        case LM::Mode_PlaceMarking:    statusTool->setText("Tool: Marking"); break;
        case LM::Mode_CreateRoundabout: statusTool->setText("Tool: Roundabout"); break;
        case LM::Mode_PlaceFurniture:  statusTool->setText("Tool: Furniture"); break;
        case LM::Mode_Measure:         statusTool->setText("Tool: Measure"); break;
        case LM::Mode_None:            statusTool->setText("Tool: View"); break;
        }
    }

    if (drawingSession != nullptr)
    {
        delete drawingSession;
        drawingSession = nullptr;
    }
    switch (aMode)
    {
    case LM::Mode_Create:
        drawingSession = new RoadCreationSession();
        break;
    case LM::Mode_StraightLine:
        {
            auto* session = new RoadCreationSession();
            session->forceStraightLine = true;
            session->autoCompleteAfterFirstSegment = true;
            drawingSession = session;
        }
        break;
    case LM::Mode_CreateLanes:
        drawingSession = new LanesCreationSession();
        break;
    case LM::Mode_Destroy:
        drawingSession = new RoadDestroySession();
        break;
    case LM::Mode_Modify:
        drawingSession = new RoadModificationSession();
        break;
    case LM::Mode_FlipLane:
        drawingSession = new LaneFlipSession();
        break;
    case LM::Mode_PlaceSign:
    case LM::Mode_PlaceMarking:
    case LM::Mode_CreateRoundabout:
    case LM::Mode_PlaceFurniture:
    case LM::Mode_Measure:
        drawingSession = nullptr;
        break;
    default:
        break;
    }
}

LM::EditMode MainWidget::GetEditMode() const
{
    return editMode;
}

void MainWidget::elegantlyHandleException(std::exception e)
{
    LM::ActionManager::Instance()->MarkException();
    auto msg = std::string(e.what()) + "\nReplayable at " + LM::ActionManager::Instance()->AutosavePath();
    auto quit = QMessageBox::question(this, "Quit now?",
        QString::fromStdString(msg), QMessageBox::Yes | QMessageBox::No);
    if (quit == QMessageBox::Yes)
    {
        QCoreApplication::quit();
    }
}

