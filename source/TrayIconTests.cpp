#include "TrayIcons.h"
#include <iostream>
#include <filesystem>
#include <fstream>
int wmain(int argc, wchar_t** argv) {
    HICON warm = quota::MetricIcon(32, L"5h", L"-", RGB(180,180,180));
    if (!warm) { std::cerr << "FAIL: quota number must produce a real taskbar icon\n"; return 1; }
    DestroyIcon(warm); // Establish a baseline after Windows initializes its stock GDI objects.
    DWORD baselineGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), baselineUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 100; ++i) {
        HICON icon = quota::MetricIcon(32, i % 2 ? L"5h" : L"W", i % 3 ? L"10" : L"-", RGB(250,180,80));
        if (!icon) { std::cerr << "FAIL: quota number must produce a real taskbar icon\n"; return 1; }
        ICONINFO info{};
        if (!GetIconInfo(icon, &info)) { std::cerr << "FAIL: Windows must accept the generated icon\n"; DestroyIcon(icon); return 1; }
        BITMAP bitmap{}; GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
        bool sizeOk = bitmap.bmWidth == 32 && bitmap.bmHeight == 32 && bitmap.bmBitsPixel == 32;
        DeleteObject(info.hbmColor); DeleteObject(info.hbmMask); DestroyIcon(icon);
        if (!sizeOk) { std::cerr << "FAIL: taskbar icon must retain its native size and alpha bitmap\n"; return 1; }
    }
    if (GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) > baselineGdi || GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) > baselineUser) {
        std::cerr << "FAIL: updating numeric icons must not leak GDI or USER resources; GDI " << baselineGdi << " -> " << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << "; USER " << baselineUser << " -> " << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) << "\n"; return 1;
    }
    std::cout << "PASS: real alpha icons and 100 render/dispose cycles without resource growth\n";
    if (argc > 1) {
        int width = 80, height = 32;
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        DWORD* pixels = nullptr; HBITMAP image = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        auto old = SelectObject(dc, image); for (int i = 0; i < width * height; ++i) pixels[i] = 0xff202020;
        HICON five = quota::MetricIcon(32, L"5h", L"-", RGB(175,175,175)), week = quota::MetricIcon(32, L"W", L"10", RGB(246,183,92));
        DrawIconEx(dc, 0, 0, five, 32, 32, 0, nullptr, DI_NORMAL); DrawIconEx(dc, 48, 0, week, 32, 32, 0, nullptr, DI_NORMAL); GdiFlush();
        BITMAPFILEHEADER header{}; header.bfType = 0x4D42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + width * height * 4;
        std::ofstream out(std::filesystem::path(argv[1]), std::ios::binary);
        out.write(reinterpret_cast<char*>(&header), sizeof(header)); out.write(reinterpret_cast<char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        out.write(reinterpret_cast<char*>(pixels), static_cast<std::streamsize>(width * height * 4));
        SelectObject(dc, old); DestroyIcon(five); DestroyIcon(week); DeleteObject(image); DeleteDC(dc);
        if (!out.good()) return 1;
    }
    return 0;
}
