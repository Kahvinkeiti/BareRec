# BareRec — native Windows screen recorder

**Minimal. Local. No accounts. No cloud. No installer.**

BareRec is being rewritten in **native C++**, using Windows Win32 APIs for screen capture and the built-in **Media Foundation H.264 encoder** for MP4 files. It does not bundle .NET, ScreenRecorderLib, FFmpeg, or third-party codec DLLs.

## Current native MVP
- One `BareRec.exe` file, built with MSVC static runtime
- Record the **primary display** to local H.264 MP4
- Start / stop using the button or **Ctrl + Shift + F9**
- Choose the recording folder
- Select 30 or 60 FPS
- English user interface
- **Video only for now: system sound and microphone recording are NOT implemented yet**
- Windows 10/11 x64; relies on the Windows Media Foundation H.264 components

## Get the native EXE
1. Open **Actions → Build BareRec Native Windows EXE**.
2. Open the latest **successful** run.
3. Download artifact **BareRec-Native-EXE**.
4. Extract the ZIP provided by GitHub, and run the single `BareRec.exe` directly from wherever you like. No installation or DLL folder required.

GitHub Actions uploads the EXE alone and prints its exact file size in the build log. The target is under 10 MiB, enforced during the build. Until CI finishes successfully and the EXE is tested on Windows, the size and behavior are not confirmed.

## Implementation and limitations
- Source: `native/main.cpp`
- Build: MSVC x64, C++20, `/MT`
- Capture: GDI `BitBlt` plus Windows Media Foundation sink writer
- The initial GDI capture is intentionally simple. It may use more CPU than a GPU-based Desktop Duplication or Windows Graphics Capture implementation, and some hardware-accelerated/protected content may be blank.
- Constant 30/60 FPS timestamps are used; performance and audio recording will need subsequent iterations.
- On Windows N editions, Media Feature Pack may be needed for Windows' media encoder.
- The old .NET files are kept in the repository as a legacy prototype, but **GitHub Actions now builds the native C++ version**.

## License
This project is under active development; license has not been chosen yet.
