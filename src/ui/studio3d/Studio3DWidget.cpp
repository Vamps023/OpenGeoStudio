// Studio3DWidget — 3D Studio workspace with O3DE integration

#include "Studio3DWidget.hpp"
#include "core/ApplicationContext.hpp"
#include "core/logger/Logger.hpp"
#include "core/project/ProjectManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QFrame>

Studio3DWidget::Studio3DWidget(ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setupUI();
    loadSettings();
    m_o3deProcess = new QProcess(this);
    connect(m_o3deProcess, &QProcess::stateChanged,
            this, &Studio3DWidget::onProcessStateChanged);
    connect(m_o3deProcess, &QProcess::readyReadStandardOutput,
            this, &Studio3DWidget::onProcessOutput);
    connect(m_o3deProcess, &QProcess::readyReadStandardError,
            this, &Studio3DWidget::onProcessOutput);
    connect(m_o3deProcess, &QProcess::errorOccurred,
            this, &Studio3DWidget::onProcessError);
    connect(m_o3deProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Studio3DWidget::onProcessFinished);
}

Studio3DWidget::~Studio3DWidget() {
    saveSettings();
    if (m_o3deProcess && m_o3deProcess->state() != QProcess::NotRunning) {
        m_o3deProcess->terminate();
        m_o3deProcess->waitForFinished(3000);
    }
}

