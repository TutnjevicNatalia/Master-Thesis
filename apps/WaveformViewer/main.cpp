// WaveformViewer
//
// A small Windows desktop app: File > Open a WAV file, and see its
// waveform drawn in the window, plus a report of every RIFF chunk found
// in the file (so Soundswell's own "SWEL" chunk shows up alongside the
// standard "fmt " and "data" chunks).
//
// This is a plain Win32 app (no MFC, no other UI framework) built with
// GDI for drawing, matching the plain C++ approach used in the rest of
// this project. It only builds on Windows.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <string>
#include <vector>
#include <stdexcept>

#include "WavReader.h"

namespace {

constexpr int kMenuFileOpen = 1001;

// Layout constants for the drawing area, in pixels.
constexpr int kInfoAreaHeight = 90; // room reserved at the top for text
constexpr int kPlotMargin = 10;     // margin below the info area and at the bottom

// Currently loaded file, kept as globals for simplicity in this small app.
voiceqc::WavData g_wav;
bool g_hasFile = false;
std::wstring g_displayFileName;
std::wstring g_lastError;

// Double buffer, recreated whenever the client area is resized.
HBITMAP g_hbmMem = nullptr;
HDC g_hdcMem = nullptr;
int g_memWidth = 0;
int g_memHeight = 0;

// Converts a narrow (ANSI code page) string to UTF 16 for use with the
// *W Win32 APIs.
std::wstring AnsiToWide(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    int required = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring result(required > 0 ? required - 1 : 0, L'\0');
    if (required > 0) {
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, result.data(), required);
    }
    return result;
}

// Converts a UTF 16 string to a narrow (ANSI code page) string, so it can
// be passed to WavReader::Load, which takes a std::string path. This
// matches the current code page, which is normally fine for Swedish file
// and folder names on a Swedish locale Windows machine, but is not a
// full Unicode solution; a std::filesystem::path based WavReader would
// be a good follow up if you ever hit a file name it cannot open.
std::string WideToAnsi(const std::wstring& s) {
    if (s.empty()) {
        return std::string();
    }
    int required = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(required > 0 ? required - 1 : 0, '\0');
    if (required > 0) {
        WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, result.data(), required, nullptr, nullptr);
    }
    return result;
}

// Builds the informational text shown at the top of the window: file
// name, sample rate, duration, and the list of RIFF chunks found.
std::wstring BuildInfoText() {
    if (!g_hasFile) {
        if (!g_lastError.empty()) {
            return L"Could not open file:\r\n" + g_lastError;
        }
        return L"File > Open... to load a .wav file";
    }

    std::wstring text = L"File: " + g_displayFileName + L"\r\n";

    double durationSeconds = g_wav.sampleRate > 0
        ? static_cast<double>(g_wav.samples.size()) / g_wav.sampleRate
        : 0.0;

    wchar_t buf[256];
    swprintf(buf, 256, L"Sample rate: %d Hz   Bit depth: %d   Channels (source): %d   Duration: %.2f s",
             g_wav.sampleRate, g_wav.bitsPerSample, g_wav.channels, durationSeconds);
    text += buf;
    text += L"\r\n";

    text += L"Chunks found: ";
    for (size_t i = 0; i < g_wav.chunks.size(); ++i) {
        if (i > 0) {
            text += L", ";
        }
        text += AnsiToWide(g_wav.chunks[i].id);
        swprintf(buf, 256, L" (%u bytes)", g_wav.chunks[i].sizeBytes);
        text += buf;
    }

    return text;
}

// Opens a file picker, loads the chosen WAV file, and stores the result
// (or the error) in the globals above.
void OnFileOpen(HWND hwnd) {
    wchar_t filePath[MAX_PATH] = {0};

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Open WAV file";

    if (!GetOpenFileNameW(&ofn)) {
        // User cancelled; leave the current state as is.
        return;
    }

    std::string narrowPath = WideToAnsi(filePath);

    try {
        g_wav = voiceqc::WavReader::Load(narrowPath);
        g_hasFile = true;
        g_lastError.clear();
        g_displayFileName = filePath;
    } catch (const std::exception& ex) {
        g_hasFile = false;
        g_lastError = AnsiToWide(ex.what());
    }

    InvalidateRect(hwnd, nullptr, TRUE);
}

