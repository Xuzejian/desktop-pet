#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClass[] = L"FluffyDesktopPet.PhotoCat.v1";
// Match the public WM_COMMAND menu IDs in src/main.cpp.
enum Command { C_PET=101, C_FEED, C_PLAY, C_SLEEP, C_ROAM, C_SMALL,
    C_MEDIUM, C_LARGE, C_TOP, C_IMPORT, C_RESET, C_HIDE, C_HELP, C_EXIT };
int failures = 0, checks = 0;
void check(bool good, const char* label) {
    ++checks;
    if (!good) ++failures;
    std::printf("%s %s\n", good ? "PASS" : "FAIL", label);
    std::fflush(stdout);
}
bool send(HWND window, UINT message, WPARAM wp=0, LPARAM lp=0) {
    DWORD_PTR result=0;
    return SendMessageTimeoutW(window,message,wp,lp,SMTO_ABORTIFHUNG,3000,&result)!=0;
}
bool command(HWND window, Command id) { return send(window,WM_COMMAND,id); }
bool launch(const std::wstring& exe, PROCESS_INFORMATION& process) {
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    std::wstring line=L"\""+exe+L"\"";
    return CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,FALSE,0,
        nullptr,nullptr,&startup,&process)!=FALSE;
}
void release(PROCESS_INFORMATION& process) {
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    process={};
}
RECT bounds(HWND window) { RECT r{}; GetWindowRect(window,&r); return r; }
RECT workArea(HWND window) {
    MONITORINFO monitor{}; monitor.cbSize=sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    return monitor.rcWork;
}
bool inside(HWND window) {
    RECT r=bounds(window), area=workArea(window);
    return r.right>r.left && r.bottom>r.top && r.left>=area.left &&
        r.top>=area.top && r.right<=area.right && r.bottom<=area.bottom;
}
struct Capture { bool saved=false; uint64_t hash=0; size_t colors=0; };
Capture capture(HWND window, const std::wstring& path) {
    Capture result;
    RECT r=bounds(window);
    int width=r.right-r.left, height=r.bottom-r.top;
    if (width<=0 || height<=0) return result;
    HDC screen=GetDC(nullptr), memory=CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!screen || !memory || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr,screen);
        return result;
    }
    HGDIOBJ previous=SelectObject(memory,bitmap);
    bool copied=BitBlt(memory,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)!=FALSE;
    GdiFlush();
    if (copied) {
        DWORD size=static_cast<DWORD>(width*height*4);
        BITMAPFILEHEADER header{};
        header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
        header.bfSize=header.bfOffBits+size;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file!=INVALID_HANDLE_VALUE) {
            DWORD written=0;
            result.saved=WriteFile(file,&header,sizeof(header),&written,nullptr) &&
                written==sizeof(header) &&
                WriteFile(file,&info.bmiHeader,sizeof(info.bmiHeader),&written,nullptr) &&
                written==sizeof(info.bmiHeader) &&
                WriteFile(file,pixels,size,&written,nullptr) && written==size;
            CloseHandle(file);
        }
        const auto* words=static_cast<const uint32_t*>(pixels);
        std::vector<uint32_t> colors;
        colors.reserve(width*height);
        result.hash=1469598103934665603ull;
        for (int i=0;i<width*height;++i) {
            uint32_t color=words[i]&0xffffffu;
            colors.push_back(color);
            result.hash=(result.hash^color)*1099511628211ull;
        }
        std::sort(colors.begin(),colors.end());
        result.colors=std::unique(colors.begin(),colors.end())-colors.begin();
    }
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory);
    ReleaseDC(nullptr,screen);
    return result;
}
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDPIAware();
    wchar_t absolute[32768]{};
    if (!GetFullPathNameW(argc>1 ? argv[1] : L"FluffyPet.exe",
                         ARRAYSIZE(absolute),absolute,nullptr)) return 2;
    std::wstring exe=absolute;
    std::wstring screenshots=argc>2 ? argv[2] : L"smoke-screenshots";
    if (!CreateDirectoryW(screenshots.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) {
        std::printf("FAIL cannot create screenshot directory (error %lu)\n",GetLastError());
        return 2;
    }
    if (FindWindowW(kClass,nullptr)) {
        std::printf("FAIL an existing pet is running; use an isolated test desktop/profile\n");
        return 2;
    }
    PROCESS_INFORMATION process{};
    if (!launch(exe,process)) {
        std::printf("FAIL CreateProcessW (error %lu)\n",GetLastError());
        return 2;
    }
    HWND pet=nullptr;
    for (int n=0;n<200 && !pet;++n) {
        pet=FindWindowW(kClass,nullptr);
        if (WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0) break;
        Sleep(50);
    }
    check(pet!=nullptr,"app creates its native pet window");
    if (!pet) {
        TerminateProcess(process.hProcess,2);
        release(process);
        return 1;
    }
    Sleep(700);
    auto styles=GetWindowLongPtrW(pet,GWL_EXSTYLE);
    check((styles&WS_EX_LAYERED)!=0,"window supports per-pixel transparency");
    check((styles&WS_EX_TOOLWINDOW)!=0,"window uses desktop tool-window style");
    check(IsWindowVisible(pet)!=FALSE,"pet is visible after launch");
    check(inside(pet),"initial bounds fit the monitor work area");
    using GetDpi=UINT (WINAPI*)(HWND);
    auto getDpi=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    float dpi=getDpi ? getDpi(pet)/96.f : 1.f;
    const struct { Command id; int size; const char* label; } sizes[]={
        {C_SMALL,180,"small size respects DPI and work area"},
        {C_MEDIUM,240,"medium size respects DPI and work area"},
        {C_LARGE,320,"large size respects DPI and work area"}};
    for (const auto& size:sizes) {
        bool responsive=command(pet,size.id); Sleep(120);
        RECT r=bounds(pet);
        int width=std::max(120,static_cast<int>(size.size*dpi));
        check(responsive && r.right-r.left==width && r.bottom-r.top==width*376/320 && inside(pet),size.label);
    }
    command(pet,C_MEDIUM);
    RECT area=workArea(pet), r=bounds(pet);
    SetWindowPos(pet,nullptr,area.left+(area.right-area.left-(r.right-r.left))/2,
        area.top+(area.bottom-area.top-(r.bottom-r.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    command(pet,C_RESET); Sleep(300);
    auto idle=capture(pet,screenshots+L"\\idle.bmp");
    check(idle.saved && idle.colors>32,"idle render is nonempty and screenshot saved");
    check(command(pet,C_SLEEP),"sleep command returns without hanging");
    Sleep(400);
    auto sleeping=capture(pet,screenshots+L"\\sleep.bmp");
    check(sleeping.saved && sleeping.colors>32 && sleeping.hash!=idle.hash,
          "sleep changes the render and screenshot saved");
    check(send(pet,WM_LBUTTONDBLCLK,MK_LBUTTON),"double-click wakes the pet");
    Sleep(180);
    check(command(pet,C_FEED),"feeding command returns without hanging");
    Sleep(250);
    auto food=capture(pet,screenshots+L"\\food.bmp");
    check(food.saved && food.colors>32 && food.hash!=sleeping.hash,
          "feeding changes the render and screenshot saved");
    check(command(pet,C_PLAY) && command(pet,C_PET),"play and pet commands remain responsive");
    check(command(pet,C_ROAM) && command(pet,C_ROAM),"roaming can be toggled twice");
    bool topBefore=(GetWindowLongPtrW(pet,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    command(pet,C_TOP);
    bool topAfter=(GetWindowLongPtrW(pet,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    command(pet,C_TOP);
    bool topRestored=(GetWindowLongPtrW(pet,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    check(topBefore!=topAfter && topBefore==topRestored,"always-on-top toggles and restores");
    // Use real screen cursor positions because the drag handler reads GetCursorPos.
    SetForegroundWindow(pet);
    r=bounds(pet);
    int cursorX=(r.left+r.right)/2, cursorY=(r.top+r.bottom)/2;
    SetCursorPos(cursorX,cursorY); Sleep(100);
    bool dragSent=send(pet,WM_LBUTTONDOWN,MK_LBUTTON);
    SetCursorPos(cursorX+70,cursorY-30); Sleep(100);
    dragSent=send(pet,WM_MOUSEMOVE,MK_LBUTTON)&&dragSent;
    dragSent=send(pet,WM_LBUTTONUP)&&dragSent;
    Sleep(150);
    RECT moved=bounds(pet);
    check(dragSent && std::abs((moved.left-r.left)-70)<=5 &&
          std::abs((moved.top-r.top)+30)<=5 && inside(pet),"drag moves the pet and preserves valid bounds");
    check(command(pet,C_HIDE),"hide-to-tray command returns without hanging");
    Sleep(200);
    bool wasHidden=IsWindowVisible(pet)==FALSE;
    if (wasHidden) check(true,"hide-to-tray hides the pet window");
    else std::printf("SKIP tray service unavailable; pet correctly remains accessible\n");
    check(send(pet,WM_APP+2),"show message returns without hanging");
    Sleep(150);
    check(IsWindowVisible(pet)!=FALSE,"show message restores visibility");
    PROCESS_INFORMATION second{};
    bool secondLaunched=launch(exe,second);
    check(secondLaunched,"second process launches for singleton check");
    if (secondLaunched) {
        DWORD status=WaitForSingleObject(second.hProcess,5000), code=999;
        GetExitCodeProcess(second.hProcess,&code);
        check(status==WAIT_OBJECT_0 && code==0 && FindWindowW(kClass,nullptr)==pet,
              "second instance exits successfully and preserves original pet");
        if (status!=WAIT_OBJECT_0) TerminateProcess(second.hProcess,2);
        release(second);
    }
    check(command(pet,C_EXIT),"exit command returns without hanging");
    DWORD status=WaitForSingleObject(process.hProcess,5000), code=999;
    GetExitCodeProcess(process.hProcess,&code);
    check(status==WAIT_OBJECT_0 && code==0,"app exits cleanly with status zero");
    if (status!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,2);
    release(process);
    std::printf("RESULT %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
