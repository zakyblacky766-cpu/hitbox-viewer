// Hitbox Viewer - Geode mod
// Draws hitboxes in levels. Left Alt drops down a menu to switch them on and off.
//
// Only long-standing, stable game functions are hooked (PlayLayer::setupHasCompleted,
// PlayLayer::postUpdate, CCScheduler::update) so the same code works across GD versions.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <cmath>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace {
    struct ToggleInfo {
        char const* key;    // setting id in mod.json
        char const* label;  // text in the drop-down menu
    };

    constexpr ToggleInfo TOGGLES[] = {
        {"show-hitboxes", "Show hitboxes"},
        {"show-solids",   "Solids (blue)"},
        {"show-hazards",  "Hazards (red)"},
        {"show-specials", "Specials (green)"},
        {"show-player",   "Player (yellow)"},
    };
    constexpr int TOGGLE_COUNT = sizeof(TOGGLES) / sizeof(TOGGLES[0]);

    bool flag(char const* key) {
        return Mod::get()->getSettingValue<bool>(key);
    }
}

// =====================================================================================
//  Drop-down menu
// =====================================================================================
class HitboxMenu : public CCLayer {
public:
    static inline HitboxMenu* s_instance = nullptr;

    CCNode* m_panel = nullptr;
    bool m_open = false;
    float m_shownY = 0.f;
    float m_hiddenY = 0.f;

    ~HitboxMenu() override {
        if (s_instance == this) s_instance = nullptr;
    }

    static HitboxMenu* create() {
        auto ret = new HitboxMenu();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;

        auto win = CCDirector::get()->getWinSize();
        constexpr float PW = 270.f;
        constexpr float PH = 262.f;

        m_panel = CCNode::create();
        m_panel->setContentSize({PW, PH});
        m_panel->setAnchorPoint({0.5f, 0.5f});
        m_shownY = win.height - PH / 2.f - 6.f;
        m_hiddenY = win.height + PH / 2.f + 10.f;
        m_panel->setPosition({win.width / 2.f, m_hiddenY});
        this->addChild(m_panel);

        auto bg = CCScale9Sprite::create("GJ_square01.png", {0.f, 0.f, 80.f, 80.f});
        bg->setContentSize({PW, PH});
        bg->setPosition({PW / 2.f, PH / 2.f});
        bg->setOpacity(240);
        m_panel->addChild(bg);

        auto title = CCLabelBMFont::create("Hitbox Viewer", "goldFont.fnt");
        title->setScale(0.7f);
        title->setPosition({PW / 2.f, PH - 22.f});
        m_panel->addChild(title);

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_panel->addChild(menu);

        // on/off switches
        for (int i = 0; i < TOGGLE_COUNT; ++i) {
            float y = PH - 58.f - i * 32.f;
            auto toggler = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(HitboxMenu::onToggle), 0.65f
            );
            toggler->setTag(i);
            toggler->toggle(flag(TOGGLES[i].key));
            toggler->setPosition({34.f, y});
            menu->addChild(toggler);

            auto label = CCLabelBMFont::create(TOGGLES[i].label, "bigFont.fnt");
            label->setScale(0.45f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({60.f, y});
            m_panel->addChild(label);
        }

        // opacity slider
        auto opLabel = CCLabelBMFont::create("Opacity", "bigFont.fnt");
        opLabel->setScale(0.4f);
        opLabel->setPosition({PW / 2.f, 58.f});
        m_panel->addChild(opLabel);

        auto slider = Slider::create(this, menu_selector(HitboxMenu::onOpacity), 0.7f);
        slider->setPosition({PW / 2.f, 38.f});
        slider->setValue(
            static_cast<float>(Mod::get()->getSettingValue<int64_t>("hitbox-opacity")) / 100.f
        );
        m_panel->addChild(slider);

        auto hint = CCLabelBMFont::create("Left Alt: open / close", "chatFont.fnt");
        hint->setScale(0.5f);
        hint->setOpacity(160);
        hint->setPosition({PW / 2.f, 12.f});
        m_panel->addChild(hint);

        // close button
        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.6f);
        auto closeBtn = CCMenuItemSpriteExtra::create(
            closeSpr, this, menu_selector(HitboxMenu::onClose)
        );
        closeBtn->setPosition({PW - 16.f, PH - 16.f});
        menu->addChild(closeBtn);

        return true;
    }

    void onToggle(CCObject* sender) {
        int i = static_cast<CCNode*>(sender)->getTag();
        if (i < 0 || i >= TOGGLE_COUNT) return;
        auto key = TOGGLES[i].key;
        Mod::get()->setSettingValue<bool>(key, !flag(key));
    }

    void onOpacity(CCObject* sender) {
        float v = static_cast<SliderThumb*>(sender)->getValue();
        int64_t pct = static_cast<int64_t>(std::round(v * 100.f));
        if (pct < 5) pct = 5;
        Mod::get()->setSettingValue<int64_t>("hitbox-opacity", pct);
    }

    void onClose(CCObject*) {
        setOpen(false);
    }

    void setOpen(bool open) {
        m_open = open;
        auto win = CCDirector::get()->getWinSize();
        m_panel->stopAllActions();
        auto move = CCMoveTo::create(0.3f, {win.width / 2.f, open ? m_shownY : m_hiddenY});
        CCAction* act = open
            ? static_cast<CCAction*>(CCEaseBackOut::create(move))
            : static_cast<CCAction*>(CCEaseBackIn::create(move));
        m_panel->runAction(act);
    }

    // Called when Left Alt is pressed
    static void toggleMenu() {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (!s_instance || s_instance->getParent() != scene) {
            auto menu = HitboxMenu::create();
            if (!menu) return;
            scene->addChild(menu, 99999);
            s_instance = menu;
        }
        s_instance->setOpen(!s_instance->m_open);
        if (flag("debug-toasts")) {
            Notification::create(
                s_instance->m_open ? "Hitbox menu: open" : "Hitbox menu: closed",
                NotificationIcon::Info, 1.f
            )->show();
        }
    }
};

