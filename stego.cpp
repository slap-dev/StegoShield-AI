// stego.cpp — Стеганография + ИИ + Сортировка + Dark Theme
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <initguid.h>
#include <wincodec.h>
#include <shlobj.h>
#include <dwmapi.h> 
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace fs = std::filesystem;

//  tools 
static std::string wstringToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], size, nullptr, nullptr);
    return out;
}
static std::wstring utf8ToWstring(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size);
    return out;
}
static std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    size_t b = s.find_last_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    return s.substr(a, b - a + 1);
}

//  Изображение (WIC) 
struct Bitmap { int width = 0, height = 0; std::vector<uint8_t> pixels; };

static bool loadImage(const wchar_t* path, Bitmap& bmp) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory))) return false;
    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) { factory->Release(); return false; }
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame))) { decoder->Release(); factory->Release(); return false; }
    IWICFormatConverter* conv = nullptr;
    factory->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) { conv->Release(); frame->Release(); decoder->Release(); factory->Release(); return false; }

    UINT w = 0, h = 0; conv->GetSize(&w, &h);
    bmp.width = (int)w; bmp.height = (int)h; bmp.pixels.resize((size_t)w * h * 3);
    HRESULT hr = conv->CopyPixels(nullptr, w * 3, (UINT)bmp.pixels.size(), bmp.pixels.data());

    conv->Release(); frame->Release(); decoder->Release(); factory->Release();
    if (FAILED(hr)) return false;
    for (size_t i = 0; i < bmp.pixels.size(); i += 3) std::swap(bmp.pixels[i], bmp.pixels[i + 2]);
    return true;
}

static bool saveImage(const wchar_t* path, const Bitmap& bmp) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory))) return false;
    IWICStream* stream = nullptr; factory->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) { stream->Release(); factory->Release(); return false; }
    IWICBitmapEncoder* enc = nullptr; factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* frame = nullptr; IPropertyBag2* props = nullptr;
    enc->CreateNewFrame(&frame, &props); frame->Initialize(props); frame->SetSize(bmp.width, bmp.height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR; frame->SetPixelFormat(&fmt);
    std::vector<uint8_t> tmp = bmp.pixels;
    for (size_t i = 0; i < tmp.size(); i += 3) std::swap(tmp[i], tmp[i + 2]);
    frame->WritePixels(bmp.height, bmp.width * 3, (UINT)tmp.size(), tmp.data());
    frame->Commit(); enc->Commit();
    if (props) props->Release(); frame->Release(); enc->Release(); stream->Release(); factory->Release();
    return true;
}

//  LSB
static bool hideLogic(const std::wstring& imgPath, const std::wstring& secret, const std::wstring& outPath, std::wstring& msg) {
    Bitmap bmp; if (!loadImage(imgPath.c_str(), bmp)) { msg = L"Ошибка загрузки."; return false; }
    std::string utf8 = wstringToUtf8(secret) + "#####";
    std::vector<uint8_t> bits;
    for (unsigned char c : utf8) for (int k = 7; k >= 0; --k) bits.push_back((c >> k) & 1);
    if (bits.size() > (size_t)bmp.width * bmp.height) { msg = L"Текст слишком длинный!"; return false; }
    size_t idx = 0;
    for (int y = 0; y < bmp.height && idx < bits.size(); ++y)
        for (int x = 0; x < bmp.width && idx < bits.size(); ++x) {
            uint8_t& blue = bmp.pixels[((size_t)y * bmp.width + x) * 3 + 2];
            blue = (blue & ~1) | bits[idx++];
        }
    if (!saveImage(outPath.c_str(), bmp)) { msg = L"Ошибка сохранения."; return false; }
    msg = L"Успешно сохранено!"; return true;
}

