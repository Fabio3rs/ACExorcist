#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <propidl.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace Gdiplus;

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kClassName[] = L"ACExorcistWnd";
constexpr int kMargin        = 10;
constexpr int kControlH      = 24;
constexpr int kLabelW        = 80;
constexpr int kListH         = 160;
constexpr int kColGap        = 12;
constexpr int kRowGap        = 10;
constexpr int kPanelTop      = 54;

// Dark theme palette  (plain const avoids constexpr + C-cast issues with RGB macro)
const COLORREF kClrBg        = RGB(18,  20,  26);
const COLORREF kClrHeader    = RGB(170,  0,   0);
const COLORREF kClrCtrlBg    = RGB(28,  31,  42);
const COLORREF kClrCtrlBg2   = RGB(22,  25,  35);
const COLORREF kClrCtrlText  = RGB(215, 215, 220);
const COLORREF kClrLabelText = RGB(150, 153, 165);
const COLORREF kClrAccent    = RGB(200,  40,  40);

enum : int {
    IDC_MODE = 100,
    IDC_CAR_FILTER,
    IDC_CAR_LIST,
    IDC_SKIN_FILTER,
    IDC_SKIN_LIST,
    IDC_TRACK_FILTER,
    IDC_TRACK_LIST,
    IDC_LAYOUT_LIST,      // seletor de layout da pista
    IDC_WEATHER_FILTER,
    IDC_WEATHER_LIST,
    IDC_PRESET_NAME,
    IDC_PRESET_LIST,
    IDC_SAVE_PRESET,
    IDC_DELETE_PRESET,
    IDC_LAUNCH_RACE,
    IDC_LAUNCH_SHOWROOM,
    IDC_LAUNCH_ORIGINAL,
    IDC_REFRESH_RESULTS,
    IDC_RESULTS,
    IDC_STATUS
};

struct SkinInfo {
    std::wstring id;
    std::wstring display;
};

struct CatalogEntry {
    std::wstring id;
    std::wstring display;
    std::wstring path;
    std::wstring country;
    std::vector<SkinInfo> skins;
};

struct AppState {
    HINSTANCE hinst{};
    HWND hwnd{};
    HWND mode{};
    HWND car_filter{};
    HWND car_list{};
    HWND skin_filter{};
    HWND skin_list{};
    HWND track_filter{};
    HWND track_list{};
    HWND weather_filter{};
    HWND weather_list{};
    HWND preset_name{};
    HWND preset_list{};
    HWND status{};
    HWND save_preset{};
    HWND delete_preset{};
    HWND launch_race{};
    HWND launch_showroom{};
    HWND launch_original{};
    HWND refresh_results{};
    // Labels: stored so layout() can reposition them on resize
    HWND lbl_cars{}, lbl_skins{}, lbl_tracks{}, lbl_weather{}, lbl_preset{};
    HWND layout_list{};   // combobox de layouts da pista
    HWND lbl_layout{};    // label "Layout"
    ULONG_PTR gdiplus_token{};
    HBITMAP preview_bitmap{};
    HBITMAP track_bitmap{};
    HBITMAP track_outline_bitmap{};   // outline/map sobreposto ao track preview
    HBITMAP flag_bitmap{};
    HFONT   app_font{};
    HFONT   title_font{};
    HBRUSH  br_bg{};        // main window background
    HBRUSH  br_ctrl{};      // listbox / edit bg
    HBRUSH  br_ctrl2{};     // alternate row / status bg
    // Dynamic preview rects (set by layout)
    RECT preview_draw_rect{};
    RECT track_draw_rect{};
    RECT flag_draw_rect{};
    RECT curve_draw_rect{};   // torque/power curve panel (replaces ListView)
    // Bitmaps de background do painel de curva (bandeira do carro + badge)
    HBITMAP car_curve_flag_bitmap{};  // bandeira do país do carro
    HBITMAP car_badge_bitmap{};       // badge/logo do carro (ui/badge.png)
    // Torque and power curves loaded from ui_car.json
    std::vector<std::pair<float,float>> curve_torque;
    std::vector<std::pair<float,float>> curve_power;
    std::wstring curve_specs_line;  // specs do carro quando sem dados de curva
    std::vector<CatalogEntry> cars;
    std::vector<CatalogEntry> tracks;
    std::vector<CatalogEntry> weather;
    std::vector<std::wstring> filtered_cars;
    std::vector<std::wstring> filtered_skins;
    std::vector<std::wstring> filtered_tracks;
    std::vector<std::wstring> filtered_weather;
    std::vector<std::wstring> preset_names;
    std::wstring selected_car;
    std::wstring selected_skin;
    std::wstring selected_track;
    std::wstring selected_layout;  // sublayout da pista (vazio = pista simples)
    std::vector<std::wstring> track_layouts; // subpastas de layout disponíveis
    std::wstring selected_weather;
    std::wstring base_dir;
    std::wstring settings_path;
};

AppState g;

CatalogEntry* find_car(const std::wstring& id);
CatalogEntry* find_track(const std::wstring& id);
CatalogEntry* find_weather(const std::wstring& id);
void load_car_curves();
void update_status_panel();

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), size);
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring trim(std::wstring s) {
    auto not_space = [](wchar_t c) { return !iswspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::wstring upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), towupper);
    return s;
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

std::wstring get_module_dir() {
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (!len || len >= MAX_PATH) return {};
    std::wstring path(buffer, buffer + len);
    auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring{} : path.substr(0, pos);
}

std::string read_file_utf8(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_file_utf8(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << content;
    return true;
}

bool file_exists(const fs::path& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

// Os arquivos JSON do Assetto Corsa contêm caracteres de controle literais
// (ASCII < 0x20) em strings de descrição, o que é JSON inválido.
// Esta função sanitiza antes de parsear para que todos os carros sejam lidos.
json parse_json_file(const fs::path& path) {
    std::string raw = read_file_utf8(path);
    // Os arquivos do AC contêm caracteres de controle literais (incluindo \n e \r)
    // dentro de strings JSON — inválido por spec. Sanitizamos rastreando se estamos
    // dentro de uma string para substituir controle chars apenas onde são ilegais.
    bool in_string = false;
    bool escaped   = false;
    for (char& c : raw) {
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && in_string) { escaped = true; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        if (in_string) {
            unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20) c = ' ';   // substitui QUALQUER controle char dentro de string
        }
    }
    return json::parse(raw);
}

std::wstring ini_get(const fs::path& path, const std::wstring& section, const std::wstring& key, const std::wstring& fallback = L"") {
    wchar_t buf[2048];
    GetPrivateProfileStringW(section.c_str(), key.c_str(), fallback.c_str(), buf, 2048, path.c_str());
    return buf;
}

void ini_set(const fs::path& path, const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), path.c_str());
}

void log_line(const std::wstring& msg) {
    std::ofstream log(fs::path(g.base_dir) / L"ACExorcist.log", std::ios::app);
    log << narrow(msg) << "\n";
}

void set_status(const std::wstring& text) {
    if (g.status) SetWindowTextW(g.status, text.c_str());
    log_line(text);
}

std::wstring normalize_key(std::wstring s) {
    return upper(trim(std::move(s)));
}

std::wstring display_or_id(const std::wstring& id, const std::wstring& display) {
    return display.empty() ? id : display;
}

std::wstring get_window_text(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring out(static_cast<size_t>(len), L'\0');
    GetWindowTextW(h, out.data(), len + 1);
    return out;
}

std::wstring to_json_w(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return {};
    return widen(j[key].get<std::string>());
}

std::wstring to_json_num_w(const json& j, const char* key) {
    if (!j.contains(key)) return {};
    if (j[key].is_string()) return widen(j[key].get<std::string>());
    if (j[key].is_number_integer()) return std::to_wstring(j[key].get<long long>());
    if (j[key].is_number_unsigned()) return std::to_wstring(j[key].get<unsigned long long>());
    if (j[key].is_number_float()) return std::to_wstring(j[key].get<double>());
    return {};
}

std::wstring country_to_flag_code(const std::wstring& country) {
    std::wstring c = lower(trim(country));
    if (c == L"italy") return L"ITA";
    if (c == L"germany") return L"DEU";
    if (c == L"france") return L"FRA";
    if (c == L"united kingdom" || c == L"uk" || c == L"england" || c == L"great britain") return L"GBR";
    if (c == L"spain") return L"ESP";
    if (c == L"portugal") return L"PRT";
    if (c == L"netherlands" || c == L"holland") return L"NLD";
    if (c == L"belgium") return L"BEL";
    if (c == L"usa" || c == L"united states" || c == L"united states of america") return L"USA";
    if (c == L"japan") return L"JPN";
    if (c == L"australia") return L"AUS";
    if (c == L"finland") return L"FIN";
    if (c == L"switzerland") return L"CHE";
    if (c == L"austria") return L"AUT";
    if (c == L"sweden") return L"SWE";
    if (c == L"canada") return L"CAN";
    if (c == L"brazil") return L"BRA";
    if (c == L"argentina") return L"ARG";
    if (c == L"poland") return L"POL";
    if (c == L"hungary") return L"HUN";
    if (c == L"romania") return L"ROU";
    if (c == L"turkey") return L"TUR";
    if (c == L"china") return L"CHN";
    if (c == L"korea" || c == L"south korea") return L"KOR";
    return L"";
}