// (Re)creates the off screen bitmap used for double buffered drawing,
// if the client area size has changed since the last paint.
void EnsureMemDC(HDC hdc, int width, int height) {
    if (g_hdcMem != nullptr && width == g_memWidth && height == g_memHeight) {
        return;
    }
    if (g_hbmMem != nullptr) {
        DeleteObject(g_hbmMem);
        g_hbmMem = nullptr;
    }
    if (g_hdcMem != nullptr) {
        DeleteDC(g_hdcMem);
        g_hdcMem = nullptr;
    }
    g_hdcMem = CreateCompatibleDC(hdc);
    g_hbmMem = CreateCompatibleBitmap(hdc, width, height);
    SelectObject(g_hdcMem, g_hbmMem);
    g_memWidth = width;
    g_memHeight = height;
}

// Draws the waveform into g_hdcMem, downsampling to one min/max pair per
// pixel column so files of any length draw in roughly constant time.
void DrawWaveform(int width, int height) {
    int plotTop = kInfoAreaHeight;
    int plotBottom = height - kPlotMargin;
    if (plotBottom <= plotTop) {
        return;
    }
    int plotHeight = plotBottom - plotTop;
    int midY = plotTop + plotHeight / 2;

    HPEN centerPen = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
    HPEN oldPen = static_cast<HPEN>(SelectObject(g_hdcMem, centerPen));
    MoveToEx(g_hdcMem, 0, midY, nullptr);
    LineTo(g_hdcMem, width, midY);
    SelectObject(g_hdcMem, oldPen);
    DeleteObject(centerPen);

    size_t totalSamples = g_wav.samples.size();
    if (totalSamples == 0 || width <= 0) {
        return;
    }

    HPEN wavePen = CreatePen(PS_SOLID, 1, RGB(30, 100, 200));
    oldPen = static_cast<HPEN>(SelectObject(g_hdcMem, wavePen));

    for (int x = 0; x < width; ++x) {
        size_t startIdx = static_cast<size_t>((static_cast<double>(x) / width) * totalSamples);
        size_t endIdx = static_cast<size_t>((static_cast<double>(x + 1) / width) * totalSamples);
        if (endIdx <= startIdx) {
            endIdx = startIdx + 1;
        }
        if (startIdx >= totalSamples) {
            break;
        }
        if (endIdx > totalSamples) {
            endIdx = totalSamples;
        }

        float minV = g_wav.samples[startIdx];
        float maxV = g_wav.samples[startIdx];
        for (size_t i = startIdx; i < endIdx; ++i) {
            float v = g_wav.samples[i];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }

        int yTop = midY - static_cast<int>(maxV * (plotHeight / 2.0f));
        int yBottom = midY - static_cast<int>(minV * (plotHeight / 2.0f));
        if (yTop == yBottom) {
            yBottom += 1; // ensure at least a 1 pixel dot is visible for silence
        }

        MoveToEx(g_hdcMem, x, yTop, nullptr);
        LineTo(g_hdcMem, x, yBottom);
    }

    SelectObject(g_hdcMem, oldPen);
    DeleteObject(wavePen);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    EnsureMemDC(hdc, width, height);

    RECT fillRect = {0, 0, width, height};
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(g_hdcMem, &fillRect, background);
    DeleteObject(background);

    if (g_hasFile) {
        DrawWaveform(width, height);
    }

    SetBkMode(g_hdcMem, TRANSPARENT);
    SetTextColor(g_hdcMem, RGB(20, 20, 20));
    RECT textRect = {kPlotMargin, kPlotMargin, width - kPlotMargin, kInfoAreaHeight};
    std::wstring info = BuildInfoText();
    DrawTextW(g_hdcMem, info.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK);

    BitBlt(hdc, 0, 0, width, height, g_hdcMem, 0, 0, SRCCOPY);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuFileOpen) {
                OnFileOpen(hwnd);
            }
            return 0;

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_hbmMem != nullptr) {
                DeleteObject(g_hbmMem);
            }
            if (g_hdcMem != nullptr) {
                DeleteDC(g_hdcMem);
            }
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

HWND CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t* className = L"VoiceQCWaveformViewerWindowClass";

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    HMENU fileMenu = CreateMenu();
    AppendMenuW(fileMenu, MF_STRING, kMenuFileOpen, L"&Open...\tCtrl+O");

    HMENU menuBar = CreateMenu();
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HWND hwnd = CreateWindowExW(
        0, className, L"VoiceQC Waveform Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 500,
        nullptr, menuBar, hInstance, nullptr);

    if (hwnd != nullptr) {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }

    return hwnd;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    HWND hwnd = CreateMainWindow(hInstance, nCmdShow);
    if (hwnd == nullptr) {
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
