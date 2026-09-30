[Tasks]
Name: "agentdesktopicon"; Description: "Create a desktop shortcut that starts TreeSheets with the agent socket (-a)"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Icons]
Name: "{autodesktop}\TreeSheets (Agent Socket)"; Filename: "{app}\TreeSheets.exe"; Parameters: "-i -a"; Tasks: agentdesktopicon
