Option Explicit
Dim shell, fso, base, scriptPath, arguments
Set shell = CreateObject("Shell.Application")
Set fso = CreateObject("Scripting.FileSystemObject")
base = fso.GetParentFolderName(WScript.ScriptFullName)
scriptPath = base & "\files\Install-GUI.ps1"
If Not fso.FileExists(scriptPath) Then
  MsgBox "Installer files are incomplete. Please extract the complete ZIP.", 16, "SCM V9.1.0"
  WScript.Quit 2
End If
arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File " & Chr(34) & scriptPath & Chr(34)
shell.ShellExecute "powershell.exe", arguments, "", "runas", 0