std::wstring infer_car_country_from_tags(const json& j) {
    if (!j.contains("tags") || !j["tags"].is_array()) return {};
    for (const auto& item : j["tags"]) {
        if (!item.is_string()) continue;
        std::wstring code = country_to_flag_code(widen(item.get<std::string>()));
        if (!code.empty()) return code;
    }
    return {};
}

HBITMAP load_bitmap_from_file(const fs::path& path) {
    if (!fs::exists(path)) return nullptr;
    Bitmap bmp(path.c_str());
    if (bmp.GetLastStatus() != Ok) return nullptr;
    HBITMAP hbmp = nullptr;
    if (bmp.GetHBITMAP(Color(0, 0, 0), &hbmp) != Ok) return nullptr;
    return hbmp;
}

void set_preview_bitmap(HBITMAP bmp) {
    if (g.preview_bitmap) DeleteObject(g.preview_bitmap);
    g.preview_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.preview_draw_rect, FALSE);
}

void set_track_bitmap(HBITMAP bmp) {
    if (g.track_bitmap) DeleteObject(g.track_bitmap);
    g.track_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.track_draw_rect, FALSE);
}

void set_track_outline_bitmap(HBITMAP bmp) {
    if (g.track_outline_bitmap) DeleteObject(g.track_outline_bitmap);
    g.track_outline_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.track_draw_rect, FALSE);
}

void set_flag_bitmap(HBITMAP bmp) {
    if (g.flag_bitmap) DeleteObject(g.flag_bitmap);
    g.flag_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.flag_draw_rect, FALSE);
}

void set_car_curve_flag_bitmap(HBITMAP bmp) {
    if (g.car_curve_flag_bitmap) DeleteObject(g.car_curve_flag_bitmap);
    g.car_curve_flag_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.curve_draw_rect, FALSE);
}

void set_car_badge_bitmap(HBITMAP bmp) {
    if (g.car_badge_bitmap) DeleteObject(g.car_badge_bitmap);
    g.car_badge_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.curve_draw_rect, FALSE);
}