void Studio3DWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // ─── Header ───
    auto* headerFrame = new QFrame(this);
    headerFrame->setFrameStyle(QFrame::StyledPanel);
    headerFrame->setStyleSheet("QFrame { background-color: #1e1e2e; border-radius: 4px; }");
    auto* headerLayout = new QVBoxLayout(headerFrame);
    auto* titleLabel = new QLabel("3D Studio — O3DE Level Design", headerFrame);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #cdd6f4; padding: 8px;");
    auto* descLabel = new QLabel(
        "Launch O3DE Editor for professional 3D level design.\n"
        "Export terrain and road data from OpenGeoStudio into your O3DE project.",
        headerFrame);
    descLabel->setStyleSheet("color: #a6adc8; padding: 0 8px 8px 8px;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(descLabel);
    mainLayout->addWidget(headerFrame);

    // ─── O3DE Configuration ───
    auto* configGroup = new QGroupBox("O3DE Configuration", this);
    auto* configLayout = new QGridLayout(configGroup);

    configLayout->addWidget(new QLabel("Project path:", configGroup), 0, 0);
    m_projectPathEdit = new QLineEdit(configGroup);
    m_projectPathEdit->setPlaceholderText("D:/git/OpenGeoStudio3D-SDK");
    configLayout->addWidget(m_projectPathEdit, 0, 1);
    m_browseProjectBtn = new QPushButton("Browse...", configGroup);
    configLayout->addWidget(m_browseProjectBtn, 0, 2);
    connect(m_browseProjectBtn, &QPushButton::clicked, this, &Studio3DWidget::onBrowseProject);

    configLayout->addWidget(new QLabel("Editor executable:", configGroup), 1, 0);
    m_editorPathEdit = new QLineEdit(configGroup);
    m_editorPathEdit->setPlaceholderText("Auto-detect from O3DE build");
    configLayout->addWidget(m_editorPathEdit, 1, 1);
    m_browseEditorBtn = new QPushButton("Browse...", configGroup);
    configLayout->addWidget(m_browseEditorBtn, 1, 2);
    connect(m_browseEditorBtn, &QPushButton::clicked, this, &Studio3DWidget::onBrowseEditor);

    mainLayout->addWidget(configGroup);

    // ─── Launch ───
    auto* launchGroup = new QGroupBox("Launch O3DE", this);
    auto* launchLayout = new QHBoxLayout(launchGroup);

    m_launchEditorBtn = new QPushButton("Launch O3DE Editor", launchGroup);
    m_launchEditorBtn->setStyleSheet(
        "QPushButton { background-color: #89b4fa; color: #1e1e2e; "
        "font-weight: bold; padding: 10px 20px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b4befe; }"
        "QPushButton:disabled { background-color: #45475a; color: #6c7086; }");
    m_launchEditorBtn->setMinimumWidth(200);
    connect(m_launchEditorBtn, &QPushButton::clicked, this, &Studio3DWidget::onLaunchEditor);
    launchLayout->addWidget(m_launchEditorBtn);

    m_launchLauncherBtn = new QPushButton("Launch Game Launcher", launchGroup);
    m_launchLauncherBtn->setStyleSheet(
        "QPushButton { padding: 10px 20px; border-radius: 4px; }");
    connect(m_launchLauncherBtn, &QPushButton::clicked, this, &Studio3DWidget::onLaunchLauncher);
    launchLayout->addWidget(m_launchLauncherBtn);

    launchLayout->addStretch();
    mainLayout->addWidget(launchGroup);

    // ─── Data Export ───
    auto* exportGroup = new QGroupBox("Export to O3DE", this);
    auto* exportLayout = new QHBoxLayout(exportGroup);

    m_exportTerrainBtn = new QPushButton("Export Terrain Heightmap", exportGroup);
    m_exportTerrainBtn->setToolTip("Export terrain elevation data as a heightmap PNG/TIF for O3DE terrain import");
    connect(m_exportTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onExportTerrain);
    exportLayout->addWidget(m_exportTerrainBtn);

    m_exportRoadsBtn = new QPushButton("Export Road Network", exportGroup);
    m_exportRoadsBtn->setToolTip("Export road network as OBJ mesh for O3DE scene import");
    connect(m_exportRoadsBtn, &QPushButton::clicked, this, &Studio3DWidget::onExportRoads);
    exportLayout->addWidget(m_exportRoadsBtn);

    exportLayout->addStretch();
    mainLayout->addWidget(exportGroup);

    // ─── Status ───
    auto* statusGroup = new QGroupBox("Status", this);
    auto* statusLayout = new QVBoxLayout(statusGroup);

    m_statusLabel = new QLabel("O3DE process: Not running", statusGroup);
    statusLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(statusGroup);
    m_progressBar->setRange(0, 0);  // indeterminate
    m_progressBar->setVisible(false);
    statusLayout->addWidget(m_progressBar);

    mainLayout->addWidget(statusGroup);

    // ─── Log ───
    auto* logGroup = new QGroupBox("O3DE Output Log", this);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(logGroup);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet(
        "QTextEdit { background-color: #11111b; color: #cdd6f4; "
        "font-family: Consolas, monospace; font-size: 11px; }");
    m_logEdit->setMinimumHeight(150);
    logLayout->addWidget(m_logEdit);
    mainLayout->addWidget(logGroup, 1);

    // Initial state
    m_launchEditorBtn->setEnabled(false);
    m_launchLauncherBtn->setEnabled(false);
}

void Studio3DWidget::loadSettings() {
    QSettings settings;
    QString projectPath = settings.value(kKeyProjectPath, "").toString();
    QString editorPath = settings.value(kKeyEditorPath, "").toString();

    // Auto-detect project path if not set or invalid
    if (projectPath.isEmpty() || !QFileInfo::exists(projectPath + "/project.json")) {
        QString detected = findO3DEProject();
        if (!detected.isEmpty()) {
            projectPath = detected;
            settings.setValue(kKeyProjectPath, projectPath);
        }
    }
    m_projectPathEdit->setText(projectPath);
    m_editorPathEdit->setText(editorPath);

    // Auto-detect editor if not set
    if (editorPath.isEmpty()) {
        QString detected = findO3DEEditor();
        if (!detected.isEmpty()) {
            m_editorPathEdit->setText(detected);
            m_launchEditorBtn->setEnabled(true);
        }
    } else {
        m_launchEditorBtn->setEnabled(QFileInfo::exists(editorPath));
    }

    // Check project exists
    if (QDir(projectPath).exists()) {
        appendLog("O3DE project found: " + projectPath);
    } else {
        appendLog("Warning: O3DE project not found at: " + projectPath);
    }
}

