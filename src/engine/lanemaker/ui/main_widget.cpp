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
#include <QComboBox>

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
    QSize largeIconSize = iconSize * 1.75;

    // === Left sidebar — vertical tool buttons (mode selectors) ===
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("roadSidebar");
    sidebar->setFixedWidth(52);
    sidebar->setStyleSheet(
        "QWidget#roadSidebar { background-color: #161b22; border-right: 1px solid #30363d; }"
        "QToolButton { background: transparent; border: none; border-radius: 6px; "
        "  padding: 6px; margin: 2px; }"
        "QToolButton:hover { background-color: #30363d; }"
        "QToolButton:checked { background-color: #0d1117; border: 2px solid #06b6d4; "
        "  border-radius: 6px; }");
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(4, 6, 4, 6);
    sidebarLayout->setSpacing(4);
    sidebarLayout->setAlignment(Qt::AlignTop);

    createModeButton = new QToolButton;
    createModeButton->setToolTip(tr("Road Mode — Draw new roads"));
    createModeButton->setIcon(QPixmap(":/icons/road_mode.png"));
    createModeButton->setIconSize(largeIconSize);
    createModeButton->setCheckable(true);
    createModeButton->setChecked(false);

    createLaneModeButton = new QToolButton;
    createLaneModeButton->setToolTip(tr("Lane Mode — Add/modify lanes"));
    createLaneModeButton->setIcon(QPixmap(":/icons/lane_mode.png"));
    createLaneModeButton->setIconSize(largeIconSize);
    createLaneModeButton->setCheckable(true);
    createLaneModeButton->setChecked(false);

    modifyModeButton = new QToolButton;
    modifyModeButton->setToolTip(tr("Modify Mode — Edit road geometry"));
    modifyModeButton->setIcon(QPixmap(":/icons/modify_mode.PNG"));
    modifyModeButton->setIconSize(largeIconSize);
    modifyModeButton->setCheckable(true);
    modifyModeButton->setChecked(false);

    destroyModeButton = new QToolButton;
    destroyModeButton->setToolTip(tr("Destroy Mode — Delete roads"));
    destroyModeButton->setIcon(QPixmap(":/icons/destroy_mode.png"));
    destroyModeButton->setIconSize(largeIconSize);
    destroyModeButton->setCheckable(true);
    destroyModeButton->setChecked(false);

    dragModeButton = new QToolButton;
    dragModeButton->setToolTip(tr("View Mode — Pan and zoom"));
    dragModeButton->setIcon(QPixmap(":/icons/view_mode.png"));
    dragModeButton->setIconSize(largeIconSize);
    dragModeButton->setCheckable(true);
    dragModeButton->setChecked(false);

    straightLineButton = new QToolButton;
    straightLineButton->setToolTip(tr("Straight Line — Click two points for a straight road"));
    straightLineButton->setText("─");
    straightLineButton->setStyleSheet(
        "QToolButton { font-size: 20px; font-weight: bold; }");
    straightLineButton->setIconSize(largeIconSize);
    straightLineButton->setCheckable(true);
    straightLineButton->setChecked(false);

    pointerModeGroup = new QButtonGroup(this);
    pointerModeGroup->setExclusive(true);
    pointerModeGroup->addButton(createModeButton);
    pointerModeGroup->addButton(straightLineButton);
    pointerModeGroup->addButton(createLaneModeButton);
    pointerModeGroup->addButton(modifyModeButton);
    pointerModeGroup->addButton(destroyModeButton);
    pointerModeGroup->addButton(dragModeButton);

    sidebarLayout->addWidget(createModeButton);
    sidebarLayout->addWidget(straightLineButton);
    sidebarLayout->addWidget(createLaneModeButton);
    sidebarLayout->addWidget(modifyModeButton);
    sidebarLayout->addWidget(destroyModeButton);
    sidebarLayout->addWidget(dragModeButton);
    sidebarLayout->addStretch();

    // LaneConfigWidget — placed at bottom of sidebar
    QSizePolicy sp_retain = laneConfig->sizePolicy();
    sp_retain.setRetainSizeWhenHidden(true);
    laneConfig->setSizePolicy(sp_retain);
    laneConfig->hide();
    g_laneConfig = laneConfig;
    sidebarLayout->addWidget(laneConfig);

    // === Top toolbar — file ops, undo/redo, draw options, view mode, map ===
    auto* topBar = new QWidget(this);
    topBar->setObjectName("roadTopBar");
    topBar->setFixedHeight(44);
    topBar->setStyleSheet(
        "QWidget#roadTopBar { background-color: #161b22; border-bottom: 1px solid #30363d; }"
        "QToolButton { background: transparent; border: none; border-radius: 4px; "
        "  padding: 4px 8px; color: #e6edf3; font-size: 12px; }"
        "QToolButton:hover { background-color: #30363d; }"
        "QToolButton:checked { background-color: #0d1117; border: 1px solid #06b6d4; }"
        "QLabel { color: #7d8590; font-size: 11px; padding: 0 4px; }");
    auto* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(8, 4, 8, 4);
    topBarLayout->setSpacing(4);

    auto makeToolbarBtn = [&](const QString& icon, const QString& tip) {
        auto* btn = new QToolButton;
        btn->setIcon(QPixmap(icon));
        btn->setIconSize(iconSize);
        btn->setToolTip(tip);
        return btn;
    };

    auto* loadButton = makeToolbarBtn(":/icons/open.png", tr("Open file"));
    auto* saveButton = makeToolbarBtn(":/icons/save.png", tr("Save file"));
    topBarLayout->addWidget(loadButton);
    topBarLayout->addWidget(saveButton);

    auto* sep1 = new QFrame;
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("color: #30363d;");
    topBarLayout->addWidget(sep1);

    auto* undoButton = makeToolbarBtn(":/icons/undo.png", tr("Undo"));
    auto* redoButton = makeToolbarBtn(":/icons/redo.png", tr("Redo"));
    topBarLayout->addWidget(undoButton);
    topBarLayout->addWidget(redoButton);

    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color: #30363d;");
    topBarLayout->addWidget(sep2);

    auto* drawOptionButton = makeToolbarBtn(":/icons/draw_option.png", tr("Draw options"));
    topBarLayout->addWidget(drawOptionButton);

    auto* sep3 = new QFrame;
    sep3->setFrameShape(QFrame::VLine);
    sep3->setStyleSheet("color: #30363d;");
    topBarLayout->addWidget(sep3);

    // SCANeR-style road profile selector
    auto* profileLabel = new QLabel("Profile:");
    topBarLayout->addWidget(profileLabel);
    profileCombo = new QComboBox(this);
    profileCombo->setMinimumWidth(220);
    profileCombo->setStyleSheet(
        "QComboBox { background: #0d1117; border: 1px solid #30363d; border-radius: 6px;"
        "padding: 4px 8px; color: #e6edf3; font-size: 12px; }"
        "QComboBox:hover { border-color: #1f6feb; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #0d1117; border: 1px solid #30363d;"
        "selection-background-color: #1f6feb; color: #e6edf3; }");
    // Populate with all profiles from the catalog
    {
        auto profiles = roads::RoadProfileCatalog::all();
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            profileCombo->addItem(it.key(), it.key());  // display text, userData
        }
        profileCombo->setCurrentText("city_2x1");
    }
    profileCombo->setToolTip(tr("Select a SCANeR-style road profile — sets lane width, lane count, and offsets"));
    topBarLayout->addWidget(profileCombo);

    auto* sep3b = new QFrame;
    sep3b->setFrameShape(QFrame::VLine);
    sep3b->setStyleSheet("color: #30363d;");
    topBarLayout->addWidget(sep3b);

    // Zoom buttons
    zoomInButton = new QToolButton(this);
    zoomInButton->setText("➕");
    zoomInButton->setToolTip(tr("Zoom in"));
    zoomInButton->setShortcut(QKeySequence::ZoomIn);
    topBarLayout->addWidget(zoomInButton);

    zoomOutButton = new QToolButton(this);
    zoomOutButton->setText("➖");
    zoomOutButton->setToolTip(tr("Zoom out"));
    zoomOutButton->setShortcut(QKeySequence::ZoomOut);
    topBarLayout->addWidget(zoomOutButton);

    fitButton = new QToolButton(this);
    fitButton->setText("Fit");
    fitButton->setToolTip(tr("Reset camera to default view"));
    topBarLayout->addWidget(fitButton);

    auto* sep4 = new QFrame;
    sep4->setFrameShape(QFrame::VLine);
    sep4->setStyleSheet("color: #30363d;");
    topBarLayout->addWidget(sep4);

    // Search bar — geocode via Nominatim, fly to location
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("🔍  Search location...");
    searchEdit->setMinimumWidth(200);
    searchEdit->setMaximumWidth(300);
    searchEdit->setStyleSheet(
        "QLineEdit { background: #0d1117; border: 1px solid #30363d; border-radius: 6px;"
        "padding: 6px 10px; color: #e6edf3; font-size: 12px; }"
        "QLineEdit:focus { border-color: #1f6feb; }"
        "QLineEdit::placeholder { color: #484f58; }");
    searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(searchEdit);

    topBarLayout->addStretch();

    // 2D/3D view mode toggle
    viewModeButton = new QToolButton(this);
    viewModeButton->setText("3D");
    viewModeButton->setToolTip(tr("Toggle 2D/3D view"));
    viewModeButton->setCheckable(true);
    viewModeButton->setChecked(false);
    viewModeButton->setMinimumWidth(50);
    topBarLayout->addWidget(viewModeButton);

    // Map toggle button
    loadMapButton = new QToolButton(this);
    loadMapButton->setText("Satellite");
    loadMapButton->setToolTip(tr("Toggle satellite map background"));
    loadMapButton->setCheckable(true);
    loadMapButton->setMinimumWidth(70);
    topBarLayout->addWidget(loadMapButton);

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

    // === Main layout: top bar + (sidebar | viewport) ===
    auto* contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(viewportContainer);

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(topBar);
    mainLayout->addLayout(contentLayout);
    setLayout(mainLayout);

    connect(createModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoCreateRoadMode);
    connect(straightLineButton, &QAbstractButton::toggled, this, &MainWidget::gotoStraightLineMode);
    connect(createLaneModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoCreateLaneMode);
    connect(destroyModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoDestroyMode);
    connect(modifyModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoModifyMode);
    connect(dragModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoDragMode);
    connect(mapViewGL, &LM::MapViewGL::MousePerformedAction, this, &MainWidget::OnMouseAction);
    connect(mapViewGL, &LM::MapViewGL::KeyPerformedAction, this, &MainWidget::OnKeyPress);

    connect(saveButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::saveToFile);
    connect(loadButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::loadFromFile);
    connect(undoButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::undo);
    connect(redoButton, &QAbstractButton::clicked, g_mainWindow, &MainWindow::redo);
    connect(drawOptionButton, &QAbstractButton::clicked, drawOptionDialog, &QDialog::open);
    connect(viewModeButton, &QAbstractButton::toggled, this, &MainWidget::toggleViewMode);
    connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWidget::onProfileChanged);
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