struct LsbExtraction { std::vector<uint8_t> bytes; bool foundMarker = false; };
static LsbExtraction extractLsbBytes(const std::wstring& imgPath, size_t maxBytes = 256) {
    LsbExtraction res; Bitmap bmp; if (!loadImage(imgPath.c_str(), bmp)) return res;
    size_t maxBits = maxBytes * 8, bitCount = 0; int cur = 0, cnt = 0;
    for (int y = 0; y < bmp.height && bitCount < maxBits; ++y) {
        for (int x = 0; x < bmp.width && bitCount < maxBits; ++x) {
            uint8_t blue = bmp.pixels[((size_t)y * bmp.width + x) * 3 + 2];
            cur = (cur << 1) | (blue & 1); ++bitCount;
            if (++cnt == 8) {
                res.bytes.push_back((uint8_t)cur); cur = 0; cnt = 0;
                size_t n = res.bytes.size();
                if (n >= 5 && res.bytes[n - 5] == '#' && res.bytes[n - 4] == '#' && res.bytes[n - 3] == '#' && res.bytes[n - 2] == '#' && res.bytes[n - 1] == '#') {
                    res.bytes.resize(n - 5); res.foundMarker = true; return res;
                }
            }
        }
    }
    return res;
}

// ИИ
struct StegoAI {
    const int INPUT_SIZE = 6, H1 = 16, OUTPUT_SIZE = 2; // 2 выхода: 0=Текст, 1=Код
    std::vector<float> w1, b1, w2, b2;
    bool isLoaded = false;

    bool loadWeights(const std::string& filename) {
        std::ifstream file(filename); if (!file.is_open()) return false;
        std::string name;
        auto readLayer = [&](std::vector<float>& vec, size_t size) {
            file >> name; vec.resize(size);
            for (size_t i = 0; i < size; ++i) file >> vec[i];
            };
        readLayer(w1, H1 * INPUT_SIZE); readLayer(b1, H1);
        readLayer(w2, OUTPUT_SIZE * H1); readLayer(b2, OUTPUT_SIZE);
        isLoaded = true; return true;
    }

    float relu(float x) { return std::max(0.0f, x); }

    int predict(const std::vector<uint8_t>& inputBytes) {
        if (!isLoaded || inputBytes.empty()) return -1;
        std::vector<float> in(INPUT_SIZE, 0.0f);
        int printable = 0, spaces = 0, newlines = 0, codeChars = 0;
        int counts[256] = { 0 };
        for (uint8_t b : inputBytes) {
            counts[b]++;
            if (b >= 32 && b <= 126) printable++;
            if (b == 32) spaces++;
            if (b == 10 || b == 13) newlines++;
            if (b == '{' || b == '}' || b == '(' || b == ')' || b == '[' || b == ']' || b == ';' || b == '=' || b == '#' || b == '<' || b == '>') codeChars++;
        }
        double entropy = 0.0;
        for (int i = 0; i < 256; ++i) {
            if (counts[i] > 0) {
                double p = (double)counts[i] / inputBytes.size();
                entropy -= p * std::log2(p);
            }
        }
        float L = (float)inputBytes.size();
        in[0] = entropy / 8.0f; in[1] = printable / L; in[2] = spaces / L;
        in[3] = newlines / L; in[4] = codeChars / L; in[5] = 0.0f;

        std::vector<float> h1(H1, 0.0f);
        for (int i = 0; i < H1; ++i) {
            for (int j = 0; j < INPUT_SIZE; ++j) h1[i] += in[j] * w1[i * INPUT_SIZE + j];
            h1[i] = relu(h1[i] + b1[i]);
        }
        std::vector<float> out(OUTPUT_SIZE, 0.0f);
        for (int i = 0; i < OUTPUT_SIZE; ++i) {
            for (int j = 0; j < H1; ++j) out[i] += h1[j] * w2[i * H1 + j];
            out[i] += b2[i];
        }
        return std::max_element(out.begin(), out.end()) - out.begin();
    }
};
static StegoAI g_ai;

//  GUI 
#define ID_PATH    101
#define ID_BROWSE  102
#define ID_TEXT    103
#define ID_HIDE    104
#define ID_EXTRACT 105
#define ID_STATUS  106
#define ID_ANALYZE 107
#define ID_SCAN    108

