#pragma once

// ============================================================
// PathHelper — Windows Unicode path conversion for libtiff
// ============================================================
//
// libtiff's TIFFOpen uses fopen() on Windows, which cannot handle
// Unicode paths (e.g. Chinese characters in OneDrive folder names).
// This helper converts Unicode paths to Windows 8.3 short paths
// (GetShortPathNameW) which are ASCII-safe and work with fopen().
//

#include <QString>
#include <QFile>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PathHelper {

// Convert a Unicode QString path to a form that libtiff's TIFFOpen
// can handle on Windows. On non-Windows platforms, returns UTF-8 as-is.
inline QString toTiffPath(const QString& path) {
#ifdef _WIN32
    // libtiff uses fopen(), which cannot open Unicode paths on Windows.
    // GetShortPathNameW needs the file to exist, so create an empty file
    // first. TIFFOpen("w") will overwrite it afterwards.
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.close();
        }
    }

    // Convert QString (UTF-16) to wchar_t*
    std::wstring wpath = path.toStdWString();

    // Get the length of the short path
    DWORD len = GetShortPathNameW(wpath.c_str(), nullptr, 0);
    if (len == 0) {
        // Fallback: return UTF-8 and hope for the best
        return path;
    }

    // Get the short path
    std::wstring shortPath(len, 0);
    DWORD result = GetShortPathNameW(wpath.c_str(), &shortPath[0], len);
    if (result == 0 || result >= len) {
        // Fallback: return UTF-8
        return path;
    }

    // Remove trailing null
    shortPath.resize(result);

    // Convert back to QString
    return QString::fromStdWString(shortPath);
#else
    return path;
#endif
}

// Get the UTF-8 const char* for TIFFOpen, using short path on Windows
inline std::string toTiffString(const QString& path) {
    return toTiffPath(path).toStdString();
}

} // namespace PathHelper
