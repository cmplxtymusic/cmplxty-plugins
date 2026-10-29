; Yoink installer additions (included by electron-builder before the stock NSIS script).
; 1) If Yoink is already installed, ask before replacing it. On Yes the stock electron-builder logic installs this version and
;    silently uninstalls the old one (user data in %APPDATA%\Yoink and the clips folder are never touched).
; 2) If the old v1 portable folder is found, ask before deleting it.
; The in-app updater starts the installer with --updated: that already is the user's yes, so no question is asked.
; Silent installs (/S) never show a prompt: they update an installed copy and leave any v1 portable folder alone.

!ifndef BUILD_UNINSTALLER
!include "WordFunc.nsh"
!insertmacro VersionCompare
!insertmacro WordFind
!include "LogicLib.nsh"

; Remove one v1 portable folder if (and only if) it really is a Yoink v1 folder.
!macro YoinkOldPortable ID DIR
  ${If} ${FileExists} "${DIR}\Yoink.exe"
  ${AndIf} ${FileExists} "${DIR}\resources\bin\yt-dlp.exe"
  ${AndIf} ${FileExists} "${DIR}\resources\app\package.json"
    ${IfNot} ${Silent}
      MessageBox MB_YESNO|MB_ICONQUESTION "An old copy of Yoink (the portable folder) was found here:$\r$\n$\r$\n${DIR}$\r$\n$\r$\nRemove it? Your clips, favorites, history and settings are kept." IDNO yoinkSkip_${ID}
      RMDir /r "${DIR}"
      ${If} ${FileExists} "${DIR}\Yoink.exe"
        MessageBox MB_OK|MB_ICONEXCLAMATION "Could not remove ${DIR}. Close the old Yoink and delete that folder yourself."
      ${EndIf}
    ${EndIf}
    yoinkSkip_${ID}:
  ${EndIf}
!macroend

Function YoinkCheckInstalled
  Push $0
  Push $1
  Push $2
  ; --updated on the command line = started by the in-app updater (StdUtils is not loaded yet at this point, so look at $CMDLINE ourselves)
  StrCpy $2 "0"
  ClearErrors
  ${WordFind} "$CMDLINE" "--updated" "E+1" $1
  ${IfNot} ${Errors}
    StrCpy $2 "1"
  ${EndIf}
  ClearErrors
  StrCpy $0 ""
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_APP_KEY}" "DisplayVersion"
  ${If} $0 == ""
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_APP_KEY}" "DisplayVersion"
  ${EndIf}
  ${If} $0 == ""
    ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_APP_KEY}" "UninstallString"
    ${If} $1 == ""
      ReadRegStr $1 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_APP_KEY}" "UninstallString"
    ${EndIf}
    ${If} $1 != ""
      StrCpy $0 "an unknown version"
    ${EndIf}
  ${EndIf}
  ${If} $0 != ""
  ${AndIfNot} ${Silent}
  ${AndIf} $2 != "1"
    ${VersionCompare} "$0" "${VERSION}" $1
    ${If} $1 == 2
    ${OrIf} $0 == "an unknown version"
      MessageBox MB_YESNO|MB_ICONQUESTION "An older version of Yoink is installed. Update it by installing this version and removing the old one?" IDYES yoinkGo
    ${ElseIf} $1 == 1
      MessageBox MB_YESNO|MB_ICONQUESTION "A newer version of Yoink ($0) is installed. Replace it with this older version (${VERSION}) and remove the newer one?" IDYES yoinkGo
    ${Else}
      MessageBox MB_YESNO|MB_ICONQUESTION "Yoink ${VERSION} is already installed. Reinstall it by installing this copy and removing the old one?" IDYES yoinkGo
    ${EndIf}
    Quit
    yoinkGo:
  ${EndIf}
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

Function YoinkCheckV1
  Push $R9
  ReadEnvStr $R9 USERPROFILE
  !insertmacro YoinkOldPortable v1_1 "$R9\Desktop\Yoink\Yoink-CMPLXTY-win64"
  !insertmacro YoinkOldPortable v1_2 "$DESKTOP\Yoink\Yoink-CMPLXTY-win64"
  !insertmacro YoinkOldPortable v1_3 "$DESKTOP\Yoink-CMPLXTY-win64"
  !insertmacro YoinkOldPortable v1_4 "$R9\Downloads\Yoink-CMPLXTY-win64"
  !insertmacro YoinkOldPortable v1_5 "$LOCALAPPDATA\Yoink"
  ; tidy the now-empty "Yoink" folder on the desktop (RMDir without /r only removes empty folders)
  RMDir "$R9\Desktop\Yoink"
  RMDir "$DESKTOP\Yoink"
  Pop $R9
FunctionEnd
!endif

!macro customInit
  Call YoinkCheckInstalled
  Call YoinkCheckV1
!macroend
