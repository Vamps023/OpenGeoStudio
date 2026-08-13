// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include <QtWidgets>
#include <QStackedLayout>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
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

MainWidget* MainWidget::instance = nullptr;

MainWidget::MainWidget(QWidget* parent)
    : QFrame(parent), laneConfig(new LaneConfigWidget), 
    drawOptionDialog(new DrawOptionDialog(g_mainWindow))
{
    instance = this;
    setFrameStyle(Sunken | StyledPanel);

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

    // Label layout
    QSize largeIconSize = iconSize * 1.75;
    QHBoxLayout* labelLayout = new QHBoxLayout;
    createModeButton = new QToolButton;
    createModeButton->setToolTip(tr("Road Mode"));
    createModeButton->setIcon(QPixmap(":/icons/road_mode.png"));
    createModeButton->setIconSize(largeIconSize);
    createModeButton->setCheckable(true);
    createModeButton->setChecked(false);
    createLaneModeButton = new QToolButton;
    createLaneModeButton->setToolTip(tr("Lane Mode"));
    createLaneModeButton->setIcon(QPixmap(":/icons/lane_mode.png"));
    createLaneModeButton->setIconSize(largeIconSize);
    createLaneModeButton->setCheckable(true);
    createLaneModeButton->setChecked(false);
    destroyModeButton = new QToolButton;
    destroyModeButton->setToolTip(tr("Destroy Mode"));
    destroyModeButton->setIcon(QPixmap(":/icons/destroy_mode.png"));
    destroyModeButton->setIconSize(largeIconSize);
    destroyModeButton->setCheckable(true);
    destroyModeButton->setChecked(false);
    modifyModeButton = new QToolButton;
    modifyModeButton->setToolTip(tr("Modify Mode"));
    modifyModeButton->setIcon(QPixmap(":/icons/modify_mode.PNG"));
    modifyModeButton->setIconSize(largeIconSize);
    modifyModeButton->setCheckable(true);
    modifyModeButton->setChecked(false);
    dragModeButton = new QToolButton;
    dragModeButton->setToolTip(tr("Drag Mode"));
    dragModeButton->setIcon(QPixmap(":/icons/view_mode.png"));
    dragModeButton->setIconSize(largeIconSize);
    dragModeButton->setCheckable(true);
    dragModeButton->setChecked(false);

    pointerModeGroup = new QButtonGroup(this);
    pointerModeGroup->setExclusive(true);
    pointerModeGroup->addButton(createModeButton);
    pointerModeGroup->addButton(createLaneModeButton);
    pointerModeGroup->addButton(modifyModeButton);
    pointerModeGroup->addButton(destroyModeButton);
    pointerModeGroup->addButton(dragModeButton);

    labelLayout->addWidget(laneConfig);
    QSizePolicy sp_retain = laneConfig->sizePolicy();
    sp_retain.setRetainSizeWhenHidden(true);
    laneConfig->setSizePolicy(sp_retain);
    laneConfig->hide();
    g_laneConfig = laneConfig;
    labelLayout->addSpacing(2 * size);
    labelLayout->addWidget(createModeButton);
    labelLayout->addWidget(createLaneModeButton);
    labelLayout->addWidget(modifyModeButton);
    labelLayout->addWidget(destroyModeButton);
    labelLayout->addWidget(dragModeButton);
    labelLayout->addStretch();

    auto loadButton = new QToolButton(this);
    loadButton->setIcon(QPixmap(":/icons/open.png"));
    loadButton->setIconSize(largeIconSize);
    labelLayout->addWidget(loadButton);
    auto saveButton = new QToolButton(this);
    saveButton->setIcon(QPixmap(":/icons/save.png"));
    saveButton->setIconSize(largeIconSize);
    labelLayout->addWidget(saveButton);

    labelLayout->addSpacing(size);

    auto undoButton = new QToolButton(this);
    undoButton->setIcon(QPixmap(":/icons/undo.png"));
    undoButton->setIconSize(largeIconSize);
    labelLayout->addWidget(undoButton);

    auto redoButton = new QToolButton(this);
    redoButton->setIcon(QPixmap(":/icons/redo.png"));
    redoButton->setIconSize(largeIconSize);
    labelLayout->addWidget(redoButton);

    labelLayout->addSpacing(size);

    auto drawOptionButton = new QToolButton(this);
    drawOptionButton->setIcon(QPixmap(":/icons/draw_option.png"));
    drawOptionButton->setIconSize(largeIconSize);
    labelLayout->addWidget(drawOptionButton);

    labelLayout->addSpacing(size);

    // 2D/3D view mode toggle button
    viewModeButton = new QToolButton(this);
    viewModeButton->setText("3D");
    viewModeButton->setToolTip(tr("Toggle 2D/3D view"));
    viewModeButton->setCheckable(true);
    viewModeButton->setChecked(false);
    viewModeButton->setIconSize(largeIconSize);
    labelLayout->addWidget(viewModeButton);

    // Load map background button
    loadMapButton = new QToolButton(this);
    loadMapButton->setText("Map");
    loadMapButton->setToolTip(tr("Load satellite map background"));
    loadMapButton->setIconSize(largeIconSize);
    labelLayout->addWidget(loadMapButton);

    // Container widget — just the OpenGL widget (map tiles rendered inside OpenGL)
    auto* viewportContainer = new QWidget(this);
    viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* containerLayout = new QVBoxLayout(viewportContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

#ifdef HAVE_MAPLIBRE
    setupMapBackground();
    if (m_mapWidget)
    {
        // MapLibre widget is kept for potential future use but hidden.
        // Map tiles are now rendered directly inside the OpenGL widget.
        m_mapWidget->setParent(viewportContainer);
        m_mapWidget->hide();
    }
#endif

    mapViewGL->setParent(viewportContainer);
    mapViewGL->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    containerLayout->addWidget(mapViewGL);

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->addLayout(labelLayout);
    mainLayout->addWidget(viewportContainer);
    setLayout(mainLayout);

    connect(createModeButton, &QAbstractButton::toggled, this, &MainWidget::gotoCreateRoadMode);
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
    connect(loadMapButton, &QAbstractButton::clicked, this, &MainWidget::loadMapBackground);
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
        // Auto-load map tiles for the 2D background
        loadMapBackground();
    }
    else
    {
        // Switch back to 3D perspective view
        viewModeButton->setText("3D");
        mapViewGL->SetViewMode(LM::MapViewGL::ViewMode::Perspective3D);
        mapViewGL->ClearMapBackground();
    }
}

void MainWidget::loadMapBackground()
{
    // Set the map center — the dynamic tile loader in MapViewGL will
    // automatically fetch visible tiles at the appropriate zoom level.
    const double lat = 18.52;  // Pune, India
    const double lon = 73.85;
    mapViewGL->SetMapCenter(lat, lon);
    qDebug() << "[MainWidget] Map center set, dynamic tile loading enabled";
}

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


