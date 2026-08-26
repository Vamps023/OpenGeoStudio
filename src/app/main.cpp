// ============================================================
// OpenGeoStudio — Qt Application Entry Point
// ============================================================
//
// Initializes QApplication, applies the global theme, creates the
// ApplicationContext and the AppMainWindow shell, and starts the
// Qt event loop.
//
// The window shell lives in app/AppMainWindow.hpp; dialogs in
// app/SettingsDialog.hpp and app/CommandPalette.hpp. All colors
// come from theme/Theme.hpp — no hex values outside it.
// ============================================================

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImageReader>
#include <QTimer>

#include "../theme/Theme.hpp"
#include "AppMainWindow.hpp"

#include "road_engine.hpp"
#include "core/ApplicationContext.hpp"
#include "core/logger/Logger.hpp"

// Bridge used by AppMainWindow.hpp so it doesn't need the engine headers
namespace ogs::appdetail { const char* roadEngineVersion() { return road_engine::versionString(); } }

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Raise Qt's image allocation limit so large merged terrain heightmaps
    // (e.g. 12288x12288 from a 3x3 tile grid at 4096 resolution) can be
    // loaded. Qt 6 defaults to 256 MB which is too small for production
    // terrain exports. Setting it to 0 disables the limit entirely.
    QImageReader::setAllocationLimit(0);

    // Set PROJ_LIB / PROJ_DATA environment variables so that ALL PROJ
    // contexts (CRSManager, CoordinateTransform, OGRE, etc.) can find proj.db.
    // Check exe-relative paths: <exeDir>/proj/proj.db and <exeDir>/proj.db.
    if (qEnvironmentVariableIsEmpty("PROJ_LIB") &&
        qEnvironmentVariableIsEmpty("PROJ_DATA")) {
        QString exeDir = QCoreApplication::applicationDirPath();
        if (QFile::exists(exeDir + "/proj/proj.db")) {
            qputenv("PROJ_LIB", (exeDir + "/proj").toLocal8Bit());
            qputenv("PROJ_DATA", (exeDir + "/proj").toLocal8Bit());
        } else if (QFile::exists(exeDir + "/proj.db")) {
            qputenv("PROJ_LIB", exeDir.toLocal8Bit());
            qputenv("PROJ_DATA", exeDir.toLocal8Bit());
        }
    }

    // Set application icon (from original Electron app assets)
    app.setWindowIcon(QIcon(":/icons/app.png"));

    // Initialize LaneMaker's Qt resources (shaders, models, icons)
    // Required because lanemaker is a static library — Qt doesn't auto-register
    // resources from static libraries in Qt 6.
    Q_INIT_RESOURCE(shaders);
    Q_INIT_RESOURCE(images);
    app.setApplicationName(QStringLiteral("OpenGeoStudio"));
    app.setApplicationVersion(QStringLiteral(OGS_VERSION));
    app.setOrganizationName(QStringLiteral("OpenGeoStudio"));

    // Enable log file next to the executable for crash diagnosis
    QDir logDir(QCoreApplication::applicationDirPath());
    logDir.mkpath(".");
    QString logPath = logDir.absoluteFilePath("log.txt");
    Logger::addFileTransport(logPath);
    appLog().info("Logging to", logPath);

    // Global dark theme — palette + stylesheet from the single theme source
    app.setPalette(ogs::theme::darkPalette());
    app.setStyleSheet(ogs::theme::appStylesheet());

    // Create application context with all services
    ApplicationContext ctx;

    AppMainWindow window(&ctx);
    window.show();

    // Auto-open project from command line
    if (argc > 1 && QString::fromLocal8Bit(argv[1]).endsWith(".ogproj")) {
        QString projPath = QString::fromLocal8Bit(argv[1]);
        QTimer::singleShot(500, &window, [&window, projPath]() {
            window.openProjectPath(projPath);
            QTimer::singleShot(1000, &window, [&window]() {
                window.activate3DStudio();
            });
        });
    }

    appLog().info("OpenGeoStudio started - Road Engine v",
                  QString::fromLatin1(road_engine::versionString()));

    return app.exec();
}
