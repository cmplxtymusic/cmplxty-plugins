; Quartz installer (NSIS 3, free). Build:  makensis -DVERSION=1.0.5 -DPAYLOAD=<Quartz.vst3 file> -DOUTFILE=Quartz-Setup.exe quartz.nsi
; - All users (admin, UAC prompt):  C:\Program Files\Common Files\VST3\Quartz.vst3     + HKLM uninstall entry
; - Current user (UAC declined / no admin / /CURRENTUSER):  %LOCALAPPDATA%\Programs\Common\VST3\Quartz.vst3 + HKCU entry
; - Upgrade: stable app id + registry key; installs over the previous version in the same scope, never touches %APPDATA%\Quartz
; - Switches: /S silent, /ALLUSERS, /CURRENTUSER
Unicode true
!ifndef VERSION
  !define VERSION "1.0.5"
!endif
!ifndef PAYLOAD
  !define PAYLOAD "Quartz.vst3"
!endif
!ifndef OUTFILE
  !define OUTFILE "Quartz-Setup.exe"
!endif
!define APPNAME "Quartz"
!define PUBLISHER "Quartz Audio"
!define APPID "{B12ECC04-91A0-4906-86E5-811035004BE9}"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}"

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
BrandingText "Quartz ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Quartz"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "Quartz VST3 installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Quartz Audio"

!define MUI_ICON "quartz.ico"
!define MUI_UNICON "quartz.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Quartz ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs the Quartz VST3 plug-in (Quartz engine + Alter) like any regular plug-in.$\r$\n$\r$\nIf Quartz is already installed, it is updated in place. Your own presets and settings are kept.$\r$\n$\r$\nClose your DAW before continuing."
!define MUI_FINISHPAGE_TITLE "Quartz is installed"
!define MUI_FINISHPAGE_TEXT "Quartz.vst3 is in your VST3 folder.$\r$\n$\r$\nFL Studio: Options > Manage plugins > Find plugins (Quartz appears under Installed > Effects).$\r$\n$\r$\nYour presets live in %APPDATA%\Quartz and are never removed by updates."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var AllUsers     ; "1" all users (HKLM), "0" current user (HKCU)
Var Params
Var OldVer
Var Vst3Dir
Var AppDir
Var Tmp

;----------------------------------------------------------------------------------------------
Function IsElevated
  System::Call 'shell32::IsUserAnAdmin() i .r0'
  Push $0
FunctionEnd

; Relaunch this exe elevated with $R0 as its arguments. Pushes 1 if the new process was started, else 0.
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

  ; existing install?
  ReadRegStr $OldVer HKLM "${UNKEY}" "DisplayVersion"
  ${If} $OldVer != ""
    StrCpy $AllUsers "1"
  ${Else}
    ReadRegStr $OldVer HKCU "${UNKEY}" "DisplayVersion"
    ${If} $OldVer != ""
      StrCpy $AllUsers "0"
    ${EndIf}
  ${EndIf}

  ; explicit switches win when no install exists yet
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

  Call IsElevated
  Pop $Tmp   ; 1 if running elevated/admin

  ${If} $AllUsers == ""
    ${If} $Tmp == "1"
      StrCpy $AllUsers "1"
    ${ElseIf} ${Silent}
      StrCpy $AllUsers "0"
    ${Else}
      MessageBox MB_YESNO|MB_ICONQUESTION "Install Quartz for all users in C:\Program Files\Common Files\VST3?$\r$\n$\r$\nYes = all users (asks for administrator permission)$\r$\nNo = only for your account" IDYES wantAll
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
    ; elevation declined / unavailable
    ${If} $OldVer != ""
    ${AndIf} ${Silent}
      Abort "An all-users install of Quartz $OldVer exists and administrator permission was not granted."
    ${EndIf}
    ReadRegStr $0 HKLM "${UNKEY}" "DisplayVersion"
    ${If} $0 != ""
      MessageBox MB_OK|MB_ICONSTOP "Quartz $0 is installed for all users. Updating it needs administrator permission, which was not granted. Run the installer again and choose Yes at the permission prompt."
      Abort
    ${EndIf}
    ${IfNot} ${Silent}
      MessageBox MB_OK|MB_ICONINFORMATION "Administrator permission was not granted.$\r$\nQuartz will be installed for your account only (%LOCALAPPDATA%\Programs\Common\VST3)."
    ${EndIf}
    StrCpy $AllUsers "0"
  ${EndIf}

  ; downgrade guard (interactive only)
  ${If} $OldVer != ""
  ${AndIfNot} ${Silent}
    ${VersionCompare} "$OldVer" "${VERSION}" $0
    ${If} $0 == 1
      MessageBox MB_YESNO|MB_ICONQUESTION "A newer version of Quartz ($OldVer) is installed. Replace it with ${VERSION}?" IDYES +2
      Abort
    ${EndIf}
  ${EndIf}

  ; locations
  ${If} $AllUsers == "1"
    SetShellVarContext all
    StrCpy $Vst3Dir "$COMMONFILES64\VST3"
    StrCpy $AppDir "$PROGRAMFILES64\Quartz"
  ${Else}
    SetShellVarContext current
    StrCpy $Vst3Dir "$LOCALAPPDATA\Programs\Common\VST3"
    StrCpy $AppDir "$LOCALAPPDATA\Programs\Quartz"
  ${EndIf}
