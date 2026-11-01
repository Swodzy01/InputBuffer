#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <array>
#include <vector>

using namespace geode::prelude;

// Secilebilir buffer degerleri (kare = oyun/fizik adimi)
static constexpr std::array<int, 12> FRAME_OPTIONS = {
	300, 250, 200, 150, 100, 50, 40, 30, 25, 20, 15, 5
};

// Menu kapaliyken de calisan ayarlar (hizli erisim icin static)
static bool g_enabled = true;
static int g_frames = 40;

static void saveSettings() {
	Mod::get()->setSavedValue("enabled", g_enabled);
	Mod::get()->setSavedValue("frames", g_frames);
}

$on_mod(Loaded) {
	g_enabled = Mod::get()->getSavedValue<bool>("enabled", true);
	g_frames = Mod::get()->getSavedValue<int>("frames", 40);
}

// ---------------------------------------------------------------
// Gizli menu (F6 ile acilir/kapanir). Kapaninca sadece pencere
// gider, buffer calismaya devam eder.
// ---------------------------------------------------------------
class BufferPopup : public geode::Popup {
protected:
	std::vector<ButtonSprite*> m_sprites;

	bool init() {
		if (!Popup::init(280.f, 220.f)) return false;
		this->setTitle("Input Buffer");

		auto menu = CCMenu::create();
		menu->setPosition(0.f, 0.f);
		m_mainLayer->addChild(menu);

		float w = m_size.width;
		float h = m_size.height;

		// Aktif/Pasif anahtari
		auto toggler = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(BufferPopup::onToggle), 0.7f
		);
		toggler->toggle(g_enabled);
		toggler->setPosition(w / 2.f - 50.f, h - 55.f);
		menu->addChild(toggler);

		auto label = CCLabelBMFont::create("Aktif", "bigFont.fnt");
		label->setScale(0.5f);
		label->setAnchorPoint({0.f, 0.5f});
		label->setPosition(w / 2.f - 30.f, h - 55.f);
		m_mainLayer->addChild(label);

		// Kare butonlari (4 sutun x 3 satir)
		for (size_t i = 0; i < FRAME_OPTIONS.size(); i++) {
			int frames = FRAME_OPTIONS[i];
			int col = static_cast<int>(i % 4);
			int row = static_cast<int>(i / 4);

			auto spr = ButtonSprite::create(
				std::to_string(frames).c_str(), 45, true,
				"bigFont.fnt", "GJ_button_04.png", 30.f, 0.6f
			);
			m_sprites.push_back(spr);

			auto btn = CCMenuItemSpriteExtra::create(
				spr, this, menu_selector(BufferPopup::onFrames)
			);
			btn->setTag(frames);
			btn->setPosition(
				w / 2.f - 84.f + col * 56.f,
				h - 100.f - row * 40.f
			);
			menu->addChild(btn);
		}

		this->refresh();
		return true;
	}

	void refresh() {
		for (size_t i = 0; i < m_sprites.size(); i++) {
			bool selected = FRAME_OPTIONS[i] == g_frames;
			m_sprites[i]->updateBGImage(
				selected ? "GJ_button_01.png" : "GJ_button_04.png"
			);
		}
	}

	void onFrames(CCObject* sender) {
		g_frames = static_cast<CCNode*>(sender)->getTag();
		saveSettings();
		this->refresh();
	}

	void onToggle(CCObject* sender) {
		// Geode'da callback icinde isToggled() eski degeri verir
		g_enabled = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
		saveSettings();
	}

public:
	static inline BufferPopup* s_instance = nullptr;

	static BufferPopup* create() {
		auto ret = new BufferPopup();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}

	static void toggleMenu() {
		if (s_instance) {
			s_instance->onClose(nullptr);
			return;
		}
		if (auto p = BufferPopup::create()) {
			s_instance = p;
			p->show();
		}
	}

	~BufferPopup() {
		if (s_instance == this) s_instance = nullptr;
	}
};

// F6 tusu: menuyu ac/kapat
class $modify(BufferKeys, CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
		if (down && !repeat && key == KEY_F6) {
			BufferPopup::toggleMenu();
			return true;
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
	}
};

// ---------------------------------------------------------------
// Buffer mantigi: zipla tusuna erken basilirsa, N kare boyunca
// hatirla ve oyuncu yere inince / orb'a degince uygula.
// ---------------------------------------------------------------
class $modify(BufferPlayer, PlayerObject) {
	struct Fields {
		int left = 0;          // kalan buffer kare sayisi
		bool held = false;     // tus su an gercekten basili mi
		bool replaying = false;
	};

	bool canAct() {
		bool onRing = m_touchingRings && m_touchingRings->count() > 0;
		return m_isOnGround || onRing;
	}

	bool pushButton(PlayerButton btn) {
		auto f = m_fields.self();
		if (btn == PlayerButton::Jump && !f->replaying) {
			f->held = true;
			// Su an zaten zıplayabiliyorsa oyun kendisi halleder
			if (g_enabled && !this->canAct()) {
				f->left = g_frames;
			}
		}
		return PlayerObject::pushButton(btn);
	}

	bool releaseButton(PlayerButton btn) {
		auto f = m_fields.self();
		if (btn == PlayerButton::Jump && !f->replaying) {
			f->held = false; // buffer kaydi kalir, erken birakma sorun degil
		}
		return PlayerObject::releaseButton(btn);
	}

	void update(float dt) {
		PlayerObject::update(dt);

		auto f = m_fields.self();
		if (!g_enabled || f->left <= 0) return;

		f->left--;
		if (this->canAct()) {
			f->left = 0;
			f->replaying = true;
			PlayerObject::pushButton(PlayerButton::Jump);
			if (!f->held) {
				PlayerObject::releaseButton(PlayerButton::Jump);
			}
			f->replaying = false;
		}
	}

	void resetObject() {
		PlayerObject::resetObject();
		m_fields->left = 0;
	}
};
