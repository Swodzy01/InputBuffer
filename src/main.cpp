#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <algorithm>
#include <array>
#include <vector>

using namespace geode::prelude;

// Secilebilir buffer degerleri (kare = oyun/fizik adimi)
static constexpr std::array<int, 12> FRAME_OPTIONS = {
	300, 250, 200, 150, 100, 50, 40, 30, 25, 20, 15, 5
};

// Menu kapaliyken de calisan ayarlar
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
// Gizli menu (F6 ile acilir/kapanir)
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
// Buffer mantigi
// Pencere (g_frames adim) icinde erken basarsan, basis basabildigin
// ILK adima tasinir. Tusu ne kadar basili tuttuysan o kadar basili
// kalir (dash orb icin sart).
// ---------------------------------------------------------------
class $modify(BufferPlayer, PlayerObject) {
	struct Fields {
		int left = 0;          // kalan buffer adimi
		bool held = false;     // tus su an gercekten basili mi
		int heldSteps = 0;     // erken basistan beri kac adim basili kaldi
		int releaseIn = 0;     // uygulanan basis kac adim sonra birakilsin
		int prevRings = 0;     // onceki adimdaki orb sayisi
		bool replaying = false;
	};

	// Basis aninda zaten basabiliyor muyuz? (oyun kendisi halleder)
	bool canActNow() {
		bool onRing = m_touchingRings && m_touchingRings->count() > 0;
		return m_isOnGround || onRing;
	}

	bool pushButton(PlayerButton btn) {
		auto f = m_fields.self();
		if (btn == PlayerButton::Jump && !f->replaying) {
			f->held = true;
			f->releaseIn = 0; // gercek basis geldi, bekleyen sahte birakmayi iptal et
			if (g_enabled && !this->canActNow()) {
				f->left = g_frames;
				f->heldSteps = 0;
				log::debug("[IB] buffer basladi: {} adim", g_frames);
			}
		}
		return PlayerObject::pushButton(btn);
	}

	bool releaseButton(PlayerButton btn) {
		auto f = m_fields.self();
		if (btn == PlayerButton::Jump && !f->replaying) {
			f->held = false;
			f->releaseIn = 0;
		}
		return PlayerObject::releaseButton(btn);
	}

	void update(float dt) {
		PlayerObject::update(dt);

		auto f = m_fields.self();

		// Yeni bir orb'a degdik mi? (orb sayisi arttiysa evet)
		int ringsNow = m_touchingRings ? static_cast<int>(m_touchingRings->count()) : 0;
		bool newRing = ringsNow > f->prevRings;
		f->prevRings = ringsNow;

		if (!g_enabled) {
			f->left = 0;
			f->releaseIn = 0;
			return;
		}

		// Sahte basisi, tusu tuttugun sure kadar sonra birak
		if (f->releaseIn > 0) {
			f->releaseIn--;
			if (f->releaseIn == 0 && !f->held) {
				f->replaying = true;
				PlayerObject::releaseButton(PlayerButton::Jump);
				f->replaying = false;
			}
		}

		if (f->left > 0) {
			if (f->held) f->heldSteps++;
			f->left--;

			if (m_isOnGround || newRing) {
				log::debug("[IB] basis uygulandi (kalan: {}, hold: {})", f->left, f->heldSteps);
				f->left = 0;
				f->replaying = true;
				PlayerObject::pushButton(PlayerButton::Jump);
				f->replaying = false;
				// Tus zaten birakildiysa, ayni sure basili tutup birak
				if (!f->held) {
					f->releaseIn = std::max(f->heldSteps, 1);
				}
			}
		}
	}

	void resetObject() {
		PlayerObject::resetObject();
		m_fields->left = 0;
		m_fields->releaseIn = 0;
		m_fields->heldSteps = 0;
		m_fields->prevRings = 0;
	}
};
