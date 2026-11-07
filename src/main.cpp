// Hitbox Viewer - Geode mod
// Draws hitboxes in levels. TAB drops down a menu to switch them on and off.
//
// Why the old Left-Alt version could show nothing:
//  * Alt is a Windows "system" key: tapping it can open the (invisible) system menu and swallow input.
//  * The menu was only created lazily inside a scheduler hook, with no other way to open it.
// Fixes: key is now TAB (Alt+Tab is ignored), the menu is created ONCE at the main menu and kept
// across scenes, and there are two extra ways to open it that need no key at all:
//  * a "Hitboxes" button on the pause screen (all platforms)
//  * a small "HB" button on-screen (everything except Windows)
// Only long-standing, stable functions are hooked, so it works across GD versions Geode supports.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
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
        ccColor3B color;    // swatch colour
    };

    constexpr ToggleInfo TOGGLES[] = {
        {"show-hitboxes", "Show hitboxes",     {255, 255, 255}},
        {"show-solids",   "Solids (blue)",     {64, 140, 255}},
        {"show-hazards",  "Hazards (red)",     {255, 51, 51}},
        {"show-specials", "Specials (green)",  {51, 255, 102}},
        {"show-player",   "Player (yellow)",   {255, 230, 25}},
    };
    constexpr int TOGGLE_COUNT = sizeof(TOGGLES) / sizeof(TOGGLES[0]);

    bool flag(char const* key) {
        return Mod::get()->getSettingValue<bool>(key);
    }
    int64_t opacityPct() {
        return Mod::get()->getSettingValue<int64_t>("hitbox-opacity");
    }
}

// redraws the boxes right away (defined after the PlayLayer hook)
void refreshHitboxes();

// =====================================================================================
//  Drop-down menu (one persistent instance, created at the main menu)
// =====================================================================================
class HitboxMenu : public CCLayer {
public:
    static inline HitboxMenu* s_instance = nullptr;

    CCNode* m_panel = nullptr;
    CCLabelBMFont* m_pctLabel = nullptr;
    bool m_open = false;
    float m_shownY = 0.f;
    float m_hiddenY = 0.f;

    static constexpr float PW = 290.f;
    static constexpr float PH = 296.f;

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
        s_instance = this;
        this->setZOrder(99999);

        auto win = CCDirector::get()->getWinSize();

        m_panel = CCNode::create();
        m_panel->setContentSize({PW, PH});
        m_panel->setAnchorPoint({0.5f, 0.5f});
        m_shownY = win.height - PH / 2.f - 6.f;
        m_hiddenY = win.height + PH / 2.f + 12.f;
        m_panel->setPosition({win.width / 2.f, m_hiddenY});
        this->addChild(m_panel);

        auto bg = CCScale9Sprite::create("GJ_square01.png", {0.f, 0.f, 80.f, 80.f});
        bg->setContentSize({PW, PH});
        bg->setPosition({PW / 2.f, PH / 2.f});
        bg->setOpacity(245);
        m_panel->addChild(bg);

        auto title = CCLabelBMFont::create("Hitbox Viewer", "goldFont.fnt");
        title->setScale(0.7f);
        title->setPosition({PW / 2.f, PH - 24.f});
        m_panel->addChild(title);

        auto rule = CCLayerColor::create({64, 140, 255, 255}, PW - 44.f, 3.f);
        rule->setPosition({22.f, PH - 44.f});
        m_panel->addChild(rule);

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_panel->addChild(menu);

        // on/off switches with colour swatches
        for (int i = 0; i < TOGGLE_COUNT; ++i) {
            float y = PH - 72.f - i * 33.f;

            auto toggler = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(HitboxMenu::onToggle), 0.65f
            );
            toggler->setTag(i);
            toggler->toggle(flag(TOGGLES[i].key));
            toggler->setPosition({36.f, y});
            menu->addChild(toggler);

            auto c = TOGGLES[i].color;
            auto swatch = CCLayerColor::create({c.r, c.g, c.b, 255}, 14.f, 14.f);
            swatch->setPosition({62.f, y - 7.f});
            m_panel->addChild(swatch);

            auto label = CCLabelBMFont::create(TOGGLES[i].label, "bigFont.fnt");
            label->setScale(0.45f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({84.f, y});
            m_panel->addChild(label);
        }

        // opacity slider with live percentage
        m_pctLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_pctLabel->setScale(0.4f);
        m_pctLabel->setPosition({PW / 2.f, 58.f});
        m_panel->addChild(m_pctLabel);
        this->updatePct();

        auto slider = Slider::create(this, menu_selector(HitboxMenu::onOpacity), 0.7f);
        slider->setPosition({PW / 2.f, 38.f});
        slider->setValue(static_cast<float>(opacityPct()) / 100.f);
        m_panel->addChild(slider);