// =====================================================================================
//  Left Alt detection (polled every frame, so it does not depend on any key-event signature)
// =====================================================================================
static bool leftAltDown() {
#ifdef GEODE_IS_WINDOWS
    // only react while Geometry Dash is the focused window (ignore Alt+Tab)
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    if (GetAsyncKeyState(VK_TAB) & 0x8000) return false;
    return (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
#elif defined(GEODE_IS_IOS)
    return false; // iOS has no Left Alt key
#else
    auto kb = CCKeyboardDispatcher::get();
    return kb && kb->getAltKeyPressed();
#endif
}

static void pollAlt() {
    static bool wasDown = false;
    bool down = leftAltDown();
    if (down && !wasDown) HitboxMenu::toggleMenu();
    wasDown = down;
}

class $modify(HVScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        pollAlt();
    }
};

// =====================================================================================
//  Hitbox drawing
// =====================================================================================
static void drawBox(CCDrawNode* node, CCRect const& r, ccColor4F fill, ccColor4F border, float width) {
    CCPoint v[4] = {
        ccp(r.getMinX(), r.getMinY()),
        ccp(r.getMaxX(), r.getMinY()),
        ccp(r.getMaxX(), r.getMaxY()),
        ccp(r.getMinX(), r.getMaxY()),
    };
    node->drawPolygon(v, 4, fill, width, border);
}

class $modify(HVPlayLayer, PlayLayer) {
    struct Fields {
        CCDrawNode* draw = nullptr;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto draw = CCDrawNode::create();
        m_objectLayer->addChild(draw, 1000);
        m_fields->draw = draw;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        pollAlt();
        this->drawHitboxes();
    }

    void drawHitboxes() {
        auto draw = m_fields->draw;
        if (!draw) return;
        draw->clear();

        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("show-hitboxes")) return;

        float alpha = static_cast<float>(mod->getSettingValue<int64_t>("hitbox-opacity")) / 100.f;
        float width = static_cast<float>(mod->getSettingValue<int64_t>("line-width")) * 0.5f;
        bool solids = flag("show-solids");
        bool hazards = flag("show-hazards");
        bool specials = flag("show-specials");

        ccColor4F blue = {0.25f, 0.55f, 1.f, alpha};
        ccColor4F red = {1.f, 0.2f, 0.2f, alpha};
        ccColor4F green = {0.2f, 1.f, 0.4f, alpha};
        auto fillOf = [](ccColor4F c) { return ccColor4F{c.r, c.g, c.b, c.a * 0.22f}; };

        // visible area in object-layer space (skip culling if the camera is zoomed)
        auto win = CCDirector::get()->getWinSize();
        float scale = m_objectLayer->getScale();
        bool cull = std::fabs(scale - 1.f) < 0.01f;
        auto lp = m_objectLayer->getPosition();
        float pad = 180.f;
        CCRect view(-lp.x - pad, -lp.y - pad, win.width + pad * 2.f, win.height + pad * 2.f);

        if (solids || hazards || specials) {
            CCObject* it = nullptr;
            CCARRAY_FOREACH(m_objects, it) {
                auto obj = static_cast<GameObject*>(it);
                if (!obj) continue;
                auto type = obj->m_objectType;
                if (type == GameObjectType::Decoration) continue;

                if (cull) {
                    float x = obj->getPositionX();
                    if (x < view.getMinX() - 60.f || x > view.getMaxX() + 60.f) continue;
                }

                ccColor4F col;
                if (type == GameObjectType::Hazard) {
                    if (!hazards) continue;
                    col = red;
                } else if (type == GameObjectType::Solid || type == GameObjectType::Slope) {
                    if (!solids) continue;
                    col = blue;
                } else {
                    if (!specials) continue;
                    col = green;
                }

                CCRect r = obj->getObjectRect();
                if (cull && !view.intersectsRect(r)) continue;
                drawBox(draw, r, fillOf(col), col, width);
            }
        }

        if (flag("show-player")) {
            ccColor4F yellow = {1.f, 0.9f, 0.1f, alpha};
            ccColor4F white = {1.f, 1.f, 1.f, alpha};
            for (PlayerObject* p : {m_player1, m_player2}) {
                if (!p || !p->isVisible()) continue;
                CCRect outer = p->getObjectRect();
                drawBox(draw, outer, fillOf(yellow), yellow, width);
                // approximate inner box (about a quarter of the outer size)
                float iw = outer.size.width * 0.25f;
                float ih = outer.size.height * 0.25f;
                CCRect inner(outer.getMidX() - iw / 2.f, outer.getMidY() - ih / 2.f, iw, ih);
                drawBox(draw, inner, fillOf(white), white, width);
            }
        }
    }
};

// =====================================================================================
//  Backup: "Hitboxes" button in the pause menu (works even without a keyboard)
// =====================================================================================
class $modify(HVPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto spr = ButtonSprite::create("Hitboxes");
        spr->setScale(0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HVPauseLayer::onHitboxes));
        auto menu = CCMenu::create();
        menu->setPosition({70.f, 30.f});
        menu->addChild(btn);
        this->addChild(menu, 10);
    }

    void onHitboxes(CCObject*) {
        HitboxMenu::toggleMenu();
    }
};

$on_mod(Loaded) {
    log::info("Hitbox Viewer loaded - press Left Alt to open the menu");
}