static HWND g_path = nullptr, g_text = nullptr, g_status = nullptr;
static HBRUSH g_bgBrush = nullptr;
static COLORREF g_bgColor = RGB(30, 30, 30);Ы

static void setStatus(const wchar_t* s) { SetWindowTextW(g_status, s); }
static std::wstring getText(HWND h) {
    int len = GetWindowTextLengthW(h); std::wstring out(len, 0);
    GetWindowTextW(h, &out[0], len + 1); return out;
}

static void onBrowse(HWND hwnd) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Images\0*.png\0All files\0*.*\0"; ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_path, buf);
}

static void onHide(HWND hwnd) {
    std::wstring path = getText(g_path), text = trim(getText(g_text));
    if (path.empty() || text.empty()) { setStatus(L"Ошибка ввода"); return; }
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PNG\0*.png\0"; ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    setStatus(L"Шифрование...");
    std::wstring msg;
    if (hideLogic(path, text, buf, msg)) { setStatus(L"Сохранено!"); MessageBoxW(hwnd, msg.c_str(), L"ОК", MB_OK); }
    else { setStatus(L"Ошибка"); MessageBoxW(hwnd, msg.c_str(), L"Ошибка", MB_OK | MB_ICONERROR); }
}

static void onExtract(HWND hwnd) {
    std::wstring path = getText(g_path);
    if (path.empty()) return;
    setStatus(L"Расшифровка...");
    auto ext = extractLsbBytes(path, 1 << 20);
    if (!ext.foundMarker) { MessageBoxW(hwnd, L"Маркер не найден.", L"Пусто", MB_OK | MB_ICONWARNING); return; }
    std::wstring txt = utf8ToWstring(std::string(ext.bytes.begin(), ext.bytes.end()));
    SetWindowTextW(g_text, txt.c_str()); setStatus(L"Найдено!");
}

static void onAnalyze(HWND hwnd) {
    std::wstring path = getText(g_path);
    if (path.empty()) { setStatus(L"Ошибка: выберите файл!"); return; }

    setStatus(L"Анализ...");
    // Читаем 1 МБ, чтобы найти маркер #####
    auto ext = extractLsbBytes(path, 1 << 20);

    std::wstring resultText;

    //маркера нет — это 100% пусто
    if (!ext.foundMarker) {
        resultText = L"Пусто / Нет скрытых данных";
    }
    else {
        //маркер есть, нейросеть решает: Текст или Код
        int classId = g_ai.predict(ext.bytes);
        if (classId == 0) resultText = L"Текст";
        else resultText = L"Программный код";
    }

    std::wstring full = L"[Проанализировано байт: " + std::to_wstring(ext.bytes.size()) + L"]\n\nРезультат ИИ: " + resultText;
    SetWindowTextW(g_text, full.c_str());
    setStatus(L"Анализ завершён");
    MessageBoxW(hwnd, resultText.c_str(), L"Результат ИИ", MB_OK | MB_ICONINFORMATION);
}

//  СКАНИРОВАНИЕ И СОРТИРОВКА 
static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED) SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    return 0;
}

