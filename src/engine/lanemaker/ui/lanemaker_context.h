#pragma once

// ============================================================
// LaneMakerContext — Abstraction for LaneMaker global state
// ============================================================
//
// ARCHITECTURAL NOTE:
//
// LaneMaker currently uses process-wide global pointers and
// singletons that are rebound on showEvent(). This makes it
// unsafe for multiple concurrent instances (Road Studio and
// Train Studio share the same World, ChangeTracker, etc.).
//
// This header documents the global state surface and provides
// a context struct that can be used to access the active
// LaneMaker instance's state in a structured way.
//
// Phase A (current): Document and centralize global access.
// Phase B (future): Migrate singletons to instance-based ownership.
//
// See:
//   - docs/REVERSE_ENGINEERING_PART1_ARCHITECTURE.md Section 0
//     "Critical Architecture Constraint"
//   - docs/REVERSE_ENGINEERING_PART3_GUIDE.md Section 28.1
//     "High-Risk Ownership/Global-State Areas"
//   - ARCHITECTURE_RULES.md
//

// Forward declarations:
// MainWindow, MainWidget, LaneConfigWidget are in the global namespace.
// MapViewGL is in the LM namespace.
class MainWindow;
class MainWidget;
class LaneConfigWidget;
namespace LM { class MapViewGL; }

namespace LM {

// ─── LaneMakerContext ───────────────────────────────────────
//
// A snapshot of the currently-active LaneMaker instance's
// key pointers. This is populated by MainWidget::rebindGlobals()
// and can be used to access the active instance without directly
// referencing the global variables.
//
// WARNING: This context is only valid for the currently-visible
// workspace. If the user switches workspaces, the context
// becomes stale. Always call LaneMakerContext::current() to
// get the latest snapshot.
//
struct LaneMakerContext {
    ::MainWindow*       mainWindow = nullptr;
    ::MainWidget*       mainWidget = nullptr;
    MapViewGL*          mapView = nullptr;
    ::LaneConfigWidget* laneConfig = nullptr;

    // Returns a snapshot of the currently-active LaneMaker context.
    // This reads the global pointers, which are rebound on showEvent().
    static LaneMakerContext current();
};

} // namespace LM
