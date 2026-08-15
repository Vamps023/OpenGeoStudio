#pragma once

// ============================================================
// Studio3DWidget — 3D Studio workspace with OGRE-Next
// ============================================================
//
// OGRE-Next is embedded directly in Qt (no external process).
// This widget provides:
//   - Embedded 3D viewport using OgreWidget
//   - Load terrain heightmap + albedo from project
//   - Orbit/zoom camera controls
//   - Export terrain data for external use
//

#include <QWidget>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>

class ApplicationContext;
class OgreWidget;

class Studio3DWidget : public QWidget {
    Q_OBJECT

public:
    explicit Studio3DWidget(ApplicationContext* ctx, QWidget* parent = nullptr);
    ~Studio3DWidget() override;

private slots:
    void onLoadTerrain();
    void onClearTerrain();
    void onResetCamera();
    void onExportTerrain();
    void onExportRoads();
    void onHeightScaleChanged(int value);

private:
    void setupUI();
    void appendLog(const QString& msg);
    QString findHeightmapInProject();
    QString findAlbedoInProject();

    ApplicationContext* m_ctx;

    // OGRE viewport
    OgreWidget* m_ogreWidget = nullptr;

    // Controls
    QPushButton* m_loadTerrainBtn = nullptr;
    QPushButton* m_clearTerrainBtn = nullptr;
    QPushButton* m_resetCameraBtn = nullptr;
    QPushButton* m_exportTerrainBtn = nullptr;
    QPushButton* m_exportRoadsBtn = nullptr;
    QSlider* m_heightScaleSlider = nullptr;
    QLabel* m_heightScaleLabel = nullptr;

    // Status
    QLabel* m_statusLabel = nullptr;
    QTextEdit* m_logEdit = nullptr;
};
