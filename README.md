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

## Download/build
Under **Actions** choose **Build Windows EXE** → **Run workflow** (or push to main). After the workflow succeeds, download artifact `BareRec-Windows-x64`, extract it and run `BareRec.exe`. This is a self-contained .NET app, so the user does not need the .NET SDK. Depending on the machine, the Microsoft Visual C++ Redistributable and Windows Media Feature Pack may be required.

## Develop
Install .NET 10 SDK on Windows, then run:
```powershell
dotnet restore
dotnet run
```

Uses [ScreenRecorderLib](https://github.com/sskodje/ScreenRecorderLib) for local capture and encoding. License: MIT.

**Status:** First release candidate; Windows runtime behavior and GitHub Actions build must be verified.