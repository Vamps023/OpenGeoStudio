#pragma once
#include <QFrame>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLineEdit>
#include <QTreeWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QDialogButtonBox>

#include "action_defs.h"
#include "marking_graphics.h"
#include <memory>
#include <map>

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
class CollapsibleSection;

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

    // Use the same Esri satellite appearance and slippy-map zoom as Terrain Studio.
    void UseSharedSatelliteView(double lat, double lon, double zoom);

    LM::MapViewGL* mapViewGL;

    // Public UI refresh methods — callable from MainWindow after
    // save/load/undo/redo operations.
    void refreshObjectTree();
    void refreshAllCustomGraphics();
    void clearAllCustomGraphics();

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
    void gotoFlipLaneMode(bool);
    void gotoPlaceSignMode(bool);
    void gotoPlaceMarkingMode(bool);
    void gotoCreateRoundaboutMode(bool);
    void gotoPlaceFurnitureMode(bool);
    void gotoMeasureMode(bool);
    void toggleSnapSettings();
    void toggleViewMode(bool checked);
    void onMapMoved();
    void onProfileChanged(int index);
    void onSelectionChanged();
    void onInspectorPropertyChanged();
    void onObjectTreeItemClicked(QTreeWidgetItem* item, int column);
    void refreshInspector();
    void runValidation();
    void onValidationItemClicked(QTreeWidgetItem* item, int column);
    void onSplitRoad();
    void onMergeRoads();
    void onReverseRoad();
    void onCrossSectionChanged();
    void onApplyCrossSection();

private:
    static MainWidget* instance;

    void SetEditMode(LM::EditMode aMode);

    void showObjectTreeContextMenu(QTreeWidgetItem* item, QPoint globalPos);
    void editMarking(const std::string& markingId);
    void editSign(const std::string& signId);
    void editFurniture(const std::string& furnitureId);
    void deleteMarking(const std::string& markingId);
    void deleteSign(const std::string& signId);
    void deleteFurniture(const std::string& furnitureId);
    void duplicateMarking(const std::string& markingId);
    void duplicateSign(const std::string& signId);
    void moveMarking(const std::string& markingId);
    void reverseMarking(const std::string& markingId);
    void mirrorMarking(const std::string& markingId);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

    void confirmEdit();
    void quitEdit();

    void elegantlyHandleException(std::exception);

    void setupMapBackground();
    void syncMapToCamera();

    LM::EditMode editMode = LM::Mode_None;
    RoadDrawingSession* drawingSession = nullptr;

    QButtonGroup* pointerModeGroup;
    QToolButton* createModeButton, * dragModeButton;
    QToolButton* viewModeButton;   // 2D/3D toggle
    QToolButton* loadMapButton;    // Load satellite map background
    QToolButton* zoomInButton;     // Zoom in
    QToolButton* zoomOutButton;    // Zoom out
    QToolButton* fitButton;        // Fit to content
    QLineEdit* searchEdit;         // Location search bar
    QComboBox* profileCombo;       // SCANeR-style road profile selector

    LaneConfigWidget* laneConfig;
    DrawOptionDialog* drawOptionDialog;

    // ─── Property Inspector (right panel) ───
    QWidget* inspectorPanel;
    class QFormLayout* inspectorForm;
    class QGroupBox* inspectorGroup;
    QLineEdit* inspectorRoadName;
    QLineEdit* inspectorRoadId;
    QDoubleSpinBox* inspectorRoadLength;
    QDoubleSpinBox* inspectorLaneWidth;
    QSpinBox* inspectorLaneCount;
    QComboBox* inspectorRoadType;
    QComboBox* inspectorTrafficDir;
    QDoubleSpinBox* inspectorSpeedLimit;

    // ─── Object/Layer Tree (right panel, below inspector) ───
    QTreeWidget* objectTree;

    // ─── Validation panel (right panel, below tree) ───
    QTreeWidget* validationTree;
    QPushButton* validateButton;

    // ─── Road operation buttons (in inspector) ───
    QPushButton* splitRoadButton;
    QPushButton* mergeRoadsButton;
    QPushButton* reverseRoadButton;

    // ─── Cross-section editor (in inspector) ───
    QGroupBox* crossSectionGroup;
    QSpinBox* csLeftLanes;
    QSpinBox* csRightLanes;
    QDoubleSpinBox* csLeftOffset;
    QDoubleSpinBox* csRightOffset;
    QDoubleSpinBox* csLaneWidth;
    QCheckBox* csHasSidewalk;
    QCheckBox* csHasCurb;
    QCheckBox* csHasShoulder;
    QCheckBox* csHasMedian;
    QPushButton* csApplyButton;

    // ─── Status bar (bottom) ───
    QLabel* statusTool;
    QLabel* statusCoords;
    QLabel* statusStation;
    QLabel* statusValidation;
    QLabel* statusSnap;

    // ─── Right panel chrome (collapsible sections) ───
    QWidget* rightPanel = nullptr;
    QToolButton* panelToggleButton = nullptr;
    QLabel* inspectorPlaceholder = nullptr;   // shown when nothing is selected
    QLineEdit* treeFilterEdit = nullptr;      // object tree search box
    QLabel* validationSummary = nullptr;      // compact validation status line
    CollapsibleSection* inspectorSection = nullptr;
    CollapsibleSection* crossSectionSection = nullptr;
    CollapsibleSection* objectTreeSection = nullptr;
    CollapsibleSection* validationSection = nullptr;

    // MapLibre background for 2D mode
#ifdef HAVE_MAPLIBRE
    QMapLibre::MapWidget* m_mapWidget = nullptr;
    LmStyleServer* m_styleServer = nullptr;
    bool m_2dMode = false;
#endif

    unsigned int nRepaints = 0;
    qint64 lastUpdateFPSMS = 0;

    // Selected sign/marking/furniture type for placement tools
    QString m_selectedSignType;
    QString m_selectedMarkingType;
    QString m_selectedFurnitureType;

    // Measurement state
    std::vector<std::array<double, 3>> m_measurePoints;

    // Roundabout creation parameters
    double m_roundaboutRadius = 20.0;
    int m_roundaboutLanes = 1;
    double m_roundaboutLaneWidth = 3.5;
    int m_roundaboutEntries = 4;
    bool m_roundaboutSidewalk = true;

    // ─── Persistent custom graphics storage ───
    // These must be stored as pointers so they persist for the road's
    // lifetime. If created as temporaries, the destructor calls Clear()
    // which removes all geometry from the viewport immediately.
    std::map<std::string, std::unique_ptr<LM::MarkingGraphics>> m_markingGraphics;
    std::map<std::string, std::unique_ptr<LM::SignGraphics>> m_signGraphics;
    std::map<std::string, std::unique_ptr<LM::FurnitureGraphics>> m_furnitureGraphics;

    /// Re-render all custom graphics (markings, signs, furniture) for a
    /// specific road. Destroys old graphics objects first (which removes
    /// their viewport geometry via Clear()), then creates new ones.
    void refreshCustomGraphics(const std::string& roadID);

    /// Remove custom graphics for a road (used when road is destroyed).
    void clearCustomGraphics(const std::string& roadID);
};