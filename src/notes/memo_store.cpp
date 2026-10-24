#include "memo_store.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>

#include "common/win_util.h"
#include "notes/memo_format.h"

namespace wp {

namespace {

std::wstring IniPath() { return AppDataDir() + L"\\organizer.ini"; }

std::wstring DefaultDir() {
    if (UsingTestDataDir()) return AppDataDir() + L"\\memos";  // tests never write to the user's Documents
    PWSTR docs = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) dir = docs;
    CoTaskMemFree(docs);
    if (dir.empty()) dir = AppDataDir();
    return dir + L"\\Stack Neverflow 메모";
}

}  // namespace

std::wstring MemoDir() {
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"memo", L"memoDir", L"", buf, MAX_PATH, IniPath().c_str());
    std::wstring dir = buf;
    if (dir.empty()) {
        dir = DefaultDir();
        // Remember where they are, so the same folder is used (and found) from now on.
        WritePrivateProfileStringW(L"memo", L"memoDir", dir.c_str(), IniPath().c_str());
    }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

std::vector<MemoInfo> ListMemos() {
    std::vector<MemoInfo> out;
    std::wstring dir = MemoDir();
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*.txt").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        MemoInfo m;
        m.path = dir + L"\\" + fd.cFileName;
        std::wstring text = ReadMemo(m.path);
        m.title = MemoTitle(text);
        m.excerpt = MemoExcerpt(text);
        m.created = fd.ftCreationTime;
        out.push_back(std::move(m));
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(out.begin(), out.end(), [](const MemoInfo& a, const MemoInfo& b) { return CompareFileTime(&a.created, &b.created) > 0; });
    // Then the order they were dragged into; memos not in it (new, or added outside the app) stay on top.
    std::vector<wchar_t> buf(32768);
    GetPrivateProfileStringW(L"memo", L"order", L"", buf.data(), (DWORD)buf.size(), IniPath().c_str());
    std::wstring order = L"|" + std::wstring(buf.data()) + L"|";
    auto rank = [&](const MemoInfo& m) {
        size_t at = order.find(L"|" + m.path.substr(m.path.find_last_of(L'\\') + 1) + L"|");
        return at == std::wstring::npos ? -1 : (int)at;
    };
    std::stable_sort(out.begin(), out.end(), [&](const MemoInfo& a, const MemoInfo& b) { return rank(a) < rank(b); });
    return out;
}

void SaveMemoOrder(const std::vector<MemoInfo>& memos) {
    std::wstring order;
    for (const MemoInfo& m : memos) order += (order.empty() ? L"" : L"|") + m.path.substr(m.path.find_last_of(L'\\') + 1);
    WritePrivateProfileStringW(L"memo", L"order", order.c_str(), IniPath().c_str());
}

std::wstring NewMemo() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"메모 %04u-%02u-%02u %02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring base = MemoDir() + L"\\" + name, path = base + L".txt";
    for (int i = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) path = base + L" (" + std::to_wstring(i) + L").txt";
    return WriteMemo(path, L"") ? path : L"";
}

std::wstring ReadMemo(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    std::string bytes;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20)) {
        bytes.resize((size_t)size.QuadPart);
        DWORD n = 0;
        ReadFile(f, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
        bytes.resize(n);
    }
    CloseHandle(f);
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);
    return Widen(bytes);
}

bool WriteMemo(const std::wstring& path, const std::wstring& text) {
    // Windows line breaks, so Notepad and every other editor show the lines.
    std::wstring crlf;
    crlf.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\r') {
            crlf += L"\r\n";
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            crlf += L"\r\n";
        } else {
            crlf += c;
        }
    }
    return WriteFileReplacing(path, Utf8(crlf), true);
}

bool RecycleMemo(const std::wstring& path) {
    if (UsingTestDataDir()) return DeleteFileW(path.c_str()) != FALSE;  // tests don't fill the user's Recycle Bin
    std::wstring from = path + L'\0';  // double-null terminated list
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

}  // namespace wp
