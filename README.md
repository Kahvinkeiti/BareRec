# BareRec

**A lightweight screen recorder for Windows. No clutter. Just record.**

BareRec is a small, native Windows screen recorder built for people who want to capture their screen and save an MP4 file — without an account, cloud storage, subscriptions or a complicated interface.

## Download

**[Download BareRec from GitHub Releases](https://github.com/Kahvinkeiti/BareRec/releases/latest)**

1. Open the Releases page and choose the newest published release.
2. Under **Assets**, download `BareRec.exe`.
3. Double-click `BareRec.exe` to launch it. No installer or separate DLL folder is needed.
4. Choose a recording folder, select your settings, and click **Start recording**.

> **Note:** The download above will work once a release has been published and `BareRec.exe` has been attached as an asset. Until then, the latest builds are available under [GitHub Actions](https://github.com/Kahvinkeiti/BareRec/actions) (GitHub sign-in may be required).

## Features

- **Simple recording:** Start and stop with one button
- **MP4 video:** Save recordings directly on your PC (H.264)
- **Portable:** One standalone EXE, with no installation required
- **Recording quality:** Quality 720p or Original resolution
- **Frame rate:** 15, 30 or 60 FPS
- **Keyboard shortcut:** `Ctrl + Shift + F9` to start or stop
- **Privacy-friendly:** No account, uploads, telemetry or cloud storage in the native recorder

## Requirements

- Windows 10 or Windows 11 (64-bit)
- Windows Media Foundation H.264 support (normally included with Windows; Windows N editions may need the optional Media Feature Pack)

**Current limitation:** BareRec records **video only**. **Microphone and system audio are not supported yet.** It currently captures the primary display, not an individually selected app window.

## How to use

1. Launch `BareRec.exe`.
2. Select where you want recordings saved.
3. Choose **Quality 720p** for a lighter recording or **Original** for native resolution.
4. Choose 15, 30 or 60 FPS.
5. Press **Start recording**, then **Stop and save** (or use `Ctrl + Shift + F9`).

The recording is saved as an MP4 file in the selected folder. On older CPUs, Original resolution and higher FPS may increase CPU usage.

## Website

BareRec includes a responsive website in [`index.html`](index.html), ready for **GitHub Pages**.

**Publish it:**
1. Open [Repository Settings → Pages](https://github.com/Kahvinkeiti/BareRec/settings/pages).
2. Under **Build and deployment**, select **Deploy from a branch**.
3. Select **main** and **/(root)**, then click **Save**.
4. After GitHub Pages publishes, the site will be at **https://kahvinkeiti.github.io/BareRec/**.

The website's **Download** button links directly to the asset named **`BareRec.exe`** from the latest GitHub Release. **Attach the EXE using that exact filename** to the latest published release; otherwise the direct download cannot work. The button starts the asset download instead of showing the GitHub Releases page. GitHub may handle the download via a redirect.

The site also links to the GitHub repository and [YouTube channel](https://youtube.com/@kahvinkeiti).

## For developers

The app is written in **native C++20**, using Windows Win32 GDI screen capture and Microsoft's Media Foundation H.264 encoder. It does not ship with .NET, FFmpeg or third-party codec DLLs.

- Source code: [`native/main.cpp`](native/main.cpp)
- Build workflow: [GitHub Actions](.github/workflows/windows-build.yml)
- Build: Windows x64, MSVC, `/MT` static runtime
- The workflow packages a standalone `BareRec.exe` as the `BareRec-Native-EXE` artifact
- The workflow enforces an EXE size limit of 10 MiB; the precise size depends on the build

### How to publish a release

1. Run or open the latest successful [Build BareRec Native Windows EXE](https://github.com/Kahvinkeiti/BareRec/actions) workflow.
2. Download **BareRec-Native-EXE** from the workflow's **Artifacts** section and extract `BareRec.exe`.
3. Go to [Releases → Draft a new release](https://github.com/Kahvinkeiti/BareRec/releases/new).
4. Create a version tag (for example, `v0.1.0`), give the release a title, and add release notes.
5. Upload **`BareRec.exe`** under **Assets**, then publish.

**Development status:** Early release. Screen capture uses GDI, so CPU usage can be high on older hardware. Hardware-accelerated capture and audio are planned improvements, not features of the current build.

## License

No license has been selected yet. All rights reserved until a license is added.
