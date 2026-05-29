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
    HWND results{};
    // Labels: stored so layout() can reposition them on resize
    HWND lbl_cars{}, lbl_skins{}, lbl_tracks{}, lbl_weather{}, lbl_preset{};
    HWND layout_list{};   // combobox de layouts da pista
    HWND lbl_layout{};    // label "Layout"
    ULONG_PTR gdiplus_token{};
    HBITMAP preview_bitmap{};
    HBITMAP track_bitmap{};
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
void update_results_list();

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

void set_flag_bitmap(HBITMAP bmp) {
    if (g.flag_bitmap) DeleteObject(g.flag_bitmap);
    g.flag_bitmap = bmp;
    if (g.hwnd) InvalidateRect(g.hwnd, &g.flag_draw_rect, FALSE);
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
    draw_panel(g.flag_bitmap,    g.flag_draw_rect,     L"Flag");
}

void add_catalog_entry(std::vector<CatalogEntry>& dest, const fs::path& dir, bool track_mode) {
    CatalogEntry e;
    e.id = dir.filename().wstring();
    e.display = e.id;
    e.path = dir.wstring();

    fs::path ui_json = dir / "ui" / (track_mode ? "ui_track.json" : "ui_car.json");
    if (file_exists(ui_json)) {
        try {
            json j = json::parse(read_file_utf8(ui_json));
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
    if (p.empty()) p = find_img(L"outline.png");
    if (p.empty()) p = find_img(L"map.png");
    set_track_bitmap(p.empty() ? nullptr : load_bitmap_from_file(p));

    auto* track = find_track(g.selected_track);
    if (track && !track->country.empty())
        set_flag_bitmap(load_bitmap_from_file(fs::path(g.base_dir) / L"content" / L"gui" / L"NationFlags" / (track->country + L".png")));
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
    update_results_list();
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
        set_flag_bitmap(load_bitmap_from_file(fs::path(g.base_dir) / L"content" / L"gui" / L"NationFlags" / (car->country + L".png")));
    }
    update_results_list();
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

void update_status_panel() {
    std::wstring text;

    // Descrição do carro
    if (!g.selected_car.empty()) {
        fs::path car_json = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"ui" / L"ui_car.json";
        if (file_exists(car_json)) {
            try {
                json j = json::parse(read_file_utf8(car_json));
                if (j.contains("description") && j["description"].is_string()) {
                    std::wstring desc = strip_html(widen(j["description"].get<std::string>()));
                    if (!desc.empty()) text += desc + L"\r\n";
                }
            } catch (...) {}
        }
    }

    // Descrição da pista (usa layout selecionado se houver)
    if (!g.selected_track.empty()) {
        fs::path ui = fs::path(g.base_dir) / L"content" / L"tracks" / g.selected_track / L"ui";
        fs::path track_json = g.selected_layout.empty()
            ? ui / L"ui_track.json"
            : ui / g.selected_layout / L"ui_track.json";
        if (!file_exists(track_json)) track_json = ui / L"ui_track.json";
        if (file_exists(track_json)) {
            try {
                json j = json::parse(read_file_utf8(track_json));
                if (j.contains("description") && j["description"].is_string()) {
                    std::wstring desc = strip_html(widen(j["description"].get<std::string>()));
                    if (!desc.empty()) text += desc + L"\r\n";
                }
            } catch (...) {}
        }
    }

    SetWindowTextW(g.status, text.c_str());
}

void update_results_list() {
    if (!g.results) return;
    ListView_DeleteAllItems(g.results);
    int row = 0;
    auto add_row = [&](const std::wstring& label, const std::wstring& value) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<LPWSTR>(label.c_str());
        ListView_InsertItem(g.results, &item);
        ListView_SetItemText(g.results, row, 1, const_cast<LPWSTR>(value.c_str()));
        ++row;
    };

    // ── Specs do carro ──────────────────────────────────────────────────
    if (!g.selected_car.empty()) {
        fs::path car_json = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"ui" / L"ui_car.json";
        if (file_exists(car_json)) {
            try {
                json j = json::parse(read_file_utf8(car_json));
                auto jw = [&](const char* k) -> std::wstring {
                    return (j.contains(k) && j[k].is_string()) ? widen(j[k].get<std::string>()) : L"";
                };
                add_row(L"Car",     jw("name"));
                add_row(L"Brand",   jw("brand"));
                add_row(L"Class",   jw("class"));
                if (j.contains("specs") && j["specs"].is_object()) {
                    auto& s = j["specs"];
                    auto sw = [&](const char* k) -> std::wstring {
                        return (s.contains(k) && s[k].is_string()) ? widen(s[k].get<std::string>()) : L"";
                    };
                    add_row(L"BHP",          sw("bhp"));
                    add_row(L"Torque",       sw("torque"));
                    add_row(L"Weight",       sw("weight"));
                    add_row(L"Top Speed",    sw("topspeed"));
                    add_row(L"Acceleration", sw("acceleration"));
                    add_row(L"P/W Ratio",    sw("pwratio"));
                }
                if (j.contains("tags") && j["tags"].is_array()) {
                    std::wstring tags;
                    for (const auto& t : j["tags"])
                        if (t.is_string()) { if (!tags.empty()) tags += L", "; tags += widen(t.get<std::string>()); }
                    if (!tags.empty()) add_row(L"Tags", tags);
                }
            } catch (...) {}
        }
    }

    // ── Specs da pista ──────────────────────────────────────────────────
    if (!g.selected_track.empty()) {
        fs::path ui = fs::path(g.base_dir) / L"content" / L"tracks" / g.selected_track / L"ui";
        fs::path track_json = (!g.selected_layout.empty() && file_exists(ui / g.selected_layout / L"ui_track.json"))
            ? ui / g.selected_layout / L"ui_track.json"
            : ui / L"ui_track.json";
        if (file_exists(track_json)) {
            try {
                json j = json::parse(read_file_utf8(track_json));
                auto jw = [&](const char* k) -> std::wstring {
                    return (j.contains(k) && j[k].is_string()) ? widen(j[k].get<std::string>()) : L"";
                };
                add_row(L"Track",    jw("name"));
                add_row(L"Country",  jw("country"));
                add_row(L"City",     jw("city"));
                std::wstring len = jw("length");
                if (!len.empty()) add_row(L"Length",   len + L" m");
                std::wstring w = jw("width");
                if (!w.empty())   add_row(L"Width",    w + L" m");
                std::wstring pb = jw("pitboxes");
                if (!pb.empty())  add_row(L"Pitboxes", pb);
                std::wstring run = jw("run");
                if (!run.empty()) add_row(L"Direction", run);
            } catch (...) {}
        }
    }

    // ── Separador + resultados de corrida ───────────────────────────────
    if (row > 0) add_row(L"─── Race Results ───", L"");

    std::wstring race_out_path = (fs::path(g.base_dir) / L"race_out.json").wstring();
    std::wstring laps_path     = (fs::path(g.base_dir) / L"laps.ini").wstring();

    if (file_exists(fs::path(race_out_path))) {
        try {
            json j = json::parse(read_file_utf8(fs::path(race_out_path)));
            add_row(L"Source", L"race_out.json");
            if (j.contains("players") && j["players"].is_array()) {
                add_row(L"Players", std::to_wstring(j["players"].size()));
                for (const auto& p : j["players"]) {
                    std::wstring line = to_json_w(p, "name") + L" | " + to_json_w(p, "car") + L" | " + to_json_w(p, "skin");
                    add_row(L"Player", line);
                }
            }
            if (j.contains("sessions") && j["sessions"].is_object()) {
                const auto& s = j["sessions"];
                if (s.contains("bestLaps") && s["bestLaps"].is_array()) {
                    add_row(L"BestLaps", std::to_wstring(s["bestLaps"].size()));
                    for (const auto& bl : s["bestLaps"]) {
                        std::wstring line = L"car=" + to_json_num_w(bl, "car") + L" time=" + to_json_num_w(bl, "time");
                        add_row(L"BestLap", line);
                    }
                }
                if (s.contains("laps") && s["laps"].is_array())
                    add_row(L"Laps", std::to_wstring(s["laps"].size()));
            }
            return;
        } catch (...) {}
    }

    if (file_exists(fs::path(laps_path))) {
        add_row(L"Source", L"laps.ini");
        wchar_t buf[2048];
        GetPrivateProfileSectionNamesW(buf, 2048, fs::path(laps_path).c_str());
        const wchar_t* p = buf;
        while (*p && row < 60) {
            std::wstring section = p;
            if (section.rfind(L"LAP_", 0) == 0) {
                std::wstring time   = ini_get(fs::path(laps_path), section, L"TIME", L"");
                std::wstring splits = ini_get(fs::path(laps_path), section, L"SPLITS", L"");
                add_row(section, L"time=" + time + L" splits=" + splits);
            }
            p += section.size() + 1;
        }
        return;
    }

    if (row == 0 || (row == 1))
        add_row(L"Source", L"No result files found");
}

void invalidate_frame() {
    if (!g.hwnd) return;
    InvalidateRect(g.hwnd, &g.preview_draw_rect, FALSE);
    InvalidateRect(g.hwnd, &g.track_draw_rect, FALSE);
    InvalidateRect(g.hwnd, &g.flag_draw_rect, FALSE);
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

void refresh_results() {
    update_results_list();
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
}

void layout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w      = rc.right;
    int left   = kMargin;
    int usable = w - 2 * kMargin;

    // 4-column layout: 3 main list columns + 1 preview column
    constexpr int kPreviewColW = 220;
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
    constexpr int kStatusH  = 80;
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

    // ---- Results and Status ---------------------------------------------
    MoveWindow(g.results, left, results_y, usable, kResultsH, TRUE);
    MoveWindow(g.status,  left, status_y,  usable, kStatusH,  TRUE);

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

    g.results = mk(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0, 0, 100, 100, IDC_RESULTS);
    ListView_SetExtendedListViewStyle(g.results, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ListView_SetBkColor(g.results,     kClrCtrlBg2);
    ListView_SetTextBkColor(g.results, kClrCtrlBg2);
    ListView_SetTextColor(g.results,   kClrCtrlText);
    {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<LPWSTR>(L"Field");
        col.cx = 140;
        ListView_InsertColumn(g.results, 0, &col);
        col.pszText = const_cast<LPWSTR>(L"Value");
        col.cx = 880;
        ListView_InsertColumn(g.results, 1, &col);
    }

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
    update_results_list();
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
                    update_status_panel();
                    update_results_list();
                }
            }
            if (id == IDC_SKIN_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.skin_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_skins.size())) {
                    g.selected_skin = extract_id_from_row(g.filtered_skins[static_cast<size_t>(idx)]);
                    // Atualiza preview sem reconstruir a lista (evita perda do highlight)
                    if (!g.selected_car.empty() && !g.selected_skin.empty()) {
                        fs::path p = fs::path(g.base_dir) / L"content" / L"cars" / g.selected_car / L"skins" / g.selected_skin / L"preview.jpg";
                        set_preview_bitmap(load_bitmap_from_file(p));
                    }
                    update_status_panel();
                    update_results_list();
                }
            }
            if (id == IDC_TRACK_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.track_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_tracks.size())) {
                    g.selected_track = extract_id_from_row(g.filtered_tracks[static_cast<size_t>(idx)]);
                    g.selected_layout.clear(); // reset layout ao mudar de pista
                    populate_layouts();
                    refresh_track_preview();
                    update_status_panel();
                    update_results_list();
                }
            }
            if (id == IDC_LAYOUT_LIST && code == CBN_SELCHANGE) {
                int idx = static_cast<int>(SendMessageW(g.layout_list, CB_GETCURSEL, 0, 0));
                if (idx >= 0 && idx < static_cast<int>(g.track_layouts.size())) {
                    g.selected_layout = g.track_layouts[static_cast<size_t>(idx)];
                    refresh_track_preview();
                    update_status_panel();
                    update_results_list();
                }
            }
            if (id == IDC_WEATHER_LIST && code == LBN_SELCHANGE) {
                int idx = listbox_selected(g.weather_list);
                if (idx >= 0 && idx < static_cast<int>(g.filtered_weather.size())) {
                    g.selected_weather = extract_id_from_row(g.filtered_weather[static_cast<size_t>(idx)]);
                    update_status_panel();
                    update_results_list();
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
            if (g.app_font)   DeleteObject(g.app_font);
            if (g.title_font) DeleteObject(g.title_font);
            if (g.br_bg)      DeleteObject(g.br_bg);
            if (g.br_ctrl)    DeleteObject(g.br_ctrl);
            if (g.br_ctrl2)   DeleteObject(g.br_ctrl2);
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
