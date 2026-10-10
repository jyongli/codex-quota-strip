#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
namespace quota {
inline HICON MetricIcon(int size, const wchar_t* label, const std::wstring& value, COLORREF tint) {
    if (size < 16 || size > 128) return nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return nullptr;
    BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = size; bitmap.bmiHeader.biHeight = -size;
    bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!color) { DeleteDC(dc); return nullptr; }
    ZeroMemory(pixels, static_cast<size_t>(size) * size * sizeof(DWORD));
    auto oldBitmap = SelectObject(dc, color); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, tint);
    int captionHeight = size * 3 / 8;
    HFONT caption = CreateFontW(-captionHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT number = CreateFontW(-(size * (value.size() >= 4 ? 17 : 21) / 32), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto oldFont = SelectObject(dc, caption);
    RECT captionRect{0,0,size,captionHeight}, numberRect{0,captionHeight-1,size,size};
    DrawTextW(dc, label, -1, &captionRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, number);
    DrawTextW(dc, value.c_str(), -1, &numberRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    GdiFlush();
    for (int i = 0; i < size * size; ++i) pixels[i] = (pixels[i] & 0x00ffffff) ? (pixels[i] | 0xff000000) : 0;
    SelectObject(dc, oldFont); SelectObject(dc, oldBitmap);
    DeleteObject(caption); DeleteObject(number); DeleteDC(dc);
    std::vector<BYTE> maskBits(static_cast<size_t>((size + 15) / 16 * 2) * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO iconInfo{}; iconInfo.fIcon = TRUE; iconInfo.hbmColor = color; iconInfo.hbmMask = mask;
    HICON icon = mask ? CreateIconIndirect(&iconInfo) : nullptr;
    DeleteObject(color); if (mask) DeleteObject(mask);
    return icon;
}
inline unsigned long long MetricIdentity(const std::wstring& home, const std::wstring& executable) {
    // Windows associates a registered GUID with the EXE path. Each location needs its own identity.
    std::wstring key = home + L"|" + executable;
    for (auto& c : key) c = static_cast<wchar_t>(std::towlower(c));
    return std::hash<std::wstring>{}(key);
}
inline GUID MetricGuid(unsigned long long identity, UINT id) {
    // New namespace retires registrations made before executable paths were part of identity.
    GUID guid{0xf3a9a706,0x0e17,0x47ab,{}};
    for (int i = 0; i < 8; ++i) guid.Data4[i] = static_cast<BYTE>(identity >> (i * 8));
    guid.Data4[7] ^= static_cast<BYTE>(id);
    return guid;
}
}
