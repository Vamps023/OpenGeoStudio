#pragma once
#include <QFrame>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLineEdit>

#include "action_defs.h"

#ifdef HAVE_MAPLIBRE
#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#include <QMapLibre/Types>
#include <QMapLibreWidgets/MapWidget>
#endif

QT_BEGIN_NAMESPACE
class QLabel;
class QSlider;
class QToolButton;
class QButtonGroup;
class QComboBox;
class QPixmap;
QT_END_NAMESPACE

class MapView;
class RoadDrawingSession;
namespace LM
{
    class MapViewGL;
}

class LaneConfigWidget;
class DrawOptionDialog;

// Minimal HTTP server to serve style JSON to MapLibre (same as MapViewportWidget)
class LmStyleServer : public QObject {
    Q_OBJECT
public:
    explicit LmStyleServer(const QByteArray& styleJson, QObject* parent = nullptr);
    [[nodiscard]] QString styleUrl() const;
private slots:
    void onNewConnection();
    void onReadyRead();
private:
    QTcpServer m_server;
    QByteArray m_styleJson;
    quint16 m_port = 0;
};

class MainWidget : public QFrame
{
    Q_OBJECT
public:
    explicit MainWidget(QWidget* parent = nullptr);

    static MainWidget* Instance();

    void Painted();

    void Reset();

    void SetModeFromReplay(int mode);

    void GoToSimulationMode(bool enabled); // force into drag mode

    LM::EditMode GetEditMode() const;

    // Rail mode — switches profile catalog to rail profiles and
    // cross-section visual to draw rails instead of road lanes.
    void SetRailMode(bool railMode);
    bool IsRailMode() const;

    LM::MapViewGL* mapViewGL;

signals:
    void HoveringChanged(QString);

    void FPSChanged(QString);

public slots:
    void toggleAntialiasing(bool);
    void OnMouseAction(LM::MouseAction);
    void OnKeyPress(LM::KeyPressAction);
    void loadMapBackground();

private slots:
    void gotoCreateRoadMode(bool);
    void gotoCreateLaneMode(bool);
    void gotoDestroyMode(bool);
    void gotoModifyMode(bool);
    void gotoDragMode(bool c=true);
    void gotoStraightLineMode(bool);
    void toggleViewMode(bool checked);
    void onMapMoved();
    void onProfileChanged(int index);

private:
    static MainWidget* instance;

    void SetEditMode(LM::EditMode aMode);

protected:
    void resizeEvent(QResizeEvent* event) override;

    void confirmEdit();
    void quitEdit();

    void elegantlyHandleException(std::exception);

    void setupMapBackground();
    void syncMapToCamera();

    LM::EditMode editMode = LM::Mode_None;
    RoadDrawingSession* drawingSession = nullptr;

    QButtonGroup* pointerModeGroup;
    QToolButton* createModeButton, * createLaneModeButton, * destroyModeButton, * modifyModeButton, * dragModeButton;
    QToolButton* straightLineButton;  // Straight line tool
    QToolButton* viewModeButton;   // 2D/3D toggle
    QToolButton* loadMapButton;    // Load satellite map background
    QToolButton* zoomInButton;     // Zoom in
    QToolButton* zoomOutButton;    // Zoom out
    QToolButton* fitButton;        // Fit to content
    QLineEdit* searchEdit;         // Location search bar
    QComboBox* profileCombo;       // SCANeR-style road profile selector

    LaneConfigWidget* laneConfig;
    DrawOptionDialog* drawOptionDialog;

    // MapLibre background for 2D mode
#ifdef HAVE_MAPLIBRE
    QMapLibre::MapWidget* m_mapWidget = nullptr;
    LmStyleServer* m_styleServer = nullptr;
    bool m_2dMode = false;
#endif

    unsigned int nRepaints = 0;
    qint64 lastUpdateFPSMS = 0;
};