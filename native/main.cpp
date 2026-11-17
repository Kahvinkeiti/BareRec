#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr UINT WM_RECORD_DONE = WM_APP + 1;
constexpr int ID_START = 1001, ID_BROWSE = 1002, ID_FOLDER = 1003, ID_FPS = 1004;
constexpr int HOTKEY_ID = 77;
HWND mainWindow{}, startButton{}, folderEdit{}, fpsCombo{}, statusText{};
HFONT uiFont{};
std::thread recordingThread;
std::atomic<bool> stopRequested{false}, recording{false};
std::wstring outputFile;

struct Completion {
    std::wstring message;
    bool success;
};

void Check(HRESULT hr, const char* step) {
    if (FAILED(hr)) {
        char message[256]{};
        sprintf_s(message, "%s (0x%08lX)", step, static_cast<unsigned long>(hr));
        throw std::runtime_error(message);
    }
}

std::wstring Wide(const std::string& utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), len);
    result.resize(static_cast<size_t>(len - 1));
    return result;
}

std::wstring Text(HWND control) {
    int len = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(control, result.data(), len + 1);
    result.resize(static_cast<size_t>(len));
    return result;
}

std::wstring DefaultFolder() {
    PWSTR value = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_DEFAULT, nullptr, &value))) {
        result = value;
        CoTaskMemFree(value);
    } else {
        wchar_t buffer[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
        result = buffer;
    }
    return (fs::path(result) / L"BareRec").wstring();
}

std::wstring MakeFilename(const std::wstring& directory) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[128]{};
    swprintf_s(name, L"BareRec_%04u-%02u-%02u_%02u-%02u-%02u_%03u.mp4",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return (fs::path(directory) / name).wstring();
}

