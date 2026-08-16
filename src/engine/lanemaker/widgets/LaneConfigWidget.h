#pragma once
#include <QtWidgets>
#include "road_profile.h"

#include <array>
#include <vector>

class CrossSectionVisual :
    public QWidget
{
    Q_OBJECT
public:
    CrossSectionVisual();

    void Reset();

    void SetOption(const LM::LanePlan&, const LM::LanePlan&);

    void SetMode(bool roadMode);
    bool IsRoadMode() const { return roadMode; }

    // Rail mode — draws rails/tracks instead of road lanes
    void SetRailMode(bool railMode);
    bool IsRailMode() const { return railMode; }

    // Rail profile settings
    void SetRailProfile(int trackCount, double gauge, double trackSpacing);
    int RailTrackCount() const { return railTrackCount; }
    double RailGauge() const { return railGauge; }
    double RailTrackSpacing() const { return railTrackSpacing; }

    LM::LanePlan activeLeftSetting;
    LM::LanePlan activeRightSetting;

signals:
    void OptionChangedByUser(LM::LanePlan left, LM::LanePlan right);
    void RailProfileChanged(int trackCount, double gauge, double trackSpacing);

protected:
    void showEvent(QShowEvent* event) override;

    const int SingleSideLaneLimit = 10;

    // Force handles alignment
    void resizeEvent(QResizeEvent* event) override;

    void paintEvent(QPaintEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    virtual QSize sizeHint() const override;

    void paintRoadSection(QPainter& painter, const QRect& rect);
    void paintRailSection(QPainter& painter, const QRect& rect);

    float TickInterval;
    int XCenter, YCenter, XLeft, XRight;

    std::array<int, 4> handleX;
    std::vector<int> dragIndex;
    std::vector<int> handleOrigin;
    int lOuterResult, lInnerResult, rOuterResult, rInnerResult;

    int dragOrigin;
    int dragLimit; // absolute value

    bool roadMode;
    bool railMode = false;
    bool changedExternally = false;

    // Rail profile parameters
    int railTrackCount = 1;
    double railGauge = 1.435;       // meters
    double railTrackSpacing = 4.0;  // meters center-to-center

    const QImage rightLogo, leftLogo;
};

class LaneConfigWidget :
    public QWidget
{
    Q_OBJECT
public:
    LaneConfigWidget(bool verticalLayout=false);

    void Reset();

    /*picking from existing*/
    void SetOption(const LM::LanePlan&, const LM::LanePlan&);

    void GotoRoadMode();
    void GotoLaneMode();
    void GotoRailMode();

    LM::LanePlan LeftResult() const { return visual->activeLeftSetting; }
    LM::LanePlan RightResult() const { return visual->activeRightSetting; }
    bool RoadMode() const { return visual->IsRoadMode(); }
    bool RailMode() const { return visual->IsRailMode(); }

    void SetRailProfile(int trackCount, double gauge, double trackSpacing);

    virtual QSize sizeHint() const override;

private slots:
    void OnOptionChange(LM::LanePlan left, LM::LanePlan right);

private:
    CrossSectionVisual* visual;
    QToolButton* leftMinus, * leftPlus, * rightMinus, * rightPlus;
    QDoubleSpinBox* laneWidthSpinner;

    const QPixmap incLogo, decLogo;
};

extern LaneConfigWidget* g_laneConfig;
