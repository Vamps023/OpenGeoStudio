#include "LaneConfigWidget.h"
#include "../../../theme/Theme.hpp"
#include "action_manager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QSettings>
#include <qevent.h>
#include <qpainter.h>

LaneConfigWidget* g_laneConfig;

CrossSectionVisual::CrossSectionVisual():
    rightLogo(":/icons/car_leaving.png"),
    leftLogo(":/icons/car_coming.png")
{
    setMinimumWidth(400);
    setMinimumHeight(50);
    Reset();
}

QSize CrossSectionVisual::sizeHint() const
{
    // Expand horizontally
    return QSize(5000, 50);
}

void CrossSectionVisual::Reset()
{
    SetOption(LM::LanePlan{ 1, 1 }, LM::LanePlan{ -1, 1 });
}

void CrossSectionVisual::SetMode(bool roadMode)
{
    if (!roadMode)
    {
        activeRightSetting.laneCount = std::max((int8_t)1, activeRightSetting.laneCount);
    }
    this->roadMode = roadMode;
    changedExternally = true;
    update();
}

void CrossSectionVisual::showEvent(QShowEvent* event)
{
    emit OptionChangedByUser(activeLeftSetting, activeRightSetting);
    QWidget::showEvent(event);
}

void CrossSectionVisual::SetOption(const LM::LanePlan& l, const LM::LanePlan& r)
{
    const QSignalBlocker blocker(this);
    activeLeftSetting = l;
    activeRightSetting = r;
    if (activeLeftSetting.laneCount == 0)
    {
        activeLeftSetting.offsetx2 = activeRightSetting.offsetx2;
    }
    if (activeRightSetting.laneCount == 0)
    {
        activeRightSetting.offsetx2 = activeLeftSetting.offsetx2;
    }
    changedExternally = true;
    repaint();
}

void CrossSectionVisual::resizeEvent(QResizeEvent* event)
{
    changedExternally = true;
    QWidget::resizeEvent(event);
}

int CrossSectionVisual::LaneAtPos(int x) const
{
    // Check right side lanes (positive direction, forward traffic)
    if (activeRightSetting.laneCount > 0)
    {
        for (int i = 0; i < activeRightSetting.laneCount; ++i)
        {
            int logoCenter = rInnerResult + (i * 2 + 1) * TickInterval;
            int halfLane = TickInterval;
            if (x >= logoCenter - halfLane && x <= logoCenter + halfLane)
                return i + 1; // 1-indexed right lane
        }
    }
    // Check left side lanes (negative direction, backward traffic)
    if (activeLeftSetting.laneCount > 0)
    {
        for (int i = 0; i < activeLeftSetting.laneCount; ++i)
        {
            int logoCenter = lInnerResult - (i * 2 + 1) * TickInterval;
            int halfLane = TickInterval;
            if (x >= logoCenter - halfLane && x <= logoCenter + halfLane)
                return -(i + 1); // 1-indexed left lane (negative)
        }
    }
    return 0; // no lane hit
}

void CrossSectionVisual::paintEvent(QPaintEvent* evt)
{
    QWidget::paintEvent(evt);

    QRect rect = evt->rect();
    QPainter painter(this);

    if (railMode)
    {
        paintRailSection(painter, rect);
    }
    else
    {
        paintRoadSection(painter, rect);
    }
}