void Record(const std::wstring& file, int fps) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Check(com, "Initialize COM");
    bool mfStarted = false;
    HDC screen = nullptr, memoryDC = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    try {
        Check(MFStartup(MF_VERSION), "Initialize Media Foundation");
        mfStarted = true;

        const int width = GetSystemMetrics(SM_CXSCREEN) & ~1;
        const int height = GetSystemMetrics(SM_CYSCREEN) & ~1;
        if (width <= 0 || height <= 0) throw std::runtime_error("No valid primary display found");
        if (static_cast<unsigned long long>(width) * height > 16000000ULL)
            throw std::runtime_error("This display resolution is not supported in the first version");

        ComPtr<IMFAttributes> attributes;
        Check(MFCreateAttributes(&attributes, 1), "Create media attributes");
        Check(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE), "Set encoder attributes");

        ComPtr<IMFSinkWriter> writer;
        Check(MFCreateSinkWriterFromURL(file.c_str(), nullptr, attributes.Get(), &writer), "Open MP4 file");

        ComPtr<IMFMediaType> output;
        Check(MFCreateMediaType(&output), "Create output media type");
        Check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video type");
        Check(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Set H264 type");
        Check(output->SetUINT32(MF_MT_AVG_BITRATE, fps == 60 ? 10000000 : 6000000), "Set bitrate");
        Check(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set interlace");
        Check(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height), "Set frame size");
        Check(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, fps, 1), "Set frame rate");
        Check(MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set pixel aspect ratio");

        DWORD stream = 0;
        Check(writer->AddStream(output.Get(), &stream), "Add video stream");

        ComPtr<IMFMediaType> input;
        Check(MFCreateMediaType(&input), "Create input media type");
        Check(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set input video type");
        Check(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 input");
        Check(input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set input interlace");
        Check(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height), "Set input dimensions");
        Check(MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, fps, 1), "Set input FPS");
        Check(MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set input aspect ratio");
        Check(writer->SetInputMediaType(stream, input.Get(), nullptr), "Configure input conversion");
        Check(writer->BeginWriting(), "Begin recording");

        screen = GetDC(nullptr);
        if (!screen) throw std::runtime_error("Could not access desktop");
        memoryDC = CreateCompatibleDC(screen);
        if (!memoryDC) throw std::runtime_error("Could not create capture context");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // Top-down RGB
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) throw std::runtime_error("Could not create capture buffer");
        oldBitmap = SelectObject(memoryDC, bitmap);

        const DWORD bytes = static_cast<DWORD>(width * height * 4);
        const LONGLONG frameDuration = 10000000LL / fps;
        auto nextFrame = std::chrono::steady_clock::now();
        LONGLONG frameNumber = 0;
        do {
            if (!BitBlt(memoryDC, 0, 0, width, height, screen, 0, 0, SRCCOPY | CAPTUREBLT))
                throw std::runtime_error("Desktop capture failed");

            ComPtr<IMFMediaBuffer> buffer;
            Check(MFCreateMemoryBuffer(bytes, &buffer), "Allocate video buffer");
            BYTE* dest = nullptr;
            Check(buffer->Lock(&dest, nullptr, nullptr), "Lock video buffer");
            std::memcpy(dest, pixels, bytes);
            buffer->Unlock();
            Check(buffer->SetCurrentLength(bytes), "Set buffer length");

            ComPtr<IMFSample> sample;
            Check(MFCreateSample(&sample), "Create video sample");
            Check(sample->AddBuffer(buffer.Get()), "Add sample pixels");
            Check(sample->SetSampleTime(frameNumber * frameDuration), "Set sample timestamp");
            Check(sample->SetSampleDuration(frameDuration), "Set sample duration");
            Check(writer->WriteSample(stream, sample.Get()), "Encode video frame");
            ++frameNumber;

            nextFrame += std::chrono::nanoseconds(1000000000LL / fps);
            std::this_thread::sleep_until(nextFrame);
            // Avoid rapidly catching up after a slow frame on low-end computers.
            if (std::chrono::steady_clock::now() > nextFrame + std::chrono::milliseconds(250))
                nextFrame = std::chrono::steady_clock::now();
        } while (!stopRequested.load());

        Check(writer->Finalize(), "Finalize MP4 recording");

        if (oldBitmap) SelectObject(memoryDC, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (memoryDC) DeleteDC(memoryDC);
        if (screen) ReleaseDC(nullptr, screen);
        writer.Reset();
        MFShutdown();
        CoUninitialize();
    } catch (...) {
        if (oldBitmap) SelectObject(memoryDC, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (memoryDC) DeleteDC(memoryDC);
        if (screen) ReleaseDC(nullptr, screen);
        if (mfStarted) MFShutdown();
        CoUninitialize();
        throw;
    }
}

void BeginRecording() {
    if (recording.exchange(true)) return;
    std::wstring directory = Text(folderEdit);
    if (directory.empty()) {
        recording = false;
        MessageBoxW(mainWindow, L"Choose a recording folder.", L"BareRec", MB_ICONWARNING);
        return;
    }
    try {
        fs::create_directories(directory);
        outputFile = MakeFilename(directory);
    } catch (const std::exception& e) {
        recording = false;
        MessageBoxW(mainWindow, Wide(e.what()).c_str(), L"Invalid folder", MB_ICONERROR);
        return;
    }
    stopRequested = false;
    int fps = SendMessageW(fpsCombo, CB_GETCURSEL, 0, 0) == 1 ? 60 : 30;
    SetWindowTextW(startButton, L"Stop and save");
    SetWindowTextW(statusText, L"Recording...");
    EnableWindow(folderEdit, FALSE);
    EnableWindow(fpsCombo, FALSE);

    recordingThread = std::thread([file = outputFile, fps] {
        auto* result = new Completion;
        try {
            Record(file, fps);
            result->success = true;
            result->message = L"Saved: " + fs::path(file).filename().wstring();
        } catch (const std::exception& error) {
            result->success = false;
            result->message = L"Recording failed:\n" + Wide(error.what());
        } catch (...) {
            result->success = false;
            result->message = L"Recording failed due to an unknown error.";
        }
        PostMessageW(mainWindow, WM_RECORD_DONE, 0, reinterpret_cast<LPARAM>(result));
    });
}

void StopRecording() {
    if (!recording) return;
    stopRequested = true;
    SetWindowTextW(statusText, L"Saving MP4...");
    EnableWindow(startButton, FALSE);
}

void SelectFolder(HWND owner) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = L"Choose recording folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&info);
    if (selected) {
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(selected, path)) SetWindowTextW(folderEdit, path);
        CoTaskMemFree(selected);
    }
}

