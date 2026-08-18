#include "road_drawing.h"
#include "junction.h"

#include "LaneConfigWidget.h"
#include "map_view_gl.h"
#include "action_manager.h"

#include <qevent.h>

RoadModificationSession::RoadModificationSession()
{}

bool RoadModificationSession::Update(const LM::MouseAction& evt)
{
    RoadDrawingSession::Update(evt);
    return RoadDestroySession::Update(evt);
}

bool RoadModificationSession::Complete()
{
    if (targetRoad.expired() || s2 == nullptr) return true;

    auto target = targetRoad.lock();
    if (std::min(*s1, *s2) == 0 &&
        dynamic_cast<LM::DirectJunction*>(target->predecessorJunction.get()) != nullptr
        ||
        std::max(*s1, *s2) == target->Length() &&
        dynamic_cast<LM::DirectJunction*>(target->successorJunction.get()) != nullptr)
    {
        spdlog::warn("Cannot modify section adjacent to Direct junction. Please delete instead.");
        return true;
    }

    double sBegin = std::min(*s1, *s2);
    double sEnd = std::max(*s1, *s2);

    auto currBeginLeft = target->generated.rr_profile.ProfileAt(sBegin, 1);
    auto currBeginRight = target->generated.rr_profile.ProfileAt(sBegin, -1);
    auto currEndLeft = target->generated.rr_profile.ProfileAt(sEnd, 1);
    auto currEndRight = target->generated.rr_profile.ProfileAt(sEnd, -1);

    if (!(sBegin == 0 && sEnd == target->Length()) &&
        (target->generated.rr_profile.HasSide(1) != (g_laneConfig->LeftResult().laneCount > 0) ||
        target->generated.rr_profile.HasSide(-1) != (g_laneConfig->RightResult().laneCount > 0)))
    {
        if (sBegin != 0 && IsProfileChangePoint(target, sBegin)
            || sEnd != 0 && IsProfileChangePoint(target, sEnd))
        {
            spdlog::warn("Direct junction cannot be created next to changing profile.");
            return true;
        }

        // Form direct junction
        std::shared_ptr<LM::Road> before, toModify, after;
        if (sEnd != target->Length())
        {
            after = LM::Road::SplitRoad(target, sEnd);
            World::Instance()->allRoads.insert(after);
            toModify = target;
        }

        if (sBegin != 0)
        {
            toModify = LM::Road::SplitRoad(target, sBegin);
            World::Instance()->allRoads.insert(toModify);
            before = target;
        }

        toModify->ModifyProfile(0, toModify->Length(), g_laneConfig->LeftResult(), g_laneConfig->RightResult());
        if (before != nullptr)
        {
            if (currBeginLeft.laneCount != 0 && g_laneConfig->LeftResult().laneCount != 0
                && currBeginLeft.offsetx2 != g_laneConfig->LeftResult().offsetx2
                ||
                currBeginRight.laneCount != 0 && g_laneConfig->RightResult().laneCount != 0
                && currBeginRight.offsetx2 != g_laneConfig->RightResult().offsetx2)
            {
                spdlog::warn("Offset must remain the same while creating direct junction.");
                return false;
            }

            auto toModifyInfo = LM::ConnectionInfo(toModify, odr::RoadLink::ContactPoint_Start);
            auto beforeInfo = LM::ConnectionInfo(before, odr::RoadLink::ContactPoint_End);

            if (currBeginLeft.laneCount <= g_laneConfig->LeftResult().laneCount
                && currBeginRight.laneCount <= g_laneConfig->RightResult().laneCount)
            {
                auto junc = std::make_shared< LM::DirectJunction>(toModifyInfo);
                junc->Attach(beforeInfo);
            }
            else if (currBeginLeft.laneCount >= g_laneConfig->LeftResult().laneCount
                && currBeginRight.laneCount >= g_laneConfig->RightResult().laneCount)
            {
                auto junc = std::make_shared< LM::DirectJunction>(beforeInfo);
                junc->Attach(toModifyInfo);
            }
            else
            {
                spdlog::warn("Can't find interface provider for direct junction.");
                return false;
            }
        }
        if (after != nullptr)
        {
            if (currEndLeft.laneCount != 0 && g_laneConfig->LeftResult().laneCount != 0
                && currEndLeft.offsetx2 != g_laneConfig->LeftResult().offsetx2
                ||
                currEndRight.laneCount != 0 && g_laneConfig->RightResult().laneCount != 0
                && currEndRight.offsetx2 != g_laneConfig->RightResult().offsetx2)
            {
                spdlog::warn("Offset must remain the same while creating direct junction!");
                return false;
            }

            auto toModifyInfo = LM::ConnectionInfo(toModify, odr::RoadLink::ContactPoint_End);
            auto afterInfo = LM::ConnectionInfo(after, odr::RoadLink::ContactPoint_Start);

            if (currEndLeft.laneCount <= g_laneConfig->LeftResult().laneCount
                && currEndRight.laneCount <= g_laneConfig->RightResult().laneCount)
            {
                auto junc = std::make_shared< LM::DirectJunction>(toModifyInfo);
                junc->Attach(afterInfo);
            }
            else if (currEndLeft.laneCount >= g_laneConfig->LeftResult().laneCount
                && currEndRight.laneCount >= g_laneConfig->RightResult().laneCount)
            {
                auto junc = std::make_shared< LM::DirectJunction>(afterInfo);
                junc->Attach(toModifyInfo);
            }
            else
            {
                spdlog::warn("Can't find interface provider for direct junction!");
                return false;
            }
        }

        UpdateEndMarkings();
        return true;
    }

    bool success = target->ModifyProfile(sBegin, sEnd, g_laneConfig->LeftResult(), g_laneConfig->RightResult());
    if (success)
    {
        UpdateEndMarkings();
    }
    return success;
}

