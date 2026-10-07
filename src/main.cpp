#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "pet_state.h"

using namespace Gdiplus;
namespace {
constexpr wchar_t CLASS_NAME[] = L"FluffyDesktopPet.PhotoCat.v1";
constexpr UINT TRAY_MESSAGE = WM_APP + 1, SHOW_MESSAGE = WM_APP + 2;
constexpr int BASE_W = 320, BASE_H = 376;
enum Command { C_PET=101, C_FEED, C_PLAY, C_SLEEP, C_ROAM, C_SMALL,
    C_MEDIUM, C_LARGE, C_TOP, C_IMPORT, C_RESET, C_HIDE, C_HELP, C_EXIT };
HWND hwnd = nullptr;
HINSTANCE instance = nullptr;
pet::State state;
std::unique_ptr<Bitmap> idleImage, sleepImage, customImage;
HDC bufferDC = nullptr;
HBITMAP bufferBitmap = nullptr;
HGDIOBJ oldBitmap = nullptr;
void* bufferPixels = nullptr;
int canvasW = 0, canvasH = 0, petSize = 240;
float dpiScale = 1.0f;
bool topmost = true, hidden = false, menuOpen = false, dragging = false;
bool pressed = false, moved = false, trayReady = false;
POINT downPoint{}, startPosition{};
HICON petIcon = nullptr;
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;
ULONGLONG lastTick = 0;
std::wstring configPath, customPath, bubble;
double bubbleUntil = 0;
int petCount = 0;

void say(const wchar_t* message, double seconds = 3.2) {
    bubble = message;
    bubbleUntil = state.time + seconds;
}

std::unique_ptr<Bitmap> loadResource(int id) {
    HRSRC res = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return nullptr;
    DWORD size = SizeofResource(instance, res);
    HGLOBAL loaded = LoadResource(instance, res);
    const void* data = LockResource(loaded);
    if (!data || !size) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    void* target = GlobalLock(memory);
    if (!target) { GlobalFree(memory); return nullptr; }
    memcpy(target, data, size);
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory); return nullptr;
    }
    std::unique_ptr<Bitmap> result;
    {
        Bitmap source(stream);
        if (source.GetLastStatus() == Ok && source.GetWidth() && source.GetHeight()) {
            // Clone the decoded pixels: GDI+ streams must outlive their source image.
            result.reset(source.Clone(0, 0, static_cast<INT>(source.GetWidth()),
                static_cast<INT>(source.GetHeight()), PixelFormat32bppPARGB));
        }
    }
    stream->Release();
    return result;
}

std::wstring settingsDirectory() {
    wchar_t appData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                              nullptr, SHGFP_TYPE_CURRENT, appData))) return L"";
    std::wstring path = std::wstring(appData) + L"\\FluffyDesktopPet";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return L"";
    return path;
}

void saveSettings() {
    if (configPath.empty()) return;
    auto put = [](const wchar_t* key, int value) {
        const auto text = std::to_wstring(value);
        WritePrivateProfileStringW(L"Pet", key, text.c_str(), configPath.c_str());
    };
    RECT rect{}; GetWindowRect(hwnd, &rect);
    put(L"X", rect.left); put(L"Y", rect.top); put(L"Size", petSize);
    put(L"Roaming", state.roaming ? 1 : 0); put(L"Topmost", topmost ? 1 : 0);
    WritePrivateProfileStringW(L"Pet", L"Image", customPath.c_str(), configPath.c_str());
}

bool loadCustom(const std::wstring& path) {
    std::unique_ptr<Bitmap> source(Bitmap::FromFile(path.c_str()));
    if (!source || source->GetLastStatus() != Ok || !source->GetWidth() ||
        !source->GetHeight() || source->GetWidth() > 8192 || source->GetHeight() > 8192)
        return false;
    std::unique_ptr<Bitmap> decoded(source->Clone(0, 0,
        static_cast<INT>(source->GetWidth()), static_cast<INT>(source->GetHeight()),
        PixelFormat32bppPARGB));
    if (!decoded || decoded->GetLastStatus() != Ok) return false;
    customImage = std::move(decoded);
    customPath = path;
    return true;
}

RECT workArea() {
    MONITORINFO monitor{}; monitor.cbSize=sizeof(MONITORINFO);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    return monitor.rcWork;
}

