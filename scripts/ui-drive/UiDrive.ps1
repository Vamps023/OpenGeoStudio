# ═══════════════════════════════════════════════════════════
# UiDrive.ps1 — Desktop automation helpers for OpenGeoStudio UI
# ═══════════════════════════════════════════════════════════
# Dot-source this file, then use:
#   . D:\git\OpenGeoStudio-Qt\scripts\ui-drive\UiDrive.ps1
#   Get-UiaTree                      # list all UI elements (name + rect)
#   Invoke-UiaClick -Name "Terrain"  # click element by name substring
#   Send-WindowClick -Fx 0.25 -Fy 0.4
#   Send-WindowRightClick -Fx 0.5 -Fy 0.5
#   Send-Scroll -Fx 0.5 -Fy 0.5 -Clicks 3          # wheel up (zoom in)
#   Send-Scroll -Fx 0.5 -Fy 0.5 -Clicks 3 -Down    # wheel down (zoom out)
#   Send-Drag -Fx1 0.3 -Fy1 0.5 -Fx2 0.6 -Fy2 0.5  # left-button drag (pan)
#   Find-UiaElement -Name "Road"                   # element rect + center
#   Wait-UiaElement -Name "Road" -TimeoutMs 10000  # poll until visible
#   Save-Screenshot -OutPath shot.png
#   Send-KeysToApp -Keys "{ENTER}"
#
# Uses only .NET Framework / Win32 — no external dependencies.
# ═══════════════════════════════════════════════════════════

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class OgsWin32 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, int dwData, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@
[OgsWin32]::SetProcessDPIAware() | Out-Null

$script:OgsProcName = "OpenGeoStudio"

function Get-AppWindow {
    param([string]$ProcName = $script:OgsProcName)
    $p = Get-Process -Name $ProcName -ErrorAction SilentlyContinue |
         Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($p) { return $p.MainWindowHandle }
    return [IntPtr]::Zero
}

function Focus-App {
    param([string]$ProcName = $script:OgsProcName)
    $h = Get-AppWindow $ProcName
    if ($h -eq [IntPtr]::Zero) { throw "[$ProcName] window not found" }
    [OgsWin32]::ShowWindow($h, 9) | Out-Null        # SW_RESTORE
    [OgsWin32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 350
    return $h
}

function Get-AppRect {
    param([string]$ProcName = $script:OgsProcName)
    $h = Focus-App $ProcName
    $r = New-Object OgsWin32+RECT
    [OgsWin32]::GetWindowRect($h, [ref]$r) | Out-Null
    [PSCustomObject]@{ L=$r.Left; T=$r.Top; R=$r.Right; B=$r.Bottom;
                       W=($r.Right-$r.Left); H=($r.Bottom-$r.Top); Handle=$h }
}

function Save-Screenshot {
    param([Parameter(Mandatory)][string]$OutPath,
          [string]$ProcName = $script:OgsProcName)
    $r = Get-AppRect $ProcName
    $bmp = New-Object System.Drawing.Bitmap($r.W, $r.H)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($r.W, $r.H)))
    $g.Dispose()
    $dir = Split-Path $OutPath -Parent
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return $r
}

function Send-RawClick {
    param([Parameter(Mandatory)][int]$X, [Parameter(Mandatory)][int]$Y)
    [OgsWin32]::SetCursorPos($X, $Y) | Out-Null
    Start-Sleep -Milliseconds 150
    [OgsWin32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
    Start-Sleep -Milliseconds 70
    [OgsWin32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
    Start-Sleep -Milliseconds 250
}

function Send-WindowClick {
    param([Parameter(Mandatory)][double]$Fx,
          [Parameter(Mandatory)][double]$Fy,
          [string]$ProcName = $script:OgsProcName)
    $r = Get-AppRect $ProcName
    Send-RawClick ([int]($r.L + $r.W * $Fx)) ([int]($r.T + $r.H * $Fy))
}

function Send-WindowRightClick {
    param([Parameter(Mandatory)][double]$Fx,
          [Parameter(Mandatory)][double]$Fy,
          [string]$ProcName = $script:OgsProcName)
    $r = Get-AppRect $ProcName
    $x = [int]($r.L + $r.W * $Fx); $y = [int]($r.T + $r.H * $Fy)
    [OgsWin32]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 150
    [OgsWin32]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)  # RIGHTDOWN
    Start-Sleep -Milliseconds 70
    [OgsWin32]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)  # RIGHTUP
    Start-Sleep -Milliseconds 250
}

function Send-Scroll {
    # Mouse wheel at a fractional position inside the app window.
    # Positive -Clicks scrolls up (typically zooms in); add -Down to invert.
    param([double]$Fx = 0.5,
          [double]$Fy = 0.5,
          [ValidateRange(1, 50)][int]$Clicks = 3,
          [switch]$Down,
          [string]$ProcName = $script:OgsProcName)
    $r = Get-AppRect $ProcName
    $x = [int]($r.L + $r.W * $Fx); $y = [int]($r.T + $r.H * $Fy)
    [OgsWin32]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 150
    $delta = $Clicks * 120
    if ($Down) { $delta = -$delta }
    [OgsWin32]::mouse_event(0x0800, 0, 0, $delta, [UIntPtr]::Zero)  # WHEEL
    Start-Sleep -Milliseconds 250
}

