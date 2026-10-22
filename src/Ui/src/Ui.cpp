#include "Ui/Ui.hpp"

#include <stdexcept>
#include <string>

#include "AssetManager/AssetManager.hpp"
#include "Constants/Constants.hpp"
#include "imgui-SFML.h"


namespace Ui {
namespace {
Fonts loaded_fonts;

// The characters every font has: Latin, Greek and Cyrillic, punctuation (– — … •), the minus sign
// and U+FFFD. Dear ImGui 1.91 bakes them at startup and shows any other character as '?'.
const ImWchar* glyph_ranges() {
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        static constexpr ImWchar extra_ranges[] = {
                0x0100, 0x017F,  // Latin Extended-A
                0x2000, 0x206F,  // General Punctuation
                0x2212, 0x2212,  // minus sign
                0xFFFD, 0xFFFD,  // replacement character
                0};
        ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(atlas.GetGlyphRangesDefault());
        builder.AddRanges(atlas.GetGlyphRangesGreek());
        builder.AddRanges(atlas.GetGlyphRangesCyrillic());
        builder.AddRanges(extra_ranges);
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

ImFont* add_font(const std::string& file, float size_px) {
    const std::string path = AssetManager::instance().path("fonts/" + file).string();
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size_px, nullptr,
            glyph_ranges());
    if (!font) {
        throw std::runtime_error("Failed to load font `" + path + "`");
    }
    return font;
}

// Dark, translucent windows with one accent colour (Constants::Ui)
void apply_theme() {
    using namespace Constants::Ui;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = WINDOW_ROUNDING;
    style.ChildRounding = WINDOW_ROUNDING;
    style.PopupRounding = WINDOW_ROUNDING;
    style.FrameRounding = FRAME_ROUNDING;
    style.GrabRounding = FRAME_ROUNDING;
    style.WindowBorderSize = 1.f;
    style.WindowPadding = WINDOW_PADDING;
    style.ItemSpacing = ITEM_SPACING;
    style.CellPadding = CELL_PADDING;
    style.WindowTitleAlign = {0.f, 0.5f};
    style.FramePadding = {8.f, 4.f};
    style.ScrollbarSize = 10.f;
    style.ScrollbarRounding = FRAME_ROUNDING;
    // Faint white on dark, and the accent's tints for what can be clicked
    const auto white = [](uint8_t alpha) { return ImVec4(sf::Color(255, 255, 255, alpha)); };
    const auto accent = [](uint8_t alpha) {
        return ImVec4(sf::Color(ACCENT.r, ACCENT.g, ACCENT.b, alpha));
    };
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = TEXT;
    colors[ImGuiCol_TextDisabled] = DIM_TEXT;
    colors[ImGuiCol_WindowBg] = PANEL_BG;
    colors[ImGuiCol_ChildBg] = white(0);
    colors[ImGuiCol_PopupBg] = PANEL_BG;
    colors[ImGuiCol_Border] = BORDER;
    colors[ImGuiCol_Separator] = BORDER;
    colors[ImGuiCol_TitleBg] = HUB_BG;
    colors[ImGuiCol_TitleBgActive] = HUB_BG;
    colors[ImGuiCol_TitleBgCollapsed] = HUB_BG;
    colors[ImGuiCol_FrameBg] = white(15);
    colors[ImGuiCol_FrameBgHovered] = white(23);
    colors[ImGuiCol_FrameBgActive] = white(31);
    colors[ImGuiCol_Header] = accent(64);
    colors[ImGuiCol_HeaderHovered] = accent(36);
    colors[ImGuiCol_HeaderActive] = accent(90);
    colors[ImGuiCol_Button] = accent(70);
    colors[ImGuiCol_ButtonHovered] = accent(110);
    colors[ImGuiCol_ButtonActive] = accent(150);
    colors[ImGuiCol_ScrollbarBg] = white(0);
    colors[ImGuiCol_ScrollbarGrab] = white(26);
    colors[ImGuiCol_ScrollbarGrabHovered] = white(46);
    colors[ImGuiCol_ScrollbarGrabActive] = white(64);
    colors[ImGuiCol_TextSelectedBg] = accent(90);
    colors[ImGuiCol_NavCursor] = ACCENT;
}
}  // namespace


const Fonts& fonts() {
    return loaded_fonts;
}

Context::Context(sf::RenderWindow& window) : window(window) {
    using namespace Constants::Ui;
    if (!ImGui::SFML::Init(window, false)) {
        throw std::runtime_error("Failed to initialize Dear ImGui");
    }
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini: windows are placed by the app
    // The app sets its own cursors (panning); keyboard navigation is for the hub
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTextEnterKeepActive = true;  // the hub's search box, where Enter runs
    try {
        loaded_fonts = Fonts{.text = add_font("Ubuntu-R.ttf", TEXT_FONT_SIZE),
                .title = add_font("Ubuntu-M.ttf", TEXT_FONT_SIZE),
                .mono = add_font("UbuntuMono-R.ttf", MONO_FONT_SIZE),
                .mono_small = add_font("UbuntuMono-R.ttf", SMALL_FONT_SIZE),
                .heading = add_font("Ubuntu-M.ttf", HEADING_FONT_SIZE),
                .card_title = add_font("Ubuntu-M.ttf", CARD_TITLE_FONT_SIZE)};
        if (!ImGui::SFML::UpdateFontTexture()) {
            throw std::runtime_error("Failed to create the font texture of Dear ImGui");
        }
    }
    catch (...) {
        window.close();  // as in the destructor
        ImGui::SFML::Shutdown();
        throw;
    }
    apply_theme();
}

Context::~Context() {
    window.close();
    ImGui::SFML::Shutdown();
    loaded_fonts = {};
}

void Context::process_event(const sf::Event& event) {
    ImGui::SFML::ProcessEvent(window, event);
}

void Context::new_frame() {
    ImGui::SFML::Update(window, frame_clock.restart());
}

void Context::render() {
    ImGui::SFML::Render(window);
    window.resetGLStates();
}
}  // namespace Ui