void draw_bitmap_scaled(HDC hdc, HBITMAP bmp, const RECT& rc) {
    if (!bmp) return;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, bmp));
    BITMAP bm{};
    GetObject(bmp, sizeof(bm), &bm);
    StretchBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void paint_frame(HDC hdc, const RECT& client) {
    // Background
    HBRUSH bg = CreateSolidBrush(kClrBg);
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    // Header band
    RECT header{0, 0, client.right, 48};
    HBRUSH bar = CreateSolidBrush(kClrHeader);
    FillRect(hdc, &header, bar);
    DeleteObject(bar);

    // Thin bottom edge on header
    RECT edge{0, 47, client.right, 48};
    HBRUSH accent = CreateSolidBrush(RGB(230, 60, 60));
    FillRect(hdc, &edge, accent);
    DeleteObject(accent);

    // Header text – esquerda: título
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldfont = reinterpret_cast<HFONT>(SelectObject(hdc, g.title_font ? g.title_font : GetStockObject(DEFAULT_GUI_FONT)));
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT title_rc{14, 0, client.right - 230, 48};
    DrawTextW(hdc, L"ACExorcist", -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, g.app_font ? g.app_font : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(hdc, RGB(220, 200, 200));
    RECT sub_rc{130, 0, client.right - 230, 48};
    DrawTextW(hdc, L"Assetto Corsa Launcher", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // "Mode:" label à esquerda do combobox
    constexpr int kModeW = 160;
    SetTextColor(hdc, RGB(200, 200, 210));
    RECT mode_lbl_rc{client.right - kMargin - kModeW - 52, 0, client.right - kMargin - kModeW - 4, 48};
    DrawTextW(hdc, L"Mode:", -1, &mode_lbl_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldfont);

    // Draw preview panels if bitmaps are loaded (positioned by layout)
    auto draw_panel = [&](HBITMAP bmp, const RECT& rc, const wchar_t* label) {
        if (rc.right <= rc.left) return;
        HBRUSH panel_bg = CreateSolidBrush(kClrCtrlBg2);
        FillRect(hdc, &rc, panel_bg);
        DeleteObject(panel_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 58, 75));
        HPEN oldpen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH oldbr = reinterpret_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldpen);
        SelectObject(hdc, oldbr);
        DeleteObject(pen);
        if (bmp) {
            draw_bitmap_scaled(hdc, bmp, RECT{rc.left+1, rc.top+1, rc.right-1, rc.bottom-1});
        } else {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(70, 73, 90));
            HFONT f = reinterpret_cast<HFONT>(SelectObject(hdc, g.app_font ? g.app_font : GetStockObject(DEFAULT_GUI_FONT)));
            RECT lr{rc.left+6, rc.top+4, rc.right-4, rc.bottom-4};
            DrawTextW(hdc, label, -1, &lr, DT_LEFT | DT_TOP);
            SelectObject(hdc, f);
        }
    };

    draw_panel(g.preview_bitmap, g.preview_draw_rect, L"Car preview");
    draw_panel(g.track_bitmap,   g.track_draw_rect,   L"Track preview");

    // Overlay do contorno da pista sobre o track preview
    if (g.track_outline_bitmap && g.track_draw_rect.right > g.track_draw_rect.left) {
        const RECT& tr = g.track_draw_rect;
        // Usa GDI+ para desenhar o outline com fundo preto transparente e 70% de opacidade
        Graphics gfx(hdc);
        gfx.SetInterpolationMode(InterpolationModeHighQualityBilinear);
        Bitmap outlineBmp(g.track_outline_bitmap, nullptr);
        // Color key: torna pixels escuros (fundo preto) transparentes
        ImageAttributes ia;
        ia.SetColorKey(Color(0, 0, 0), Color(80, 80, 80));
        // Matriz de cor: aplica 65% de opacidade global
        ColorMatrix cm = {{
            {1, 0, 0, 0,     0},
            {0, 1, 0, 0,     0},
            {0, 0, 1, 0,     0},
            {0, 0, 0, 0.65f, 0},
            {0, 0, 0, 0,     1}
        }};
        ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        int iw = static_cast<int>(outlineBmp.GetWidth());
        int ih = static_cast<int>(outlineBmp.GetHeight());
        gfx.DrawImage(&outlineBmp,
            Rect(tr.left + 1, tr.top + 1, tr.right - tr.left - 2, tr.bottom - tr.top - 2),
            0, 0, iw, ih, UnitPixel, &ia);
    }
    // Bandeira: se painel existe mas bitmap nulo e pista está selecionada, mostra aviso
    if (g.flag_draw_rect.right > g.flag_draw_rect.left) {
        if (g.flag_bitmap) {
            draw_panel(g.flag_bitmap, g.flag_draw_rect, L"Flag");
        } else {
            // Fundo + borda igual ao draw_panel
            HBRUSH panel_bg = CreateSolidBrush(kClrCtrlBg2);
            FillRect(hdc, &g.flag_draw_rect, panel_bg);
            DeleteObject(panel_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 58, 75));
            HPEN oldpen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
            HBRUSH oldbr = reinterpret_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
            Rectangle(hdc, g.flag_draw_rect.left, g.flag_draw_rect.top, g.flag_draw_rect.right, g.flag_draw_rect.bottom);
            SelectObject(hdc, oldpen); SelectObject(hdc, oldbr);
            DeleteObject(pen);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(70, 73, 90));
            HFONT ff = reinterpret_cast<HFONT>(SelectObject(hdc, g.app_font ? g.app_font : GetStockObject(DEFAULT_GUI_FONT)));
            RECT flr{g.flag_draw_rect.left+6, g.flag_draw_rect.top+4, g.flag_draw_rect.right-4, g.flag_draw_rect.bottom-4};
            const wchar_t* flag_msg = g.selected_track.empty() ? L"Flag" : L"Bandeira não disponível";
            DrawTextW(hdc, flag_msg, -1, &flr, DT_LEFT | DT_TOP | DT_WORDBREAK);
            SelectObject(hdc, ff);
        }
    }

    // ── Torque / Power curve panel ──────────────────────────────────────
    const RECT& cr = g.curve_draw_rect;
    if (cr.right > cr.left) {
        // Background + border
        HBRUSH cbg = CreateSolidBrush(kClrCtrlBg2);
        FillRect(hdc, &cr, cbg);
        DeleteObject(cbg);
        HPEN border = CreatePen(PS_SOLID, 1, RGB(55, 58, 75));
        HPEN oldp = reinterpret_cast<HPEN>(SelectObject(hdc, border));
        HBRUSH oldb = reinterpret_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);
        SelectObject(hdc, oldp); SelectObject(hdc, oldb);
        DeleteObject(border);

        // ── Bandeira do carro + badge como background (GDI+) ─────────────
        {
            // Helper: desenha HBITMAP com opacidade global + color key opcional
            auto draw_bmp_alpha = [&](HBITMAP hbmp, Rect dest,
                                      float alpha,
                                      bool use_color_key = false,
                                      Color ck_lo = Color(0,0,0),
                                      Color ck_hi = Color(0,0,0)) {
                if (!hbmp) return;
                Graphics gfx(hdc);
                gfx.SetInterpolationMode(InterpolationModeHighQualityBilinear);
                Bitmap bmp(hbmp, nullptr);
                ImageAttributes ia;
                if (use_color_key)
                    ia.SetColorKey(ck_lo, ck_hi);
                ColorMatrix cm = {{
                    {1, 0, 0, 0,     0},
                    {0, 1, 0, 0,     0},
                    {0, 0, 1, 0,     0},
                    {0, 0, 0, alpha, 0},
                    {0, 0, 0, 0,     1}
                }};
                ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
                int sw = static_cast<int>(bmp.GetWidth());
                int sh = static_cast<int>(bmp.GetHeight());
                gfx.DrawImage(&bmp, dest, 0, 0, sw, sh, UnitPixel, &ia);
            };

            int pw = cr.right  - cr.left;
            int ph = cr.bottom - cr.top;

            // Bandeira: centralizada, proporção correta, ~75% da altura do painel
            if (g.car_curve_flag_bitmap) {
                BITMAP bm{}; GetObject(g.car_curve_flag_bitmap, sizeof(bm), &bm);
                float aspect = (bm.bmHeight > 0) ? static_cast<float>(bm.bmWidth) / bm.bmHeight : 1.5f;
                int dh = static_cast<int>(ph * 0.75f);
                int dw = static_cast<int>(dh * aspect);
                if (dw > pw - 4) { dw = pw - 4; dh = static_cast<int>(dw / aspect); }
                int dx = cr.left + (pw - dw) / 2;
                int dy = cr.top  + (ph - dh) / 2;
                draw_bmp_alpha(g.car_curve_flag_bitmap, Rect(dx, dy, dw, dh), 0.10f);
            }

            // Badge: canto inferior direito, 60×60 no máximo, mantendo proporção
            if (g.car_badge_bitmap) {
                constexpr int kBadgeMax = 60;
                BITMAP bm{}; GetObject(g.car_badge_bitmap, sizeof(bm), &bm);
                float aspect = (bm.bmHeight > 0) ? static_cast<float>(bm.bmWidth) / bm.bmHeight : 1.0f;
                int dw, dh;
                if (aspect >= 1.0f) { dw = kBadgeMax; dh = static_cast<int>(kBadgeMax / aspect); }
                else                { dh = kBadgeMax; dw = static_cast<int>(kBadgeMax * aspect); }
                int dx = cr.right  - dw - 6;
                int dy = cr.bottom - dh - 6;
                // badge tem fundo branco geralmente — torna branco/quase-branco transparente
                draw_bmp_alpha(g.car_badge_bitmap, Rect(dx, dy, dw, dh), 0.30f,
                               true, Color(190, 190, 190), Color(255, 255, 255));
            }
        }

        if (g.curve_torque.empty() && g.curve_power.empty()) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(70, 73, 90));
            HFONT f2 = reinterpret_cast<HFONT>(SelectObject(hdc, g.app_font ? g.app_font : GetStockObject(DEFAULT_GUI_FONT)));
            RECT lr{cr.left+8, cr.top+6, cr.right-4, cr.bottom-4};
            if (g.selected_car.empty()) {
                DrawTextW(hdc, L"Selecione um carro para ver as curvas", -1, &lr, DT_LEFT | DT_TOP | DT_WORDBREAK);
            } else if (!g.curve_specs_line.empty()) {
                // Exibe specs do carro + aviso de ausência de curvas
                DrawTextW(hdc, g.curve_specs_line.c_str(), -1, &lr, DT_LEFT | DT_TOP | DT_WORDBREAK);
                RECT lr2{cr.left+8, cr.top+22, cr.right-4, cr.bottom-4};
                SetTextColor(hdc, RGB(55, 58, 72));
                DrawTextW(hdc, L"Dados de curva não disponíveis para este carro", -1, &lr2, DT_LEFT | DT_TOP | DT_WORDBREAK);
            } else {
                DrawTextW(hdc, L"Dados de curva não disponíveis", -1, &lr, DT_LEFT | DT_TOP | DT_WORDBREAK);
            }
            SelectObject(hdc, f2);
        } else {
            constexpr int kPad = 44; // left margin for y-axis labels
            constexpr int kBotPad = 18; // bottom margin for x-axis labels
            int gx = cr.left  + kPad;
            int gy = cr.top   + 8;
            int gw = cr.right  - cr.left - kPad - 6;
            int gh = cr.bottom - cr.top  - gy + cr.top - kBotPad;

            // Compute raw ranges
            float max_rpm = 0, max_torque = 0, max_power = 0;
            for (auto& [r, v] : g.curve_torque) { max_rpm = std::max(max_rpm, r); max_torque = std::max(max_torque, v); }
            for (auto& [r, v] : g.curve_power)  { max_rpm = std::max(max_rpm, r); max_power  = std::max(max_power,  v); }
            float max_y = std::max(max_torque, max_power);

            if (max_rpm > 0 && max_y > 0) {
                // ── "Nice" round step helper ──────────────────────────────
                auto nice_step = [](float maxv, int n) -> float {
                    float raw = maxv / n;
                    // magnitude = largest power of 10 ≤ raw
                    float mag = 1.f;
                    if (raw >= 1.f) { while (mag * 10.f <= raw) mag *= 10.f; }
                    else            { while (mag > raw) mag /= 10.f; }
                    float norm = raw / mag;
                    float nice = (norm < 1.5f) ? 1.f : (norm < 3.5f) ? 2.5f : (norm < 7.5f) ? 5.f : 10.f;
                    return nice * mag;
                };

                // Align axis maxima to grid
                float y_step       = nice_step(max_y,   4);
                float x_step       = nice_step(max_rpm, 5);
                float y_axis_max   = std::ceil(max_y   / y_step)   * y_step;
                float x_axis_max   = std::ceil(max_rpm / x_step)   * x_step;

                // Pixel mappers using grid-aligned maxima
                auto px_x = [&](float rpm) -> int { return gx + static_cast<int>(rpm / x_axis_max * gw); };
                auto px_y = [&](float val) -> int { return gy + gh - static_cast<int>(val / y_axis_max * gh); };
                auto to_px = [&](float rpm, float val) -> POINT { return {px_x(rpm), px_y(val)}; };

                SetBkMode(hdc, TRANSPARENT);
                HFONT f2 = reinterpret_cast<HFONT>(SelectObject(hdc, g.app_font ? g.app_font : GetStockObject(DEFAULT_GUI_FONT)));
                wchar_t nb[32];

                // ── Grid ─────────────────────────────────────────────────
                // Horizontal lines (Y axis) with labels
                HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(42, 47, 68));
                SelectObject(hdc, grid_pen);
                for (float yv = y_step; yv <= y_axis_max + 0.01f; yv += y_step) {
                    int yy = px_y(yv);
                    if (yy < gy || yy > gy + gh) continue;
                    MoveToEx(hdc, gx, yy, nullptr); LineTo(hdc, gx + gw, yy);
                    swprintf(nb, 32, L"%.0f", yv);
                    SetTextColor(hdc, RGB(68, 74, 100));
                    RECT rl{cr.left + 2, yy - 7, gx - 3, yy + 7};
                    DrawTextW(hdc, nb, -1, &rl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
                // Vertical lines (X axis / RPM) with labels
                for (float xv = x_step; xv <= x_axis_max + 0.01f; xv += x_step) {
                    int xx = px_x(xv);
                    if (xx < gx || xx > gx + gw) continue;
                    MoveToEx(hdc, xx, gy, nullptr); LineTo(hdc, xx, gy + gh);
                    swprintf(nb, 32, L"%.0f", xv);
                    SetTextColor(hdc, RGB(68, 74, 100));
                    RECT rl{xx - 26, gy + gh + 2, xx + 26, cr.bottom - 1};
                    DrawTextW(hdc, nb, -1, &rl, DT_CENTER | DT_TOP);
                }
                // Left axis border line
                MoveToEx(hdc, gx, gy, nullptr); LineTo(hdc, gx, gy + gh + 1);
                // Bottom axis border line
                MoveToEx(hdc, gx, gy + gh, nullptr); LineTo(hdc, gx + gw, gy + gh);
                DeleteObject(grid_pen);

                // ── "rpm" unit label at bottom-right ─────────────────────
                SetTextColor(hdc, RGB(55, 60, 82));
                RECT ru{gx + gw - 26, gy + gh + 2, cr.right - 4, cr.bottom - 1};
                DrawTextW(hdc, L"rpm", -1, &ru, DT_LEFT | DT_TOP);

                // ── Torque curve — red ────────────────────────────────────
                if (g.curve_torque.size() >= 2) {
                    std::vector<POINT> pts;
                    pts.reserve(g.curve_torque.size());
                    for (auto& [r, v] : g.curve_torque) pts.push_back(to_px(r, v));
                    HPEN tp = CreatePen(PS_SOLID, 2, RGB(210, 55, 55));
                    SelectObject(hdc, tp);
                    Polyline(hdc, pts.data(), static_cast<int>(pts.size()));
                    DeleteObject(tp);
                }
                // ── Power curve — orange ──────────────────────────────────
                if (g.curve_power.size() >= 2) {
                    std::vector<POINT> pts;
                    pts.reserve(g.curve_power.size());
                    for (auto& [r, v] : g.curve_power) pts.push_back(to_px(r, v));
                    HPEN pp = CreatePen(PS_SOLID, 2, RGB(215, 135, 25));
                    SelectObject(hdc, pp);
                    Polyline(hdc, pts.data(), static_cast<int>(pts.size()));
                    DeleteObject(pp);
                }

                // ── Y-axis unit labels (colored, at top-left) ─────────────
                if (max_torque > 0) {
                    SetTextColor(hdc, RGB(200, 70, 70));
                    RECT r2{cr.left + 1, gy, gx - 2, gy + 13};
                    DrawTextW(hdc, L"Nm", -1, &r2, DT_RIGHT | DT_TOP);
                }
                if (max_power > 0) {
                    SetTextColor(hdc, RGB(200, 135, 40));
                    RECT r2{cr.left + 1, gy + 13, gx - 2, gy + 26};
                    DrawTextW(hdc, L"hp", -1, &r2, DT_RIGHT | DT_TOP);
                }

                // ── Legend (top-left inside chart area) ───────────────────
                int lx = gx + 8, ly = gy + 4;
                HPEN lp = CreatePen(PS_SOLID, 3, RGB(210, 55, 55));
                SelectObject(hdc, lp);
                MoveToEx(hdc, lx, ly + 7, nullptr); LineTo(hdc, lx + 16, ly + 7);
                DeleteObject(lp);
                SetTextColor(hdc, RGB(220, 80, 80));
                RECT lr{lx + 19, ly, lx + 80, ly + 15};
                DrawTextW(hdc, L"Torque", -1, &lr, DT_LEFT | DT_TOP);

                lp = CreatePen(PS_SOLID, 3, RGB(215, 135, 25));
                SelectObject(hdc, lp);
                MoveToEx(hdc, lx + 80, ly + 7, nullptr); LineTo(hdc, lx + 96, ly + 7);
                DeleteObject(lp);
                SetTextColor(hdc, RGB(215, 150, 50));
                lr = {lx + 99, ly, lx + 160, ly + 15};
                DrawTextW(hdc, L"Power", -1, &lr, DT_LEFT | DT_TOP);

                SelectObject(hdc, f2);
            }
        }
    }
}