void CrossSectionVisual::paintRoadSection(QPainter& painter, const QRect& rect)
{
    const int RectGap = 2;

    YCenter = rect.y() + rect.height() / 2;
    XLeft = rect.x() + rect.width() / 20;
    XRight = rect.x() + rect.width() / 20 * 19;
    XCenter = rect.x() + rect.width() / 2;
    // Draw background — dark theme
    painter.setBrush(ogs::theme::c::qBase());
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, 0, rect.width(), rect.height());
    // Draw ruler — subtle gray
    TickInterval = static_cast<float>(XRight - XLeft) / (2 * SingleSideLaneLimit);
    QPen colorPen(ogs::theme::c::qBorder());
    painter.setPen(colorPen);
    painter.drawLine(XCenter - SingleSideLaneLimit * TickInterval, YCenter,
                     XCenter + SingleSideLaneLimit * TickInterval, YCenter);
    // Draw ticks — subtle
    const int TickHeight = rect.height() / 2;
    colorPen.setColor(ogs::theme::c::qBorderSub());
    painter.setPen(colorPen);
    for (int tick = -SingleSideLaneLimit; tick <= SingleSideLaneLimit; ++tick)
    {
        int TickX = XCenter + TickInterval * tick;
        painter.drawLine(TickX, YCenter - TickHeight / 2, TickX, YCenter + TickHeight / 2);
    }

    auto lOffsetx2 = activeLeftSetting.offsetx2;
    auto rOffsetx2 = activeRightSetting.offsetx2;

    lOuterResult = XCenter - static_cast<float>(lOffsetx2 + activeLeftSetting.laneCount * 2) * TickInterval;
    lInnerResult = XCenter - static_cast<float>(lOffsetx2) * TickInterval;
    rOuterResult = XCenter + static_cast<float>(-rOffsetx2 + activeRightSetting.laneCount * 2) * TickInterval;
    rInnerResult = XCenter + static_cast<float>(-rOffsetx2) * TickInterval;

    if (changedExternally)
    {
        handleX[0] = lOuterResult;
        handleX[1] = lInnerResult;
        handleX[2] = rInnerResult;
        handleX[3] = rOuterResult;
        changedExternally = false;
    }

    // Draw result — left lanes in red, right lanes in green (brighter for dark theme)
    colorPen.setWidth(3);
    if (activeLeftSetting.laneCount != 0)
    {
        colorPen.setColor(QColor(248, 81, 73));  // bright red
        painter.setPen(colorPen);
        painter.drawLine(lOuterResult, YCenter, lInnerResult, YCenter);
        for (int i = 0; i != activeLeftSetting.laneCount; ++i)
        {
            int logoCenter = lInnerResult - (i * 2 + 1) * TickInterval;
            QRectF rect(logoCenter - TickHeight / 2, YCenter - TickHeight, TickHeight, TickHeight);
            painter.drawImage(rect, leftLogo);
            // Highlight selected left lane
            if (selectedLane == -(i + 1))
            {
                painter.setBrush(QColor(31, 111, 235, 80)); // semi-transparent blue
                painter.setPen(QPen(QColor(31, 111, 235), 2));
                QRectF hlRect(logoCenter - TickInterval, YCenter - TickHeight, TickInterval * 2, TickHeight * 2);
                painter.drawRoundedRect(hlRect, 4, 4);
                painter.setPen(colorPen);
                painter.setBrush(Qt::NoBrush);
            }
        }
    }

    if (activeRightSetting.laneCount != 0)
    {
        colorPen.setColor(QColor(63, 185, 80));  // bright green
        painter.setPen(colorPen);
        painter.drawLine(rOuterResult, YCenter, rInnerResult, YCenter);
        for (int i = 0; i != activeRightSetting.laneCount; ++i)
        {
            int logoCenter = rInnerResult + (i * 2 + 1) * TickInterval;
            QRectF rect(logoCenter - TickHeight / 2, YCenter - TickHeight, TickHeight, TickHeight);
            painter.drawImage(rect, rightLogo);
            // Highlight selected right lane
            if (selectedLane == (i + 1))
            {
                painter.setBrush(QColor(31, 111, 235, 80)); // semi-transparent blue
                painter.setPen(QPen(QColor(31, 111, 235), 2));
                QRectF hlRect(logoCenter - TickInterval, YCenter - TickHeight, TickInterval * 2, TickHeight * 2);
                painter.drawRoundedRect(hlRect, 4, 4);
                painter.setPen(colorPen);
                painter.setBrush(Qt::NoBrush);
            }
        }
    }

    // Draw Handles — dark theme: light gray handles, blue when dragging
    colorPen.setWidth(5);
    colorPen.setColor(QColor(ogs::theme::c::TextSoft));
    painter.setPen(colorPen);

    for (int i = 0; i != handleX.size(); ++i)
    {
        if ((i == 0 || i == 1) && handleX[0] == handleX[1]) continue;
        if ((i == 2 || i == 3) && handleX[2] == handleX[3]) continue;

        auto handle = handleX[i];
        colorPen.setColor(dragIndex.empty() ||
            std::find(dragIndex.begin(), dragIndex.end(), i) == dragIndex.end() ?
            QColor(139, 148, 158) : QColor(31, 111, 235));  // gray : blue
        painter.setPen(colorPen);
        painter.drawLine(handle, YCenter - TickHeight / 2, handle, YCenter + TickHeight / 2);
    }
}