void Studio3DWidget::saveSettings() {
    QSettings settings;
    settings.setValue(kKeyProjectPath, m_projectPathEdit->text());
    settings.setValue(kKeyEditorPath, m_editorPathEdit->text());
}

QString Studio3DWidget::findO3DEEditor() const {
    // Look for Editor.exe — the pre-built SDK has it, project builds don't
    QStringList candidates = {
        // Pre-built SDK install
        "C:/O3DE/26.05/bin/Windows/profile/Default/Editor.exe",
        // From-source builds
        "D:/git/OpenGeoStudio3D/build/windows/bin/profile/Editor.exe",
        "D:/git/o3de/build/windows/bin/profile/Editor.exe",
    };
    for (const auto& path : candidates) {
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

QString Studio3DWidget::findO3DEProject() const {
    // Look for project.json in common locations
    QStringList candidates = {
        "D:/git/OpenGeoStudio3D-SDK",
        "D:/git/OpenGeoStudio3D",
    };
    for (const auto& path : candidates) {
        if (QFileInfo::exists(path + "/project.json")) return path;
    }
    return {};
}

QString Studio3DWidget::findO3DELauncher() const {
    QString projectPath = m_projectPathEdit->text();
    QStringList candidates = {
        // SDK project build
        projectPath + "/build/windows/bin/profile/OpenGeoStudio3D.GameLauncher.exe",
        projectPath + "/build/windows/bin/profile/OpenGeoStudio3D.UnifiedLauncher.exe",
        // From-source project build
        "D:/git/OpenGeoStudio3D/build/windows/bin/profile/OpenGeoStudio3D.GameLauncher.exe",
    };
    for (const auto& path : candidates) {
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

void Studio3DWidget::appendLog(const QString& msg) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logEdit->append(QString("[%1] %2").arg(timestamp, msg));
}

void Studio3DWidget::onLaunchEditor() {
    QString editorPath = m_editorPathEdit->text().trimmed();
    QString projectPath = m_projectPathEdit->text().trimmed();

    if (editorPath.isEmpty() || !QFileInfo::exists(editorPath)) {
        QMessageBox::warning(this, "O3DE Editor Not Found",
            "Could not find O3DE Editor executable.\n"
            "Please build O3DE first or set the correct path.");
        return;
    }

    if (projectPath.isEmpty() || !QDir(projectPath).exists()) {
        QMessageBox::warning(this, "Project Not Found",
            "O3DE project directory not found.\n"
            "Please set the correct project path.");
        return;
    }

    appendLog("Launching O3DE Editor...");
    appendLog("  Editor: " + editorPath);
    appendLog("  Project: " + projectPath);

    m_progressBar->setVisible(true);
    m_launchEditorBtn->setEnabled(false);

    // Launch O3DE Editor with the project
    // O3DE Editor requires --project-path argument
    QStringList args;
    args << "--project-path" << projectPath;
    m_o3deProcess->start(editorPath, args);
}

void Studio3DWidget::onLaunchLauncher() {
    QString launcherPath = findO3DELauncher();
    if (launcherPath.isEmpty()) {
        QMessageBox::warning(this, "Launcher Not Found",
            "Could not find the game launcher.\n"
            "Build the project first.");
        return;
    }
    appendLog("Launching Game Launcher: " + launcherPath);
    QProcess::startDetached(launcherPath);
}

void Studio3DWidget::onExportTerrain() {
    QString o3deProjectPath = m_projectPathEdit->text().trimmed();
    if (o3deProjectPath.isEmpty() || !QDir(o3deProjectPath).exists()) {
        QMessageBox::warning(this, "Project Not Found", "Set a valid O3DE project path first.");
        return;
    }

    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    // Export from OpenGeoStudio project's Terrain folder to O3DE project's Assets
    QString exportDir = o3deProjectPath + "/Assets/terrain";
    QDir().mkpath(exportDir);

    // Copy heightmaps
    QString terrainDir = m_ctx->projects().current().basePath + "/Terrain";
    QString hmDir = terrainDir + "/heightmaps";
    QString albDir = terrainDir + "/albedo";

    int hmCount = 0, albCount = 0;

    // Copy heightmap files (convert TIFF to PNG for O3DE compatibility)
    if (QDir(hmDir).exists()) {
        QStringList pngFiles = QDir(hmDir).entryList(QStringList() << "*.png", QDir::Files);
        for (const auto& f : pngFiles) {
            QString src = hmDir + "/" + f;
            QString dst = exportDir + "/heightmap_" + f;
            if (QFile::exists(dst)) QFile::remove(dst);
            if (QFile::copy(src, dst)) {
                hmCount++;
                appendLog("Copied heightmap: " + f);
            }
        }
        // Also copy TIFF files
        QStringList tiffFiles = QDir(hmDir).entryList(QStringList() << "*.tif" << "*.tiff", QDir::Files);
        for (const auto& f : tiffFiles) {
            QString src = hmDir + "/" + f;
            QString dst = exportDir + "/heightmap_" + f;
            if (QFile::exists(dst)) QFile::remove(dst);
            if (QFile::copy(src, dst)) {
                hmCount++;
                appendLog("Copied heightmap: " + f);
            }
        }
    }

    // Copy albedo files
    if (QDir(albDir).exists()) {
        QStringList pngFiles = QDir(albDir).entryList(QStringList() << "*.png", QDir::Files);
        for (const auto& f : pngFiles) {
            QString src = albDir + "/" + f;
            QString dst = exportDir + "/albedo_" + f;
            if (QFile::exists(dst)) QFile::remove(dst);
            if (QFile::copy(src, dst)) {
                albCount++;
                appendLog("Copied albedo: " + f);
            }
        }
        QStringList tiffFiles = QDir(albDir).entryList(QStringList() << "*.tif" << "*.tiff", QDir::Files);
        for (const auto& f : tiffFiles) {
            QString src = albDir + "/" + f;
            QString dst = exportDir + "/albedo_" + f;
            if (QFile::exists(dst)) QFile::remove(dst);
            if (QFile::copy(src, dst)) {
                albCount++;
                appendLog("Copied albedo: " + f);
            }
        }
    }

    // Copy manifest if it exists
    QString manifestSrc = terrainDir + "/manifest.json";
    if (QFile::exists(manifestSrc)) {
        QString manifestDst = exportDir + "/manifest.json";
        if (QFile::exists(manifestDst)) QFile::remove(manifestDst);
        QFile::copy(manifestSrc, manifestDst);
        appendLog("Copied terrain manifest");
    }

    if (hmCount > 0) {
        appendLog(QString("Terrain export complete: %1 heightmaps, %2 albedo maps").arg(hmCount).arg(albCount));
        QMessageBox::information(this, "Export Complete",
            QString("Exported %1 heightmap(s) and %2 albedo map(s) to:\n%3")
                .arg(hmCount).arg(albCount).arg(exportDir));
    } else {
        appendLog("Terrain export failed — no terrain data found.");
        QMessageBox::warning(this, "Export Failed",
            "No terrain data found in project.\n"
            "Export terrain in Terrain Studio first.");
    }
}

void Studio3DWidget::onExportRoads() {
    QString o3deProjectPath = m_projectPathEdit->text().trimmed();
    if (o3deProjectPath.isEmpty() || !QDir(o3deProjectPath).exists()) {
        QMessageBox::warning(this, "Project Not Found", "Set a valid O3DE project path first.");
        return;
    }

    QString exportDir = o3deProjectPath + "/Assets/roads";
    QDir().mkpath(exportDir);

    QString outputPath = exportDir + "/road_network.obj";
    appendLog("Exporting road network to: " + outputPath);

    if (exportRoadMesh(outputPath)) {
        appendLog("Road network export complete.");
        QMessageBox::information(this, "Export Complete",
            "Road network exported to:\n" + outputPath);
    } else {
        appendLog("Road export failed — no road data available.");
        QMessageBox::warning(this, "Export Failed",
            "No road network data available.\n"
            "Create roads in Road Studio first.");
    }
}

void Studio3DWidget::onBrowseProject() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select O3DE Project Directory",
        m_projectPathEdit->text());
    if (!dir.isEmpty()) {
        m_projectPathEdit->setText(dir);
        saveSettings();
    }
}

void Studio3DWidget::onBrowseEditor() {
    QString path = QFileDialog::getOpenFileName(this, "Select O3DE Editor Executable",
        m_editorPathEdit->text(), "Executable (*.exe)");
    if (!path.isEmpty()) {
        m_editorPathEdit->setText(path);
        m_launchEditorBtn->setEnabled(true);
        saveSettings();
    }
}

void Studio3DWidget::onProcessStateChanged(QProcess::ProcessState state) {
    switch (state) {
    case QProcess::NotRunning:
        m_statusLabel->setText("O3DE process: Not running");
        m_progressBar->setVisible(false);
        m_launchEditorBtn->setEnabled(true);
        break;
    case QProcess::Starting:
        m_statusLabel->setText("O3DE process: Starting...");
        break;
    case QProcess::Running:
        m_statusLabel->setText("O3DE process: Running");
        m_progressBar->setVisible(false);
        break;
    }
}

void Studio3DWidget::onProcessOutput() {
    QByteArray output = m_o3deProcess->readAllStandardOutput();
    QByteArray error = m_o3deProcess->readAllStandardError();
    if (!output.isEmpty()) {
        appendLog(QString::fromUtf8(output).trimmed());
    }
    if (!error.isEmpty()) {
        appendLog("[stderr] " + QString::fromUtf8(error).trimmed());
    }
}

void Studio3DWidget::onProcessError(QProcess::ProcessError error) {
    QString msg;
    switch (error) {
    case QProcess::FailedToStart: msg = "Failed to start O3DE Editor"; break;
    case QProcess::Crashed: msg = "O3DE Editor crashed"; break;
    case QProcess::Timedout: msg = "O3DE Editor timed out"; break;
    default: msg = "O3DE Editor error"; break;
    }
    appendLog("[ERROR] " + msg);
    m_progressBar->setVisible(false);
    m_launchEditorBtn->setEnabled(true);
}

void Studio3DWidget::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    appendLog(QString("O3DE Editor exited (code=%1, status=%2)")
        .arg(exitCode)
        .arg(exitStatus == QProcess::NormalExit ? "normal" : "crashed"));
    m_progressBar->setVisible(false);
    m_launchEditorBtn->setEnabled(true);
}

