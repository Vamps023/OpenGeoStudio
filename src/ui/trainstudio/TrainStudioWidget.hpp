#pragma once

// ============================================================
// TrainStudioWidget — Combined Train Studio workspace
// ============================================================

#include "../../core/ApplicationContext.hpp"
#include "TrainStudioStore.hpp"
#include "TrainViewport2D.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QLabel>
#include <QStatusBar>

class TrainStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit TrainStudioWidget(ApplicationContext* ctx, QWidget* parent = nullptr)
        : QWidget(parent), m_ctx(ctx)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_store = new TrainStudioStore(&m_ctx->events(), this);

        // Toolbar
        m_toolbar = new QToolBar("Train Studio", this);
        m_toolbar->setMovable(false);
        setupToolbar();
        layout->addWidget(m_toolbar);

        // 2D viewport
        m_viewport = new TrainViewport2D(ctx, m_store, this);
        layout->addWidget(m_viewport, 1);

        // Status bar (matching reference: track count, control point count)
        m_statusBar = new QLabel(this);
        m_statusBar->setStyleSheet(
            "QLabel { background: #0d1117; color: #7d8590; padding: 4px 12px;"
            "border-top: 1px solid #30363d; font-size: 12px; }");
        layout->addWidget(m_statusBar);

        // Keyboard hints (bottom-left overlay)
        m_hintsLabel = new QLabel(m_viewport);
        m_hintsLabel->setText("Shift+drag: Pan  |  Scroll: Zoom  |  V: Select  |  L: Line  |  A: Arc  |  Esc: Cancel");
        m_hintsLabel->setStyleSheet(
            "QLabel { background: rgba(13,17,23,200); color: #7d8590; padding: 6px 12px;"
            "border-radius: 6px; font-size: 11px; }");
        m_hintsLabel->move(12, 0);
        m_hintsLabel->raise();

        updateStatusBar();
        connect(m_store, &TrainStudioStore::tracksChanged, this, [this]() { updateStatusBar(); });
    }

    void updateStatusBar() {
        int trackCount = m_store->tracks().size();
        int pointCount = 0;
        for (const auto& t : m_store->tracks()) pointCount += t.points.size();
        m_statusBar->setText(
            QString("Tracks: %1  |  Control Points: %2  |  OpenGeoStudio Train Studio")
                .arg(trackCount).arg(pointCount));
    }

    TrainStudioStore* store() { return m_store; }
    TrainViewport2D* viewport() { return m_viewport; }
    MapViewportWidget* mapWidget() { return m_viewport ? m_viewport->mapWidget() : nullptr; }

private:
    void setupToolbar() {
        // Tools
        auto* toolGroup = new QActionGroup(m_toolbar);

        QAction* selectAct = m_toolbar->addAction("Select (V)");
        selectAct->setShortcut(QKeySequence("V"));
        selectAct->setCheckable(true);
        selectAct->setChecked(true);
        toolGroup->addAction(selectAct);
        connect(selectAct, &QAction::triggered, this, [this]() { m_store->setTool(trains::Tool::Select); });

        QAction* lineAct = m_toolbar->addAction("Line (L)");
        lineAct->setShortcut(QKeySequence("L"));
        lineAct->setCheckable(true);
        toolGroup->addAction(lineAct);
        connect(lineAct, &QAction::triggered, this, [this]() { m_store->setTool(trains::Tool::Line); });

        QAction* arcAct = m_toolbar->addAction("Arc (A)");
        arcAct->setShortcut(QKeySequence("A"));
        arcAct->setCheckable(true);
        toolGroup->addAction(arcAct);
        connect(arcAct, &QAction::triggered, this, [this]() { m_store->setTool(trains::Tool::Arc); });

        m_toolbar->addSeparator();

        // Undo/Redo
        QAction* undoAct = m_toolbar->addAction("Undo");
        undoAct->setShortcut(QKeySequence::Undo);
        connect(undoAct, &QAction::triggered, this, [this]() { m_store->undo(); });
        connect(m_store, &TrainStudioStore::historyChanged, this, [undoAct, this]() {
            undoAct->setEnabled(m_store->canUndo());
        });

        QAction* redoAct = m_toolbar->addAction("Redo");
        redoAct->setShortcut(QKeySequence::Redo);
        connect(redoAct, &QAction::triggered, this, [this]() { m_store->redo(); });
        connect(m_store, &TrainStudioStore::historyChanged, this, [redoAct, this]() {
            redoAct->setEnabled(m_store->canRedo());
        });

        m_toolbar->addSeparator();

        // Delete selected
        QAction* deleteAct = m_toolbar->addAction("Delete Selected");
        deleteAct->setShortcut(QKeySequence::Delete);
        connect(deleteAct, &QAction::triggered, this, [this]() {
            if (!m_store->selection().trackId.isEmpty())
                m_store->deleteTrack(m_store->selection().trackId);
        });

        // Clear all
        QAction* clearAct = m_toolbar->addAction("Clear All");
        connect(clearAct, &QAction::triggered, this, [this]() { m_store->clearAll(); });

        m_toolbar->addSeparator();

        // Export XML
        QAction* exportAct = m_toolbar->addAction("Export XML");
        connect(exportAct, &QAction::triggered, this, [this]() {
            const QString path = QFileDialog::getSaveFileName(
                this, "Export Network XML", "base_network.xml", "XML Files (*.xml)");
            if (path.isEmpty()) return;
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                QTextStream stream(&file);
                stream << m_store->exportNetworkXml();
                file.close();
                QMessageBox::information(this, "Export", "Network XML exported successfully.");
            }
        });

        // Import OSM Railways
        QAction* importOsmAct = m_toolbar->addAction("Import OSM");
        connect(importOsmAct, &QAction::triggered, this, [this]() {
            bool ok = false;
            QString bbox = QInputDialog::getText(
                this, "Import OSM Railways",
                "Bounding box (south,west,north,east):",
                QLineEdit::Normal, "18.4,73.7,18.6,73.9", &ok);
            if (!ok || bbox.isEmpty()) return;

            auto parts = bbox.split(',');
            if (parts.size() != 4) {
                QMessageBox::warning(this, "Import OSM", "Invalid bbox format. Use: south,west,north,east");
                return;
            }

            double south = parts[0].toDouble();
            double west = parts[1].toDouble();
            double north = parts[2].toDouble();
            double east = parts[3].toDouble();

            m_store->importOsmRailways(south, west, north, east);
            QMessageBox::information(this, "Import OSM",
                "Import started. Railways will appear when download completes.");
        });

        connect(m_store, &TrainStudioStore::osmImportFinished, this,
            [this](bool success, const QString& msg) {
                if (success)
                    QMessageBox::information(this, "Import OSM", msg);
                else
                    QMessageBox::warning(this, "Import OSM", "Import failed: " + msg);
            });
    }

    ApplicationContext* m_ctx;
    TrainStudioStore* m_store = nullptr;
    QToolBar* m_toolbar = nullptr;
    TrainViewport2D* m_viewport = nullptr;
    QLabel* m_statusBar = nullptr;
    QLabel* m_hintsLabel = nullptr;
};