void CrossSectionVisual::paintRailSection(QPainter& painter, const QRect& rect)
{
    YCenter = rect.y() + rect.height() / 2;
    XLeft = rect.x() + 20;
    XRight = rect.x() + rect.width() - 20;
    XCenter = rect.x() + rect.width() / 2;

    // Scale: pixels per meter
    // Show up to ~20m width
    const double totalWidthM = 20.0;
    const double ppm = static_cast<double>(XRight - XLeft) / totalWidthM;
    TickInterval = static_cast<float>(ppm);

    // Draw dark background
    painter.setBrush(ogs::theme::c::qBase());
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, 0, rect.width(), rect.height());

    // Draw ballast background (dark brown/gravel for dark theme)
    double ballastWidthM = railTrackCount * railTrackSpacing + 1.0;
    int ballastLeft = XCenter - static_cast<int>(ballastWidthM * ppm / 2);
    int ballastRight = XCenter + static_cast<int>(ballastWidthM * ppm / 2);
    int ballastHeight = rect.height() * 2 / 3;
    painter.setBrush(QColor(60, 50, 40));  // dark brown ballast
    painter.setPen(Qt::NoPen);
    painter.drawRect(ballastLeft, YCenter - ballastHeight / 2,
                     ballastRight - ballastLeft, ballastHeight);

    // Draw ruler line — subtle gray
    QPen colorPen(ogs::theme::c::qBorder());
    painter.setPen(colorPen);
    painter.drawLine(XLeft, YCenter + ballastHeight / 2 + 2,
                     XRight, YCenter + ballastHeight / 2 + 2);

    // Draw each track
    const int railHeight = ballastHeight * 2 / 3;
    const int gaugePx = static_cast<int>(railGauge * ppm);
    const int spacingPx = static_cast<int>(railTrackSpacing * ppm);

    // Center the track group
    int firstTrackCenter;
    if (railTrackCount == 1)
        firstTrackCenter = XCenter;
    else
        firstTrackCenter = XCenter - ((railTrackCount - 1) * spacingPx) / 2;

    for (int t = 0; t < railTrackCount; t++)
    {
        int trackCenter = firstTrackCenter + t * spacingPx;
        int railLeft = trackCenter - gaugePx / 2;
        int railRight = trackCenter + gaugePx / 2;

        // Draw sleepers (ties) — short perpendicular lines
        colorPen.setColor(QColor(80, 60, 40));
        colorPen.setWidth(2);
        painter.setPen(colorPen);
        int sleeperLen = gaugePx + static_cast<int>(0.5 * ppm);  // gauge + 0.5m
        int sleeperStart = railLeft - (sleeperLen - gaugePx) / 2;
        int numSleepers = 12;
        for (int s = 0; s < numSleepers; s++)
        {
            int sx = ballastLeft + 5 + s * (ballastRight - ballastLeft - 10) / (numSleepers - 1);
            painter.drawLine(sx, YCenter - sleeperLen / 2,
                            sx, YCenter + sleeperLen / 2);
        }

        // Draw rails (two steel-colored bars)
        colorPen.setColor(QColor(180, 180, 190));  // steel gray
        colorPen.setWidth(3);
        painter.setPen(colorPen);
        painter.drawLine(railLeft, YCenter - railHeight / 2,
                        railLeft, YCenter + railHeight / 2);
        painter.drawLine(railRight, YCenter - railHeight / 2,
                        railRight, YCenter + railHeight / 2);

        // Draw rail head (thicker top)
        colorPen.setWidth(4);
        colorPen.setColor(QColor(200, 200, 210));
        painter.setPen(colorPen);
        painter.drawLine(railLeft - 1, YCenter - railHeight / 2,
                        railLeft + 1, YCenter - railHeight / 2);
        painter.drawLine(railRight - 1, YCenter - railHeight / 2,
                        railRight + 1, YCenter - railHeight / 2);
    }

    // Draw gauge label — light text for dark theme
    colorPen.setColor(ogs::theme::c::qText());
    colorPen.setWidth(1);
    painter.setPen(colorPen);
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    QString gaugeLabel = QString("%1mm  %2T").arg(int(railGauge * 1000)).arg(railTrackCount);
    painter.drawText(QPoint(XCenter - 40, YCenter + ballastHeight / 2 + 15), gaugeLabel);
}

void CrossSectionVisual::SetRailMode(bool rail)
{
    railMode = rail;
    if (rail) roadMode = true;  // rail mode uses road mode's lane plan internally
    changedExternally = true;
    update();
}

void CrossSectionVisual::SetRailProfile(int trackCount, double gauge, double trackSpacing)
{
    railTrackCount = std::max(1, std::min(trackCount, 4));
    railGauge = gauge;
    railTrackSpacing = trackSpacing;
    changedExternally = true;
    update();
    emit RailProfileChanged(railTrackCount, railGauge, railTrackSpacing);
}

void CrossSectionVisual::mousePressEvent(QMouseEvent* evt)
{
    if (evt->button() == Qt::MouseButton::LeftButton)
    {
        // First, try lane selection (clicking on a car icon)
        int lane = LaneAtPos(evt->x());
        if (lane != 0)
        {
            selectedLane = (selectedLane == lane) ? 0 : lane; // toggle selection
            update();
            emit LaneSelected(selectedLane);
            return;
        }

        // Otherwise, handle drag handles (existing behavior)
        if (lOuterResult < evt->x() && evt->x() < lInnerResult)
        {
            std::sort(handleX.begin(), handleX.end());
            dragIndex.push_back(0);
            dragIndex.push_back(1);
            handleOrigin.push_back(handleX[0]);
            handleOrigin.push_back(handleX[1]);
            dragOrigin = evt->x();
            dragLimit = std::abs(handleX[1] - handleX[2]);
            update();
            return;
        }

        if (rInnerResult < evt->x() && evt->x() < rOuterResult)
        {
            dragIndex.push_back(2);
            dragIndex.push_back(3);
            handleOrigin.push_back(handleX[2]);
            handleOrigin.push_back(handleX[3]);
            dragOrigin = evt->x();
            dragLimit = std::abs(handleX[1] - handleX[2]);

            update();
            return;
        }
    }
}

