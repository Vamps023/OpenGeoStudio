#include "change_tracker.h"
#include "id_generator.h"
#include "road.h"
#include "junction.h"
#include "world.h"
#include "test/validation.h"
#include "action_manager.h"
#include "preference.h"
#include "util.h"
#include "spatial_indexer.h"

#include <spdlog/spdlog.h>
#include <exception>
#include <fstream>

namespace {
    // Crash diagnostic helper — writes to crash_diag.txt with immediate flush
    // so we can see the last line before a hard crash.
    void diag(const std::string& msg) {
        std::ofstream f("crash_diag.txt", std::ios::app);
        f << msg << "\n";
        f.flush();
    }
}

extern UserPreference g_preference;

namespace LM
{
    ChangeTracker* ChangeTracker::instance = nullptr;

    ChangeTracker* ChangeTracker::Instance()
    {
        if (instance == nullptr)
        {
            instance = new ChangeTracker;
        }
        return instance;
    }

    void ChangeTracker::PostLoadActions()
    {
        diag("PostLoadActions: " + std::to_string(odrMap.id_to_road.size()) +
             " roads, " + std::to_string(odrMap.id_to_junction.size()) + " junctions");
        // Temporarily hold shared_ptr to Connecting road until they get owned by junction
        std::vector<std::shared_ptr<LM::Road>> connectingRoadHolder;
        diag("PostLoadActions: generating road graphics...");
        int roadIdx = 0;
        for (const auto& id2Road : TQDM(odrMap.id_to_road))
        {
            roadIdx++;
            diag("PostLoadActions: road " + std::to_string(roadIdx) +
                 " / " + std::to_string(odrMap.id_to_road.size()) +
                 " (id=" + id2Road.first + ", junction=" + id2Road.second.junction + ")");
            auto rrRoad = std::make_shared<LM::Road>(id2Road.second);
            try
            {
                rrRoad->GenerateAllSectionGraphics();
            }
            catch (const std::exception& e)
            {
                spdlog::warn("Road {} graphics generation failed, skipping: {}",
                    id2Road.first, e.what());
            }
            catch (...)
            {
                spdlog::warn("Road {} graphics generation failed (unknown), skipping",
                    id2Road.first);
            }
            if (id2Road.second.junction == "-1")
            {
                World::Instance()->allRoads.insert(rrRoad);
            }
            else
            {
                connectingRoadHolder.push_back(rrRoad);
            }
        }

        // Temporarily hold shared_ptr to Junction until they get owned by connected road
        std::map<std::string, std::shared_ptr<LM::AbstractJunction>> id2RRJunction;

        // Load junctions and their connecting roads
        for (const auto& id2Junction : odrMap.id_to_junction)
        {
            std::shared_ptr<LM::AbstractJunction> rrJunc;
            try
            {
                if (id2Junction.second.type == odr::JunctionType::Common)
                {
                    rrJunc = std::make_shared<LM::Junction>(id2Junction.second);
                }
                else
                {
                    rrJunc = std::make_shared<LM::DirectJunction>(id2Junction.second);
                }
            }
            catch (const std::exception& e)
            {
                spdlog::warn("Junction {} creation failed, skipping: {}",
                    id2Junction.first, e.what());
                continue;
            }
            catch (...)
            {
                spdlog::warn("Junction {} creation failed (unknown), skipping",
                    id2Junction.first);
                continue;
            }
            id2RRJunction.emplace(id2Junction.first, rrJunc);
        }
        // Link connected road <-> pred/succ junction
        for (auto& road : World::Instance()->allRoads)
        {
            try
            {
                if (road->generated.successor.type == odr::RoadLink::Type_Junction)
                {
                    auto rrJunctionIt = id2RRJunction.find(road->generated.successor.id);
                    if (rrJunctionIt == id2RRJunction.end())
                    {
                        spdlog::warn("Road {} successor references undefined junction {}, skipping link",
                            road->ID(), road->generated.successor.id);
                    }
                    else
                    {
                        rrJunctionIt->second->AttachNoRegenerate(LM::ConnectionInfo{ road, odr::RoadLink::ContactPoint_End });
                    }
                }
                if (road->generated.predecessor.type == odr::RoadLink::Type_Junction)
                {
                    auto rrJunctionIt = id2RRJunction.find(road->generated.predecessor.id);
                    if (rrJunctionIt == id2RRJunction.end())
                    {
                        spdlog::warn("Road {} predecessor references undefined junction {}, skipping link",
                            road->ID(), road->generated.predecessor.id);
                    }
                    else
                    {
                        rrJunctionIt->second->AttachNoRegenerate(LM::ConnectionInfo(road, odr::RoadLink::ContactPoint_Start));
                    }
                }
            }
            catch (const std::exception& e)
            {
                spdlog::warn("Road {} junction link failed, skipping: {}",
                    road->ID(), e.what());
            }
            catch (...)
            {
                spdlog::warn("Road {} junction link failed (unknown), skipping",
                    road->ID());
            }
        }

        std::cout << "Generating junction graphics ";
        for (auto& idAndjunc : TQDM(id2RRJunction))
        {
            try
            {
                idAndjunc.second->GenerateGraphics();
            }
            catch (const std::exception& e)
            {
                spdlog::warn("Junction {} graphics generation failed, skipping: {}",
                    idAndjunc.second->ID(), e.what());
            }
        }

        while (!redoStack.empty())
        {
            redoStack.pop();
        }
        while (!undoStack.empty())
        {
            undoStack.pop();
        }

        PostChangeActions();
    }

