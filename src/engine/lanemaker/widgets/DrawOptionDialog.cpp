#include "DrawOptionDialog.h"

#include "LaneConfigWidget.h"
#include "map_view_gl.h"

#include <QHBoxLayout>
#include <QFrame>

DrawOptionDialog::DrawOptionDialog(QWidget* parent):
    AnimatedPopupDialog(QSize(parent->width() / 3 * 2, parent->height() / 2), true, parent)
{
    // Dark theme for the dialog itself
    setStyleSheet(
        "DrawOptionDialog { background-color: #0d1117; border: 1px solid #30363d; border-radius: 12px; }"
        "QLabel { color: #e6edf3; font-size: 12px; }"
        "QSlider::groove:horizontal { background: #21262d; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #1f6feb; width: 16px; height: 16px;"
        "  margin: -5px 0; border-radius: 8px; }"
        "QSlider::handle:horizontal:hover { background: #388bfd; }"
        "QDial { background: #161b22; border: 1px solid #30363d; border-radius: 4px; }"
        "QDial::handle { background: #1f6feb; width: 8px; border-radius: 4px; }"
        "QDial::handle:hover { background: #388bfd; }");

    auto* mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // Section header — Lane Configuration
    auto* laneHeader = new QLabel("Lane Configuration");
    laneHeader->setStyleSheet(
        "QLabel { color: #7d8590; font-size: 11px; font-weight: bold;"
        "  text-transform: uppercase; letter-spacing: 1px; padding: 2px 0; }");
    mainLayout->addWidget(laneHeader);

    // Lane config widget (vertical layout mode, no profile selector)
    laneConfig = new LaneConfigWidget(true, false);
    mainLayout->addWidget(laneConfig);

    // Separator
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #21262d; background-color: #21262d; max-height: 1px;");
    mainLayout->addWidget(sep);

    // Section header — Elevation
    auto* elevHeader = new QLabel("Road Elevation");
    elevHeader->setStyleSheet(
        "QLabel { color: #7d8590; font-size: 11px; font-weight: bold;"
        "  text-transform: uppercase; letter-spacing: 1px; padding: 2px 0; }");
    mainLayout->addWidget(elevHeader);

    // Elevation dial + display in a horizontal layout
    auto* elevLayout = new QHBoxLayout;
    elevLayout->setSpacing(12);

    heightConfig = new QDial;
    heightConfig->setRange(-6, 6);
    heightConfig->setNotchesVisible(true);
    heightConfig->setFixedSize(80, 80);
    elevLayout->addWidget(heightConfig);

    // Elevation value display
    heightDisplay = new QLabel("G");
    heightDisplay->setStyleSheet(
        "QLabel { color: #e6edf3; font-size: 24px; font-weight: bold;"
        "  background: #161b22; border: 1px solid #30363d; border-radius: 8px;"
        "  padding: 8px 16px; min-width: 40px; }");
    heightDisplay->setAlignment(Qt::AlignCenter);
    elevLayout->addWidget(heightDisplay);

    elevLayout->addStretch();
    mainLayout->addLayout(elevLayout);

    setLayout(mainLayout);

    connect(heightConfig, &QDial::valueChanged, this, [this](int val)
        {
            QString disp = QString::number(val);
            if (val > 0)
            {
                disp = "+" + disp;
            }
            else if (val == 0)
            {
                disp = "G";
            }
            heightDisplay->setText(disp);
        });
}

void DrawOptionDialog::showEvent(QShowEvent* e)
{
    if (g_laneConfig->RoadMode())
    {
        laneConfig->GotoRoadMode();
    }
    else
    {
        laneConfig->GotoLaneMode();
    }
    laneConfig->SetOption(g_laneConfig->LeftResult(), g_laneConfig->RightResult());
    heightConfig->setValue(LM::g_createRoadElevationOption);
    AnimatedPopupDialog::showEvent(e);
}

void DrawOptionDialog::closeEvent(QCloseEvent* e)
{
    g_laneConfig->SetOption(laneConfig->LeftResult(), laneConfig->RightResult());
    LM::g_createRoadElevationOption = heightConfig->value();
    AnimatedPopupDialog::closeEvent(e);
}