void CrossSectionVisual::mouseMoveEvent(QMouseEvent* evt)
{
    if (dragIndex.empty()) return;

    bool draggingLeft = dragIndex[0] == 0;
    int dragDelta = evt->localPos().x() - dragOrigin;
    if (draggingLeft && handleX[2] != handleX[3])
    {
        dragDelta = std::min(dragDelta, dragLimit);
    }
    else if (!draggingLeft & handleX[0] != handleX[1])
    {
        dragDelta = std::max(dragDelta, -dragLimit);
    }
    if (dragDelta == 0) return;

    for (int i = 0; i != dragIndex.size(); ++i)
    {
        int dragX = handleOrigin[i] + dragDelta;
        dragX = std::max(XLeft, dragX);
        dragX = std::min(XRight - 1, dragX);
        int tickAt = std::round((dragX - XCenter) / TickInterval);
        dragX = tickAt * TickInterval + XCenter;
        handleX[dragIndex[i]] = dragX;
    }

    activeLeftSetting.offsetx2 = std::round(static_cast<float>(XCenter - handleX[1]) / TickInterval);
    activeLeftSetting.laneCount = std::floor(static_cast<int>(std::round(static_cast<float>(handleX[1] - handleX[0]) / TickInterval)) / 2);

    activeRightSetting.offsetx2 = std::round(static_cast<float>(XCenter - handleX[2]) / TickInterval);
    activeRightSetting.laneCount = std::floor(static_cast<int>(std::round(static_cast<float>(handleX[3] - handleX[2]) / TickInterval)) / 2);

    update();
}

void CrossSectionVisual::mouseReleaseEvent(QMouseEvent* evt)
{
    if (evt->button() == Qt::MouseButton::LeftButton)
    {
        // Quit drag
        dragIndex.clear();
        handleOrigin.clear();
        update();

        emit OptionChangedByUser(activeLeftSetting, activeRightSetting);
    }
}

