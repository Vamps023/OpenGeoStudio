#pragma once

// ============================================================
// RoadStudioWidget — Embeds LaneMaker's MainWindow directly
// ============================================================

#include <QWidget>
#include <QVBoxLayout>

// LaneMaker's MainWindow — contains the full road editor
#include "main_window.h"

class RoadStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit RoadStudioWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_lmMainWindow = new MainWindow(this);
        layout->addWidget(m_lmMainWindow);
    }

    MainWindow* laneMakerWindow() { return m_lmMainWindow; }

private:
    MainWindow* m_lmMainWindow = nullptr;
};
