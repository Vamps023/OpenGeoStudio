#include "LaneConfigWidget.h"
#include "action_manager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QIcon>
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
    painter.setBrush(QColor(13, 17, 23));  // #0d1117
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, 0, rect.width(), rect.height());
    // Draw ruler — subtle gray
    TickInterval = static_cast<float>(XRight - XLeft) / (2 * SingleSideLaneLimit);
    QPen colorPen(QColor(48, 54, 61));  // #30363d
    painter.setPen(colorPen);
    painter.drawLine(XCenter - SingleSideLaneLimit * TickInterval, YCenter,
                     XCenter + SingleSideLaneLimit * TickInterval, YCenter);
    // Draw ticks — subtle
    const int TickHeight = rect.height() / 2;
    colorPen.setColor(QColor(33, 38, 45));  // #21262d
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
    colorPen.setColor(QColor(139, 148, 158));  // #8b949e
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
    painter.setBrush(QColor(13, 17, 23));  // #0d1117
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
    QPen colorPen(QColor(48, 54, 61));  // #30363d
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
    colorPen.setColor(QColor(230, 237, 243));  // #e6edf3
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

LaneConfigWidget::LaneConfigWidget(bool verticalLayout):
    visual(new CrossSectionVisual),
    leftMinus(new QToolButton), leftPlus(new QToolButton),
    rightMinus(new QToolButton), rightPlus(new QToolButton),
    swapDirectionButton(new QToolButton),
    flipLaneButton(new QToolButton),
    laneWidthSpinner(new QDoubleSpinBox),
    incLogo(":/icons/add.png"), decLogo(":/icons/minus.png")
{
    setMinimumWidth(550);

    // Dark theme for the entire LaneConfigWidget
    setStyleSheet(
        "LaneConfigWidget { background-color: #0d1117; border: 1px solid #21262d; border-radius: 8px; }"
        "QLabel { color: #7d8590; font-size: 11px; }"
        "QToolButton { background: #161b22; border: 1px solid #30363d; border-radius: 6px; "
        "  padding: 4px; margin: 1px; }"
        "QToolButton:hover { background: #21262d; border-color: #1f6feb; }"
        "QToolButton:pressed { background: #0d1117; }"
        "QToolButton:disabled { color: #484f58; border-color: #21262d; background: #0d1117; }"
        "QDoubleSpinBox { background: #161b22; border: 1px solid #30363d; border-radius: 6px;"
        "  padding: 4px; color: #e6edf3; font-size: 12px; }"
        "QDoubleSpinBox:hover { border-color: #1f6feb; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  background: #21262d; border: none; border-radius: 3px; width: 16px; }"
        "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {"
        "  background: #30363d; }");

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
    laneWidthSpinner->setFixedWidth(70);

    visual->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    QBoxLayout* layout;
    if (verticalLayout)
    {
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);
        layout->addWidget(visual, 1, Qt::AlignHCenter);
        QHBoxLayout* bottomLayout = new QHBoxLayout;
        bottomLayout->setSpacing(4);
        bottomLayout->addWidget(leftMinus);
        bottomLayout->addWidget(leftPlus);
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(flipLaneButton);
        bottomLayout->addWidget(swapDirectionButton);
        bottomLayout->addStretch(1);
        auto* wLabel = new QLabel("Lane W:");
        wLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
        bottomLayout->addWidget(wLabel);
        bottomLayout->addWidget(laneWidthSpinner);
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(rightMinus);
        bottomLayout->addWidget(rightPlus);
        layout->addLayout(bottomLayout);
        visual->setMinimumHeight(100);
    }
    else
    {
        layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);
        layout->addWidget(leftMinus);
        layout->addWidget(leftPlus);
        layout->addWidget(visual);
        layout->addWidget(flipLaneButton);
        layout->addWidget(swapDirectionButton);
        auto* wLabel = new QLabel("W:");
        wLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
        layout->addWidget(wLabel);
        layout->addWidget(laneWidthSpinner);
        layout->addWidget(rightMinus);
        layout->addWidget(rightPlus);
    }
    setLayout(layout);

    // Update LaneWidth when spinner changes
    connect(laneWidthSpinner, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [](double val) {
                LM::LaneWidth = val;
            });

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
    visual->SetRailMode(false);
    visual->SetMode(true);
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
    visual->SetRailMode(true);
    visual->SetMode(true);
}

void LaneConfigWidget::SetRailProfile(int trackCount, double gauge, double trackSpacing)
{
    visual->SetRailProfile(trackCount, gauge, trackSpacing);
}