LaneConfigWidget::LaneConfigWidget(bool verticalLayout, bool showProfileSelector):
    visual(new CrossSectionVisual),
    leftMinus(new QToolButton), leftPlus(new QToolButton),
    rightMinus(new QToolButton), rightPlus(new QToolButton),
    swapDirectionButton(new QToolButton),
    flipLaneButton(new QToolButton),
    laneWidthSpinner(new QDoubleSpinBox),
    profileCombo(new QComboBox),
    speedLimitSpinner(new QDoubleSpinBox),
    sidewalkCheck(new QCheckBox("Sidewalk")),
    curbCheck(new QCheckBox("Curb")),
    modifiedLabel(new QLabel),
    resetButton(new QPushButton("Reset")),
    savePresetButton(new QPushButton("Save as Preset")),
    hasProfileSelector(showProfileSelector),
    incLogo(":/icons/add.png"), decLogo(":/icons/minus.png")
{
    setMinimumWidth(550);

    // Dark theme for the entire LaneConfigWidget — Cross-Section Studio
    setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("LaneConfigWidget { background-color: %BgBase%; border: 1px solid %BorderSub%; border-radius: 8px; }"
        "QLabel { color: %TextMuted%; font-size: 11px; }"
        "QGroupBox { color: %TextMuted%; font-size: 11px; font-weight: bold; "
        "  border: 1px solid %BorderSub%; border-radius: 6px; margin-top: 10px; padding-top: 6px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QToolButton { background: %BgSurface%; border: 1px solid %Border%; border-radius: 6px; "
        "  padding: 4px; margin: 1px; }"
        "QToolButton:hover { background: %BorderSub%; border-color: %Accent%; }"
        "QToolButton:pressed { background: %BgBase%; }"
        "QToolButton:disabled { color: %TextFaint%; border-color: %BorderSub%; background: %BgBase%; }"
        "QDoubleSpinBox { background: %BgSurface%; border: 1px solid %Border%; border-radius: 6px;"
        "  padding: 4px; color: %Text%; font-size: 12px; }"
        "QDoubleSpinBox:hover { border-color: %Accent%; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  background: %BorderSub%; border: none; border-radius: 3px; width: 16px; }"
        "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {"
        "  background: %Border%; }"
        "QComboBox { background: %BgSurface%; border: 1px solid %Border%; border-radius: 6px;"
        "  padding: 4px 8px; color: %Text%; font-size: 12px; }"
        "QComboBox:hover { border-color: %Accent%; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: %BgSurface%; border: 1px solid %BorderSub%;"
        "  selection-background-color: %Accent%; color: %Text%; }"
        "QCheckBox { color: %Text%; font-size: 11px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px;"
        "  border: 1px solid %Border%; background: %BgSurface%; }"
        "QCheckBox::indicator:checked { background: %Accent%; border-color: %Accent%; }"
        "QPushButton { background: %BgSurface%; border: 1px solid %Border%; border-radius: 6px;"
        "  padding: 4px 10px; color: %Text%; font-size: 11px; }"
        "QPushButton:hover { background: %BorderSub%; border-color: %Accent%; }"
        "QPushButton:pressed { background: %BgBase%; }"
        "QPushButton:disabled { color: %TextFaint%; border-color: %BorderSub%; }")));

    int size = style()->pixelMetric(QStyle::PM_ToolBarIconSize);
    if (verticalLayout) size *= 2;
    QSize iconSize(size, size);
    leftMinus->setIcon(decLogo);
    leftMinus->setIconSize(iconSize);
    leftMinus->setToolTip("Remove left lane");
    leftPlus->setIcon(incLogo);
    leftPlus->setIconSize(iconSize);
    leftPlus->setToolTip("Add left lane");
    rightMinus->setIcon(decLogo);
    rightMinus->setIconSize(iconSize);
    rightMinus->setToolTip("Remove right lane");
    rightPlus->setIcon(incLogo);
    rightPlus->setIconSize(iconSize);
    rightPlus->setToolTip("Add right lane");

    // Swap Direction button — swaps left/right lane plans to reverse traffic direction
    swapDirectionButton->setIcon(QIcon(":/rs/svg/swap_direction.svg"));
    swapDirectionButton->setIconSize(iconSize);
    swapDirectionButton->setToolTip("Swap all lane directions (reverse traffic flow)");

    // Flip Lane button — flips the direction of the selected individual lane
    flipLaneButton->setIcon(QIcon(":/rs/svg/flip_lane.svg"));
    flipLaneButton->setIconSize(iconSize);
    flipLaneButton->setToolTip("Flip selected lane direction (move lane to opposite side)");
    flipLaneButton->setEnabled(false); // disabled until a lane is selected

    // Lane width spinner — configurable (1.5-5.0m, step 0.25m)
    laneWidthSpinner->setRange(1.5, 5.0);
    laneWidthSpinner->setSingleStep(0.25);
    laneWidthSpinner->setValue(LM::LaneWidth);
    laneWidthSpinner->setSuffix("m");
    laneWidthSpinner->setToolTip("Lane width in meters");
    laneWidthSpinner->setMinimumWidth(95); // "3.25 m" + spin buttons must fit

    // Speed limit spinner (20-150 km/h)
    speedLimitSpinner->setRange(20, 150);
    speedLimitSpinner->setSingleStep(10);
    speedLimitSpinner->setValue(50);
    speedLimitSpinner->setSuffix(" km/h");
    speedLimitSpinner->setToolTip("Road speed limit");
    speedLimitSpinner->setMinimumWidth(120); // "50.00 km/h" + spin buttons must fit

    // Profile combo — populated later by PopulateRoadProfiles/PopulateRailProfiles
    profileCombo->setToolTip("Select a road profile preset — sets lanes, width, speed, sidewalk, curb");
    profileCombo->setMinimumWidth(200);

    // Modified indicator — subtle, only visible when modified
    modifiedLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %Warning%; font-size: 10px; font-style: italic;")));
    modifiedLabel->hide();

    // Reset / Save buttons
    resetButton->setToolTip("Reset cross-section to the selected profile preset");
    resetButton->setEnabled(false);
    savePresetButton->setToolTip("Save the current cross-section as a new custom profile");
    savePresetButton->setEnabled(false);

    visual->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    QBoxLayout* layout;
    if (verticalLayout)
    {
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);

        // ── Profile section (only when this instance owns the presets;
        // the DrawOptionDialog instance doesn't — dead empty combo otherwise) ──
        if (hasProfileSelector)
        {
            auto* profileRow = new QHBoxLayout;
            profileRow->setSpacing(4);
            auto* pLabel = new QLabel("Preset:");
            pLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px; font-weight: bold;")));
            profileRow->addWidget(pLabel);
            profileRow->addWidget(profileCombo, 1);
            layout->addLayout(profileRow);

            // Modified indicator
            layout->addWidget(modifiedLabel);
        }

        // ── Cross-section visual ──
        layout->addWidget(visual, 1, Qt::AlignHCenter);
        visual->setMinimumHeight(100);

        // ── Lane controls row ──
        QHBoxLayout* bottomLayout = new QHBoxLayout;
        bottomLayout->setSpacing(4);
        bottomLayout->addWidget(leftMinus);
        bottomLayout->addWidget(leftPlus);
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(flipLaneButton);
        bottomLayout->addWidget(swapDirectionButton);
        bottomLayout->addStretch(1);
        auto* wLabel = new QLabel("Lane W:");
        wLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
        bottomLayout->addWidget(wLabel);
        bottomLayout->addWidget(laneWidthSpinner);
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(rightMinus);
        bottomLayout->addWidget(rightPlus);
        layout->addLayout(bottomLayout);

        // ── Road metadata row ──
        auto* metaRow = new QHBoxLayout;
        metaRow->setSpacing(8);
        auto* sLabel = new QLabel("Speed:");
        sLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
        metaRow->addWidget(sLabel);
        metaRow->addWidget(speedLimitSpinner);
        metaRow->addSpacing(8);
        metaRow->addWidget(sidewalkCheck);
        metaRow->addWidget(curbCheck);
        metaRow->addStretch(1);
        layout->addLayout(metaRow);

        // ── Reset / Save row (preset operations — need a profile selector) ──
        if (hasProfileSelector)
        {
            auto* actionRow = new QHBoxLayout;
            actionRow->setSpacing(4);
            actionRow->addStretch(1);
            actionRow->addWidget(resetButton);
            actionRow->addWidget(savePresetButton);
            layout->addLayout(actionRow);
        }
    }
    else
    {
        // Horizontal (compact) layout — used in toolbar mode
        layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);
        layout->addWidget(leftMinus);
        layout->addWidget(leftPlus);
        layout->addWidget(visual);
        layout->addWidget(flipLaneButton);
        layout->addWidget(swapDirectionButton);
        auto* wLabel = new QLabel("W:");
        wLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
        layout->addWidget(wLabel);
        layout->addWidget(laneWidthSpinner);
        layout->addWidget(rightMinus);
        layout->addWidget(rightPlus);
        // In horizontal mode, hide the profile/metadata sections (they're in the sidebar)
        profileCombo->hide();
        speedLimitSpinner->hide();
        sidewalkCheck->hide();
        curbCheck->hide();
        modifiedLabel->hide();
        resetButton->hide();
        savePresetButton->hide();
    }
    setLayout(layout);

    // Update LaneWidth when spinner changes
    connect(laneWidthSpinner, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LaneConfigWidget::OnLaneWidthChanged);

    // Profile combo — load preset when selection changes
    connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LaneConfigWidget::OnProfileComboChanged);

    // Speed/sidewalk/curb changes → mark modified + emit signal
    connect(speedLimitSpinner, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double) {
        if (!applyingProfile) { CheckModified(); emit RoadMetadataChanged(speedLimitSpinner->value(), sidewalkCheck->isChecked(), curbCheck->isChecked()); }
    });
    connect(sidewalkCheck, &QCheckBox::toggled, [this](bool) {
        if (!applyingProfile) { CheckModified(); emit RoadMetadataChanged(speedLimitSpinner->value(), sidewalkCheck->isChecked(), curbCheck->isChecked()); }
    });
    connect(curbCheck, &QCheckBox::toggled, [this](bool) {
        if (!applyingProfile) { CheckModified(); emit RoadMetadataChanged(speedLimitSpinner->value(), sidewalkCheck->isChecked(), curbCheck->isChecked()); }
    });

    // Reset / Save buttons
    connect(resetButton, &QPushButton::clicked, this, &LaneConfigWidget::OnResetToProfile);
    connect(savePresetButton, &QPushButton::clicked, this, &LaneConfigWidget::OnSaveAsPreset);

    connect(visual, &CrossSectionVisual::OptionChangedByUser, this, &LaneConfigWidget::OnOptionChange);
    connect(leftMinus, &QAbstractButton::clicked, [this]()
        {
            auto lPlan = LeftResult();
            const auto rPlan = RightResult();
            if (lPlan.laneCount > 1 || lPlan.laneCount == 1 && rPlan.laneCount != 0)
            {
                lPlan.laneCount--;
                SetOption(lPlan, RightResult());
            }
        });
    
    connect(leftPlus, &QAbstractButton::clicked, [this]()
        {
            auto lPlan = LeftResult();
            const auto rPlan = RightResult();
            if (lPlan.laneCount < 8)
            {
                if (lPlan.laneCount == 0)
                    lPlan.offsetx2 = std::max((int8_t)0, rPlan.offsetx2);
                lPlan.laneCount++;
                SetOption(lPlan, RightResult());
            }
        });
    connect(rightMinus, &QAbstractButton::clicked, [this]()
        {
            auto rPlan = RightResult();
            const auto lPlan = LeftResult();
            if (rPlan.laneCount > 1 || rPlan.laneCount == 1 && lPlan.laneCount != 0 && visual->IsRoadMode())
            {
                rPlan.laneCount--;
                SetOption(LeftResult(), rPlan);
            }
        });
    connect(rightPlus, &QAbstractButton::clicked, [this]()
        {
            auto rPlan = RightResult();
            const auto lPlan = LeftResult();
            if (rPlan.laneCount < 8)
            {
                if (rPlan.laneCount == 0)
                    rPlan.offsetx2 = std::min((int8_t)0, lPlan.offsetx2);
                rPlan.laneCount++;
                SetOption(LeftResult(), rPlan);
            }
        });

    // Swap Direction — reverses traffic flow by swapping left/right lane plans
    connect(swapDirectionButton, &QAbstractButton::clicked, [this]()
        {
            auto lPlan = LeftResult();
            auto rPlan = RightResult();
            // Swap: left becomes right and vice versa
            // Negate offsetx2 because the side reference flips
            LM::LanePlan newLeft, newRight;
            newLeft.laneCount = rPlan.laneCount;
            newLeft.offsetx2 = -rPlan.offsetx2;
            newRight.laneCount = lPlan.laneCount;
            newRight.offsetx2 = -lPlan.offsetx2;
            SetOption(newLeft, newRight);
            visual->selectedLane = 0;
            flipLaneButton->setEnabled(false);
        });

    // Enable/disable flip button based on lane selection
    connect(visual, &CrossSectionVisual::LaneSelected, this,
        [this](int laneIndex) {
            flipLaneButton->setEnabled(laneIndex != 0);
        });

    // Flip selected lane — moves one lane from one side to the other
    connect(flipLaneButton, &QAbstractButton::clicked, [this]()
        {
            int sel = visual->selectedLane;
            if (sel == 0) return;

            auto lPlan = LeftResult();
            auto rPlan = RightResult();

            if (sel > 0)
            {
                // Right lane selected → move to left side
                if (rPlan.laneCount <= 0) return;
                rPlan.laneCount--;
                if (rPlan.laneCount == 0)
                    lPlan.offsetx2 = rPlan.offsetx2;
                if (lPlan.laneCount == 0)
                    lPlan.offsetx2 = -rPlan.offsetx2;
                lPlan.laneCount++;
            }
            else
            {
                // Left lane selected → move to right side
                if (lPlan.laneCount <= 0) return;
                lPlan.laneCount--;
                if (lPlan.laneCount == 0)
                    rPlan.offsetx2 = lPlan.offsetx2;
                if (rPlan.laneCount == 0)
                    rPlan.offsetx2 = -lPlan.offsetx2;
                rPlan.laneCount++;
            }

            SetOption(lPlan, rPlan);
            visual->selectedLane = 0;
            flipLaneButton->setEnabled(false);
        });
}