void add_catalog_entry(std::vector<CatalogEntry>& dest, const fs::path& dir, bool track_mode) {
    CatalogEntry e;
    e.id = dir.filename().wstring();
    e.display = e.id;
    e.path = dir.wstring();

    fs::path ui_json = dir / "ui" / (track_mode ? "ui_track.json" : "ui_car.json");
    if (file_exists(ui_json)) {
        try {
            json j = parse_json_file(ui_json);
            if (j.contains("name")) e.display = widen(j["name"].get<std::string>());
            if (track_mode && j.contains("country") && j["country"].is_string()) {
                e.country = country_to_flag_code(widen(j["country"].get<std::string>()));
            } else if (!track_mode) {
                e.country = infer_car_country_from_tags(j);
            }
        } catch (...) {
        }
    }

    if (!track_mode) {
        fs::path skins = dir / "skins";
        if (fs::exists(skins) && fs::is_directory(skins)) {
            for (const auto& entry : fs::directory_iterator(skins)) {
                if (!entry.is_directory()) continue;
                SkinInfo skin;
                skin.id = entry.path().filename().wstring();
                skin.display = skin.id;
                e.skins.push_back(std::move(skin));
            }
        }
        if (e.skins.empty()) {
            e.skins.push_back({L"default", L"default"});
        }
    }

    dest.push_back(std::move(e));
}

void load_catalog() {
    g.cars.clear();
    g.tracks.clear();
    g.weather.clear();

    fs::path content = fs::path(g.base_dir) / L"content";
    fs::path cars = content / "cars";
    fs::path tracks = content / "tracks";
    fs::path weather = content / "weather";

    if (fs::exists(cars)) {
        for (const auto& entry : fs::directory_iterator(cars)) {
            if (entry.is_directory()) add_catalog_entry(g.cars, entry.path(), false);
        }
    }
    if (fs::exists(tracks)) {
        for (const auto& entry : fs::directory_iterator(tracks)) {
            if (entry.is_directory()) add_catalog_entry(g.tracks, entry.path(), true);
        }
    }
    if (fs::exists(weather)) {
        for (const auto& entry : fs::directory_iterator(weather)) {
            if (!entry.is_directory()) continue;
            CatalogEntry e;
            e.id = entry.path().filename().wstring();
            e.display = e.id;
            e.path = entry.path().wstring();
            g.weather.push_back(std::move(e));
        }
    }
}

void refresh_preset_names() {
    g.preset_names.clear();
    wchar_t buf[16384];
    GetPrivateProfileSectionNamesW(buf, 16384, g.settings_path.c_str());
    const wchar_t* p = buf;
    while (*p) {
        std::wstring section = p;
        if (section.rfind(L"PRESET_", 0) == 0) {
            g.preset_names.push_back(section.substr(7));
        }
        p += section.size() + 1;
    }
}

void fill_listbox(HWND h, const std::vector<std::wstring>& items) {
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SendMessageW(h, LB_RESETCONTENT, 0, 0);
    for (const auto& item : items)
        SendMessageW(h, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, nullptr, TRUE);
}

