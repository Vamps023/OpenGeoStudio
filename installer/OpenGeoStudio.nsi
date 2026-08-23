; ============================================================
; OpenGeoStudio Windows Installer (NSIS)
; ============================================================
; Build with: makensis installer/OpenGeoStudio.nsi
; Requires NSIS 3.x with the following plugins:
;   - none (standard NSIS only)
;
; The installer packages the deploy/ directory contents into a
; professional Windows installer with Start Menu shortcuts,
; optional desktop shortcut, and uninstall support.
; ============================================================

!define APP_NAME "OpenGeoStudio"
!define APP_VERSION "1.0.0"
!define APP_PUBLISHER "OpenGeoStudio Project"
!define APP_URL "https://github.com/Vamps023/OpenGeoStudio"
!define APP_EXE "OpenGeoStudio.exe"
!define APP_REGKEY "Software\OpenGeoStudio"
!define APP_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenGeoStudio"

; ---- Compiler settings ----
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
RequestExecutionLevel admin

; ---- Version info embedded in installer ----
VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "OpenGeoStudio"
VIAddVersionKey "FileVersion" "1.0.0.0"
VIAddVersionKey "ProductVersion" "1.0.0.0"
VIAddVersionKey "CompanyName" "OpenGeoStudio Project"
VIAddVersionKey "FileDescription" "OpenGeoStudio Installer"
VIAddVersionKey "LegalCopyright" "OpenGeoStudio Project"

; ---- Include Modern UI 2 ----
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; ---- Variables ----
Var StartMenuFolder

; ---- Modern UI Settings ----
!define MUI_ABORTWARNING
!define MUI_ICON "app-icon.ico"
!define MUI_UNICON "app-icon.ico"

; ---- Pages ----
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY

; Start Menu page
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${APP_REGKEY}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; ---- Uninstaller pages ----
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; ---- Languages ----
!insertmacro MUI_LANGUAGE "English"

; ---- Installer name and output ----
Name "${APP_NAME} ${APP_VERSION}"
OutFile "..\build\OpenGeoStudio-${APP_VERSION}-Windows-x64.exe"
InstallDir "$PROGRAMFILES64\OpenGeoStudio"
InstallDirRegKey HKCU "${APP_REGKEY}" "InstallDir"
ShowInstDetails show
ShowUnInstDetails show

; ============================================================
; Install Sections
; ============================================================

Section "OpenGeoStudio (required)" SecCore
    SectionIn RO

    SetOutPath "$INSTDIR"
    
    ; Copy all files from the deploy directory
    ; Exclude transient files (log.txt, *.log) that may be locked by a
    ; running instance of the application.
    File /r /x log.txt /x *.log /x *.tmp "..\build\deploy\*.*"

    ; Write installation registry keys
    WriteRegStr HKCU "${APP_REGKEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${APP_REGKEY}" "Version" "${APP_VERSION}"
    
    ; Write uninstall registry keys
    WriteRegStr HKCU "${APP_UNINST_KEY}" "DisplayName" "OpenGeoStudio ${APP_VERSION}"
    WriteRegStr HKCU "${APP_UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKCU "${APP_UNINST_KEY}" "DisplayIcon" "$\"$INSTDIR\${APP_EXE}$\""
    WriteRegStr HKCU "${APP_UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${APP_UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKCU "${APP_UNINST_KEY}" "URLInfoAbout" "${APP_URL}"
    WriteRegStr HKCU "${APP_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    
    ; Calculate installed size for Add/Remove Programs
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKCU "${APP_UNINST_KEY}" "EstimatedSize" "$0"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Start Menu shortcuts" SecStartMenu
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
        CreateShortcut "$SMPROGRAMS\$StartMenuFolder\OpenGeoStudio.lnk" "$INSTDIR\${APP_EXE}"
        CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Uninstall OpenGeoStudio.lnk" "$INSTDIR\uninstall.exe"
    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortcut "$DESKTOP\OpenGeoStudio.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

; ============================================================
; Section Descriptions
; ============================================================
LangString DESC_SecCore ${LANG_ENGLISH} "OpenGeoStudio application and all required runtime files."
LangString DESC_SecStartMenu ${LANG_ENGLISH} "Create Start Menu shortcuts for easy access."
LangString DESC_SecDesktop ${LANG_ENGLISH} "Create a desktop shortcut for quick launch."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} $(DESC_SecCore)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ============================================================
; Uninstaller
; ============================================================
Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR"
    
    ; Remove Start Menu shortcuts
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    Delete "$SMPROGRAMS\$StartMenuFolder\OpenGeoStudio.lnk"
    Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall OpenGeoStudio.lnk"
    RMDir "$SMPROGRAMS\$StartMenuFolder"
    
    ; Remove desktop shortcut
    Delete "$DESKTOP\OpenGeoStudio.lnk"
    
    ; Remove registry keys
    DeleteRegKey HKCU "${APP_UNINST_KEY}"
    DeleteRegKey HKCU "${APP_REGKEY}"
SectionEnd
