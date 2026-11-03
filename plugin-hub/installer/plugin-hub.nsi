; CMPLXTY Plugin Hub - Windows installer (GPLv3). Installs only the hub app; the hub itself installs the plug-ins.
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
Name "CMPLXTY Plugin Hub ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\CMPLXTY\Plugin Hub"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "CMPLXTY Plugin Hub"
VIAddVersionKey "CompanyName" "CMPLXTY"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "CMPLXTY Plugin Hub installer"
VIAddVersionKey "LegalCopyright" "GPLv3"
!define MUI_FINISHPAGE_RUN "$INSTDIR\CMPLXTY Plugin Hub.exe"
!insertmacro MUI_PAGE_LICENSE "..\COPYING"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\CMPLXTY Plugin Hub"
Section "Hub"
  SetRegView 64
  SetOutPath "$INSTDIR"
  File "${STAGE}\CMPLXTY Plugin Hub.exe"
  File "/oname=COPYING.txt" "..\COPYING"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  SetShellVarContext all
  CreateShortcut "$SMPROGRAMS\CMPLXTY Plugin Hub.lnk" "$INSTDIR\CMPLXTY Plugin Hub.exe"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "CMPLXTY Plugin Hub"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "CMPLXTY"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\CMPLXTY Plugin Hub.exe"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
SectionEnd
Section "Uninstall"
  ; removes the hub only - installed plug-ins and presets stay
  SetRegView 64
  SetShellVarContext all
  Delete "$SMPROGRAMS\CMPLXTY Plugin Hub.lnk"
  Delete "$INSTDIR\CMPLXTY Plugin Hub.exe"
  Delete "$INSTDIR\COPYING.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNKEY}"
SectionEnd
