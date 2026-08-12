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
};
