#pragma once

// ============================================================
// Studio3DWidget — 3D Studio workspace with O3DE integration
// ============================================================
//
// Provides a launcher and bridge for O3DE Editor:
//   - Launch O3DE Editor with the OpenGeoStudio3D project
//   - Export terrain heightmaps from Terrain Studio to O3DE
//   - Export road network meshes from Road Studio to O3DE
//   - Monitor O3DE process status
//   - Configure O3DE project path
//
// O3DE runs as a separate process (it cannot be embedded in Qt).
// This widget provides the launch/control UI and data export bridge.
//

#include <QWidget>
#include <QString>
#include <QProcess>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>

class ApplicationContext;

class Studio3DWidget : public QWidget {
    Q_OBJECT

public:
    explicit Studio3DWidget(ApplicationContext* ctx, QWidget* parent = nullptr);
    ~Studio3DWidget() override;

private slots:
    void onLaunchEditor();
    void onLaunchLauncher();
    void onExportTerrain();
    void onExportRoads();
    void onBrowseProject();
    void onBrowseEditor();
    void onProcessStateChanged(QProcess::ProcessState state);
    void onProcessOutput();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    void appendLog(const QString& msg);
    QString findO3DEEditor() const;
    QString findO3DELauncher() const;
    bool exportTerrainHeightmap(const QString& outputPath);
    bool exportRoadMesh(const QString& outputPath);

    ApplicationContext* m_ctx;

    // O3DE paths
    QLineEdit* m_projectPathEdit = nullptr;
    QLineEdit* m_editorPathEdit = nullptr;

    // Launch buttons
    QPushButton* m_launchEditorBtn = nullptr;
    QPushButton* m_launchLauncherBtn = nullptr;
    QPushButton* m_browseProjectBtn = nullptr;
    QPushButton* m_browseEditorBtn = nullptr;

    // Export buttons
    QPushButton* m_exportTerrainBtn = nullptr;
    QPushButton* m_exportRoadsBtn = nullptr;

    // Status
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // Log
    QTextEdit* m_logEdit = nullptr;

    // O3DE process
    QProcess* m_o3deProcess = nullptr;

    // Settings keys
    static constexpr const char* kKeyProjectPath = "studio3d/projectPath";
    static constexpr const char* kKeyEditorPath = "studio3d/editorPath";
};