    void ChangeTracker::PostChangeActions()
    {
        SpatialIndexer::Instance()->RebuildTree();
        if (g_preference.alwaysVerify)
            LTest::Validation::ValidateMap();
    }

    void ChangeTracker::StartRecordEdit()
    {
        IDGenerator::ForType(IDType::Road)->ClearChangeList();
        IDGenerator::ForType(IDType::Junction)->ClearChangeList();
    }

    void ChangeTracker::FinishRecordEdit(bool abort)
    {
        auto roadChanges = IDGenerator::ForType(IDType::Road)->ConsumeChanges();
        auto junctionChanges = IDGenerator::ForType(IDType::Junction)->ConsumeChanges();
        if (roadChanges.empty() && junctionChanges.empty())
        {
            return;
        }

        MapChange recordEntry;

        for (const auto& change : roadChanges)
        {
            std::string id = std::to_string(change.first);
            void* ptr = change.second;

            RoadChange roadChange;
            auto oldIt = odrMap.id_to_road.find(id);
            if (oldIt != odrMap.id_to_road.end())
            {
                spdlog::trace("[Edit] Road {} (len={}) removal recorded", id, oldIt->second.length);
                roadChange.before.emplace(oldIt->second);
                odrMap.id_to_road.erase(id);
            }

            if (ptr != nullptr)
            {
                Road* newRoad = static_cast<Road*>(ptr);
                roadChange.after.emplace(newRoad->generated);
                assert(odrMap.id_to_road.find(newRoad->ID()) == odrMap.id_to_road.end());
                odrMap.id_to_road.emplace(newRoad->ID(), newRoad->generated);
                spdlog::trace("[Edit] Road {} (len={}) creation recorded", id, newRoad->Length());
            }
            recordEntry.roadChanges.push_back(roadChange);
        }
        
        for (const auto& change : junctionChanges)
        {
            std::string id = std::to_string(change.first);
            void* ptr = change.second;

            JunctionChange junctionChange;
            auto oldIt = odrMap.id_to_junction.find(id);
            if (oldIt != odrMap.id_to_junction.end())
            {
                junctionChange.before.emplace(oldIt->second);
                odrMap.id_to_junction.erase(id);
                spdlog::trace("[Edit] Junction {} removal recorded", id);
            }

            if (ptr != nullptr)
            {
                AbstractJunction* newJunction = static_cast<AbstractJunction*>(ptr);
                junctionChange.after.emplace(newJunction->generated);
                assert(odrMap.id_to_junction.find(newJunction->ID()) == odrMap.id_to_junction.end());
                odrMap.id_to_junction.emplace(newJunction->ID(), newJunction->generated);
                spdlog::trace("[Edit] Junction {} creation recorded", id);
            }
            recordEntry.junctionChanges.push_back(junctionChange);
        }
        
        if (abort)
        {
            RestoreChange(recordEntry);
        }
        else
        {
            undoStack.emplace(recordEntry);
            while (!redoStack.empty())
            {
                redoStack.pop();
            }
        }

        PostChangeActions();
    }

    void ChangeTracker::Clear()
    {
        Road::ClearingMap = true;
        SpatialIndexer::Instance()->Clear();
        World::Instance()->allRoads.clear();
        Road::ClearingMap = false;
        IDGenerator::Reset();
        odrMap.id_to_road.clear();
        odrMap.id_to_junction.clear();
        SpatialIndexer::Instance()->RebuildTree();
    }

    void ChangeTracker::Save(std::string path)
    {
        odrMap.export_file(path);
    }