void LaneConfigWidget::Reset()
{
    visual->Reset();
}

void LaneConfigWidget::OnOptionChange(LM::LanePlan left, LM::LanePlan right)
{
    LM::ActionManager::Instance()->Record(left, right);
    CheckModified();
}

void LaneConfigWidget::SetOption(const LM::LanePlan& l, const LM::LanePlan& r)
{
    if (LeftResult() == l && RightResult() == r)
    {
        return;
    }
    visual->SetOption(l, r);
    OnOptionChange(l, r);
}

QSize LaneConfigWidget::sizeHint() const
{
    return QSize(500, 60);
}

void LaneConfigWidget::GotoRoadMode()
{
    show();
    bool wasRail = visual->IsRailMode();
    visual->SetRailMode(false);
    visual->SetMode(true);
    // Populate road profiles only if we have a profile selector and need to
    if (hasProfileSelector && (profileCombo->count() == 0 || wasRail))
        PopulateRoadProfiles();
}

void LaneConfigWidget::SetRoadModeOnly()
{
    bool wasRail = visual->IsRailMode();
    visual->SetRailMode(false);
    visual->SetMode(true);
    if (hasProfileSelector && (profileCombo->count() == 0 || wasRail))
        PopulateRoadProfiles();
}

void LaneConfigWidget::SetRailModeOnly()
{
    bool wasRoad = !visual->IsRailMode();
    visual->SetRailMode(true);
    visual->SetMode(true);
    if (hasProfileSelector && (profileCombo->count() == 0 || wasRoad))
        PopulateRailProfiles();
}

