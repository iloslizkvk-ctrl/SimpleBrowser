#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>

namespace {
constexpr int ID_OPEN = 1001;
constexpr int ID_EXTRACT = 1002;
constexpr int ID_CREATE = 1003;
constexpr int ID_STATUS = 1004;
constexpr int ID_PATH = 1005;
constexpr UINT COPY_OPTIONS = 4 | 16 | 1024; // no progress UI, confirm-no, no error UI

HWND g_pathLabel = nullptr;
HWND g_statusLabel = nullptr;
std::wstring g_archivePath;

void SetStatus(const std::wstring& text) {
    if (g_statusLabel) SetWindowTextW(g_statusLabel, text.c_str());
}

std::wstring PickFile(HWND owner, bool save) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"ZIP archives (*.zip)\0*.zip\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? (OFN_OVERWRITEPROMPT | OFN_NOREADONLYRETURN) : OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? path : L"";
}

std::wstring PickFolder(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? path : L"";
}

HRESULT GetDispId(IDispatch* object, const wchar_t* name, DISPID* id) {
    LPOLESTR names[] = { const_cast<LPOLESTR>(name) };
    return object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, id);
}

IDispatch* ShellNamespace(IDispatch* shell, const std::wstring& path) {
    DISPID id = 0;
    if (FAILED(GetDispId(shell, L"NameSpace", &id))) return nullptr;
    VARIANT arg;
    VariantInit(&arg);
    arg.vt = VT_BSTR;
    arg.bstrVal = SysAllocString(path.c_str());
    if (!arg.bstrVal) return nullptr;
    DISPPARAMS params{ &arg, nullptr, 1, 0 };
    VARIANT result;
    VariantInit(&result);
    const HRESULT hr = shell->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &result, nullptr, nullptr);
    VariantClear(&arg);
    if (FAILED(hr) || result.vt != VT_DISPATCH || !result.pdispVal) {
        VariantClear(&result);
        return nullptr;
    }
    IDispatch* folder = result.pdispVal;
    folder->AddRef();
    VariantClear(&result);
    return folder;
}

IDispatch* FolderItems(IDispatch* folder) {
    DISPID id = 0;
    if (FAILED(GetDispId(folder, L"Items", &id))) return nullptr;
    DISPPARAMS params{};
    VARIANT result;
    VariantInit(&result);
    const HRESULT hr = folder->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &result, nullptr, nullptr);
    if (FAILED(hr) || result.vt != VT_DISPATCH || !result.pdispVal) {
        VariantClear(&result);
        return nullptr;
    }
    IDispatch* items = result.pdispVal;
    items->AddRef();
    VariantClear(&result);
    return items;
}

HRESULT CopyHere(IDispatch* destination, IDispatch* source) {
    DISPID id = 0;
    HRESULT hr = GetDispId(destination, L"CopyHere", &id);
    if (FAILED(hr)) return hr;
    VARIANT args[2];
    VariantInit(&args[0]);
    VariantInit(&args[1]);
    args[0].vt = VT_I4; // COM arguments are in reverse order
    args[0].lVal = COPY_OPTIONS;
    args[1].vt = VT_DISPATCH;
    args[1].pdispVal = source;
    source->AddRef();
    DISPPARAMS params{ args, nullptr, 2, 0 };
    VARIANT result;
    VariantInit(&result);
    hr = destination->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &result, nullptr, nullptr);
    VariantClear(&result);
    VariantClear(&args[1]);
    return hr;
}

bool CreateEmptyZip(const std::wstring& path) {
    // An empty ZIP archive is a 22-byte End of Central Directory record.
    const unsigned char eocd[22] = { 'P', 'K', 5, 6 };
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, eocd, sizeof(eocd), &written, nullptr);
    CloseHandle(file);
    return ok && written == sizeof(eocd);
}

bool StartCopy(const std::wstring& sourcePath, const std::wstring& destinationPath) {
    IDispatch* shell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void**>(&shell));
    if (FAILED(hr) || !shell) return false;
    IDispatch* source = ShellNamespace(shell, sourcePath);
    IDispatch* destination = ShellNamespace(shell, destinationPath);
    IDispatch* items = source ? FolderItems(source) : nullptr;
    bool ok = items && destination && SUCCEEDED(CopyHere(destination, items));
    if (items) items->Release();
    if (source) source->Release();
    if (destination) destination->Release();
    shell->Release();
    return ok;
}

void ExtractArchive(HWND owner) {
    if (g_archivePath.empty()) {
        g_archivePath = PickFile(owner, false);
        if (g_archivePath.empty()) return;
        SetWindowTextW(g_pathLabel, g_archivePath.c_str());
    }
    const std::wstring folder = PickFolder(owner, L"Choose a folder for the extracted files");
    if (folder.empty()) return;
    if (StartCopy(g_archivePath, folder)) {
        SetStatus(L"Extraction started. Windows may take a moment to finish copying.");
    } else {
        SetStatus(L"Could not start extraction. Check that this is a valid ZIP file.");
    }
}

void CreateArchive(HWND owner) {
    const std::wstring folder = PickFolder(owner, L"Choose the folder whose contents will be archived");
    if (folder.empty()) return;
    const std::wstring archive = PickFile(owner, true);
    if (archive.empty()) return;
    if (!CreateEmptyZip(archive)) {
        SetStatus(L"Could not create the ZIP file.");
        return;
    }
    if (StartCopy(folder, archive)) {
        g_archivePath = archive;
        SetWindowTextW(g_pathLabel, archive.c_str());
        SetStatus(L"Archive creation started. Windows may take a moment to finish copying.");
    } else {
        SetStatus(L"Could not start archiving. The empty ZIP file may remain at the selected path.");
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto make = [&](DWORD style, const wchar_t* text, int id, int x, int y, int w, int h) {
            HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        make(BS_PUSHBUTTON, L"Open ZIP...", ID_OPEN, 18, 18, 120, 32);
        make(BS_PUSHBUTTON, L"Extract...", ID_EXTRACT, 148, 18, 120, 32);
        make(BS_PUSHBUTTON, L"Create ZIP...", ID_CREATE, 278, 18, 120, 32);
        g_pathLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"No archive selected", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 18, 66, 380, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PATH)), nullptr, nullptr);
        SendMessageW(g_pathLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_statusLabel = CreateWindowExW(0, L"STATIC", L"ZIP archives use the Windows Shell.", WS_CHILD | WS_VISIBLE | SS_LEFT, 18, 108, 380, 44, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
        SendMessageW(g_statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_OPEN:
            g_archivePath = PickFile(hwnd, false);
            if (!g_archivePath.empty()) {
                SetWindowTextW(g_pathLabel, g_archivePath.c_str());
                SetStatus(L"ZIP selected. Click Extract to choose a destination folder.");
            }
            return 0;
        case ID_EXTRACT: ExtractArchive(hwnd); return 0;
        case ID_CREATE: CreateArchive(hwnd); return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) {
        MessageBoxW(nullptr, L"Could not initialize Windows Shell support.", L"SimpleArchive", MB_OK | MB_ICONERROR);
        return 1;
    }
    const wchar_t className[] = L"SimpleArchiveWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) {
        CoUninitialize();
        return 1;
    }
    HWND hwnd = CreateWindowExW(0, className, L"SimpleArchive", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 430, 205, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