// Seleciona o item da listbox cujo id (parte após " | ") bate com `selected_id`
void restore_listbox_selection(HWND h, const std::vector<std::wstring>& items, const std::wstring& selected_id) {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        auto pos = items[static_cast<size_t>(i)].rfind(L" | ");
        std::wstring id = (pos == std::wstring::npos) ? items[static_cast<size_t>(i)] : items[static_cast<size_t>(i)].substr(pos + 3);
        if (id == selected_id) {
            SendMessageW(h, LB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
}

int listbox_selected(HWND h) {
    return static_cast<int>(SendMessageW(h, LB_GETCURSEL, 0, 0));
}

std::wstring listbox_item(HWND h, int idx) {
    wchar_t buf[512];
    SendMessageW(h, LB_GETTEXT, idx, reinterpret_cast<LPARAM>(buf));
    return buf;
}

// Remove tags HTML simples (ex: <br/>) e converte para texto plano
std::wstring strip_html(std::wstring s) {
    for (const auto* br : {L"<br/>", L"<br />", L"<BR/>", L"<BR />"}) {
        std::wstring tag = br;
        size_t pos = 0;
        while ((pos = s.find(tag, pos)) != std::wstring::npos)
            s.replace(pos, tag.size(), L"\r\n");
    }
    std::wstring out; out.reserve(s.size());
    bool in_tag = false;
    for (wchar_t c : s) {
        if (c == L'<')      { in_tag = true;  continue; }
        if (c == L'>')      { in_tag = false; continue; }
        if (!in_tag) out += c;
    }
    return trim(out);
}

// Descobre layouts disponíveis e popula o combobox IDC_LAYOUT_LIST
void populate_layouts() {
    g.track_layouts.clear();
    SendMessageW(g.layout_list, CB_RESETCONTENT, 0, 0);
    if (g.selected_track.empty()) {
        ShowWindow(g.layout_list, SW_HIDE);
        ShowWindow(g.lbl_layout,  SW_HIDE);
        return;
    }
    fs::path ui = fs::path(g.base_dir) / L"content" / L"tracks" / g.selected_track / L"ui";
    if (fs::exists(ui) && fs::is_directory(ui)) {
        for (const auto& e : fs::directory_iterator(ui))
            if (e.is_directory())
                g.track_layouts.push_back(e.path().filename().wstring());
        std::sort(g.track_layouts.begin(), g.track_layouts.end());
    }
    bool multi = !g.track_layouts.empty();
    ShowWindow(g.layout_list, multi ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lbl_layout,  multi ? SW_SHOW : SW_HIDE);
    if (!multi) { g.selected_layout.clear(); return; }

    for (const auto& l : g.track_layouts)
        SendMessageW(g.layout_list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(l.c_str()));

    int sel = 0;
    for (int i = 0; i < static_cast<int>(g.track_layouts.size()); ++i)
        if (g.track_layouts[static_cast<size_t>(i)] == g.selected_layout) { sel = i; break; }
    SendMessageW(g.layout_list, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    g.selected_layout = g.track_layouts[static_cast<size_t>(sel)];
}

void update_cars() {
    std::wstring needle = lower(get_window_text(g.car_filter));
    g.filtered_cars.clear();
    for (const auto& car : g.cars) {
        std::wstring label = display_or_id(car.id, car.display);
        if (needle.empty() || lower(label).find(needle) != std::wstring::npos || lower(car.id).find(needle) != std::wstring::npos) {
            g.filtered_cars.push_back(label + L" | " + car.id);
        }
    }
    fill_listbox(g.car_list, g.filtered_cars);
    restore_listbox_selection(g.car_list, g.filtered_cars, g.selected_car);
}

void refresh_track_preview() {
    if (g.selected_track.empty()) return;
    fs::path base = fs::path(g.base_dir) / L"content" / L"tracks" / g.selected_track;
    fs::path ui   = base / L"ui";

    auto find_img = [&](std::wstring_view name) -> fs::path {
        // 1. Layout selecionado (se houver)
        if (!g.selected_layout.empty()) {
            fs::path p = ui / g.selected_layout / name;
            if (file_exists(p)) return p;
        }
        // 2. Direto em ui/ (pistas simples)
        { fs::path p = ui / name; if (file_exists(p)) return p; }
        // 3. Qualquer sublayout
        if (fs::exists(ui) && fs::is_directory(ui)) {
            for (const auto& entry : fs::directory_iterator(ui)) {
                if (!entry.is_directory()) continue;
                fs::path p = entry.path() / name;
                if (file_exists(p)) return p;
            }
        }
        // 4. Raiz (pistas modadas antigas)
        { fs::path p = base / name; if (file_exists(p)) return p; }
        return {};
    };

    fs::path p = find_img(L"preview.png");
    // Se não há foto de preview, usa outline/map como imagem principal
    if (p.empty()) p = find_img(L"map.png");
    set_track_bitmap(p.empty() ? nullptr : load_bitmap_from_file(p));

    // Overlay do contorno da pista (branco sobre preto → sobreposição semi-transparente)
    fs::path o = find_img(L"outline.png");
    set_track_outline_bitmap(o.empty() ? nullptr : load_bitmap_from_file(o));

    auto* track = find_track(g.selected_track);
    if (track && !track->country.empty()) {
        HBITMAP fb = load_bitmap_from_file(fs::path(g.base_dir) / L"content" / L"gui" / L"NationFlags" / (track->country + L".png"));
        set_flag_bitmap(fb);  // pode ser nullptr se arquivo não existir
    } else {
        set_flag_bitmap(nullptr);  // pista sem país → limpa bandeira anterior
    }
}

void update_tracks() {
    std::wstring needle = lower(get_window_text(g.track_filter));
    g.filtered_tracks.clear();
    for (const auto& track : g.tracks) {
        std::wstring label = display_or_id(track.id, track.display);
        if (needle.empty() || lower(label).find(needle) != std::wstring::npos || lower(track.id).find(needle) != std::wstring::npos) {
            g.filtered_tracks.push_back(label + L" | " + track.id);
        }
    }
    fill_listbox(g.track_list, g.filtered_tracks);
    restore_listbox_selection(g.track_list, g.filtered_tracks, g.selected_track);
    populate_layouts();
    refresh_track_preview();
    update_status_panel();
}

void update_weather() {
    std::wstring needle = lower(get_window_text(g.weather_filter));
    g.filtered_weather.clear();
    for (const auto& w : g.weather) {
        std::wstring label = display_or_id(w.id, w.display);
        if (needle.empty() || lower(label).find(needle) != std::wstring::npos || lower(w.id).find(needle) != std::wstring::npos) {
            g.filtered_weather.push_back(label + L" | " + w.id);
        }
    }
    fill_listbox(g.weather_list, g.filtered_weather);
}

void update_skins() {
    std::wstring needle = lower(get_window_text(g.skin_filter));
    g.filtered_skins.clear();
    for (const auto& car : g.cars) {
        if (car.id != g.selected_car) continue;
        for (const auto& skin : car.skins) {
            std::wstring label = display_or_id(skin.id, skin.display);
            if (needle.empty() || lower(label).find(needle) != std::wstring::npos || lower(skin.id).find(needle) != std::wstring::npos) {
                g.filtered_skins.push_back(label + L" | " + skin.id);
            }
        }
    }
    fill_listbox(g.skin_list, g.filtered_skins);
    restore_listbox_selection(g.skin_list, g.filtered_skins, g.selected_skin);
    if (!g.selected_car.empty() && !g.selected_skin.empty()) {
        fs::path p = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"skins" / g.selected_skin / L"preview.jpg";
        set_preview_bitmap(load_bitmap_from_file(p));
    }
    auto* car = find_car(g.selected_car);
    if (car && !car->country.empty()) {
        HBITMAP fb = load_bitmap_from_file(fs::path(g.base_dir) / L"content" / L"gui" / L"NationFlags" / (car->country + L".png"));
        set_flag_bitmap(fb);  // pode ser nullptr se arquivo não existir
    } else {
        set_flag_bitmap(nullptr);  // carro sem país → limpa bandeira anterior
    }
    update_status_panel();
}

std::wstring extract_id_from_row(const std::wstring& row) {
    auto pos = row.rfind(L" | ");
    return pos == std::wstring::npos ? row : row.substr(pos + 3);
}

CatalogEntry* find_car(const std::wstring& id) {
    for (auto& c : g.cars) if (c.id == id) return &c;
    return nullptr;
}

CatalogEntry* find_track(const std::wstring& id) {
    for (auto& t : g.tracks) if (t.id == id) return &t;
    return nullptr;
}

CatalogEntry* find_weather(const std::wstring& id) {
    for (auto& w : g.weather) if (w.id == id) return &w;
    return nullptr;
}

void load_car_curves() {
    g.curve_torque.clear();
    g.curve_power.clear();
    g.curve_specs_line.clear();
    set_car_curve_flag_bitmap(nullptr);
    set_car_badge_bitmap(nullptr);
    if (!g.selected_car.empty()) {
        fs::path car_ui   = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"ui";
        fs::path car_json = car_ui / L"ui_car.json";
        if (file_exists(car_json)) {
            try {
                json j = parse_json_file(car_json);
                auto load = [&](const char* key, std::vector<std::pair<float,float>>& out) {
                    if (!j.contains(key) || !j[key].is_array()) return;
                    for (const auto& pt : j[key]) {
                        if (!pt.is_array() || pt.size() < 2) continue;
                        try {
                            float rpm = std::stof(pt[0].get<std::string>());
                            float val = std::stof(pt[1].get<std::string>());
                            out.push_back({rpm, val});
                        } catch (...) {}
                    }
                };
                load("torqueCurve", g.curve_torque);
                load("powerCurve",  g.curve_power);

                // Se sem curvas, monta linha de specs para exibir no painel
                if (g.curve_torque.empty() && g.curve_power.empty()) {
                    std::wstring line;
                    if (j.contains("name") && j["name"].is_string())
                        line = widen(j["name"].get<std::string>());
                    if (j.contains("specs") && j["specs"].is_object()) {
                        auto& s = j["specs"];
                        auto sw = [&](const char* k) -> std::wstring {
                            return (s.contains(k) && s[k].is_string()) ? widen(s[k].get<std::string>()) : L"";
                        };
                        auto app = [&](const wchar_t* lbl, const std::wstring& val) {
                            if (val.empty() || val == L"--" || val.rfind(L"--", 0) == 0) return;
                            if (!line.empty()) line += L"   ";
                            line += lbl + val;
                        };
                        app(L"", sw("bhp"));
                        app(L"", sw("torque"));
                        app(L"", sw("weight"));
                        app(L"top: ", sw("topspeed"));
                        app(L"P/W: ", sw("pwratio"));
                    }
                    g.curve_specs_line = line;
                }

                // Bandeira do país do carro (via tags)
                std::wstring flag_code = infer_car_country_from_tags(j);
                if (!flag_code.empty()) {
                    fs::path fp = fs::path(g.base_dir) / L"content" / L"gui" / L"NationFlags" / (flag_code + L".png");
                    set_car_curve_flag_bitmap(load_bitmap_from_file(fp));
                }
            } catch (...) {}
        }
        // Badge do carro (content/cars/{id}/ui/badge.png)
        fs::path badge_path = car_ui / L"badge.png";
        set_car_badge_bitmap(load_bitmap_from_file(badge_path));
    }
    if (g.hwnd) InvalidateRect(g.hwnd, &g.curve_draw_rect, FALSE);
}

void update_status_panel() {
    std::wstring text;

    // ── Specs do carro (linha compacta) ─────────────────────────────────
    if (!g.selected_car.empty()) {
        fs::path car_json = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"ui" / L"ui_car.json";
        if (file_exists(car_json)) {
            try {
                json j = parse_json_file(car_json);
                // Nome + specs numa linha
                std::wstring specs_line;
                auto app = [&](const wchar_t* lbl, const std::wstring& val) {
                    if (val.empty() || val == L"--" || val.rfind(L"--", 0) == 0) return;
                    if (!specs_line.empty()) specs_line += L"   ";
                    specs_line += lbl + val;
                };
                if (j.contains("name") && j["name"].is_string())
                    specs_line = widen(j["name"].get<std::string>());
                if (j.contains("specs") && j["specs"].is_object()) {
                    auto& s = j["specs"];
                    auto sw = [&](const char* k) -> std::wstring {
                        return (s.contains(k) && s[k].is_string()) ? widen(s[k].get<std::string>()) : L"";
                    };
                    if (!specs_line.empty()) specs_line += L"   ";
                    app(L"",      sw("bhp"));
                    app(L"",      sw("torque"));
                    app(L"",      sw("weight"));
                    app(L"top: ", sw("topspeed"));
                    app(L"P/W: ", sw("pwratio"));
                }
                if (!specs_line.empty()) text += specs_line + L"\r\n";
                // Descrição
                if (j.contains("description") && j["description"].is_string()) {
                    std::wstring desc = strip_html(widen(j["description"].get<std::string>()));
                    if (!desc.empty()) text += desc + L"\r\n";
                }
            } catch (...) {}
        }
    }

    // ── Specs + descrição da pista ───────────────────────────────────────
    if (!g.selected_track.empty()) {
        fs::path ui = fs::path(g.base_dir) / L"content" / L"tracks" / g.selected_track / L"ui";
        fs::path track_json = (!g.selected_layout.empty() && file_exists(ui / g.selected_layout / L"ui_track.json"))
            ? ui / g.selected_layout / L"ui_track.json"
            : ui / L"ui_track.json";
        if (!file_exists(track_json)) track_json = ui / L"ui_track.json";
        if (file_exists(track_json)) {
            try {
                json j = parse_json_file(track_json);
                auto jw = [&](const char* k) -> std::wstring {
                    return (j.contains(k) && j[k].is_string()) ? widen(j[k].get<std::string>()) : L"";
                };
                std::wstring track_line = jw("name");
                auto tapp = [&](const wchar_t* lbl, const std::wstring& val) {
                    if (val.empty()) return;
                    if (!track_line.empty()) track_line += L"   ";
                    track_line += lbl + val;
                };
                tapp(L"", jw("country"));
                std::wstring len = jw("length");
                if (!len.empty()) tapp(L"", len + L"m");
                tapp(L"pits: ", jw("pitboxes"));
                if (!track_line.empty()) text += track_line + L"\r\n";
                if (j.contains("description") && j["description"].is_string()) {
                    std::wstring desc = strip_html(widen(j["description"].get<std::string>()));
                    if (!desc.empty()) text += desc + L"\r\n";
                }
            } catch (...) {}
        }
    }

    // ── Resultados de corrida ────────────────────────────────────────────
    fs::path race_out = fs::path(g.base_dir) / L"race_out.json";
    fs::path laps     = fs::path(g.base_dir) / L"laps.ini";
    if (file_exists(race_out)) {
        try {
            json j = parse_json_file(race_out);
            text += L"── Race Results (race_out.json) ──\r\n";
            if (j.contains("players") && j["players"].is_array()) {
                for (const auto& p : j["players"])
                    text += to_json_w(p, "name") + L" | " + to_json_w(p, "car") + L" | " + to_json_w(p, "skin") + L"\r\n";
            }
        } catch (...) {}
    } else if (file_exists(laps)) {
        text += L"── Race Results (laps.ini) ──\r\n";
        wchar_t buf[4096];
        GetPrivateProfileSectionNamesW(buf, 4096, laps.c_str());
        const wchar_t* p = buf;
        int cnt = 0;
        while (*p && cnt < 30) {
            std::wstring section = p;
            if (section.rfind(L"LAP_", 0) == 0) {
                text += section + L": time=" + ini_get(laps, section, L"TIME", L"") + L"\r\n";
                ++cnt;
            }
            p += section.size() + 1;
        }
    }

    SetWindowTextW(g.status, text.c_str());
}

void refresh_results() { update_status_panel(); }

void invalidate_frame() {
    if (!g.hwnd) return;
    InvalidateRect(g.hwnd, &g.preview_draw_rect, FALSE);
    InvalidateRect(g.hwnd, &g.track_draw_rect, FALSE);
    InvalidateRect(g.hwnd, &g.flag_draw_rect, FALSE);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void load_last_state() {
    g.selected_car     = ini_get(g.settings_path, L"LAST", L"CAR", L"");
    g.selected_skin    = ini_get(g.settings_path, L"LAST", L"SKIN", L"");
    g.selected_track   = ini_get(g.settings_path, L"LAST", L"TRACK", L"");
    g.selected_layout  = ini_get(g.settings_path, L"LAST", L"LAYOUT", L"");
    g.selected_weather = ini_get(g.settings_path, L"LAST", L"WEATHER", L"");
    if (g.selected_car.empty() && !g.cars.empty()) g.selected_car = g.cars.front().id;
    if (g.selected_track.empty() && !g.tracks.empty()) g.selected_track = g.tracks.front().id;
    if (g.selected_weather.empty() && !g.weather.empty()) g.selected_weather = g.weather.front().id;
    auto* car = find_car(g.selected_car);
    if (car && (g.selected_skin.empty() || std::none_of(car->skins.begin(), car->skins.end(), [&](const SkinInfo& s) { return s.id == g.selected_skin; }))) {
        g.selected_skin = car->skins.empty() ? L"default" : car->skins.front().id;
    }
}

void save_last_state() {
    ini_set(g.settings_path, L"LAST", L"CAR", g.selected_car);
    ini_set(g.settings_path, L"LAST", L"SKIN", g.selected_skin);
    ini_set(g.settings_path, L"LAST", L"TRACK", g.selected_track);
    ini_set(g.settings_path, L"LAST", L"LAYOUT", g.selected_layout);
    ini_set(g.settings_path, L"LAST", L"WEATHER", g.selected_weather);
}

void load_preset(const std::wstring& name) {
    if (name.empty()) return;
    g.selected_car = ini_get(g.settings_path, L"PRESET_" + name, L"CAR", g.selected_car);
    g.selected_skin = ini_get(g.settings_path, L"PRESET_" + name, L"SKIN", g.selected_skin);
    g.selected_track = ini_get(g.settings_path, L"PRESET_" + name, L"TRACK", g.selected_track);
    g.selected_weather = ini_get(g.settings_path, L"PRESET_" + name, L"WEATHER", g.selected_weather);
    if (auto* car = find_car(g.selected_car); car && std::none_of(car->skins.begin(), car->skins.end(), [&](const SkinInfo& s) { return s.id == g.selected_skin; })) {
        g.selected_skin = car->skins.empty() ? L"default" : car->skins.front().id;
    }
    update_skins();
    set_status(L"Preset loaded: " + name);
}

void save_preset(const std::wstring& name) {
    std::wstring n = trim(name);
    if (n.empty()) return;
    std::wstring section = L"PRESET_" + n;
    ini_set(g.settings_path, section, L"CAR", g.selected_car);
    ini_set(g.settings_path, section, L"SKIN", g.selected_skin);
    ini_set(g.settings_path, section, L"TRACK", g.selected_track);
    ini_set(g.settings_path, section, L"WEATHER", g.selected_weather);
    refresh_preset_names();
    SendMessageW(g.preset_list, CB_RESETCONTENT, 0, 0);
    for (const auto& p : g.preset_names) SendMessageW(g.preset_list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.c_str()));
    set_status(L"Preset saved: " + n);
}

bool patch_race_ini() {
    fs::path path = fs::path(g.base_dir) / L"cfg" / L"race.ini";
    if (!file_exists(path)) return false;
    ini_set(path, L"RACE", L"MODEL", g.selected_car);
    ini_set(path, L"RACE", L"TRACK", g.selected_track);
    // AC usa CONFIG_TRACK para selecionar o layout em pistas multi-layout
    ini_set(path, L"RACE", L"CONFIG_TRACK", g.selected_layout);
    ini_set(path, L"RACE", L"CARS", L"1");
    ini_set(path, L"WEATHER", L"NAME", g.selected_weather);
    ini_set(path, L"CAR_0", L"MODEL", g.selected_car);
    ini_set(path, L"CAR_0", L"SKIN", g.selected_skin);
    ini_set(path, L"CAR_0", L"MODEL_CONFIG", L"");
    return true;
}

bool patch_showroom_ini() {
    fs::path path = fs::path(g.base_dir) / L"cfg" / L"showroom_start.ini";
    if (!file_exists(path)) return false;
    ini_set(path, L"SHOWROOM", L"CAR", g.selected_car);
    ini_set(path, L"SHOWROOM", L"SKIN", g.selected_skin);
    ini_set(path, L"SHOWROOM", L"TRACK", L"showroom");
    return true;
}

bool ensure_steam_appid() {
    fs::path path = fs::path(g.base_dir) / L"steam_appid.txt";
    if (file_exists(path)) return true;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "244210\n";
    return true;
}

bool run_process(const fs::path& exe, DWORD* exit_code = nullptr) {
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');
    BOOL ok = CreateProcessW(exe.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g.base_dir.c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (exit_code) *exit_code = code;
    return true;
}

bool launch_original() {
    return run_process(fs::path(g.base_dir) / L"AssettoCorsa_original.exe");
}

bool launch_race() {
    save_last_state();
    if (!patch_race_ini()) return false;
    DWORD code = 0;
    bool ok = run_process(fs::path(g.base_dir) / L"acs.exe", &code);
    if (ok) {
        std::wstring msg = L"Race finished, exit code " + std::to_wstring(code);
        set_status(msg);
        return true;
    }
    return false;
}

bool launch_showroom() {
    save_last_state();
    if (!patch_showroom_ini()) return false;
    DWORD code = 0;
    bool ok = run_process(fs::path(g.base_dir) / L"acShowroom.exe", &code);
    if (ok) {
        std::wstring msg = L"Showroom finished, exit code " + std::to_wstring(code);
        set_status(msg);
        return true;
    }
    return false;
}

void apply_selection_to_ui() {
    auto select_in_list = [](HWND list, const std::vector<std::wstring>& items, const std::wstring& id) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (extract_id_from_row(items[static_cast<size_t>(i)]) == id) {
                SendMessageW(list, LB_SETCURSEL, i, 0);
                return;
            }
        }
    };
    update_cars();
    update_tracks();
    update_weather();
    update_skins();
    select_in_list(g.car_list, g.filtered_cars, g.selected_car);
    select_in_list(g.track_list, g.filtered_tracks, g.selected_track);
    select_in_list(g.weather_list, g.filtered_weather, g.selected_weather);
    select_in_list(g.skin_list, g.filtered_skins, g.selected_skin);
    load_car_curves();
    populate_layouts();
    refresh_track_preview();
}

void layout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w      = rc.right;
    int left   = kMargin;
    int usable = w - 2 * kMargin;

    // 4-column layout: 3 main list columns + 1 preview column
    constexpr int kPreviewColW = 280;
    int lists_w = usable - kPreviewColW - kColGap;
    int col     = (lists_w - 2 * kColGap) / 3;
    int col1    = left;
    int col2    = col1 + col + kColGap;
    int col3    = col2 + col + kColGap;
    int col4    = col3 + col + kColGap;   // preview column

    // Vertical rhythm
    constexpr int kHeaderH  = 48;
    constexpr int kListH1   = 155;   // main list height
    constexpr int kListH2   = 120;   // weather list height
    constexpr int kLGap     = 6;
    constexpr int kBtnH     = 30;
    constexpr int kBtnGap   = 6;
    constexpr int kResultsH = 130;
    constexpr int kStatusH  = 150;
    constexpr int kSaveBtnW = 130;

    int filter1_y = kHeaderH + kLGap;                 // ~54
    int list1_y   = filter1_y + kControlH + kLGap;
    int row2_y    = list1_y   + kListH1   + kRowGap;
    int list2_y   = row2_y   + kControlH + kLGap;
    int results_y = list2_y  + kListH2   + kRowGap;
    int status_y  = results_y + kResultsH + kRowGap;

    // ---- Mode (direita do header) -------------------------------------------
    constexpr int kModeW = 160;
    constexpr int kModeLblW = 46;  // largura do texto "Mode:" pintado
    MoveWindow(g.mode, w - kMargin - kModeW, (kHeaderH - kControlH) / 2, kModeW, kControlH, TRUE);

    // ---- Row 1: Cars | Skins | Tracks -----------------------------------
    auto place_col = [&](HWND lbl, HWND filt, HWND list, int cx) {
        MoveWindow(lbl,  cx,            filter1_y + 3, kLabelW,       kControlH, TRUE);
        MoveWindow(filt, cx + kLabelW,  filter1_y,     col - kLabelW, kControlH, TRUE);
        MoveWindow(list, cx,            list1_y,       col,           kListH1,   TRUE);
    };
    place_col(g.lbl_cars,   g.car_filter,   g.car_list,   col1);
    place_col(g.lbl_skins,  g.skin_filter,  g.skin_list,  col2);
    place_col(g.lbl_tracks, g.track_filter, g.track_list, col3);

    // ---- Preview column (col4) ------------------------------------------
    // Row-1 area: filter label height + gap + list height
    int row1_total = (list1_y - filter1_y) + kListH1;   // ~185 px
    // Row-2 area: split into track preview + flag (no overlap with buttons)
    int row2_total = (list2_y - row2_y) + kListH2;      // ~150 px
    constexpr int kFlagH = 44;
    int kTrkH = row2_total - kFlagH - 4;

    g.preview_draw_rect = {col4, filter1_y,
                           col4 + kPreviewColW, filter1_y + row1_total};
    g.track_draw_rect   = {col4, row2_y,
                           col4 + kPreviewColW, row2_y + kTrkH};
    g.flag_draw_rect    = {col4, row2_y + kTrkH + 4,
                           col4 + kPreviewColW, row2_y + kTrkH + 4 + kFlagH};

    // ---- Row 2 col1: Weather --------------------------------------------
    MoveWindow(g.lbl_weather,    col1,           row2_y + 3, kLabelW,       kControlH, TRUE);
    MoveWindow(g.weather_filter, col1 + kLabelW, row2_y,     col - kLabelW, kControlH, TRUE);
    MoveWindow(g.weather_list,   col1,           list2_y,    col,           kListH2,   TRUE);

    // ---- Row 2 col2: Preset ---------------------------------------------
    int pedit_w  = col - kLabelW - kSaveBtnW - 4;
    int pcombo_w = col - kSaveBtnW - 4;

    MoveWindow(g.lbl_preset,    col2,                   row2_y + 3, kLabelW,    kControlH, TRUE);
    MoveWindow(g.preset_name,   col2 + kLabelW,         row2_y,     pedit_w,    kControlH, TRUE);
    MoveWindow(g.save_preset,   col2 + col - kSaveBtnW, row2_y,     kSaveBtnW,  kControlH, TRUE);
    MoveWindow(g.preset_list,   col2,                   list2_y,    pcombo_w,   140,       TRUE);
    MoveWindow(g.delete_preset, col2 + pcombo_w + 4,    list2_y,    kSaveBtnW,  kControlH, TRUE);

    // ---- Row 2 col3: Layout selector + botões de launch ----------------
    // Layout ocupa a primeira linha; botões seguem abaixo
    constexpr int kLayoutH = kControlH;
    int layout_y  = row2_y;
    int btns_start = layout_y + kLayoutH + kLGap;

    MoveWindow(g.lbl_layout,  col3,            layout_y + 3, kLabelW,       kLayoutH,   TRUE);
    MoveWindow(g.layout_list, col3 + kLabelW,  layout_y,     col - kLabelW, kLayoutH,   TRUE);

    int btns_x = col3;
    int btns_w = col;
    int half   = (btns_w - 4) / 2;
    MoveWindow(g.launch_race,     btns_x,            btns_start,                      half,              kBtnH, TRUE);
    MoveWindow(g.launch_showroom, btns_x + half + 4, btns_start,                      btns_w - half - 4, kBtnH, TRUE);
    MoveWindow(g.launch_original, btns_x,            btns_start + kBtnH + kBtnGap,   btns_w,             kBtnH, TRUE);
    MoveWindow(g.refresh_results, btns_x,            btns_start + 2*(kBtnH+kBtnGap), btns_w,             kBtnH, TRUE);

    // ---- Curve panel (replaces ListView) + Status ----------------------
    g.curve_draw_rect = {left, results_y, left + usable, results_y + kResultsH};
    MoveWindow(g.status, left, status_y, usable, kStatusH, TRUE);

    InvalidateRect(hwnd, nullptr, FALSE);
}

void create_controls(HWND hwnd) {
    // Fonts
    g.app_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g.title_font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    // Brushes for WM_CTLCOLOR
    g.br_bg    = CreateSolidBrush(kClrBg);
    g.br_ctrl  = CreateSolidBrush(kClrCtrlBg);
    g.br_ctrl2 = CreateSolidBrush(kClrCtrlBg2);

    HFONT font = g.app_font ? g.app_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto mk = [&](LPCWSTR cls, LPCWSTR txt, DWORD style, int x, int y, int w, int h, int id) {
        HWND hctl = CreateWindowExW(0, cls, txt, style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.hinst, nullptr);
        SendMessageW(hctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return hctl;
    };

    int y = 0; // placeholder – layout() positions everything
    // Sem label STATIC "Mode" – o texto é pintado no header via paint_frame
    g.mode = mk(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, y, 170, 200, IDC_MODE);
    SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Race"));
    SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Showroom"));
    SendMessageW(g.mode, CB_SETCURSEL, 0, 0);

    g.lbl_cars    = mk(L"STATIC",  L"Cars",    WS_CHILD | WS_VISIBLE, 0, 0, kLabelW, kControlH, 0);
    g.car_filter  = mk(L"EDIT",    L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 100, kControlH, IDC_CAR_FILTER);
    g.car_list    = mk(L"LISTBOX", L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 0, 0, 100, kListH, IDC_CAR_LIST);

    g.lbl_skins   = mk(L"STATIC",  L"Skins",   WS_CHILD | WS_VISIBLE, 0, 0, kLabelW, kControlH, 0);
    g.skin_filter = mk(L"EDIT",    L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 100, kControlH, IDC_SKIN_FILTER);
    g.skin_list   = mk(L"LISTBOX", L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 0, 0, 100, kListH, IDC_SKIN_LIST);

    g.lbl_tracks    = mk(L"STATIC",  L"Tracks",  WS_CHILD | WS_VISIBLE, 0, 0, kLabelW, kControlH, 0);
    g.track_filter  = mk(L"EDIT",    L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 100, kControlH, IDC_TRACK_FILTER);
    g.track_list    = mk(L"LISTBOX", L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 0, 0, 100, kListH, IDC_TRACK_LIST);
    // Layout selector – visível só para pistas multi-layout (ShowWindow gerenciado em populate_layouts)
    g.lbl_layout  = mk(L"STATIC",   L"Layout",  WS_CHILD, 0, 0, kLabelW, kControlH, 0);
    g.layout_list = mk(L"COMBOBOX", L"",        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 100, 200, IDC_LAYOUT_LIST);

    g.lbl_weather    = mk(L"STATIC",  L"Weather", WS_CHILD | WS_VISIBLE, 0, 0, kLabelW, kControlH, 0);
    g.weather_filter = mk(L"EDIT",    L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 100, kControlH, IDC_WEATHER_FILTER);
    g.weather_list   = mk(L"LISTBOX", L"",        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 0, 0, 100, 120, IDC_WEATHER_LIST);

    g.lbl_preset  = mk(L"STATIC",   L"Preset",        WS_CHILD | WS_VISIBLE, 0, 0, kLabelW, kControlH, 0);
    g.preset_name = mk(L"EDIT",     L"",               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 100, kControlH, IDC_PRESET_NAME);
    g.preset_list = mk(L"COMBOBOX", L"",               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 100, 150, IDC_PRESET_LIST);
    g.save_preset   = mk(L"BUTTON", L"Save preset",   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 120, kControlH, IDC_SAVE_PRESET);
    g.delete_preset = mk(L"BUTTON", L"Load preset",   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 120, kControlH, IDC_DELETE_PRESET);

    g.launch_race     = mk(L"BUTTON", L"Launch Race",     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 120, 30, IDC_LAUNCH_RACE);
    g.launch_showroom = mk(L"BUTTON", L"Launch Showroom", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 120, 30, IDC_LAUNCH_SHOWROOM);
    g.launch_original = mk(L"BUTTON", L"Launch Original", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 120, 30, IDC_LAUNCH_ORIGINAL);
    g.refresh_results = mk(L"BUTTON", L"Refresh Results", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 120, 30, IDC_REFRESH_RESULTS);

    g.status = mk(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, 0, 100, 70, IDC_STATUS);
}

void load_presets_into_combo() {
    refresh_preset_names();
    SendMessageW(g.preset_list, CB_RESETCONTENT, 0, 0);
    for (const auto& p : g.preset_names) SendMessageW(g.preset_list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.c_str()));
}

void init_state() {
    g.base_dir = get_module_dir();
    g.settings_path = fs::path(g.base_dir) / L"ACExorcist.ini";
    load_catalog();
    load_last_state();
    ensure_steam_appid();
}

void refresh_ui_from_state() {
    apply_selection_to_ui();
    load_presets_into_combo();
    if (!g.preset_names.empty()) SendMessageW(g.preset_list, CB_SETCURSEL, 0, 0);
    update_status_panel();
    update_status_panel();
}

void open_original_and_exit() {
    run_process(fs::path(g.base_dir) / L"AssettoCorsa_original.exe");
    PostQuitMessage(0);
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            create_controls(hwnd);
            init_state();
            refresh_ui_from_state();
            layout(hwnd);
            return 0;
        case WM_SIZE:
            layout(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            RECT rc{};
            HDC hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP back = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
            HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, back));
            paint_frame(mem, rc);
            BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(back);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wparam);
            int code = HIWORD(wparam);
            if (id == IDC_CAR_FILTER && code == EN_CHANGE) update_cars();
            if (id == IDC_SKIN_FILTER && code == EN_CHANGE) update_skins();
            if (id == IDC_TRACK_FILTER && code == EN_CHANGE) update_tracks();
            if (id == IDC_WEATHER_FILTER && code == EN_CHANGE) update_weather();
            if (id == IDC_CAR_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.car_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_cars.size())) {
                    g.selected_car = extract_id_from_row(g.filtered_cars[static_cast<size_t>(idx)]);
                    auto* car = find_car(g.selected_car);
                    if (car) g.selected_skin = car->skins.empty() ? L"default" : car->skins.front().id;
                    update_skins();
                    load_car_curves();
                    update_status_panel();
                }
            }
            if (id == IDC_SKIN_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.skin_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_skins.size())) {
                    g.selected_skin = extract_id_from_row(g.filtered_skins[static_cast<size_t>(idx)]);
                    if (!g.selected_car.empty() && !g.selected_skin.empty()) {
                        fs::path p = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"skins" / g.selected_skin / L"preview.jpg";
                        set_preview_bitmap(load_bitmap_from_file(p));
                    }
                    update_status_panel();
                }
            }
            if (id == IDC_TRACK_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.track_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_tracks.size())) {
                    g.selected_track = extract_id_from_row(g.filtered_tracks[static_cast<size_t>(idx)]);
                    g.selected_layout.clear();
                    populate_layouts();
                    refresh_track_preview();
                    update_status_panel();
                }
            }
            if (id == IDC_LAYOUT_LIST && code == CBN_SELCHANGE) {
                int idx = static_cast<int>(SendMessageW(g.layout_list, CB_GETCURSEL, 0, 0));
                if (idx >= 0 && idx < static_cast<int>(g.track_layouts.size())) {
                    g.selected_layout = g.track_layouts[static_cast<size_t>(idx)];
                    refresh_track_preview();
                    update_status_panel();
                }
            }
            if (id == IDC_WEATHER_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.weather_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_weather.size())) {
                    g.selected_weather = extract_id_from_row(g.filtered_weather[static_cast<size_t>(idx)]);
                    update_status_panel();
                    update_status_panel();
                }
            }
            if (id == IDC_SAVE_PRESET && code == BN_CLICKED) {
                save_preset(get_window_text(g.preset_name));
            }
            if (id == IDC_DELETE_PRESET && code == BN_CLICKED) {
                int idx = static_cast<int>(SendMessageW(g.preset_list, CB_GETCURSEL, 0, 0));
                if (idx >= 0 && idx < static_cast<int>(g.preset_names.size())) {
                    load_preset(g.preset_names[static_cast<size_t>(idx)]);
                    apply_selection_to_ui();
                }
            }
            if (id == IDC_LAUNCH_RACE && code == BN_CLICKED) {
                if (!launch_race()) MessageBoxW(hwnd, L"Failed to launch race.", L"ACExorcist", MB_ICONERROR);
            }
            if (id == IDC_LAUNCH_SHOWROOM && code == BN_CLICKED) {
                if (!launch_showroom()) MessageBoxW(hwnd, L"Failed to launch showroom.", L"ACExorcist", MB_ICONERROR);
            }
            if (id == IDC_LAUNCH_ORIGINAL && code == BN_CLICKED) {
                if (!launch_original()) MessageBoxW(hwnd, L"Failed to launch the original launcher.", L"ACExorcist", MB_ICONERROR);
            }
            if (id == IDC_REFRESH_RESULTS && code == BN_CLICKED) refresh_results();
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g.app_font)            DeleteObject(g.app_font);
            if (g.title_font)          DeleteObject(g.title_font);
            if (g.br_bg)               DeleteObject(g.br_bg);
            if (g.br_ctrl)             DeleteObject(g.br_ctrl);
            if (g.br_ctrl2)            DeleteObject(g.br_ctrl2);
            if (g.track_outline_bitmap) DeleteObject(g.track_outline_bitmap);
            if (g.car_curve_flag_bitmap) DeleteObject(g.car_curve_flag_bitmap);
            if (g.car_badge_bitmap)      DeleteObject(g.car_badge_bitmap);
            PostQuitMessage(0);
            return 0;
        // ---- Dark theme for child controls --------------------------------
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kClrCtrlText);
            SetBkColor(hdc, kClrCtrlBg);
            return reinterpret_cast<LRESULT>(g.br_ctrl ? g.br_ctrl : GetStockObject(BLACK_BRUSH));
        }
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kClrCtrlText);
            SetBkColor(hdc, kClrCtrlBg);
            return reinterpret_cast<LRESULT>(g.br_ctrl ? g.br_ctrl : GetStockObject(BLACK_BRUSH));
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kClrLabelText);
            SetBkColor(hdc, kClrBg);
            return reinterpret_cast<LRESULT>(g.br_bg ? g.br_bg : GetStockObject(BLACK_BRUSH));
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int ncmdshow) {
    if (wcsstr(GetCommandLineW(), L"--original")) {
        g.base_dir = get_module_dir();
        if (!g.base_dir.empty()) {
            init_state();
            launch_original();
        }
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    GdiplusStartupInput gdi_input{};
    GdiplusStartup(&g.gdiplus_token, &gdi_input, nullptr);

    g.hinst = hinst;

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"ACExorcist - Assetto Corsa Launcher", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1200, 780, nullptr, nullptr, hinst, nullptr);
    if (!hwnd) return 1;

    g.hwnd = hwnd;
    ShowWindow(hwnd, ncmdshow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g.preview_bitmap) DeleteObject(g.preview_bitmap);
    if (g.track_bitmap)   DeleteObject(g.track_bitmap);
    if (g.flag_bitmap)    DeleteObject(g.flag_bitmap);
    if (g.gdiplus_token) GdiplusShutdown(g.gdiplus_token);
    return static_cast<int>(msg.wParam);
}