void LaneConfigWidget::GotoLaneMode()
{
    show();
    visual->SetRailMode(false);
    visual->SetMode(false);
}

void LaneConfigWidget::GotoRailMode()
{
    show();
    bool wasRoad = !visual->IsRailMode();
    visual->SetRailMode(true);
    visual->SetMode(true);
    // Populate rail profiles only if we have a profile selector and need to
    if (hasProfileSelector && (profileCombo->count() == 0 || wasRoad))
        PopulateRailProfiles();
}

void LaneConfigWidget::SetRailProfile(int trackCount, double gauge, double trackSpacing)
{
    visual->SetRailProfile(trackCount, gauge, trackSpacing);
}

// ── Cross-Section Studio: profile integration ──────────────────────

void LaneConfigWidget::PopulateRoadProfiles()
{
    {
        QSignalBlocker blocker(profileCombo);
        profileCombo->clear();
        auto profiles = roads::RoadProfileCatalog::all();
        for (auto it = profiles.begin(); it != profiles.end(); ++it)
            profileCombo->addItem(it.key(), it.key());
        profileCombo->setCurrentText("city_2x1");
        profileCombo->setToolTip("Select a road profile preset");
    }
    // Load the default profile to sync lane config + metadata (signals unblocked)
    LoadProfile("city_2x1");
}

void LaneConfigWidget::PopulateRailProfiles()
{
    {
        QSignalBlocker blocker(profileCombo);
        profileCombo->clear();
        auto profiles = roads::RailProfileCatalog::all();
        for (auto it = profiles.begin(); it != profiles.end(); ++it)
            profileCombo->addItem(it.key(), it.key());
        profileCombo->setCurrentText("single_standard");
        profileCombo->setToolTip("Select a rail profile preset");
    }
    // Load the default rail profile (signals unblocked)
    LoadProfile("single_standard");
}

