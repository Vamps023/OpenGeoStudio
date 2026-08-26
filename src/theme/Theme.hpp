#pragma once

// ============================================================
// Theme — OpenGeoStudio design tokens + global stylesheet
// ============================================================
//
// Single source of truth for the GitHub-dark visual language used
// by both the application shell and the LaneMaker engine UI.
// All hardcoded colors in UI code must come from here.
//
// Refined-dark palette:
//   surface-base  #0d1117   surface-panel #161b22  elevated #1c2128
//   input/hover   #21262d   edge          #30363d  edge-hi  #484f58
//   fg-primary    #e6edf3   fg-muted      #7d8590  faint    #484f58
//   accent (cyan) #06b6d4   ok #3fb950  warn #d29922  err #f85149
// ============================================================

#include <QColor>
#include <QPalette>
#include <QString>

namespace ogs {
namespace theme {

// ─── Color tokens ────────────────────────────────────────────
namespace c {
inline constexpr const char* BgBase     = "#0d1117"; // window background
inline constexpr const char* BgSurface  = "#161b22"; // panels / cards / inputs alt
inline constexpr const char* BgActive   = "#1c2128"; // pressed / elevated
inline constexpr const char* BgOverlay  = "#21262d"; // inputs / hover fill
inline constexpr const char* Border     = "#30363d";
inline constexpr const char* BorderSub  = "#21262d"; // subtle separators
inline constexpr const char* BorderHi   = "#484f58";
inline constexpr const char* Text       = "#e6edf3";
inline constexpr const char* TextMuted  = "#7d8590";
inline constexpr const char* TextFaint  = "#484f58";
inline constexpr const char* TextDim    = "#6e7681";
inline constexpr const char* TextSoft   = "#8b949e";
inline constexpr const char* Accent     = "#06b6d4"; // primary — cyan
inline constexpr const char* AccentSoft = "rgba(6,182,212,0.15)";
inline constexpr const char* AccentEdge = "rgba(6,182,212,0.2)";
inline constexpr const char* Success    = "#3fb950";
inline constexpr const char* AccentBright = "#22d3ee"; // accent hover
        inline constexpr const char* OnAccent   = "#ffffff";   // text on accent fills

inline constexpr const char* Danger     = "#f85149";
inline constexpr const char* Warning    = "#d29922";
inline constexpr const char* Info       = "#1f6feb";

// QColor conveniences for painter / item code
inline QColor qBase()    { return QColor(BgBase); }
inline QColor qSurface() { return QColor(BgSurface); }
inline QColor qOverlay() { return QColor(BgOverlay); }
inline QColor qBorder()  { return QColor(Border); }
inline QColor qBorderSub(){ return QColor(BorderSub); }
inline QColor qText()    { return QColor(Text); }
inline QColor qMuted()   { return QColor(TextMuted); }
inline QColor qAccent()  { return QColor(Accent); }
inline QColor qSuccess() { return QColor(Success); }
inline QColor qDanger()  { return QColor(Danger); }
inline QColor qWarn()    { return QColor(Warning); }
} // namespace c

// ─── Spacing / radius / type scale ──────────────────────────
inline constexpr int SpaceS = 4, SpaceM = 8, SpaceL = 12;
inline constexpr int RadiusS = 4, RadiusM = 6;
inline constexpr int FontSmall = 11, FontNormal = 13, FontTitle = 14;

// ─── Shared stylesheet fragments ────────────────────────────
// Repeated status-label / separator / dialog styles live here so
// call sites stay short and no hex leaks outside this file.
inline constexpr const char* kLabelOk           = "QLabel { color: #3fb950; }";
inline constexpr const char* kLabelDanger       = "QLabel { color: #f85149; }";
inline constexpr const char* kLabelInfo         = "QLabel { color: #06b6d4; }";
inline constexpr const char* kLabelOkSmall      = "QLabel { color: #3fb950; font-size: 11px; }";
inline constexpr const char* kLabelDangerSmall  = "QLabel { color: #f85149; font-size: 11px; }";
inline constexpr const char* kSeparatorStyle    = "color: #21262d;";
inline constexpr const char* kDialogBase =
    "QDialog { background: #0d1117; } QLabel { color: #e6edf3; } ";

// ─── Dark palette ────────────────────────────────────────────
inline QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x0d, 0x11, 0x17));
    p.setColor(QPalette::WindowText, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::Base, QColor(0x16, 0x1b, 0x22));
    p.setColor(QPalette::AlternateBase, QColor(0x1c, 0x21, 0x28));
    p.setColor(QPalette::Text, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::Button, QColor(0x1c, 0x21, 0x28));
    p.setColor(QPalette::ButtonText, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::Highlight, QColor(0x06, 0xb6, 0xd4));
    p.setColor(QPalette::HighlightedText, QColor(0x0d, 0x11, 0x17));
    p.setColor(QPalette::ToolTipBase, QColor(0x1c, 0x21, 0x28));
    p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xed, 0xf3));
    p.setColor(QPalette::PlaceholderText, QColor(0x7d, 0x85, 0x90));
    p.setColor(QPalette::Light, QColor(0x21, 0x26, 0x2d));
    p.setColor(QPalette::Midlight, QColor(0x1c, 0x21, 0x28));
    p.setColor(QPalette::Mid, QColor(0x16, 0x1b, 0x22));
    p.setColor(QPalette::Dark, QColor(0x0d, 0x11, 0x17));
    p.setColor(QPalette::Shadow, QColor(0x0d, 0x11, 0x17));
    p.setColor(QPalette::Link, QColor(0x06, 0xb6, 0xd4));
    p.setColor(QPalette::LinkVisited, QColor(0x0e, 0x74, 0x90));
    return p;
}

