
!include "MUI.nsh"
!define MUI_FINISHPAGE_RUN "$INSTDIR\TreeSheets.exe"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "TreeSheets\tsinst.bmp"
/*
doesn't show?
!define MUI_HEADERIMAGE_UNBITMAP "TreeSheets\tsinst.bmp"
*/

Name "TreeSheets"

OutFile "windows_treesheets_setup.exe"

XPStyle on

InstallDir $PROGRAMFILES64\TreeSheets

InstallDirRegKey HKLM "Software\TreeSheets" "Install_Dir"

SetCompressor /SOLID lzma
XPStyle on

Page components #"" ba ""
Page directory
Page instfiles
!insertmacro MUI_PAGE_FINISH

UninstPage uninstConfirm
UninstPage instfiles

!insertmacro MUI_LANGUAGE "English"

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
  SetRegView 64
  FindWindow $0 "TreeSheets" ""
  StrCmp $0 0 continueInstall
    MessageBox MB_ICONSTOP|MB_OK "TreeSheets is already running, please close it and try again."
    Abort
  continueInstall:
FunctionEnd

Function un.onInit
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

  WriteRegStr HKLM SOFTWARE\TreeSheets "Install_Dir" "$INSTDIR"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "DisplayName" "TreeSheets"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets" "NoRepair" 1
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

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TreeSheets"
  DeleteRegKey HKLM SOFTWARE\TreeSheets

  RMDir /r "$SMPROGRAMS\TreeSheets"
  RMDir /r "$INSTDIR"

SectionEnd
