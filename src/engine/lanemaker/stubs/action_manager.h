#pragma once
// Stub for action_manager.h
namespace LM {
    class ActionManager {
    public:
        static ActionManager* Instance() { static ActionManager inst; return &inst; }
        void RecordAction(const std::string&) {}
    };
}