FunctionEnd

; Remove the installed bundle; a loaded DLL can be renamed but not deleted, so rename it out of the way.
Function RemoveBundle
  ; $0 = bundle dir (…\Quartz.vst3)
  ${If} ${FileExists} "$0\Contents\x86_64-win\Quartz.vst3"
    ClearErrors
    Delete "$0\Contents\x86_64-win\Quartz.vst3"
    ${If} ${FileExists} "$0\Contents\x86_64-win\Quartz.vst3"
      Delete "$0\Contents\x86_64-win\Quartz.old"
      Rename "$0\Contents\x86_64-win\Quartz.vst3" "$0\Contents\x86_64-win\Quartz.old"
      Delete /REBOOTOK "$0\Contents\x86_64-win\Quartz.old"
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Quartz VST3" SecMain
  SectionIn RO
  SetRegView 64
  StrCpy $0 "$Vst3Dir\Quartz.vst3"
  DetailPrint "Installing to $0"
  retryBundle:
  Call RemoveBundle
  ClearErrors
  CreateDirectory "$0\Contents\x86_64-win"
  SetOutPath "$0\Contents\x86_64-win"
  File "/oname=Quartz.vst3" "${PAYLOAD}"
  ${If} ${Errors}
    ${If} ${Silent}
      Abort "Could not write Quartz.vst3 (is your DAW running?)."
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Quartz.vst3 is in use. Close your DAW and press Retry." IDRETRY retryBundle
    Abort
  ${EndIf}

  ; app folder with uninstaller + licences
  CreateDirectory "$AppDir"
  SetOutPath "$AppDir"
  File "LICENSES.txt"
  File "/oname=Quartz.ico" "quartz.ico"
  WriteUninstaller "$AppDir\Uninstall.exe"

  ; Start Menu entry
  CreateDirectory "$SMPROGRAMS\Quartz"
  CreateShortcut "$SMPROGRAMS\Quartz\Uninstall Quartz.lnk" "$AppDir\Uninstall.exe" "" "$AppDir\Quartz.ico"

  ; registry (Apps & features)
  ${If} $AllUsers == "1"
    StrCpy $1 "HKLM"
    WriteRegStr HKLM "${UNKEY}" "DisplayName" "Quartz (VST3)"
    WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNKEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$AppDir\Quartz.ico"
    WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$AppDir"
    WriteRegStr HKLM "${UNKEY}" "VST3Dir" "$Vst3Dir"
    WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$AppDir\Uninstall.exe"'
    WriteRegStr HKLM "${UNKEY}" "QuietUninstallString" '"$AppDir\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
    WriteRegDWORD HKLM "${UNKEY}" "EstimatedSize" 15000
    WriteRegStr HKLM "Software\Quartz Audio\Quartz" "Version" "${VERSION}"
    DeleteRegKey HKCU "${UNKEY}"
  ${Else}
    WriteRegStr HKCU "${UNKEY}" "DisplayName" "Quartz (VST3)"
    WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNKEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$AppDir\Quartz.ico"
    WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$AppDir"
    WriteRegStr HKCU "${UNKEY}" "VST3Dir" "$Vst3Dir"
    WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$AppDir\Uninstall.exe"'
    WriteRegStr HKCU "${UNKEY}" "QuietUninstallString" '"$AppDir\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
    WriteRegDWORD HKCU "${UNKEY}" "EstimatedSize" 15000
    WriteRegStr HKCU "Software\Quartz Audio\Quartz" "Version" "${VERSION}"
  ${EndIf}
  ; user presets (%APPDATA%\Quartz) are deliberately not touched here.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  DetailPrint "Done."