        char const* hintText = "TAB or the HB button: open / close";
        auto hint = CCLabelBMFont::create(hintText, "chatFont.fnt");
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

        // small always-visible button (works on every platform, no keyboard needed)
        auto hbMenu = CCMenu::create();
        hbMenu->setPosition({0.f, 0.f});
        auto hbSpr = ButtonSprite::create("HB");
        hbSpr->setScale(0.45f);
        auto hbBtn = CCMenuItemSpriteExtra::create(
            hbSpr, this, menu_selector(HitboxMenu::onHB)
        );
        hbBtn->setPosition({34.f, win.height - 18.f});
        hbMenu->addChild(hbBtn);
        this->addChild(hbMenu);
        return true;
    }

    void updatePct() {
        m_pctLabel->setString(fmt::format("Opacity: {}%", opacityPct()).c_str());
    }

    void onToggle(CCObject* sender) {
        int i = static_cast<CCNode*>(sender)->getTag();
        if (i < 0 || i >= TOGGLE_COUNT) return;
        auto key = TOGGLES[i].key;
        Mod::get()->setSettingValue<bool>(key, !flag(key));
        refreshHitboxes();
    }

    void onOpacity(CCObject* sender) {
        float v = static_cast<SliderThumb*>(sender)->getValue();
        int64_t pct = static_cast<int64_t>(std::round(v * 100.f));
        if (pct < 5) pct = 5;
        Mod::get()->setSettingValue<int64_t>("hitbox-opacity", pct);
        this->updatePct();
        refreshHitboxes();
    }

    void onClose(CCObject*) { setOpen(false); }
    void onHB(CCObject*) { setOpen(!m_open); }

    // make sure the menu is a child of the scene that is on screen right now
    void attachToScene() {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene || this->getParent() == scene) return;
        this->removeFromParentAndCleanup(false);
        scene->addChild(this, 99999);
    }

    void setOpen(bool open) {
        m_open = open;
        log::info("Hitbox menu {}", open ? "opened" : "closed");
        Notification::create(open ? "Hitbox menu opened" : "Hitbox menu closed",
                             NotificationIcon::None, 0.6f)->show();
        this->attachToScene();
        this->setVisible(true);
        auto win = CCDirector::get()->getWinSize();
        m_panel->stopAllActions();
        m_panel->setPositionX(win.width / 2.f);
        float targetY = open ? m_shownY : m_hiddenY;
        if (!m_panel->isRunning()) {   // not in a running scene: no animation possible, jump instead
            m_panel->setPositionY(targetY);
            return;
        }
        auto move = CCMoveTo::create(0.3f, {win.width / 2.f, targetY});
        CCAction* act = open
            ? static_cast<CCAction*>(CCEaseBackOut::create(move))
            : static_cast<CCAction*>(CCEaseBackIn::create(move));
        m_panel->runAction(act);
    }

    static void toggleMenu() {
        if (s_instance) s_instance->setOpen(!s_instance->m_open);
        else log::warn("Hitbox menu not created yet (it is created when the main menu shows)");
    }
};

// create the menu once and keep it alive in every scene
class $modify(HVMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (!HitboxMenu::s_instance) {
            Notification::create("Hitbox Viewer loaded - press Tab or click HB",
                                 NotificationIcon::Success, 2.5f)->show();
            log::info("Hitbox Viewer loaded");
            if (auto menu = HitboxMenu::create()) {
                menu->retain();   // singleton: never freed, so scene changes can't destroy it
                SceneManager::get()->keepAcrossScenes(menu);
            }
        }
        return true;
    }
};

// button on the pause screen - works on every platform, no keyboard needed
class $modify(HVPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        auto spr = ButtonSprite::create("Hitboxes");
        spr->setScale(0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HVPauseLayer::onHitboxes));
        btn->setPosition({win.width - 60.f, 24.f});
        menu->addChild(btn);
        this->addChild(menu, 100);
    }

    void onHitboxes(CCObject*) {
        HitboxMenu::toggleMenu();
    }
};

// =====================================================================================
//  TAB detection (Windows). Polled every frame, so it never depends on a key-event signature.
// =====================================================================================
static bool tabDown() {
#ifdef GEODE_IS_WINDOWS
    // only react while Geometry Dash is the focused window
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    // ignore Alt+Tab / Ctrl+Tab
    if (GetAsyncKeyState(VK_MENU) & 0x8000) return false;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) return false;
    return (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
#else
    return false;
#endif
}

class $modify(HVScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        static bool announced = false;
        if (!announced) { announced = true; log::info("Hitbox Viewer key polling is running"); }
        static bool wasDown = false;
        bool down = tabDown();
        if (down && !wasDown) HitboxMenu::toggleMenu();
        wasDown = down;
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

void refreshHitboxes() {
    if (auto pl = PlayLayer::get()) static_cast<HVPlayLayer*>(pl)->drawHitboxes();
}
