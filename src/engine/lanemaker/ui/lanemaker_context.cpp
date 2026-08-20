// LaneMakerContext — implementation

#include "lanemaker_context.h"
#include "main_window.h"
#include "main_widget.h"
#include "../engine/map_view_gl.h"
#include "../widgets/LaneConfigWidget.h"

namespace LM {

LaneMakerContext LaneMakerContext::current()
{
    LaneMakerContext ctx;
    ctx.mainWindow = g_mainWindow;             // global namespace
    ctx.mainWidget  = MainWidget::Instance();   // global namespace
    ctx.mapView     = g_mapViewGL;              // LM namespace (this file is in LM)
    ctx.laneConfig  = g_laneConfig;             // global namespace
    return ctx;
}

} // namespace LM
