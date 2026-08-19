#pragma once
#include <QtWidgets>
#include "road_profile.h"
#include "RoadTypes.hpp"

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

    // Lane selection: 0 = none, +N = Nth lane on right side, -N = Nth lane on left side
    int selectedLane = 0;

    // Hit-test: returns lane index at x position, or 0 if none
    int LaneAtPos(int x) const;

signals:
    void OptionChangedByUser(LM::LanePlan left, LM::LanePlan right);
    void RailProfileChanged(int trackCount, double gauge, double trackSpacing);
    void LaneSelected(int laneIndex);

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

    /// Load a road profile preset by key (e.g. "city_2x1")
    void LoadProfile(const QString& key);

    /// Populate the profile combo with road profiles (called when entering road mode)
    void PopulateRoadProfiles();

    /// Populate the profile combo with rail profiles (called when entering rail mode)
    void PopulateRailProfiles();

    /// Current profile key
    QString CurrentProfileKey() const { return currentProfileKey; }

    /// Whether the current config has been modified from the loaded profile
    bool IsModifiedFromProfile() const { return modifiedFromProfile; }

    virtual QSize sizeHint() const override;

signals:
    /// Emitted when the profile selection changes (by user or programmatically)
    void ProfileChanged(const QString& key);
    /// Emitted when road-level metadata (speed/sidewalk/curb) changes
    void RoadMetadataChanged(double speedLimit, bool hasSidewalk, bool hasCurb);

private slots:
    void OnOptionChange(LM::LanePlan left, LM::LanePlan right);
    void OnProfileComboChanged(int index);
    void OnLaneWidthChanged(double val);
    void OnResetToProfile();
    void OnSaveAsPreset();
    void CheckModified();

private:
    CrossSectionVisual* visual;
    QToolButton* leftMinus, * leftPlus, * rightMinus, * rightPlus;
    QToolButton* swapDirectionButton;  // Swap left/right lane direction
    QToolButton* flipLaneButton;       // Flip selected lane direction
    QDoubleSpinBox* laneWidthSpinner;

    // ── Unified Cross-Section Studio additions ──
    QComboBox* profileCombo;           // Profile preset selector (moved from top bar)
    QDoubleSpinBox* speedLimitSpinner;  // Speed limit (km/h)
    QCheckBox* sidewalkCheck;           // Has sidewalk?
    QCheckBox* curbCheck;               // Has curb?
    QLabel* modifiedLabel;             // "Modified from <profile>" indicator
    QPushButton* resetButton;          // Reset to profile preset
    QPushButton* savePresetButton;     // Save current config as new preset

    QString currentProfileKey;         // Key of the currently loaded profile
    bool modifiedFromProfile = false;  // True if user has tweaked away from the preset
    bool applyingProfile = false;      // Guard to suppress modified-flag while loading a preset

    // Snapshot of the loaded profile for comparison + reset
    roads::RoadProfile loadedProfile;

    const QPixmap incLogo, decLogo;
};

extern LaneConfigWidget* g_laneConfig;