void clampWindow() {
    RECT rect{}; GetWindowRect(hwnd, &rect);
    RECT area = workArea();
    int x = static_cast<int>(pet::clampAxis(rect.left, canvasW, area.left, area.right));
    int y = static_cast<int>(pet::clampAxis(rect.top, canvasH, area.top, area.bottom));
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void freeBuffer() {
    if (bufferDC && oldBitmap) SelectObject(bufferDC, oldBitmap);
    if (bufferBitmap) DeleteObject(bufferBitmap);
    if (bufferDC) DeleteDC(bufferDC);
    bufferDC = nullptr; bufferBitmap = nullptr; oldBitmap = nullptr; bufferPixels = nullptr;
}

bool resizeCanvas(bool keepBottom = true) {
    RECT previous{}; GetWindowRect(hwnd, &previous);
    int oldW = canvasW, oldH = canvasH;
    freeBuffer();
    canvasW = std::max(120, static_cast<int>(petSize * dpiScale));
    canvasH = canvasW * BASE_H / BASE_W;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = canvasW;
    info.bmiHeader.biHeight = -canvasH;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bufferDC = CreateCompatibleDC(nullptr);
    bufferBitmap = CreateDIBSection(bufferDC, &info, DIB_RGB_COLORS, &bufferPixels, nullptr, 0);
    if (!bufferDC || !bufferBitmap || !bufferPixels) return false;
    oldBitmap = SelectObject(bufferDC, bufferBitmap);
    int x = previous.left + (oldW && keepBottom ? (oldW-canvasW)/2 : 0);
    int y = previous.top + (oldH && keepBottom ? oldH-canvasH : 0);
    SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 x, y, canvasW, canvasH, SWP_NOACTIVATE);
    clampWindow();
    return true;
}

void roundRect(Graphics& g, const RectF& r, float radius, const Color& color) {
    GraphicsPath path;
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight()-d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight()-d, r.GetBottom()-d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom()-d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush brush(color); g.FillPath(&brush, &path);
}