SectionEnd

;----------------------------------------------------------------------------------------------
Var UnAll
Var UnVst3
Var UnParams

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
    StrCpy $R0 '$UnParams'
    System::Call 'shell32::ShellExecuteW(p 0, w "runas", w "$EXEPATH", w "$R0", p 0, i 1) p .r2'
    ${If} $2 > 32
      Quit
    ${EndIf}
    MessageBox MB_OK|MB_ICONSTOP "Removing the all-users install of Quartz needs administrator permission."
    Abort
  ${EndIf}
  ${If} $UnAll == "1"
    SetShellVarContext all
    ReadRegStr $UnVst3 HKLM "${UNKEY}" "VST3Dir"
  ${Else}
    SetShellVarContext current
    ReadRegStr $UnVst3 HKCU "${UNKEY}" "VST3Dir"
  ${EndIf}
  ${If} $UnVst3 == ""
    ${If} $UnAll == "1"
      StrCpy $UnVst3 "$COMMONFILES64\VST3"
    ${Else}
      StrCpy $UnVst3 "$LOCALAPPDATA\Programs\Common\VST3"
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetRegView 64
  StrCpy $0 "$UnVst3\Quartz.vst3"
  DetailPrint "Removing $0"
  ${If} ${FileExists} "$0\Contents\x86_64-win\Quartz.vst3"
    Delete "$0\Contents\x86_64-win\Quartz.vst3"
    ${If} ${FileExists} "$0\Contents\x86_64-win\Quartz.vst3"
      Delete "$0\Contents\x86_64-win\Quartz.old"
      Rename "$0\Contents\x86_64-win\Quartz.vst3" "$0\Contents\x86_64-win\Quartz.old"
      Delete /REBOOTOK "$0\Contents\x86_64-win\Quartz.old"
    ${EndIf}
  ${EndIf}
  Delete "$0\Contents\x86_64-win\Quartz.old"
  RMDir "$0\Contents\x86_64-win"
  RMDir "$0\Contents"
  RMDir "$0"
  Delete "$SMPROGRAMS\Quartz\Uninstall Quartz.lnk"
  RMDir "$SMPROGRAMS\Quartz"
  Delete "$INSTDIR\LICENSES.txt"
  Delete "$INSTDIR\Quartz.ico"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  ${If} $UnAll == "1"
    DeleteRegKey HKLM "${UNKEY}"
    DeleteRegKey HKLM "Software\Quartz Audio\Quartz"
    DeleteRegKey /ifempty HKLM "Software\Quartz Audio"
  ${Else}
    DeleteRegKey HKCU "${UNKEY}"
    DeleteRegKey HKCU "Software\Quartz Audio\Quartz"
    DeleteRegKey /ifempty HKCU "Software\Quartz Audio"
  ${EndIf}
  ; user presets are kept unless the user asks otherwise
  ${IfNot} ${Silent}
    ${If} ${FileExists} "$APPDATA\Quartz\Presets"
      MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your own Quartz presets in $APPDATA\Quartz?" IDNO keepPresets
      RMDir /r "$APPDATA\Quartz"
      keepPresets:
    ${EndIf}
  ${EndIf}
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