void LaneConfigWidget::LoadProfile(const QString& key)
{
    applyingProfile = true;

    if (visual->IsRailMode())
    {
        roads::RailProfile rp = roads::RailProfileCatalog::get(key);
        LM::LaneWidth = rp.gauge + 0.5;

        LM::LanePlan leftPlan, rightPlan;
        if (rp.trackCount == 1)
        {
            leftPlan.laneCount = 0; leftPlan.offsetx2 = 0;
            rightPlan.laneCount = 1; rightPlan.offsetx2 = 0;
        }
        else
        {
            int rightLanes = (rp.trackCount + 1) / 2;
            int leftLanes = rp.trackCount - rightLanes;
            leftPlan.laneCount = static_cast<int8_t>(leftLanes); leftPlan.offsetx2 = 0;
            rightPlan.laneCount = static_cast<int8_t>(rightLanes); rightPlan.offsetx2 = 0;
        }
        SetOption(leftPlan, rightPlan);
        SetRailProfile(rp.trackCount, rp.gauge, rp.trackSpacing);

        auto* spinner = findChild<QDoubleSpinBox*>();
        if (spinner) { QSignalBlocker s(spinner); spinner->setValue(LM::LaneWidth); }
    }
    else
    {
        loadedProfile = roads::RoadProfileCatalog::get(key);

        LM::LaneWidth = loadedProfile.laneWidth;

        LM::LanePlan leftPlan, rightPlan;
        leftPlan.laneCount = static_cast<int8_t>(loadedProfile.leftLanes);
        leftPlan.offsetx2 = static_cast<int8_t>(loadedProfile.leftOffsetX2);
        rightPlan.laneCount = static_cast<int8_t>(loadedProfile.rightLanes);
        rightPlan.offsetx2 = static_cast<int8_t>(loadedProfile.rightOffsetX2);
        SetOption(leftPlan, rightPlan);

        // Update metadata fields
        QSignalBlocker s1(speedLimitSpinner), s2(sidewalkCheck), s3(curbCheck), s4(laneWidthSpinner);
        speedLimitSpinner->setValue(loadedProfile.speedLimit);
        sidewalkCheck->setChecked(loadedProfile.hasSidewalk);
        curbCheck->setChecked(loadedProfile.hasCurb);
        laneWidthSpinner->setValue(loadedProfile.laneWidth);
    }

    currentProfileKey = key;
    modifiedFromProfile = false;
    modifiedLabel->hide();
    resetButton->setEnabled(false);
    savePresetButton->setEnabled(false);

    applyingProfile = false;
    emit ProfileChanged(key);
}

void LaneConfigWidget::OnProfileComboChanged(int index)
{
    if (index < 0) return;
    QString key = profileCombo->itemData(index).toString();
    LoadProfile(key);
}

void LaneConfigWidget::OnLaneWidthChanged(double val)
{
    LM::LaneWidth = val;
    if (!applyingProfile) CheckModified();
}

void LaneConfigWidget::CheckModified()
{
    if (applyingProfile || currentProfileKey.isEmpty()) return;

    bool modified = false;

    // Compare lane config
    auto lPlan = LeftResult();
    auto rPlan = RightResult();

    if (lPlan.laneCount != loadedProfile.leftLanes ||
        rPlan.laneCount != loadedProfile.rightLanes ||
        lPlan.offsetx2 != loadedProfile.leftOffsetX2 ||
        rPlan.offsetx2 != loadedProfile.rightOffsetX2)
        modified = true;

    if (std::abs(LM::LaneWidth - loadedProfile.laneWidth) > 0.001) modified = true;
    if (std::abs(speedLimitSpinner->value() - loadedProfile.speedLimit) > 0.01) modified = true;
    if (sidewalkCheck->isChecked() != loadedProfile.hasSidewalk) modified = true;
    if (curbCheck->isChecked() != loadedProfile.hasCurb) modified = true;

    if (modified != modifiedFromProfile)
    {
        modifiedFromProfile = modified;
        if (modified)
        {
            modifiedLabel->setText(QString("  ✎ Modified from \"%1\"").arg(currentProfileKey));
            modifiedLabel->show();
            resetButton->setEnabled(true);
            savePresetButton->setEnabled(true);
        }
        else
        {
            modifiedLabel->hide();
            resetButton->setEnabled(false);
            savePresetButton->setEnabled(false);
        }
    }
}

void LaneConfigWidget::OnResetToProfile()
{
    if (currentProfileKey.isEmpty()) return;
    LoadProfile(currentProfileKey);
}

void LaneConfigWidget::OnSaveAsPreset()
{
    // Save the current configuration as a custom preset in QSettings.
    // The preset is persisted per-user and can be recalled later.
    if (currentProfileKey.isEmpty()) return;

    QSettings settings("OpenGeoStudio", "LaneMaker");
    int customCount = settings.value("profiles/customCount", 0).toInt();
    QString customKey = QString("custom_%1").arg(customCount + 1);

    settings.beginGroup("profiles/" + customKey);
    settings.setValue("laneWidth", laneWidthSpinner->value());
    settings.setValue("speedLimit", speedLimitSpinner->value());
    settings.setValue("hasSidewalk", sidewalkCheck->isChecked());
    settings.setValue("hasCurb", curbCheck->isChecked());
    settings.endGroup();
    settings.setValue("profiles/customCount", customCount + 1);

    // Reload to clear modified state
    LoadProfile(currentProfileKey);
}