static void onScanFolder(HWND hwnd) {
    if (!g_ai.isLoaded) { MessageBoxW(hwnd, L"Нет model_weights.txt!", L"Ошибка", MB_ICONERROR); return; }

    wchar_t folderPath[MAX_PATH] = {};
    BROWSEINFOW bi = {}; bi.hwndOwner = hwnd; bi.lpszTitle = L"Выберите папку с PNG:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE; bi.lpfn = BrowseCallbackProc; bi.lParam = (LPARAM)folderPath;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    SHGetPathFromIDListW(pidl, folderPath); CoTaskMemFree(pidl);

    fs::path dir(folderPath);
    fs::path dirText = dir / "Sorted_Text", dirCode = dir / "Sorted_Code", dirEmpty = dir / "Sorted_Empty";
    fs::create_directories(dirText); fs::create_directories(dirCode); fs::create_directories(dirEmpty);

    int count = 0, moved = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".png" || entry.path().extension() == ".PNG") {
            std::wstring wpath = entry.path().wstring();

            // Ищем маркер. МБ на конец текста
            auto ext = extractLsbBytes(wpath, 1 << 20);

            fs::path targetDir;
            if (!ext.foundMarker) {
                // маркера нет — это 100% пустышка, ИИ не нужен
                targetDir = dirEmpty;
            }
            else {
                // есть — ИИ решает:текст/код
                int cls = g_ai.predict(ext.bytes); // ext.bytes уже без маркера
                targetDir = (cls == 0) ? dirText : dirCode; // 0 = Текст, 1 = Код
            }

            try {
                fs::rename(entry.path(), targetDir / entry.path().filename());
                moved++;
            }
            catch (...) {}
            count++;
        }
    }
    std::wstring msg = L"Обработано: " + std::to_wstring(count) + L"\nПеремещено: " + std::to_wstring(moved);
    MessageBoxW(hwnd, msg.c_str(), L"Сортировка", MB_OK);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        // Тёмная тема для окна (Windows 10/11)
        BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        g_bgBrush = CreateSolidBrush(g_bgColor);
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        HFONT hBig = CreateFontW(24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        HWND h;

        h = CreateWindowW(L"STATIC", L"StegoShield AI", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 15, 760, 40, hwnd, nullptr, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hBig, TRUE);

        g_path = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER, 20, 80, 640, 30, hwnd, (HMENU)ID_PATH, hi, nullptr);
        SendMessageW(g_path, WM_SETFONT, (WPARAM)hFont, TRUE);
        h = CreateWindowW(L"BUTTON", L"Обзор", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 670, 80, 110, 30, hwnd, (HMENU)ID_BROWSE, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_text = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER, 20, 130, 760, 220, hwnd, (HMENU)ID_TEXT, hi, nullptr);
        SendMessageW(g_text, WM_SETFONT, (WPARAM)hFont, TRUE);

        int btnY = 370, btnW = 180, btnH = 45;
        h = CreateWindowW(L"BUTTON", L"ЗАШИФРОВАТЬ", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, btnY, btnW, btnH, hwnd, (HMENU)ID_HIDE, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        h = CreateWindowW(L"BUTTON", L"РАСШИФРОВАТЬ", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 210, btnY, btnW, btnH, hwnd, (HMENU)ID_EXTRACT, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        h = CreateWindowW(L"BUTTON", L"АНАЛИЗ (1 файл)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 400, btnY, btnW, btnH, hwnd, (HMENU)ID_ANALYZE, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        h = CreateWindowW(L"BUTTON", L"СОРТИРОВКА", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 590, btnY, btnW, btnH, hwnd, (HMENU)ID_SCAN, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_status = CreateWindowW(L"STATIC", L"Готов к работе", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 440, 760, 20, hwnd, (HMENU)ID_STATUS, hi, nullptr);
        SendMessageW(g_status, WM_SETFONT, (WPARAM)hFont, TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, RGB(230, 230, 230));
        SetBkColor(hdc, g_bgColor);
        return (LRESULT)g_bgBrush;
    }
    case WM_ERASEBKGND: {
        RECT rect; GetClientRect(hwnd, &rect);
        FillRect((HDC)w, &rect, g_bgBrush);
        return 1;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_BROWSE:  onBrowse(hwnd);  return 0;
        case ID_HIDE:    onHide(hwnd);    return 0;
        case ID_EXTRACT: onExtract(hwnd); return 0;
        case ID_ANALYZE: onAnalyze(hwnd); return 0;
        case ID_SCAN:    onScanFolder(hwnd); return 0;
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_ai.loadWeights("model_weights.txt");

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"StegoClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = nullptr; // Убрали стандартный фон
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"StegoClass", L"StegoShield AI", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 510, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow); UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}