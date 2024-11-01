
!include "MUI2.nsh"
!define MUI_FINISHPAGE_RUN "$INSTDIR\TreeSheets.exe"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "TreeSheets\tsinst.bmp"
!define MUI_ICON "TreeSheets\icon1.ico"
/*
doesn't show?
!define MUI_HEADERIMAGE_UNBITMAP "TreeSheets\tsinst.bmp"
*/

Unicode true

Name "TreeSheets"

OutFile "windows_treesheets_setup.exe"

RequestExecutionLevel user

ManifestDPIAware true

InstallDir "$LOCALAPPDATA\Programs\TreeSheets"

InstallDirRegKey HKCU "Software\TreeSheets" "Install_Dir"

SetCompressor /SOLID lzma

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English" ; The first language is the default language
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "Russian"

!insertmacro MUI_RESERVEFILE_LANGDLL

/*
AddBrandingImage top 65

Function ba
	File TreeSheets\dot3.bmp
  SetBrandingImage TreeSheets\dot3.bmp
FunctionEnd

Function un.ba
  SetBrandingImage TreeSheets\dot3.bmp
FunctionEnd
*/

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
  SetRegView 64
  FindWindow $0 "TreeSheets" ""
  StrCmp $0 0 continueInstall
    MessageBox MB_ICONSTOP|MB_OK "TreeSheets is already running, please close it and try again."
    Abort
  continueInstall:
FunctionEnd

Function un.onInit
  !insertmacro MUI_UNGETLANGUAGE
  FindWindow $0 "TreeSheets" ""
  StrCmp $0 0 continueInstall
    MessageBox MB_ICONSTOP|MB_OK "TreeSheets is still running, please close it and try again."
    Abort
  continueInstall:
FunctionEnd

Section "TreeSheets (required)"

  SectionIn RO

  SetOutPath $INSTDIR

  File /r "TS\*.*"

  WriteRegStr HKCU SOFTWARE\TreeSheets "Install_Dir" "$INSTDIR"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "DisplayName" "TreeSheets"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

SectionEnd

/*
Section "Visual C++ redistributable runtime"

  ExecWait '"$INSTDIR\redist\vcredist_x86.exe"'

SectionEnd
*/

Section "Start Menu Shortcuts"

  CreateDirectory "$SMPROGRAMS\TreeSheets"
  CreateDirectory "$APPDATA\TreeSheetsdbs\"

  SetOutPath "$INSTDIR"

  CreateShortCut "$SMPROGRAMS\TreeSheets\TreeSheets.lnk"    "$INSTDIR\TreeSheets.exe"  "" "$INSTDIR\TreeSheets.exe"  0
  CreateShortCut "$SMPROGRAMS\TreeSheets\Uninstall.lnk"     "$INSTDIR\uninstall.exe"   "" "$INSTDIR\uninstall.exe"   0
  CreateShortCut "$SMPROGRAMS\TreeSheets\Documentation.lnk" "$INSTDIR\readme.html"     "" "$INSTDIR\readme.html"     0
  CreateShortCut "$SMPROGRAMS\TreeSheets\Examples.lnk"      "$INSTDIR\Examples\"       "" "$INSTDIR\Examples\"       0

SectionEnd

Section "Uninstall"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets"
  DeleteRegKey HKCU SOFTWARE\TreeSheets

  RMDir /r "$SMPROGRAMS\TreeSheets"
  RMDir /r "$INSTDIR"

SectionEnd
