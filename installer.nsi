; ── EKG-EQ VST3 Installer ──────────────────────────────────────────
; Lumina Aerospace · AuraTone Technology
; Installs to C:\Program Files\Common Files\VST3\ (industry standard)

Unicode True

!define APP_NAME       "EKG-EQ"
!define APP_DISPLAY    "EKG·EQ"
!define VERSION        "1.0.0"
!define PUBLISHER      "Lumina Aerospace"
!define UNINSTKEY      "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}-VST3"
!define VST3_BUNDLE    "build\VST3\Release\EKG-EQ.vst3"
!define REGVIEW        64

!include "MUI2.nsh"
!include "x64.nsh"

Name "${APP_DISPLAY} ${VERSION}"
OutFile "EKG-EQ-VST3-Setup-${VERSION}-x64.exe"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; ── MUI pages ──────────────────────────────────────────────────────
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; ── Install ─────────────────────────────────────────────────────────
Section "EKG-EQ VST3" SecMain
    SetRegView 64

    ; Destination: C:\Program Files\Common Files\VST3\EKG-EQ.vst3\
    ReadRegStr $0 HKLM \
        "SOFTWARE\Microsoft\Windows\CurrentVersion" "CommonFilesDir"
    StrCmp $0 "" 0 +2
    StrCpy $0 "C:\Program Files\Common Files"

    StrCpy $INSTDIR "$0\VST3\EKG-EQ.vst3"

    ; DLL
    SetOutPath "$INSTDIR\Contents\x86_64-win"
    File "${VST3_BUNDLE}\Contents\x86_64-win\EKG-EQ.vst3"

    ; Bundle metadata (Windows Explorer plugin icon)
    SetOutPath "$INSTDIR"
    File "${VST3_BUNDLE}\PlugIn.ico"

    ; ── Add/Remove Programs entry ──────────────────────────────────
    WriteRegStr   HKLM "${UNINSTKEY}" "DisplayName"     "${APP_DISPLAY} VST3"
    WriteRegStr   HKLM "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   HKLM "${UNINSTKEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify"        1
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair"        1

    ; Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    MessageBox MB_OK "${APP_DISPLAY} VST3 installed.$\n$\nRescan plugins in your DAW to load it.$\nLook for EKG-EQ under Fx | EQ."
SectionEnd

; ── Uninstall ───────────────────────────────────────────────────────
Section "Uninstall"
    SetRegView 64

    ReadRegStr $0 HKLM \
        "SOFTWARE\Microsoft\Windows\CurrentVersion" "CommonFilesDir"
    StrCmp $0 "" 0 +2
    StrCpy $0 "C:\Program Files\Common Files"

    StrCpy $INSTDIR "$0\VST3\EKG-EQ.vst3"

    Delete "$INSTDIR\Contents\x86_64-win\EKG-EQ.vst3"
    RMDir  "$INSTDIR\Contents\x86_64-win"
    RMDir  "$INSTDIR\Contents"
    Delete "$INSTDIR\PlugIn.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
