#pragma once

#include "SFML/System.hpp"
#include "SFML/Graphics.hpp"
#include "AssetManager/AssetManager.hpp"

class IPanel : public sf::Drawable, public sf::Transformable {
public:
    virtual ~IPanel() = default;
    virtual sf::Vector2f get_size() const = 0;
    virtual bool is_visible() const = 0;
    virtual void set_visible(bool visible) = 0;
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
        DisplayedData *operator->();
    private:
        PanelBase* panel;
    };

    friend class WriteHandle;

    PanelBase(sf::Vector2u size);
    WriteHandle write_handle();
    void set_visible(bool visible);
    bool is_visible() const;
    sf::Vector2f get_size() const;
protected:
    sf::Text text;
    sf::RenderTexture texture;
    DisplayedData displayed_data;
private:
    void clear();
    void bake();
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;

    sf::Sprite sprite;
    bool visible;
};

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle::WriteHandle(PanelBase* panel) : panel(panel) {}

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle::~WriteHandle() { panel->bake(); }

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::WriteHandle::WriteHandle(WriteHandle&& wh) noexcept : 
        panel(wh.panel) {
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
DisplayedData *PanelBase<Derived, DisplayedData>::WriteHandle::operator->() { 
    return &(panel->displayed_data); 
}

template <typename Derived, typename DisplayedData>
PanelBase<Derived, DisplayedData>::PanelBase(sf::Vector2u size) : texture(size), 
        sprite(texture.getTexture()), 
        text(AssetManager::instance().get<sf::Font>("fonts/UbuntuMono-R.ttf")), visible(true) {
    text.setPosition({5.0f, 10.0f});
    text.setCharacterSize(16);
    text.setLineSpacing(1.4f);
    texture.setSmooth(false);
    sprite.setScale({1.0f, -1.0f});
    sprite.setPosition({0.0f, static_cast<float>(size.y)});
    clear();
}

template <typename Derived, typename DisplayedData>
typename PanelBase<Derived, DisplayedData>::WriteHandle PanelBase<Derived, DisplayedData>::write_handle() {
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
    clear();
    static_cast<Derived*>(this)->bake_impl();
}

template <typename Derived, typename DisplayedData>
void PanelBase<Derived, DisplayedData>::draw(sf::RenderTarget& target, sf::RenderStates states) 
        const {
    if (!visible) {
        return;
    }
    states.transform.combine(getTransform());
    target.draw(sprite, states);
}
