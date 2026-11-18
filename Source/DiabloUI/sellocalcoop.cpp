/**
 * @file sellocalcoop.cpp
 *
 * Local co-op lobby: select seat count and a hero per seat, then start.
 */
#include "DiabloUI/sellocalcoop.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/hero/selhero.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "control/control.hpp"
#include "engine/assets.hpp"
#include "engine/point.hpp"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "utils/language.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

bool selcoop_endMenu = false;
int selcoop_seats = 2;
uint32_t selcoop_saveNumbers[4] = {};

std::vector<std::unique_ptr<UiItemBase>> vecSelCoopDialog;
std::vector<std::unique_ptr<UiListItem>> vecSelCoopItems;

void SelcoopFree()
{
	ArtBackground = std::nullopt;
	vecSelCoopDialog.clear();
	vecSelCoopItems.clear();
}

void SelcoopSelect(size_t value)
{
	if (value < vecSelCoopItems.size()) {
		selcoop_seats = vecSelCoopItems[value]->m_value;
	}
	selcoop_endMenu = true;
}

void SelcoopEsc()
{
	selcoop_seats = 0; // signals cancel
	selcoop_endMenu = true;
}

bool SelcoopSelectSeats()
{
	selcoop_endMenu = false;

	LoadBackgroundArt("ui_art\\selgame");
	UiAddBackground(&vecSelCoopDialog);
	UiAddLogo(&vecSelCoopDialog);

	const Point uiPosition = GetUIRectangle().position;

	SDL_Rect rect = MakeSdlRect(uiPosition.x + 24, uiPosition.y + 161, 590, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtText>(
	    _("Local Co-op: Number of Players").data(), rect,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	vecSelCoopItems.clear();
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("2 Players"), 2));
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("3 Players"), 3));
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("4 Players"), 4));

	rect = MakeSdlRect(uiPosition.x + 265, uiPosition.y + 256, 320, 26);
	vecSelCoopDialog.push_back(std::make_unique<UiList>(
	    vecSelCoopItems, 3, uiPosition.x + 265, uiPosition.y + 256,
	    320, 26, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

	SDL_Rect rectOk = MakeSdlRect(uiPosition.x + 239, uiPosition.y + 390, 120, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtTextButton>(
	    _("OK"), &UiFocusNavigationSelect, rectOk,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	SDL_Rect rectCancel = MakeSdlRect(uiPosition.x + 489, uiPosition.y + 390, 144, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtTextButton>(
	    _("Cancel"), &UiFocusNavigationEsc, rectCancel,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	UiInitList(nullptr, SelcoopSelect, SelcoopEsc, vecSelCoopDialog, true);

	while (!selcoop_endMenu && !UiItemMouseEvents(nullptr, vecSelCoopDialog)) {
		UiClearScreen();
		UiPollAndRender();
	}

	SelcoopFree();
	return selcoop_seats >= 2;
}

} // namespace

bool UiLocalCoopLobbyDialog()
{
	// Step 1: pick seat count
	if (!SelcoopSelectSeats())
		return false; // cancelled

	const int seats = selcoop_seats;

	// Step 2: pick a hero for each seat
	for (int seat = 0; seat < seats; ++seat) {
		_selhero_selections heroResult = SELHERO_NEW_DUNGEON;
		uint32_t saveNumber = 0;

		UiSelHeroMultDialog(
		    pfile_ui_set_hero_infos, // fninfo
		    pfile_ui_save_create,    // fncreate
		    pfile_delete_save,       // fnremove
		    pfile_ui_set_class_stats, // fnstats
		    &heroResult, &saveNumber);

		if (heroResult == SELHERO_PREVIOUS || saveNumber == 0) {
			// User cancelled or didn't select a hero
			return false;
		}

		selcoop_saveNumbers[seat] = saveNumber;
	}

	// Step 3: configure the session
	GetOptions().Gameplay.localCoopPlayers.SetValue(static_cast<LocalCoopPlayers>(seats));
	LoadLocalCoopSettings();
	gbLocalCoopSeats = static_cast<uint8_t>(seats);

	// Store the per-seat save slots so InitLocalCoop loads the right heroes
	for (int seat = 0; seat < seats; ++seat) {
		gbLocalCoopSaveSlot[seat] = selcoop_saveNumbers[seat];
	}

	return true;
}

} // namespace devilution