void MainWidget::onProfileChanged(int index)
{
    if (index < 0) return;
    QString key = profileCombo->itemData(index).toString();

    // Check if we're in rail mode (rail profiles loaded)
    if (g_laneConfig && g_laneConfig->RailMode())
    {
        // Rail profile selection
        roads::RailProfile railProfile = roads::RailProfileCatalog::get(key);

        // Update lane width to match rail gauge + ballast
        LM::LaneWidth = railProfile.gauge + 0.5;  // gauge + clearance

        // For rail, use single lane per track; trackCount maps to laneCount
        LM::LanePlan leftPlan, rightPlan;
        if (railProfile.trackCount == 1)
        {
            leftPlan.laneCount = 0;
            leftPlan.offsetx2 = 0;
            rightPlan.laneCount = 1;
            rightPlan.offsetx2 = 0;
        }
        else
        {
            // Multiple tracks: split evenly left/right
            int rightLanes = (railProfile.trackCount + 1) / 2;
            int leftLanes = railProfile.trackCount - rightLanes;
            leftPlan.laneCount = static_cast<int8_t>(leftLanes);
            leftPlan.offsetx2 = 0;
            rightPlan.laneCount = static_cast<int8_t>(rightLanes);
            rightPlan.offsetx2 = 0;
        }

        laneConfig->SetOption(leftPlan, rightPlan);
        laneConfig->SetRailProfile(railProfile.trackCount,
                                    railProfile.gauge,
                                    railProfile.trackSpacing);

        auto* spinner = laneConfig->findChild<QDoubleSpinBox*>();
        if (spinner) {
            QSignalBlocker blocker(spinner);
            spinner->setValue(LM::LaneWidth);
        }

        if (mapViewGL) mapViewGL->update();
        return;
    }

    // Road profile selection (default)
    roads::RoadProfile profile = roads::RoadProfileCatalog::get(key);

    // Update global lane width
    LM::LaneWidth = profile.laneWidth;

    // Update lane configuration: left/right lanes and offsets
    LM::LanePlan leftPlan, rightPlan;
    leftPlan.laneCount = static_cast<int8_t>(profile.leftLanes);
    leftPlan.offsetx2 = static_cast<int8_t>(profile.leftOffsetX2);
    rightPlan.laneCount = static_cast<int8_t>(profile.rightLanes);
    rightPlan.offsetx2 = static_cast<int8_t>(profile.rightOffsetX2);

    // Apply to the lane config widget (this records the action and repaints)
    laneConfig->SetOption(leftPlan, rightPlan);

    // Update the lane width spinner to reflect the profile's lane width
    // The spinner's valueChanged signal will also set LM::LaneWidth, but
    // we already set it above so block signals to avoid redundant work.
    auto* spinner = laneConfig->findChild<QDoubleSpinBox*>();
    if (spinner) {
        QSignalBlocker blocker(spinner);
        spinner->setValue(profile.laneWidth);
    }

    // Force a repaint of the viewport to show updated geometry
    if (mapViewGL) mapViewGL->update();
}