void textAt(Graphics& g, const wchar_t* value, float size, const RectF& rect,
            Color color, bool centered = true) {
    std::unique_ptr<Font> font;
    for (const wchar_t* name : {L"Microsoft YaHei UI", L"Microsoft YaHei",
                                L"Noto Sans CJK SC", L"Noto Serif CJK SC", L"Segoe UI"}) {
        auto candidate = std::make_unique<Font>(name, size, FontStyleRegular, UnitPixel);
        if (candidate->GetLastStatus() == Ok) { font = std::move(candidate); break; }
    }
    if (!font) font = std::make_unique<Font>(FontFamily::GenericSansSerif(), size,
                                            FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(centered ? StringAlignmentCenter : StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(value, -1, font.get(), rect, &format, &brush);
}

void drawHeart(Graphics& g, float x, float y, float s, BYTE opacity) {
    GraphicsPath p;
    p.AddBezier(x, y+s*.3f, x-s*.8f, y-s*.45f, x-s, y+s*.45f, x, y+s);
    p.AddBezier(x, y+s, x+s, y+s*.45f, x+s*.8f, y-s*.45f, x, y+s*.3f);
    p.CloseFigure();
    SolidBrush b(Color(opacity, 232, 127, 146)); g.FillPath(&b, &p);
}

void drawBowl(Graphics& g) {
    float t = static_cast<float>(state.moodTime);
    SolidBrush bowl(Color(255, 128, 177, 167)), rim(Color(255, 205, 229, 220));
    SolidBrush food(Color(255, 146, 98, 63));
    g.FillPie(&bowl, 214.f, 324.f, 75.f, 49.f, 0.f, 180.f);
    g.FillEllipse(&rim, 214.f, 326.f, 75.f, 17.f);
    for (int i=0; i<9; ++i) {
        if (i > 8 - static_cast<int>(t)) continue;
        g.FillEllipse(&food, 224.f + (i%5)*11.f, 329.f + (i/5)*5.f, 8.f, 5.f);
    }
    textAt(g, L"好吃！", 16, RectF(210, 292, 84, 30), Color(255, 86, 110, 94));
}

void render() {
    if (!bufferPixels || hidden) return;
    Bitmap surface(canvasW, canvasH, canvasW*4, PixelFormat32bppPARGB,
                   static_cast<BYTE*>(bufferPixels));
    {
        Graphics g(&surface);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const float scale = static_cast<float>(canvasW)/BASE_W;
        g.ScaleTransform(scale, scale);
        Bitmap* sprite = customImage ? customImage.get() :
            (state.mood == pet::Mood::Sleeping && sleepImage ? sleepImage.get() : idleImage.get());
        if (sprite) {
            const bool sleeping = state.mood == pet::Mood::Sleeping;
            float imageW = static_cast<float>(sprite->GetWidth());
            float imageH = static_cast<float>(sprite->GetHeight());
            float factor = std::min(294.f/imageW, 294.f/imageH);
            float w = imageW*factor, h = imageH*factor;
            float bounce = static_cast<float>(state.bob());
            if (state.mood == pet::Mood::Eating) bounce += 3.f*std::sin(static_cast<float>(state.moodTime)*7.f);
            float squish = static_cast<float>(state.squash());
            GraphicsState saved = g.Save();
            g.TranslateTransform(BASE_W/2.f, 368.f-bounce);
            if (dragging) g.RotateTransform(static_cast<float>(std::sin(state.time*8)*5));
            if (!sleeping && state.direction < 0 && state.walking()) g.ScaleTransform(-1.f, 1.f);
            g.ScaleTransform(1.f/squish, squish);
            g.DrawImage(sprite, RectF(-w/2, -h, w, h), 0, 0, imageW, imageH, UnitPixel);
            g.Restore(saved);
        }
        if (state.mood == pet::Mood::Happy || state.mood == pet::Mood::Playing) {
            for (int i=0; i<3; ++i) {
                double phase = std::fmod(state.moodTime*.65 + i*.31, 1.0);
                float x = i%2 ? 266.f : 46.f+i*9.f;
                drawHeart(g, x, 152.f-static_cast<float>(phase)*65.f,
                          12.f+i*2.f, static_cast<BYTE>((1-phase)*230));
            }
        }
        if (state.mood == pet::Mood::Eating) drawBowl(g);
        if (state.mood == pet::Mood::Sleeping) {
            float y = 88.f-static_cast<float>(std::fmod(state.time, 2.5))*8.f;
            textAt(g, L"z Z", 27, RectF(227,y,73,50), Color(235, 124, 146, 153));
        }
        if (state.time < bubbleUntil && !bubble.empty()) {
            RectF r(17, 12, 286, 52);
            roundRect(g, RectF(18, 15, 286, 52), 17, Color(25, 51, 39, 31));
            roundRect(g, r, 17, Color(248, 255, 250, 240));
            PointF triangle[] = {PointF(151,62), PointF(168,62), PointF(157,73)};
            SolidBrush b(Color(248,255,250,240)); g.FillPolygon(&b,triangle,3);
            textAt(g, bubble.c_str(), 16, RectF(26,16,268,44), Color(255, 96, 76, 61));
        }
        g.Flush(FlushIntentionSync);
    }
    POINT zero{0,0}; SIZE size{canvasW,canvasH};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(hwnd, screen, nullptr, &size, bufferDC, &zero, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

void addTray() {
    tray = {};
    tray.cbSize = sizeof(tray); tray.hWnd = hwnd; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TRAY_MESSAGE; tray.hIcon = petIcon;
    lstrcpynW(tray.szTip, L"毛球 · 右键打开菜单，双击显示猫咪", ARRAYSIZE(tray.szTip));
    trayReady = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
}

void showPet() {
    hidden = false;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    clampWindow(); render();
}

void setSize(int size) {
    petSize = size;
    if (!resizeCanvas()) { DestroyWindow(hwnd); return; }
    render(); saveSettings();
}

void command(int id) {
    switch (id) {
    case C_PET:
        state.setMood(pet::Mood::Happy);
        say((++petCount % 3 == 0) ? L"再摸一下，就一下～" : L"呼噜呼噜，喜欢你。 "); break;
    case C_FEED:
        state.setMood(pet::Mood::Eating); say(L"开饭啦！小鱼干在哪里？"); break;
    case C_PLAY:
        state.setMood(pet::Mood::Playing); say(L"看我的猫猫弹跳！"); break;
    case C_SLEEP:
        if (state.mood == pet::Mood::Sleeping) {
            state.setMood(pet::Mood::Idle); say(L"醒啦，继续陪你。 ");
        } else {
            state.setMood(pet::Mood::Sleeping); say(L"我先眯一会儿……", 2.5);
        }
        break;
    case C_ROAM:
        state.roaming = !state.roaming;
        say(state.roaming ? L"去桌面上散个步～" : L"好，我就在这里陪你。 ");
        saveSettings(); break;
    case C_SMALL: setSize(180); break;
    case C_MEDIUM: setSize(240); break;
    case C_LARGE: setSize(320); break;
    case C_TOP:
        topmost = !topmost;
        SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        saveSettings(); break;
    case C_IMPORT: {
        wchar_t path[32768]{};
        OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"透明 PNG 图片\0*.png\0所有图片\0*.png;*.jpg;*.jpeg;*.bmp\0\0";
        ofn.lpstrFile = path; ofn.nMaxFile = ARRAYSIZE(path);
        ofn.lpstrTitle = L"选择宠物外形（透明 PNG 效果最好）";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        menuOpen = true;
        if (GetOpenFileNameW(&ofn)) {
            if (loadCustom(path)) {
                state.setMood(pet::Mood::Happy); say(L"换上新外形啦！"); saveSettings();
            } else MessageBoxW(hwnd, L"无法读取这张图片。请使用宽高不超过 8192 像素的 PNG 或 JPG。",
                               L"毛球", MB_OK | MB_ICONINFORMATION);
        }
        menuOpen = false; break;
    }
    case C_RESET:
        customImage.reset(); customPath.clear(); state.setMood(pet::Mood::Idle);
        say(L"你的照片小猫回来啦。 "); saveSettings(); break;
    case C_HIDE:
        if (trayReady) { hidden = true; ShowWindow(hwnd, SW_HIDE); }
        else say(L"托盘暂不可用，我先留在这里。 ");
        break;
    case C_HELP:
        menuOpen = true;
        MessageBoxW(hwnd,
            L"毛球 · 你的照片小猫\n\n"
            L"单击：摸摸它\n拖动：移动位置\n双击：睡觉 / 叫醒\n右键：喂食、逗猫、散步、大小和退出\n\n"
            L"托盘图标：双击显示，右键打开菜单。\n"
            L"使用“散步”可以暂停或恢复自动移动。\n"
            L"自定义外形推荐透明 PNG；普通照片会保留背景。\n\n"
            L"本程序离线运行，无需安装，不会自动开机启动。\n"
            L"设置保存在 %LOCALAPPDATA%\\FluffyDesktopPet。",
            L"毛球 · 使用说明", MB_OK | MB_ICONINFORMATION);
        menuOpen = false; break;
    case C_EXIT: DestroyWindow(hwnd); return;
    }
    render();
}

void popupMenu(POINT point) {
    HMENU menu = CreatePopupMenu(), sizes = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"毛球  /  今天也陪着你");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, C_PET, L"摸摸我");
    AppendMenuW(menu, MF_STRING, C_FEED, L"喂小鱼干");
    AppendMenuW(menu, MF_STRING, C_PLAY, L"逗猫 · 跳一跳");
    AppendMenuW(menu, MF_STRING, C_SLEEP, state.mood == pet::Mood::Sleeping ? L"叫醒猫咪" : L"睡个好觉");
    AppendMenuW(menu, MF_STRING | (state.roaming ? MF_CHECKED : 0), C_ROAM, L"自动散步");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sizes, MF_STRING | (petSize==180 ? MF_CHECKED : 0), C_SMALL, L"小 · 180");
    AppendMenuW(sizes, MF_STRING | (petSize==240 ? MF_CHECKED : 0), C_MEDIUM, L"中 · 240");
    AppendMenuW(sizes, MF_STRING | (petSize==320 ? MF_CHECKED : 0), C_LARGE, L"大 · 320");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"猫咪大小");
    AppendMenuW(menu, MF_STRING | (topmost ? MF_CHECKED : 0), C_TOP, L"总在最前面");
    AppendMenuW(menu, MF_STRING, C_IMPORT, L"自定义外形…");
    if (customImage) AppendMenuW(menu, MF_STRING, C_RESET, L"恢复照片猫咪");
    AppendMenuW(menu, MF_STRING, C_HIDE, L"隐藏到托盘");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, C_HELP, L"使用说明");
    AppendMenuW(menu, MF_STRING, C_EXIT, L"退出");
    menuOpen = true;
    SetForegroundWindow(hwnd);
    int selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                    point.x, point.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    menuOpen = false;
    DestroyMenu(menu);
    if (selected) command(selected);
}