function Send-Drag {
    # Left-button drag between two fractional window positions.
    # Moves through interpolated points so Qt/OGRE sees a smooth gesture.
    param([Parameter(Mandatory)][double]$Fx1,
          [Parameter(Mandatory)][double]$Fy1,
          [Parameter(Mandatory)][double]$Fx2,
          [Parameter(Mandatory)][double]$Fy2,
          [ValidateRange(2, 200)][int]$Steps = 20,
          [int]$StepDelayMs = 15,
          [string]$ProcName = $script:OgsProcName)
    $r = Get-AppRect $ProcName
    $x1 = [int]($r.L + $r.W * $Fx1); $y1 = [int]($r.T + $r.H * $Fy1)
    $x2 = [int]($r.L + $r.W * $Fx2); $y2 = [int]($r.T + $r.H * $Fy2)
    [OgsWin32]::SetCursorPos($x1, $y1) | Out-Null
    Start-Sleep -Milliseconds 150
    [OgsWin32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)      # LEFTDOWN
    Start-Sleep -Milliseconds 80
    for ($i = 1; $i -le $Steps; $i++) {
        $t = $i / $Steps
        [OgsWin32]::SetCursorPos([int]($x1 + ($x2 - $x1) * $t),
                                 [int]($y1 + ($y2 - $y1) * $t)) | Out-Null
        Start-Sleep -Milliseconds $StepDelayMs
    }
    Start-Sleep -Milliseconds 80
    [OgsWin32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)      # LEFTUP
    Start-Sleep -Milliseconds 250
}

function Send-KeysToApp {
    param([Parameter(Mandatory)][string]$Keys,
          [string]$ProcName = $script:OgsProcName)
    Focus-App $ProcName | Out-Null
    Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
    Start-Sleep -Milliseconds 200
}

function Find-UiaElement {
    # Locate a single UIA element by name substring.
    # Returns Name, ControlType, rect (L/T/W/H) and center point (CenterX/CenterY),
    # or throws if not found.
    param([Parameter(Mandatory)][string]$Name,
          [string]$ProcName = $script:OgsProcName)
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $h = Get-AppWindow $ProcName
    if ($h -eq [IntPtr]::Zero) { throw "window not found" }
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
    $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                         [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($e in $all) {
        try {
            $n = $e.Current.Name
            if ($n -and $n.IndexOf($Name, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $rc = $e.Current.BoundingRectangle
                return [PSCustomObject]@{
                    Element     = $e
                    Name        = ($n -replace "`r?`n", ' / ')
                    ControlType = $e.Current.ControlType.ProgrammaticName.Replace('ControlType.', '')
                    L = [int]$rc.X; T = [int]$rc.Y
                    W = [int]$rc.Width; H = [int]$rc.Height
                    CenterX = [int]($rc.X + $rc.Width  / 2)
                    CenterY = [int]($rc.Y + $rc.Height / 2)
                }
            }
        } catch { }
    }
    throw "UIA element not found matching: $Name"
}

function Wait-UiaElement {
    # Poll the UIA tree until an element matching -Name appears, or throw on timeout.
    param([Parameter(Mandatory)][string]$Name,
          [ValidateRange(100, 600000)][int]$TimeoutMs = 10000,
          [ValidateRange(50, 5000)][int]$PollMs = 250,
          [string]$ProcName = $script:OgsProcName)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { return Find-UiaElement -Name $Name -ProcName $ProcName } catch { }
        Start-Sleep -Milliseconds $PollMs
    }
    throw "Timed out after ${TimeoutMs}ms waiting for UIA element: $Name"
}

function Get-UiaTree {
    param([string]$ProcName = $script:OgsProcName)
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $h = Get-AppWindow $ProcName
    if ($h -eq [IntPtr]::Zero) { throw "window not found" }
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
    $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                         [System.Windows.Automation.Condition]::TrueCondition)
    $out = @()
    foreach ($e in $all) {
        try {
            $n = $e.Current.Name
            if (-not $n) { continue }
            $rc = $e.Current.BoundingRectangle
            $out += ("{0} | {1} | x={2} y={3} w={4} h={5}" -f `
                $e.Current.ControlType.ProgrammaticName.Replace('ControlType.',''), `
                ($n -replace "`r?`n", ' / '), `
                [int]$rc.X, [int]$rc.Y, [int]$rc.Width, [int]$rc.Height)
        } catch { }
    }
    return $out
}

function Invoke-UiaClick {
    param([Parameter(Mandatory)][string]$Name,
          [switch]$DoubleClick,
          [string]$ProcName = $script:OgsProcName)
    $el = Find-UiaElement -Name $Name -ProcName $ProcName
    try {
        $pt = $el.Element.GetClickablePoint()
    } catch {
        # Fall back to element center when no clickable point is exposed
        $pt = New-Object System.Windows.Point($el.CenterX, $el.CenterY)
    }
    Send-RawClick ([int]$pt.X) ([int]$pt.Y)
    if ($DoubleClick) {
        Start-Sleep -Milliseconds 120
        Send-RawClick ([int]$pt.X) ([int]$pt.Y)
    }
    return ("clicked [{0}]" -f $el.Name)
}