// ============================================================================
// LaneFlipSession — click on a lane on a drawn road to flip its direction
// ============================================================================

LaneFlipSession::LaneFlipSession()
{}

bool LaneFlipSession::Update(const LM::MouseAction& evt)
{
    auto g_road = GetPointerRoad();

    // Update cursor position
    if (g_road != nullptr)
    {
        bool onSegBoundary;
        auto snapped = g_road->generated.get_xyz(GetAdjustedS(&onSegBoundary), 0, 0);
        cursorItem->SetTranslation(snapped);
        cursorItem->EnableHighlight(RoadDrawingSession::Snap_Line);
    }
    else
    {
        auto groundPos = LM::g_PointerOnGround;
        cursorItem->SetTranslation({groundPos[0], groundPos[1], 0});
        cursorItem->EnableHighlight(RoadDrawingSession::Snap_Nothing);
    }
    SetHighlightTo(g_road);

    // Handle click on a road (not on a connecting road / junction)
    if (evt.button == Qt::LeftButton &&
        evt.type == QEvent::Type::MouseButtonPress &&
        g_road != nullptr && !g_road->IsConnectingRoad())
    {
        targetRoad = g_road;
        staged = true;

        // Determine which side the click is on using geometry.
        // This works in both 2D and 3D — we compute the cross product of
        // the road's direction at the click S with the offset from the
        // reference line to the click point.
        double s = LM::g_PointerRoadS;
        if (LM::g_PointerLane != 0)
        {
            // Ray caster gave us a lane — use it directly
            targetLane = LM::g_PointerLane;
        }
        else
        {
            // 2D mode or ray cast miss — determine side geometrically
            auto refPos = g_road->generated.get_xyz(s, 0, 0);
            auto grad = g_road->generated.ref_line.get_grad_xy(s);
            odr::Vec2D roadDir{ grad[0], grad[1] };
            // Normalize
            double dirLen = std::sqrt(roadDir[0] * roadDir[0] + roadDir[1] * roadDir[1]);
            if (dirLen > 1e-9)
            {
                roadDir[0] /= dirLen;
                roadDir[1] /= dirLen;
            }
            // Vector from ref line to click point
            odr::Vec2D toClick{
                LM::g_PointerOnGround[0] - refPos[0],
                LM::g_PointerOnGround[1] - refPos[1]
            };
            // Cross product Z: positive = click is on left side, negative = right side
            double crossZ = roadDir[0] * toClick[1] - roadDir[1] * toClick[0];
            targetLane = (crossZ > 0) ? -1 : 1;  // left side = negative, right side = positive
        }

        // Get current profile at the click position
        auto currLeft = g_road->generated.rr_profile.ProfileAt(s, 1);
        auto currRight = g_road->generated.rr_profile.ProfileAt(s, -1);

        newLeftPlan = currLeft;
        newRightPlan = currRight;

        if (targetLane > 0)
        {
            // Right side lane clicked → move one lane to left side
            if (currRight.laneCount > 0)
            {
                newRightPlan.laneCount = currRight.laneCount - 1;
                if (newRightPlan.laneCount == 0)
                    newLeftPlan.offsetx2 = currRight.offsetx2;
                if (currLeft.laneCount == 0)
                    newLeftPlan.offsetx2 = -currRight.offsetx2;
                newLeftPlan.laneCount = currLeft.laneCount + 1;
            }
        }
        else
        {
            // Left side lane clicked → move one lane to right side
            if (currLeft.laneCount > 0)
            {
                newLeftPlan.laneCount = currLeft.laneCount - 1;
                if (newLeftPlan.laneCount == 0)
                    newRightPlan.offsetx2 = currLeft.offsetx2;
                if (currRight.laneCount == 0)
                    newRightPlan.offsetx2 = -currLeft.offsetx2;
                newRightPlan.laneCount = currRight.laneCount + 1;
            }
        }

        // DON'T apply preview here — modifying the road during the mouse
        // event would rebuild the spatial indexer while it's being queried,
        // causing a crash in 3D mode. Just show confirm/cancel buttons.
        UpdateHint();
    }

    return true;
}

void LaneFlipSession::ApplyPreview()
{
    // No longer used — modification is applied in Complete()
}

bool LaneFlipSession::Complete()
{
    if (!staged) return true;
    auto target = targetRoad.lock();
    if (!target) return true;

    // Apply the modification now — this is safe because we're not
    // inside a mouse event / ray cast query anymore.
    target->ModifyProfile(0, target->Length(), newLeftPlan, newRightPlan);
    UpdateEndMarkings();

    staged = false;
    targetRoad.reset();
    confirmButton.reset();
    cancelButton.reset();
    return true;
}

bool LaneFlipSession::Cancel()
{
    if (!staged)
    {
        return false;
    }

    // Nothing was applied yet — just clear the staged state
    staged = false;
    targetRoad.reset();
    confirmButton.reset();
    cancelButton.reset();
    return true;
}

void LaneFlipSession::UpdateHint()
{
    auto target = targetRoad.lock();
    if (staged && target)
    {
        // Show confirm/cancel buttons at the click position
        auto btnPos = target->generated.get_xyz(LM::g_PointerRoadS, 0, 10.0);
        confirmButton.emplace(btnPos, QPixmap(":/icons/confirm.png"),
            QRect(-40, -60, 60, 60), Qt::Key_Space);
        cancelButton.emplace(btnPos, QPixmap(":/icons/cancel.png"),
            QRect(40, -60, 60, 60), Qt::Key_Escape);
    }
    else
    {
        confirmButton.reset();
        cancelButton.reset();
    }
}