void tick() {
    ULONGLONG now = GetTickCount64();
    double dt = std::min(.06, (now-lastTick)/1000.0); lastTick = now;
    if (hidden || menuOpen) return;
    state.tick(dt);
    if (state.walking() && !pressed) {
        static double fractionalTravel = 0;
        fractionalTravel += dt * 17.0 * dpiScale;
        int step = static_cast<int>(fractionalTravel);
        fractionalTravel -= step;
        if (step > 0) {
            RECT r{}; GetWindowRect(hwnd,&r);
            RECT area=workArea();
            int x = r.left + step*state.direction;
            int end = std::max(area.left, area.right-canvasW);
            if (x <= area.left) { x = area.left; state.direction = 1; }
            if (x >= end) { x = end; state.direction = -1; }
            SetWindowPos(hwnd,nullptr,x,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
    }
    render();
}

LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated && msg == taskbarCreated) { addTray(); return 0; }
    switch (msg) {
    case WM_TIMER: tick(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT p; BeginPaint(window,&p); EndPaint(window,&p); return 0; }
    case WM_LBUTTONDOWN:
        pressed = true; moved = false; GetCursorPos(&downPoint);
        { RECT r{}; GetWindowRect(window,&r); startPosition={r.left,r.top}; }
        SetCapture(window); return 0;
    case WM_MOUSEMOVE:
        if (pressed) {
            POINT p{}; GetCursorPos(&p);
            if (std::abs(p.x-downPoint.x) > GetSystemMetrics(SM_CXDRAG) ||
                std::abs(p.y-downPoint.y) > GetSystemMetrics(SM_CYDRAG)) moved = true;
            if (moved) {
                if (!dragging) { KillTimer(window,2); dragging=true; state.setMood(pet::Mood::Dragging); say(L"被你拎起来啦～",2); }
                SetWindowPos(window,nullptr,startPosition.x+p.x-downPoint.x,
                             startPosition.y+p.y-downPoint.y,0,0,
                             SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            }
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!pressed) return 0;
        bool clicked = !moved;
        pressed=false; dragging=false; ReleaseCapture();
        if (clicked) {
            // Delay single-click until Windows' double-click interval has elapsed.
            SetTimer(window,2,GetDoubleClickTime(),nullptr);
        } else {
            state.setMood(pet::Mood::Idle); clampWindow(); saveSettings();
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        KillTimer(window,2); pressed=false; dragging=false; ReleaseCapture();
        command(C_SLEEP); return 0;
    case WM_CAPTURECHANGED:
        if (pressed || dragging) {
            pressed=false; dragging=false; state.setMood(pet::Mood::Idle); clampWindow();
        }
        return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: {
        POINT p{}; GetCursorPos(&p); popupMenu(p); return 0;
    }
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_HAND)); return TRUE;
    case WM_DPICHANGED: {
        dpiScale=HIWORD(wp)/96.f;
        const RECT* r=reinterpret_cast<const RECT*>(lp);
        SetWindowPos(window,nullptr,r->left,r->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        if (!resizeCanvas(false)) { DestroyWindow(window); return 0; }
        if (pressed) {
            RECT current{}; GetWindowRect(window,&current);
            startPosition={current.left,current.top}; GetCursorPos(&downPoint);
        }
        return 0;
    }
    case WM_DISPLAYCHANGE: clampWindow(); return 0;
    case WM_SETTINGCHANGE:
        if (wp==SPI_SETWORKAREA) clampWindow();
        return 0;
    case SHOW_MESSAGE: showPet(); say(L"我在这里！"); return 0;
    case TRAY_MESSAGE:
        if (lp==WM_LBUTTONDBLCLK) { showPet(); say(L"我回来陪你啦！"); }
        else if (lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) {
            POINT p{}; GetCursorPos(&p); popupMenu(p);
        }
        return 0;
    case WM_COMMAND: command(LOWORD(wp)); return 0;
    case WM_QUERYENDSESSION: saveSettings(); return TRUE;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        saveSettings(); KillTimer(window,1); KillTimer(window,2);
        Shell_NotifyIconW(NIM_DELETE,&tray); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}

// Handle the click timer separately so it never drives the animation clock.
LRESULT CALLBACK dispatch(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg==WM_TIMER && wp==2) {
        KillTimer(window,2);
        if (!pressed && !dragging && !menuOpen) command(C_PET);
        return 0;
    }
    return windowProc(window,msg,wp,lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int) {
    instance = app;
    HANDLE mutex = CreateMutexW(nullptr,FALSE,L"Local\\FluffyDesktopPet.PhotoCat.v1");
    if (mutex && GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(CLASS_NAME,nullptr);
        if (existing) PostMessageW(existing,SHOW_MESSAGE,0,0);
        CloseHandle(mutex); return 0;
    }
    using SetDpiContext = BOOL (WINAPI*)(HANDLE);
    auto setDpi = reinterpret_cast<SetDpiContext>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
    if (setDpi) setDpi(reinterpret_cast<HANDLE>(-4));
    else SetProcessDPIAware();
    HRESULT com = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input; ULONG_PTR token=0;
    if (GdiplusStartup(&token,&input,nullptr)!=Ok) return 1;
    idleImage=loadResource(201); sleepImage=loadResource(202);
    if (!idleImage || idleImage->GetLastStatus()!=Ok) {
        MessageBoxW(nullptr,L"猫咪图片加载失败，请重新解压程序。",L"毛球",MB_OK|MB_ICONERROR);
        idleImage.reset(); sleepImage.reset(); GdiplusShutdown(token);
        if (SUCCEEDED(com)) CoUninitialize();
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    auto directory=settingsDirectory();
    if (!directory.empty()) configPath=directory+L"\\settings.ini";
    auto readInt=[](const wchar_t* key,int def) {
        return configPath.empty() ? def : static_cast<int>(GetPrivateProfileIntW(L"Pet",key,def,configPath.c_str()));
    };
    petSize=readInt(L"Size",240);
    if (petSize!=180 && petSize!=240 && petSize!=320) petSize=240;
    topmost=readInt(L"Topmost",1)!=0; state.roaming=readInt(L"Roaming",1)!=0;
    if (!configPath.empty()) {
        wchar_t path[32768]{};
        GetPrivateProfileStringW(L"Pet",L"Image",L"",path,ARRAYSIZE(path),configPath.c_str());
        if (path[0]) loadCustom(path);
    }
    petIcon=static_cast<HICON>(LoadImageW(app,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE));
    if (!petIcon) petIcon=LoadIconW(nullptr,IDI_APPLICATION);
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.style=CS_DBLCLKS;
    wc.lpfnWndProc=dispatch; wc.hInstance=app; wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
    wc.hIcon=petIcon; wc.hIconSm=petIcon; wc.lpszClassName=CLASS_NAME;
    if (!RegisterClassExW(&wc)) return 1;
    RECT desktop{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);
    int x=readInt(L"X",desktop.right-330), y=readInt(L"Y",desktop.bottom-310);
    hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|(topmost?WS_EX_TOPMOST:0),
        CLASS_NAME,L"毛球 · 桌面宠物",WS_POPUP,x,y,240,282,
        nullptr,nullptr,app,nullptr);
    int result = 1;
    if (hwnd) {
        using GetWindowDpi = UINT (WINAPI*)(HWND);
        auto getDpi=reinterpret_cast<GetWindowDpi>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        dpiScale=getDpi ? getDpi(hwnd)/96.f : 1.f;
        if (resizeCanvas()) {
            taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
            addTray(); say(L"你好呀！右键我，看看菜单。",7);
            ShowWindow(hwnd,SW_SHOWNOACTIVATE); render();
            lastTick=GetTickCount64(); SetTimer(hwnd,1,33,nullptr);
            MSG message{};
            while (GetMessageW(&message,nullptr,0,0)>0) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            result=0;
        } else DestroyWindow(hwnd);
    }
    freeBuffer(); customImage.reset(); idleImage.reset(); sleepImage.reset();
    GdiplusShutdown(token);
    if (SUCCEEDED(com)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return result;
}