HWND AddControl(HWND parent, const wchar_t* type, const wchar_t* label, DWORD styles,
                int x, int y, int width, int height, int id = 0) {
    HWND result = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | styles,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    return result;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        uiFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        AddControl(hwnd, L"STATIC", L"BareRec", SS_LEFT, 22, 17, 390, 40);
        AddControl(hwnd, L"STATIC", L"Record. Save. Done.", SS_LEFT, 22, 62, 320, 27);
        AddControl(hwnd, L"STATIC", L"Recording folder", SS_LEFT, 22, 103, 350, 28);
        folderEdit = AddControl(hwnd, L"EDIT", DefaultFolder().c_str(), WS_BORDER | ES_AUTOHSCROLL,
                                22, 133, 320, 31, ID_FOLDER);
        AddControl(hwnd, L"BUTTON", L"Browse", BS_PUSHBUTTON, 354, 132, 105, 33, ID_BROWSE);
        AddControl(hwnd, L"STATIC", L"Frame rate", SS_LEFT, 22, 185, 110, 28);
        fpsCombo = AddControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                              134, 180, 90, 150, ID_FPS);
        SendMessageW(fpsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 FPS"));
        SendMessageW(fpsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"60 FPS"));
        SendMessageW(fpsCombo, CB_SETCURSEL, 0, 0);
        AddControl(hwnd, L"STATIC", L"Ctrl + Shift + F9: start / stop", SS_LEFT,
                   22, 232, 420, 27);
        startButton = AddControl(hwnd, L"BUTTON", L"Start recording", BS_DEFPUSHBUTTON,
                                 22, 276, 437, 48, ID_START);
        statusText = AddControl(hwnd, L"STATIC", L"Ready (video only)", SS_LEFT,
                                22, 342, 437, 45);
        RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_START:
            if (recording) StopRecording(); else BeginRecording();
            return 0;
        case ID_BROWSE:
            if (!recording) SelectFolder(hwnd);
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            if (recording) StopRecording(); else BeginRecording();
            return 0;
        }
        break;
    case WM_RECORD_DONE: {
        std::unique_ptr<Completion> result(reinterpret_cast<Completion*>(lParam));
        if (recordingThread.joinable()) recordingThread.join();
        recording = false;
        EnableWindow(startButton, TRUE);
        EnableWindow(folderEdit, TRUE);
        EnableWindow(fpsCombo, TRUE);
        SetWindowTextW(startButton, L"Start recording");
        SetWindowTextW(statusText, result->success ? result->message.c_str() : L"Recording error");
        if (!result->success) MessageBoxW(hwnd, result->message.c_str(), L"BareRec", MB_ICONERROR);
        return 0;
    }
    case WM_CLOSE:
        if (recording) {
            StopRecording();
            MessageBoxW(hwnd, L"Saving the recording. Close BareRec after the video has finished saving.",
                        L"BareRec", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        if (recordingThread.joinable()) recordingThread.join();
        DeleteObject(uiFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    const wchar_t* className = L"BareRecNativeWindow";
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = className;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&cls);
    mainWindow = CreateWindowExW(0, className, L"BareRec — Lightweight Screen Recorder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
        CW_USEDEFAULT, 500, 440, nullptr, nullptr, instance, nullptr);
    if (!mainWindow) {
        CoUninitialize();
        return 1;
    }
    ShowWindow(mainWindow, show);
    UpdateWindow(mainWindow);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