void MainWidget::SetRailMode(bool railMode)
{
    if (railMode)
    {
        // Switch profile combo to rail profiles
        if (profileCombo)
        {
            QSignalBlocker blocker(profileCombo);
            profileCombo->clear();
            auto railProfiles = roads::RailProfileCatalog::all();
            for (auto it = railProfiles.begin(); it != railProfiles.end(); ++it) {
                profileCombo->addItem(it.key(), it.key());
            }
            profileCombo->setCurrentText("single_standard");
            profileCombo->setToolTip(
                tr("Select a rail profile — sets gauge, track count, and spacing"));
        }
        // Switch cross-section visual to rail mode
        if (laneConfig)
        {
            laneConfig->GotoRailMode();
            auto defaultProfile = roads::RailProfileCatalog::get("single_standard");
            laneConfig->SetRailProfile(defaultProfile.trackCount,
                                        defaultProfile.gauge,
                                        defaultProfile.trackSpacing);
        }
    }
    else
    {
        // Restore road profiles
        if (profileCombo)
        {
            QSignalBlocker blocker(profileCombo);
            profileCombo->clear();
            auto roadProfiles = roads::RoadProfileCatalog::all();
            for (auto it = roadProfiles.begin(); it != roadProfiles.end(); ++it) {
                profileCombo->addItem(it.key(), it.key());
            }
            profileCombo->setCurrentText("city_2x1");
            profileCombo->setToolTip(
                tr("Select a SCANeR-style road profile — sets lane width, lane count, and offsets"));
        }
        if (laneConfig)
        {
            laneConfig->GotoRoadMode();
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

void MainWidget::gotoDragMode(bool checked)
{
    if (!checked) return;
    SetEditMode(LM::Mode_None);
    LM::ActionManager::Instance()->Record(LM::Mode_None);
    laneConfig->hide();
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
}

void MainWidget::SetModeFromReplay(int mode)
{
    switch (mode)
    {
    case LM::Mode_Create:
        createModeButton->setChecked(true);
        break;
    case LM::Mode_StraightLine:
        straightLineButton->setChecked(true);
        break;
    case LM::Mode_CreateLanes:
        createLaneModeButton->setChecked(true);
        break;
    case LM::Mode_Modify:
        modifyModeButton->setChecked(true);
        break;
    case LM::Mode_Destroy:
        destroyModeButton->setChecked(true);
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
    editMode = aMode;

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