    bool ChangeTracker::Load(std::string path)
    {
        diag("ChangeTracker::Load: " + path);
        try
        {
            diag("ChangeTracker::Load: calling odrMap.Load()...");
            if (!odrMap.Load(path))
            {
                diag("ChangeTracker::Load: odrMap.Load returned false");
                return false;
            }
            diag("ChangeTracker::Load: odrMap.Load succeeded");
        }
        catch (const std::exception& e)
        {
            diag(std::string("ChangeTracker::Load: parse error: ") + e.what());
            Clear();
            return false;
        }
        catch (...)
        {
            diag("ChangeTracker::Load: unknown parse error");
            Clear();
            return false;
        }
        try
        {
            diag("ChangeTracker::Load: calling PostLoadActions()...");
            PostLoadActions();
            diag("ChangeTracker::Load: PostLoadActions done");
        }
        catch (const std::exception& e)
        {
            spdlog::error("Failed to build map from {}: {}", path, e.what());
            Clear();
            return false;
        }
        catch (...)
        {
            spdlog::error("Unknown error building map from: {}", path);
            Clear();
            return false;
        }
        return true;
    }

    bool ChangeTracker::LoadStr(std::string content)
    {
        try
        {
            if (!odrMap.LoadString(content))
            {
                return false;
            }
        }
        catch (const std::exception& e)
        {
            spdlog::error("Failed to parse XODR content: {}", e.what());
            Clear();
            return false;
        }
        catch (...)
        {
            spdlog::error("Unknown error parsing XODR content");
            Clear();
            return false;
        }
        try
        {
            PostLoadActions();
        }
        catch (const std::exception& e)
        {
            spdlog::error("Failed to build map from content: {}", e.what());
            Clear();
            return false;
        }
        catch (...)
        {
            spdlog::error("Unknown error building map from content");
            Clear();
            return false;
        }
        return true;
    }

    bool ChangeTracker::Undo()
    {
        if (undoStack.empty())
        {
            return false;
        }
        auto lastRedo = undoStack.top();
        undoStack.pop();
        MapChange thisTimeChange;

        for (const auto& change : lastRedo.roadChanges)
        {
            thisTimeChange.roadChanges.push_back(RoadChange{ change.after, change.before });
        }
        for (const auto& change : lastRedo.junctionChanges)
        {
            thisTimeChange.junctionChanges.push_back(JunctionChange{ change.after, change.before });
        }

        redoStack.emplace(thisTimeChange);
        RestoreChange(lastRedo);
        return true;
    }

    bool ChangeTracker::Redo()
    {
        if (redoStack.empty())
        {
            return false;
        }
        auto lastUndo = redoStack.top();
        redoStack.pop();
        MapChange thisTimeChange;

        for (const auto& change : lastUndo.roadChanges)
        {
            thisTimeChange.roadChanges.push_back(RoadChange{ change.after, change.before });
        }
        for (const auto& change : lastUndo.junctionChanges)
        {
            thisTimeChange.junctionChanges.push_back(JunctionChange{ change.after, change.before });
        }

        undoStack.emplace(thisTimeChange);
        RestoreChange(lastUndo);
        return true;
    }

