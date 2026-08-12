#pragma once

// ============================================================
// HomeWidget — Home workspace start screen
// ============================================================
//
// Replaces renderer/panels/RecentProjects/RecentProjects.tsx.
// Shows template cards (Terrain, Road Studio) and recent projects list.
//

#include "../../core/ApplicationContext.hpp"

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>

class HomeWidget : public QWidget {
    Q_OBJECT

public:
    explicit HomeWidget(ApplicationContext* ctx, QWidget* parent = nullptr);

signals:
    void newProjectRequested(const QString& templateId);
    void openProjectRequested(const QString& filePath);

private slots:
    void onCreateTerrain();
    void onCreateRoadStudio();
    void onOpenFile();
    void onRecentItemClicked(int row);
    void onSearchChanged(const QString& text);
    void onRecentChanged();

private:
    void setupUi();
    void refreshRecent(const QString& filter = {});

    ApplicationContext* m_ctx;
    QLineEdit* m_search = nullptr;
    QListWidget* m_recentList = nullptr;
    QLabel* m_welcomeLabel = nullptr;
};
