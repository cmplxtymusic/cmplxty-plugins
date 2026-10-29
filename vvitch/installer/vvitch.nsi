; CMPLXTY VVITCH installer (NSIS 3, free). Derived from the Quartz installer template. Build with scripts/build-installer.sh, or:
;   makensis -DVERSION=1.0.0 -DSTAGE=<dir with "CMPLXTY VVITCH.vst3" + ".clap"> -DOUTFILE=<out.exe> vvitch.nsi
; - All users (admin, UAC prompt):  Common Files\VST3 and Common Files\CLAP + HKLM uninstall entry
; - Current user (UAC declined / no admin / /CURRENTUSER):  %LOCALAPPDATA%\Programs\Common\{VST3,CLAP} + HKCU entry
; - Upgrade: stable app id + registry key; installs over the previous version in the same scope (no uninstall first),
;   never touches %APPDATA%\CMPLXTY\VVITCH (settings)
; - Switches: /S silent, /ALLUSERS, /CURRENTUSER, /NOCLAP, /NOVST3
; - Uses no comctl32/TaskDialog APIs.
Unicode true
!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef STAGE
  !define STAGE "..\dist\stage-windows-x64"
!endif
!ifndef OUTFILE
  !define OUTFILE "CMPLXTY-VVITCH-Setup-${VERSION}-win.exe"
!endif
!define APPNAME "CMPLXTY VVITCH"
!define PUBLISHER "CMPLXTY"
!define PLUG "CMPLXTY VVITCH"
!define APPID "{003A6760-370F-40F0-81E2-14A94AE8461F}"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}"
!define VKEY "Software\CMPLXTY\VVITCH"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WordFunc.nsh"
!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro VersionCompare

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ManifestDPIAware true
ShowInstDetails show
BrandingText "${APPNAME} ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "${APPNAME} VST3 + CLAP installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "CMPLXTY, GPLv3"

!define MUI_ICON "vvitch.ico"
!define MUI_UNICON "vvitch.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_WELCOMEPAGE_TITLE "${APPNAME} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs the VVITCH plug-in (witch house polysynth; VST3 and CLAP) like any regular plug-in.$\r$\n$\r$\nIf VVITCH is already installed it is updated in place. Your settings and your own presets are kept.$\r$\n$\r$\nClose your DAW before continuing."
!define MUI_FINISHPAGE_TITLE "VVITCH is installed"
!define MUI_FINISHPAGE_TEXT "The plug-in is in your VST3 and CLAP folders.$\r$\n$\r$\nFL Studio: Options > Manage plugins > Find plugins (VVITCH appears under Installed > Effects).$\r$\n$\r$\nYour settings and presets live in %APPDATA%\CMPLXTY\VVITCH and are never removed by updates or by uninstalling."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var AllUsers     ; "1" all users (HKLM), "0" current user (HKCU)
Var Params
Var OldVer
Var Vst3Dir
Var ClapDir
Var AppDir
Var Tmp

;----------------------------------------------------------------------------------------------
; Delete a file; a DLL loaded by a DAW can be renamed but not deleted, so rename it out of the way. $R9 = file path.
Function SafeDelete
  ${If} ${FileExists} "$R9"
    ClearErrors
    Delete "$R9"
    ${If} ${FileExists} "$R9"
      Delete "$R9.old"
      Rename "$R9" "$R9.old"
      Delete /REBOOTOK "$R9.old"
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "VST3 plug-in" SecVst3
  SetRegView 64
  StrCpy $0 "$Vst3Dir\${PLUG}.vst3"
  DetailPrint "Installing VST3 to $0"
  retryVst:
  StrCpy $R9 "$0\Contents\x86_64-win\${PLUG}.vst3"
  Call SafeDelete
  Delete "$0\Contents\Resources\moduleinfo.json"
  ClearErrors
  CreateDirectory "$0\Contents\x86_64-win"
  SetOutPath "$0\Contents\x86_64-win"
  File "/oname=${PLUG}.vst3" "${STAGE}\${PLUG}.vst3\Contents\x86_64-win\${PLUG}.vst3"
  ${If} ${Errors}
    ${If} ${Silent}
      Abort "Could not write ${PLUG}.vst3 (is your DAW running?)."
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${PLUG}.vst3 is in use. Close your DAW and press Retry." IDRETRY retryVst
    Abort
  ${EndIf}
  CreateDirectory "$0\Contents\Resources"
  SetOutPath "$0\Contents\Resources"
  File "${STAGE}\${PLUG}.vst3\Contents\Resources\moduleinfo.json"
  WriteRegStr SHCTX "${VKEY}" "VST3Dir" "$Vst3Dir"