    void ChangeTracker::RestoreChange(const MapChange& change)
    {
        // Tear down after
        // LMTODO: can be costly for long road

        // for the rare event that tearing down road changes nulls all shared_ptr to junction
        std::map<std::string, odr::Junction> tornDownJunctions;

        for (const auto& change : change.roadChanges)
        {
            if (change.after.has_value())
            {
                odrMap.id_to_road.erase(change.after->id);

                auto& odrRoad = change.after.get();
                if (odrRoad.junction == "-1")
                {
                    auto createdID = odrRoad.id;
                    auto roadPtr = IDGenerator::ForType(IDType::Road)->GetByID<Road>(createdID)->shared_from_this();
                    World::Instance()->allRoads.erase(roadPtr);

                    if (roadPtr->successorJunction != nullptr)
                    {
                        if (roadPtr->successorJunction.use_count() == 1)
                        {
                            tornDownJunctions.emplace(roadPtr->successorJunction->ID(), roadPtr->successorJunction->generated);
                            spdlog::trace("preserve successorJunction");
                        }
                        roadPtr->successorJunction->DetachNoRegenerate(roadPtr);
                    }

                    if (roadPtr->predecessorJunction != nullptr)
                    {
                        if (roadPtr->predecessorJunction.use_count() == 1)
                        {
                            tornDownJunctions.emplace(roadPtr->predecessorJunction->ID(), roadPtr->predecessorJunction->generated);
                            spdlog::trace("preserve predecessorJunction");
                        }
                        roadPtr->predecessorJunction->DetachNoRegenerate(roadPtr);
                    }
                }
            }
        }

        std::set<std::shared_ptr<LM::Road>> roadAffected;
        for (const auto& change : change.junctionChanges)
        {
            if (change.after.has_value())
            {
                odrMap.id_to_junction.erase(change.after->id);

                auto& odrJunction = change.after.get();
                auto createdID = odrJunction.id;
                auto junctionPtr = IDGenerator::ForType(IDType::Junction)->GetByID<LM::AbstractJunction>(createdID);

                if (junctionPtr == nullptr)
                {
                    // Junction could have already been destroyed after deleting all connecting roads
                    continue;
                }                

                auto commonJunctionPtr = dynamic_cast<LM::Junction*>(junctionPtr);
                if (commonJunctionPtr != nullptr)
                {
                    commonJunctionPtr->connectingRoads.clear();
                }
                auto formedFrom = junctionPtr->formedFrom;
                for (auto connected : formedFrom)
                {
                    auto road = connected.road.lock();
                    if (road != nullptr)
                    {
                        if (road->successorJunction.get() == junctionPtr)
                        {
                            junctionPtr->DetachNoRegenerate(road);
                            roadAffected.insert(road);
                        }
                        if (road->predecessorJunction.get() == junctionPtr)
                        {
                            junctionPtr->DetachNoRegenerate(road);
                            roadAffected.insert(road);
                        }
                    }
                }
            }
        }

        // Restore before (Roads)
        for (const auto& change : change.roadChanges)
        {
            if (change.before.has_value())
            {
                assert(odrMap.id_to_road.find(change.before->id) == odrMap.id_to_road.end());
                odrMap.id_to_road.emplace(change.before->id, change.before.get());

                spdlog::trace("Undo::Restore Road {}", change.before->id);
                auto odrRoad = change.before.get();
                auto rrRoad = std::make_shared<LM::Road>(odrRoad);
                rrRoad->GenerateAllSectionGraphics();
                roadAffected.insert(rrRoad);
                if (odrRoad.junction == "-1")
                {
                    World::Instance()->allRoads.insert(rrRoad);
                }
            }
        }

        // Restore before (Junctions)
        std::vector<std::shared_ptr<LM::AbstractJunction>> junctionRestored;
        for (const auto& change : change.junctionChanges)
        {
            if (change.before.has_value())
            {
                assert(odrMap.id_to_junction.find(change.before->id) == odrMap.id_to_junction.end());
                odrMap.id_to_junction.emplace(change.before->id, change.before.get());
                const auto& odrJunction = change.before.get();
                std::shared_ptr<LM::AbstractJunction> rrJunc;
                if (odrJunction.type == odr::JunctionType::Common)
                {
                    rrJunc = std::make_shared<LM::Junction>(odrJunction);
                }
                else
                {
                    rrJunc = std::make_shared<LM::DirectJunction>(odrJunction);
                }
                junctionRestored.push_back(rrJunc);
                spdlog::trace("Undo::Restore Junction {}", rrJunc->ID());
            }
        }

        // Link connected road <-> pred/succ junction
        for (auto rrRoad: roadAffected)
        {
            auto odrRoad = rrRoad->generated;
                
            if (odrRoad.successor.type == odr::RoadLink::Type_Junction &&
                odrRoad.successor.id != "-1")
            {
                auto juncPtr = IDGenerator::ForType(IDType::Junction)->GetByID<LM::AbstractJunction>(odrRoad.successor.id);
                if (juncPtr == nullptr)
                {
                    auto odrJunction = tornDownJunctions.at(odrRoad.successor.id);
                    if (odrJunction.type == odr::JunctionType::Common)
                    {
                        juncPtr = std::make_shared<LM::Junction>(odrJunction).get();
                    }
                    else
                    {
                        juncPtr = std::make_shared<LM::DirectJunction>(odrJunction).get();
                    }
                }
                juncPtr->AttachNoRegenerate(LM::ConnectionInfo(rrRoad, odr::RoadLink::ContactPoint_End));
            }

            if (odrRoad.predecessor.type == odr::RoadLink::Type_Junction &&
                odrRoad.predecessor.id != "-1")
            {
                auto juncPtr = (IDGenerator::ForType(IDType::Junction)->GetByID<LM::AbstractJunction>(odrRoad.predecessor.id));
                std::shared_ptr<LM::AbstractJunction> juncPtrShared;
                if (juncPtr == nullptr)
                {
                    auto odrJunction = tornDownJunctions.at(odrRoad.predecessor.id);
                    if (odrJunction.type == odr::JunctionType::Common)
                    {
                        juncPtrShared = std::make_shared<LM::Junction>(odrJunction);
                    }
                    else
                    {
                        juncPtrShared = std::make_shared<LM::DirectJunction>(odrJunction);
                    }
                }
                else
                {
                    juncPtrShared = juncPtr->shared_from_this();
                }
                juncPtrShared->AttachNoRegenerate(LM::ConnectionInfo(rrRoad, odr::RoadLink::ContactPoint_Start));
            }
        }

        for (auto junc : junctionRestored)
        {
            junc->GenerateGraphics();
        }

        PostChangeActions();
    }

    const odr::OpenDriveMap& ChangeTracker::Map()
    {
        return odrMap;
    }
}