// ─── Global application stylesheet ──────────────────────────
inline QString appStylesheet()
{
    using namespace c;
    return QStringLiteral(
        // Scrollbars
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar:horizontal { height: 8px; background: transparent; }"
        "QScrollBar::handle { background: #30363d; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:hover { background: #484f58; }"
        "QScrollBar::add-line, QScrollBar::sub-line { border: none; height: 0; width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"

        // Dialogs
        "QDialog { background: #0d1117; }"

        // Toolbars — top bar
        "QToolBar { background: #0d1117; border: none; border-bottom: 1px solid #30363d; spacing: 2px; padding: 4px 6px; }"
        "QToolBar::separator { width: 1px; height: 20px; background: #30363d; margin: 4px 8px; }"
        "QToolBar QToolButton { padding: 5px 12px; border-radius: 6px; color: #7d8590; font-size: 13px; }"
        "QToolBar QToolButton:hover { background: #21262d; color: #e6edf3; }"
        "QToolBar QToolButton:checked { background: rgba(6,182,212,0.15); color: #06b6d4; border: 1px solid rgba(6,182,212,0.4); }"

        // Menu bar
        "QMenuBar { background: #0d1117; color: #e6edf3; border-bottom: 1px solid #30363d; padding: 2px; }"
        "QMenuBar::item { padding: 4px 12px; background: transparent; border-radius: 4px; }"
        "QMenuBar::item:selected { background: #21262d; }"
        "QMenu { background: #161b22; border: 1px solid #30363d; color: #e6edf3; }"
        "QMenu::item { padding: 6px 24px; border-radius: 4px; }"
        "QMenu::item:selected { background: #21262d; }"
        "QMenu::separator { height: 1px; background: #30363d; margin: 4px 8px; }"

        // Status bar
        "QStatusBar { background: #0d1117; color: #7d8590; border-top: 1px solid #30363d; font-size: 12px; }"
        "QStatusBar::item { border: none; }"
        "QStatusBar QLabel { color: #7d8590; padding: 0 8px; }"

        // Dock widgets
        "QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; background: #0d1117; }"
        "QDockWidget::title { background: #161b22; border-bottom: 1px solid #30363d; padding: 6px 12px; color: #e6edf3; font-weight: bold; }"

        // Group boxes
        "QGroupBox { border: 1px solid #30363d; border-radius: 6px; margin-top: 12px; padding-top: 8px; color: #e6edf3; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #7d8590; }"

        // Inputs
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #21262d; border: 1px solid #30363d; border-radius: 4px; padding: 4px 8px; color: #e6edf3; selection-background-color: rgba(6,182,212,0.3); }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #06b6d4; }"
        "QLineEdit::placeholder { color: #484f58; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: #1c2128; border: 1px solid #30363d; color: #e6edf3; selection-background-color: #21262d; }"

        // Buttons
        "QPushButton { background: #21262d; border: 1px solid #30363d; border-radius: 6px; padding: 6px 16px; color: #e6edf3; }"
        "QPushButton:hover { background: #30363d; border-color: #484f58; }"
        "QPushButton:pressed { background: #1c2128; }"
        "QPushButton:disabled { color: #484f58; background: #161b22; }"

        // List widgets
        "QListWidget { background: #0d1117; border: 1px solid #30363d; border-radius: 6px; color: #e6edf3; }"
        "QListWidget::item { padding: 8px 12px; border-bottom: 1px solid #21262d; }"
        "QListWidget::item:hover { background: #161b22; }"
        "QListWidget::item:selected { background: rgba(6,182,212,0.15); color: #06b6d4; border-left: 3px solid #06b6d4; }"

        // Labels
        "QLabel { color: #e6edf3; }"

        // Shared muted placeholder / empty-state text
        "QLabel#emptyPlaceholder { color: #7d8590; font-size: 13px; padding: 20px; }"

        // Checkboxes
        "QCheckBox { color: #e6edf3; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px; border: 1px solid #30363d; background: #21262d; }"
        "QCheckBox::indicator:checked { background: #06b6d4; border-color: #06b6d4; }"

        // Progress bar
        "QProgressBar { background: #21262d; border: 1px solid #30363d; border-radius: 4px; text-align: center; color: #e6edf3; }"
        "QProgressBar::chunk { background: #06b6d4; border-radius: 3px; }"

        // Tab widget
        "QTabWidget::pane { border: 1px solid #30363d; background: #0d1117; }"
        "QTabBar::tab { background: #161b22; color: #7d8590; padding: 6px 16px; border: 1px solid #30363d; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:selected { background: #0d1117; color: #06b6d4; border-bottom: 2px solid #06b6d4; }"
        "QTabBar::tab:hover:!selected { background: #21262d; }"

        // Tool buttons (small)
        "QToolButton { padding: 4px 8px; border-radius: 4px; }"

        // Tree / table views
        "QTreeWidget, QTreeView { background: #0d1117; border: 1px solid #30363d; border-radius: 6px; color: #e6edf3; font-size: 12px; }"
        "QTreeWidget::item, QTreeView::item { padding: 3px 6px; }"
        "QTreeWidget::item:selected, QTreeView::item:selected { background: rgba(6,182,212,0.15); color: #06b6d4; }"
        "QHeaderView::section { background: #161b22; color: #7d8590; border: none; border-bottom: 1px solid #30363d; padding: 4px 6px; font-size: 11px; }"

        // Text editors
        "QTextEdit, QPlainTextEdit { background: #0d1117; border: 1px solid #30363d; border-radius: 6px; color: #e6edf3; selection-background-color: rgba(6,182,212,0.3); }"

        // Sliders
        "QSlider::groove:horizontal { background: #21262d; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #06b6d4; width: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { background: #22d3ee; }"

        // Splitters
        "QSplitter::handle { background: #30363d; }"
        "QSplitter::handle:horizontal { width: 1px; }"
        "QSplitter::handle:vertical { height: 1px; }");
}

