#pragma once

#include "AssetManager/AssetManager.hpp"
#include "SFML/Graphics.hpp"
#include "SFML/System.hpp"
#include "Logger/Logger.hpp"

class IPanel : public sf::Drawable, public sf::Transformable {
public:
    virtual ~IPanel() = default;
    virtual sf::Vector2f get_size() const = 0;
    virtual bool is_visible() const = 0;
    virtual void set_visible(bool visible) = 0;
    virtual void clear() = 0;

protected:
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const = 0;
};

template <typename Derived, typename DisplayedData>
class PanelBase : public IPanel {
public:
    class WriteHandle {
    public:
        WriteHandle() = delete;
        WriteHandle(PanelBase* panel);
        ~WriteHandle();
        WriteHandle(const WriteHandle&) = delete;
        WriteHandle& operator=(const WriteHandle&) = delete;
        WriteHandle(WriteHandle&& wh) noexcept;
        WriteHandle& operator=(WriteHandle&& wh) noexcept;
        DisplayedData* operator->();

    private:
        PanelBase* panel;
    };

    friend class WriteHandle;

    PanelBase(uint32_t width);
    WriteHandle write_handle();
    void set_visible(bool visible);
    bool is_visible() const;
    sf::Vector2f get_size() const;

protected:
    sf::Text text;
    sf::RenderTexture texture;
    DisplayedData displayed_data;

    void clear() override;

private:
    constexpr static uint32_t VERTICAL_PADDING_PX = 6;

    sf::Sprite sprite;
    bool visible;

    void bake();
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;
    void auto_height();
};

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle::WriteHandle(PanelBase* panel) : panel(panel) {}

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle::~WriteHandle() {
    panel->bake();
}

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle::WriteHandle(WriteHandle&& wh) noexcept
        : panel(wh.panel) {
    wh.panel = nullptr;
}

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle&
PanelBase<Derived, DisplayedData>::WriteHandle::operator=(WriteHandle&& wh) noexcept {
    if (this != &wh) {
        panel = wh.panel;
        wh.panel = nullptr;
    }
    return *this;
}

template <typename Derived, typename DisplayedData>
DisplayedData* PanelBase<Derived, DisplayedData>::WriteHandle::operator->() {
    return &(panel->displayed_data);
}

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::PanelBase(uint32_t width)
        : texture({width, 1}), sprite(texture.getTexture()),
          text(AssetManager::instance().get<sf::Font>("fonts/UbuntuMono-R.ttf")), visible(true) {
    text.setPosition({5.0f, 3.0f});
    text.setCharacterSize(16);
    text.setLineSpacing(1.4f);
    texture.setSmooth(false);
    sprite.setScale({1.0f, -1.0f});
    sprite.setPosition({0.0f, 1.0f});
    bake();
}

template <typename Derived, typename DisplayedData>
typename PanelBase<Derived, DisplayedData>::WriteHandle
PanelBase<Derived, DisplayedData>::write_handle() {
    return WriteHandle(this);
}


template <typename Derived, typename DisplayedData>
void PanelBase<Derived, DisplayedData>::set_visible(bool visible) {
    this->visible = visible;
    if (visible) {
        bake();
    }
}

template <typename Derived, typename DisplayedData>
bool PanelBase<Derived, DisplayedData>::is_visible() const {
    return visible;
}

template <typename Derived, typename DisplayedData>
sf::Vector2f PanelBase<Derived, DisplayedData>::get_size() const {
    return sf::Vector2f(texture.getSize());
}

template <typename Derived, typename DisplayedData>
void PanelBase<Derived, DisplayedData>::clear() {
    texture.clear(sf::Color(40, 40, 40, 180));
}

template <typename Derived, typename DisplayedData>
void PanelBase<Derived, DisplayedData>::bake() {
    static_cast<Derived*>(this)->set_panel_text();
    auto_height();
    clear();
    texture.draw(text);
}

template <typename Derived, typename DisplayedData>
void PanelBase<Derived, DisplayedData>::draw(sf::RenderTarget& target,
        sf::RenderStates states) const {
    if (!visible) {
        return;
    }
    states.transform.combine(getTransform());
    target.draw(sprite, states);
}

template <typename Derived, typename DisplayedData>
void PanelBase<Derived, DisplayedData>::auto_height() {
    const auto &str = text.getString();
    const size_t line_count = std::count(str.begin(), str.end(), '\n');


    const auto text_height = static_cast<float>(line_count * text.getCharacterSize()) * 
            text.getLineSpacing();
    
    const sf::Vector2u new_texture_size{texture.getSize().x, static_cast<uint32_t>(text_height) + VERTICAL_PADDING_PX};
    if (new_texture_size.y != texture.getSize().y) {
        if (!texture.resize(new_texture_size)) {
            Log::warning("Failed resizing panel texture to ({}, {})", new_texture_size.x, 
                    new_texture_size.y);
            return;
        }
        sprite.setTexture(texture.getTexture(), true);
        sprite.setPosition({0.0f, static_cast<float>(new_texture_size.y)});
    }
}