SectionEnd

Section "CLAP plug-in" SecClap
  SetRegView 64
  DetailPrint "Installing CLAP to $ClapDir"
  retryClap:
  StrCpy $R9 "$ClapDir\${PLUG}.clap"
  Call SafeDelete
  ClearErrors
  CreateDirectory "$ClapDir"
  SetOutPath "$ClapDir"
  File "${STAGE}\${PLUG}.clap"
  ${If} ${Errors}
    ${If} ${Silent}
      Abort "Could not write ${PLUG}.clap (is your DAW running?)."
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${PLUG}.clap is in use. Close your DAW and press Retry." IDRETRY retryClap
    Abort
  ${EndIf}
  WriteRegStr SHCTX "${VKEY}" "ClapDir" "$ClapDir"
SectionEnd

Section "-Common"
  SetRegView 64
  CreateDirectory "$AppDir"
  SetOutPath "$AppDir"
  File "LICENSES.txt"
  File "/oname=VVITCH.ico" "vvitch.ico"
  WriteUninstaller "$AppDir\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\CMPLXTY"
  CreateShortcut "$SMPROGRAMS\CMPLXTY\Uninstall VVITCH.lnk" "$AppDir\Uninstall.exe" "" "$AppDir\VVITCH.ico"

  ; registry (Apps & features); SHCTX is HKLM after SetShellVarContext all, HKCU after current
  WriteRegStr SHCTX "${UNKEY}" "DisplayName" "${APPNAME} (VST3 + CLAP)"
  WriteRegStr SHCTX "${UNKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr SHCTX "${UNKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr SHCTX "${UNKEY}" "DisplayIcon" "$AppDir\VVITCH.ico"
  WriteRegStr SHCTX "${UNKEY}" "InstallLocation" "$AppDir"
  WriteRegStr SHCTX "${UNKEY}" "VST3Dir" "$Vst3Dir"
  WriteRegStr SHCTX "${UNKEY}" "ClapDir" "$ClapDir"
  WriteRegStr SHCTX "${UNKEY}" "UninstallString" '"$AppDir\Uninstall.exe"'
  WriteRegStr SHCTX "${UNKEY}" "QuietUninstallString" '"$AppDir\Uninstall.exe" /S'
  WriteRegDWORD SHCTX "${UNKEY}" "NoModify" 1
  WriteRegDWORD SHCTX "${UNKEY}" "NoRepair" 1
  WriteRegDWORD SHCTX "${UNKEY}" "EstimatedSize" 12000
  WriteRegStr SHCTX "${VKEY}" "Version" "${VERSION}"
  ${If} $AllUsers == "1"
    DeleteRegKey HKCU "${UNKEY}"
  ${EndIf}
  ; plug-in settings (%APPDATA%\CMPLXTY\VVITCH) are deliberately not touched here.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  DetailPrint "Done."
SectionEnd

Function TryElevate
  System::Call 'shell32::ShellExecuteW(p 0, w "runas", w "$EXEPATH", w "$R0", p 0, i 1) p .r1'
  ${If} $1 > 32
    Push 1
  ${Else}
    Push 0
  ${EndIf}
FunctionEnd

Function .onInit
  SetRegView 64
  ${GetParameters} $Params
  StrCpy $AllUsers ""
  StrCpy $OldVer ""

  ReadRegStr $OldVer HKLM "${UNKEY}" "DisplayVersion"
  ${If} $OldVer != ""
    StrCpy $AllUsers "1"
  ${Else}
    ReadRegStr $OldVer HKCU "${UNKEY}" "DisplayVersion"
    ${If} $OldVer != ""
      StrCpy $AllUsers "0"
    ${EndIf}
  ${EndIf}

  ClearErrors
  ${GetOptions} $Params "/ALLUSERS" $0
  ${IfNot} ${Errors}
    ${If} $AllUsers == ""
      StrCpy $AllUsers "1"
    ${EndIf}
  ${EndIf}
  ClearErrors
  ${GetOptions} $Params "/CURRENTUSER" $0
  ${IfNot} ${Errors}
    ${If} $AllUsers == ""
      StrCpy $AllUsers "0"
    ${EndIf}
  ${EndIf}

  System::Call 'shell32::IsUserAnAdmin() i .r0'
  StrCpy $Tmp $0   ; 1 if running elevated/admin

  ${If} $AllUsers == ""
    ${If} $Tmp == "1"
      StrCpy $AllUsers "1"
    ${ElseIf} ${Silent}
      StrCpy $AllUsers "0"
    ${Else}
      MessageBox MB_YESNO|MB_ICONQUESTION "Install VVITCH for all users in C:\Program Files\Common Files?$\r$\n$\r$\nYes = all users (asks for administrator permission)$\r$\nNo = only for your account" IDYES wantAll
      StrCpy $AllUsers "0"
      Goto scopeDone
      wantAll:
      StrCpy $AllUsers "1"
    ${EndIf}
  ${EndIf}
  scopeDone:

  ; all-users install needs elevation: relaunch elevated; if the user declines UAC, fall back to a per-user install
  ${If} $AllUsers == "1"
  ${AndIf} $Tmp != "1"
    StrCpy $R0 '$Params'
    ClearErrors
    ${GetOptions} $Params "/ALLUSERS" $0
    ${If} ${Errors}
      StrCpy $R0 '$Params /ALLUSERS'
    ${EndIf}
    Call TryElevate
    Pop $0
    ${If} $0 == "1"
      Quit
    ${EndIf}
    ${If} $OldVer != ""
    ${AndIf} ${Silent}
      Abort "An all-users install of VVITCH $OldVer exists and administrator permission was not granted."
    ${EndIf}
    ReadRegStr $0 HKLM "${UNKEY}" "DisplayVersion"
    ${If} $0 != ""
      MessageBox MB_OK|MB_ICONSTOP "VVITCH $0 is installed for all users. Updating it needs administrator permission, which was not granted. Run the installer again and choose Yes at the permission prompt."
      Abort
    ${EndIf}
    ${IfNot} ${Silent}
      MessageBox MB_OK|MB_ICONINFORMATION "Administrator permission was not granted.$\r$\nVVITCH will be installed for your account only (%LOCALAPPDATA%\Programs\Common)."
    ${EndIf}
    StrCpy $AllUsers "0"
  ${EndIf}

  ; downgrade guard (interactive only)
  ${If} $OldVer != ""
  ${AndIfNot} ${Silent}
    ${VersionCompare} "$OldVer" "${VERSION}" $0
    ${If} $0 == 1
      MessageBox MB_YESNO|MB_ICONQUESTION "A newer version of VVITCH ($OldVer) is installed. Replace it with ${VERSION}?" IDYES +2
      Abort
    ${EndIf}
  ${EndIf}

  ${If} $AllUsers == "1"
    SetShellVarContext all
    StrCpy $Vst3Dir "$COMMONFILES64\VST3"
    StrCpy $ClapDir "$COMMONFILES64\CLAP"
    StrCpy $AppDir "$PROGRAMFILES64\CMPLXTY\VVITCH"
  ${Else}
    SetShellVarContext current
    StrCpy $Vst3Dir "$LOCALAPPDATA\Programs\Common\VST3"
    StrCpy $ClapDir "$LOCALAPPDATA\Programs\Common\CLAP"
    StrCpy $AppDir "$LOCALAPPDATA\Programs\CMPLXTY\VVITCH"
  ${EndIf}

  ; component switches for silent installs
  ClearErrors
  ${GetOptions} $Params "/NOCLAP" $0
  ${IfNot} ${Errors}
    SectionSetFlags ${SecClap} 0
  ${EndIf}
  ClearErrors
  ${GetOptions} $Params "/NOVST3" $0
  ${IfNot} ${Errors}
    SectionSetFlags ${SecVst3} 0
  ${EndIf}
FunctionEnd

;----------------------------------------------------------------------------------------------
Var UnAll
Var UnVst3
Var UnClap
Var UnParams

Function un.SafeDelete
  ${If} ${FileExists} "$R9"
    Delete "$R9"
    ${If} ${FileExists} "$R9"
      Delete "$R9.old"
      Rename "$R9" "$R9.old"
      Delete /REBOOTOK "$R9.old"
    ${EndIf}
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
  ${GetParameters} $UnParams
  StrCpy $UnAll "0"
  ReadRegStr $0 HKLM "${UNKEY}" "InstallLocation"
  ${If} $0 == "$INSTDIR"
    StrCpy $UnAll "1"
  ${EndIf}
  System::Call 'shell32::IsUserAnAdmin() i .r1'
  ${If} $UnAll == "1"
  ${AndIf} $1 != "1"
    System::Call 'shell32::ShellExecuteW(p 0, w "runas", w "$EXEPATH", w "$UnParams", p 0, i 1) p .r2'
    ${If} $2 > 32
      Quit
    ${EndIf}
    MessageBox MB_OK|MB_ICONSTOP "Removing the all-users install of VVITCH needs administrator permission."
    Abort
  ${EndIf}
  ${If} $UnAll == "1"
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}
  ReadRegStr $UnVst3 SHCTX "${UNKEY}" "VST3Dir"
  ReadRegStr $UnClap SHCTX "${UNKEY}" "ClapDir"
  ${If} $UnVst3 == ""
    ${If} $UnAll == "1"
      StrCpy $UnVst3 "$COMMONFILES64\VST3"
    ${Else}
      StrCpy $UnVst3 "$LOCALAPPDATA\Programs\Common\VST3"
    ${EndIf}
  ${EndIf}
  ${If} $UnClap == ""
    ${If} $UnAll == "1"
      StrCpy $UnClap "$COMMONFILES64\CLAP"
    ${Else}
      StrCpy $UnClap "$LOCALAPPDATA\Programs\Common\CLAP"
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetRegView 64
  StrCpy $0 "$UnVst3\${PLUG}.vst3"
  DetailPrint "Removing $0"
  StrCpy $R9 "$0\Contents\x86_64-win\${PLUG}.vst3"
  Call un.SafeDelete
  Delete "$0\Contents\x86_64-win\${PLUG}.vst3.old"
  Delete "$0\Contents\Resources\moduleinfo.json"
  RMDir "$0\Contents\Resources"
  RMDir "$0\Contents\x86_64-win"
  RMDir "$0\Contents"
  RMDir "$0"
  DetailPrint "Removing $UnClap\${PLUG}.clap"
  StrCpy $R9 "$UnClap\${PLUG}.clap"
  Call un.SafeDelete
  Delete "$UnClap\${PLUG}.clap.old"
  Delete "$SMPROGRAMS\CMPLXTY\Uninstall VVITCH.lnk"
  RMDir "$SMPROGRAMS\CMPLXTY"
  Delete "$INSTDIR\LICENSES.txt"
  Delete "$INSTDIR\VVITCH.ico"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  RMDir "$INSTDIR\.."
  DeleteRegKey SHCTX "${UNKEY}"
  DeleteRegKey SHCTX "${VKEY}"
  DeleteRegKey /ifempty SHCTX "Software\CMPLXTY"
  ; settings are kept unless the user asks otherwise (interactive only)
  ${IfNot} ${Silent}
    ${If} ${FileExists} "$APPDATA\CMPLXTY\VVITCH\*.*"
      MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your VVITCH settings in $APPDATA\CMPLXTY\VVITCH?" IDNO keepSettings
      RMDir /r "$APPDATA\CMPLXTY\VVITCH"
      RMDir "$APPDATA\CMPLXTY"
      keepSettings:
    ${EndIf}
  ${EndIf}
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
