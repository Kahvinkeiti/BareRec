# BareRec

BareRec is a minimalist Windows screen recorder. Record your primary display to a local MP4 without accounts, ads, cloud uploads, or a complicated editor.

## Features
- Start/stop with one button or **Ctrl+Shift+F9**
- H.264 MP4 recordings in your selected local folder
- Optional system audio and microphone
- 30/60 FPS
- Minimizes while recording; tray menu offers Stop
- Local-only settings in `%APPDATA%\BareRec\settings.json`

**Requirements:** Windows 10/11, x64. The app records the primary monitor, not an individually selected window. Audio and recording support vary by device/Windows configuration.

## Download / run
In **Actions**, open the latest successful **Build Windows EXE** workflow run and download the `BareRec-Single-EXE` artifact. GitHub wraps artifacts in a ZIP: extract the single `BareRec.exe` and run it from anywhere. No .NET installation or separate DLL folder is needed.

The executable is self-contained and embeds the native dependencies. At startup, the .NET single-file host may extract internal components to a temporary per-user cache, but users only need to keep the EXE. This build is not yet verified on Windows. If a recording fails, please share the full exception message.

The single-file .NET build can still be large; an under-10-MB executable would require a different implementation.

## Develop
Install .NET 10 SDK on Windows, then run:
```powershell
dotnet restore
dotnet run
```

Uses [ScreenRecorderLib](https://github.com/sskodje/ScreenRecorderLib) for local capture and encoding. License: MIT.

**Troubleshooting:** If recording shows `BadImageFormatException`, please send the exception and note that the build is now single-file with full extraction enabled. The application interface is in English. Windows runtime behavior still needs verification.