// ─── Token resolution ────────────────────────────────────────
// Stylesheets can embed "%Token%" placeholders (e.g. "%Accent%") and be
// passed through resolveTokens(); this keeps large sheets readable while
// all colors stay defined in this file.
inline QString resolveTokens(QString qss)
{
    struct Pair { const char* tok; const char* val; };
    static constexpr Pair kSubs[] = {
        {"%BgBase%",     c::BgBase},
        {"%BgSurface%",  c::BgSurface},
        {"%BgActive%",   c::BgActive},
        {"%BgOverlay%",  c::BgOverlay},
        {"%Border%",     c::Border},
        {"%BorderSub%",  c::BorderSub},
        {"%BorderHi%",   c::BorderHi},
        {"%Text%",       c::Text},
        {"%TextMuted%",  c::TextMuted},
        {"%TextFaint%",  c::TextFaint},
        {"%TextDim%",    c::TextDim},
        {"%TextSoft%",   c::TextSoft},
        {"%Accent%",     c::Accent},
        {"%AccentBright%", c::AccentBright},{"%OnAccent%",     c::OnAccent},
        {"%AccentSoft%", c::AccentSoft},
        {"%AccentEdge%", c::AccentEdge},
        {"%Success%",    c::Success},
        {"%Danger%",     c::Danger},
        {"%Warning%",    c::Warning},
        {"%Info%",       c::Info},
    };
    for (const auto& p : kSubs)
        qss.replace(QLatin1String(p.tok), QLatin1String(p.val));
    return qss;
}

} // namespace theme
} // namespace ogs