bool Studio3DWidget::exportTerrainHeightmap(const QString& outputPath) {
    if (!m_ctx || !m_ctx->projects().hasProject()) return false;

    // Look for exported TIFF files in the project's Terrain folder
    QString terrainDir = m_ctx->projects().current().basePath + "/Terrain";
    QDir dir(terrainDir);
    if (!dir.exists()) return false;

    QStringList tiffFiles = dir.entryList(QStringList() << "*.tif" << "*.tiff", QDir::Files);
    if (tiffFiles.isEmpty()) return false;

    // Copy the first TIFF to the O3DE project assets
    QString srcFile = terrainDir + "/" + tiffFiles.first();
    if (QFile::exists(outputPath)) QFile::remove(outputPath);
    return QFile::copy(srcFile, outputPath);
}

bool Studio3DWidget::exportRoadMesh(const QString& outputPath) {
    if (!m_ctx || !m_ctx->projects().hasProject()) return false;

    // Look for road files in the project's Roads folder
    QString roadDir = m_ctx->projects().current().basePath + "/Roads";
    QDir dir(roadDir);
    if (!dir.exists()) return false;

    // Look for .xodr files (OpenDRIVE) that can be imported into O3DE
    QStringList roadFiles = dir.entryList(QStringList() << "*.xodr" << "*.obj" << "*.fbx", QDir::Files);
    if (roadFiles.isEmpty()) return false;

    QString srcFile = roadDir + "/" + roadFiles.first();
    if (QFile::exists(outputPath)) QFile::remove(outputPath);
    return QFile::copy(srcFile, outputPath);
}
