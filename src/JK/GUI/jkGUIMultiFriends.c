#include "jkGUIMultiFriends.h"

#ifdef PLATFORM_STEAM

#include "General/Darray.h"
#include "General/stdBitmap.h"
#include "General/stdFont.h"
#include "General/stdStrTable.h"
#include "General/stdFileUtil.h"
#include "Engine/rdMaterial.h" // TODO move stdVBuffer
#include "stdPlatform.h"
#include "jk.h"
#include "Gui/jkGUIRend.h"
#include "Gui/jkGUI.h"
#include "Gui/jkGUIDialog.h"
#include "Gameplay/sithPlayer.h"
#include "World/jkPlayer.h"
#include "Main/jkStrings.h"
#include "Win95/stdDisplay.h"
#include "General/stdString.h"
#include "Dss/sithMulti.h"
#include "Win95/stdComm.h"
#include "Modules/std/std3D.h"
#include "General/stdMath.h"

static int listbox_images[2] = { JKGUI_BM_UP_15, JKGUI_BM_DOWN_15 };
static int listbox_images2[2] = { JKGUI_BM_UP_15, JKGUI_BM_DOWN_15 };

static const char* jkGuiMultiFriends_statusStrings[] =
{
	"GUIEXT_OFFLINE",
	"GUIEXT_ONLINE",
	"GUIEXT_BUSY",
	"GUIEXT_AWAY",
	"GUIEXT_SNOOZE"
};

static uint32_t jkGuiMultiFriends_msStart = 0;

static Darray jkGuiMultiFriends_stringArray;

void jkGuiMultiFriends_ListDraw(jkGuiElement* element_, jkGuiMenu* menu, stdVBuffer* vbuf, int redraw);
int jkGuiMultiFriends_ListEventHandler(jkGuiElement* element, jkGuiMenu* menu, int eventType, int eventParam);
int jkGuiMultiFriends_ListClick(jkGuiElement* pElement, jkGuiMenu* pMenu, int mouseX, int mouseY, BOOL redraw);

//typedef struct jkGuiTexInfo
//{
//	int textHeight;
//	int numTextEntries;
//	int maxTextEntries;
//	int textScrollY;
//	int anonymous_18;
//	rdRect rect;
//} jkGuiTexInfo;


enum jkGuiMultiFriendsButton_t
{
	GUI_DONE   = 1,
	GUI_INVITE = 100,
};

enum jkGuiMultiFriendsElement_t
{
	MULTI_FRIENDS_TITLE_TEXT = 1,
	MULTI_FRIENDS_LISTBOX    = 2,
	MULTI_FRIENDS_DONE_BTN   = 3,
	MULTI_FRIENDS_INVITE_BTN = 4,
};

static jkGuiElement jkGuiMultiFriends_buttons[] = {
	{ELEMENT_TEXT,			         0,  0,                   0,  3, {0, 0x19A, 0x280, 20},  1,  0,  0,  0,  0,  0, {0},  0},
	{ELEMENT_TEXT,			         0,  6,     "GUIEXT_INVITE",  3, {20, 20, 0x258, 0x28},  1,  0,  0,  0,  0,  0, {0},  0},
	//{ELEMENT_TEXT,		         0,  2, "GUI_CHOOSEAPROVIDER",  2, {170, 0x82, 0x1D6, 0x28},  1,  0,  0,  0,  0,  0, {0},  0},
	{ELEMENT_LISTBOX,		         1,  0,                   0,  0, { 280, 100, 320, 300 },  1,  0, 0,  jkGuiMultiFriends_ListDraw,  jkGuiMultiFriends_ListClick, listbox_images, { 36, 0, 0, 0, 0, { 0, 0, 0, 0 } },  0, jkGuiMultiFriends_ListEventHandler},
	{ELEMENT_TEXTBUTTON,	GUI_INVITE,  2,     "GUIEXT_INVITE",  3,  { 0, 190, 200, 20 },  1,  0,  0,  0,  0,  0, {0},  0},
	{ELEMENT_TEXTBUTTON,	  GUI_DONE,  2,          "GUI_DONE",  3, {20, 0x1AE, 0xC8, 0x28},  1,  0,  0,  0,  0,  0, {0},  0},
	{ELEMENT_END,			         0,  0,                   0,  0, {0},  0,  0,  0,  0,  0,  0, {0},  0},
};

static jkGuiMenu jkGuiMultiFriends_menu =
{
	jkGuiMultiFriends_buttons, -1, 65535, 65535, 15, NULL, NULL, jkGui_stdBitmaps, jkGui_stdFonts, 0, jkGuiMultiFriends_Refresh, "thermloop01.wav", "thrmlpu2.wav", NULL, NULL, NULL, 0, NULL, NULL
};

int jkGuiMultiFriends_PopulateFriendList(Darray* pDarray, jkGuiElement* pElement)
{
	stdComm_EnumFriends();

	jkGuiRend_DarrayFreeEntry(pDarray);

	int i = 0;
	for (; i < DirectPlay_numFriends; ++i)
	{
		char name[32];
		stdString_WcharToChar(name, DirectPlay_apFriends[i].name, 0x1Fu);
		name[31] = 0;

		jkGuiRend_AddStringEntry(pDarray, name, i);
	}

	jkGuiRend_DarrayReallocStr(pDarray, 0, 0);
	jkGuiRend_SetClickableString(pElement, pDarray);
	return i;
}

int jkGuiMultiFriends_Show(int a1)
{
	jkGuiRend_UpdateAudio();
	jkGui_SetModeMenu(jkGui_stdBitmaps[JKGUI_BM_BK_BUILD_LOAD]->palette);
	jkGuiRend_DarrayNewStr(&jkGuiMultiFriends_stringArray, 5, 1);

	jkGuiMultiFriends_msStart = stdPlatform_GetTimeMsec();
	jkGuiMultiFriends_buttons[MULTI_FRIENDS_LISTBOX].selectedTextEntry = 0;

	int result;
	while (1)
	{
		jkGuiMultiFriends_PopulateFriendList(&jkGuiMultiFriends_stringArray, &jkGuiMultiFriends_buttons[MULTI_FRIENDS_LISTBOX]);
		jkGuiRend_MenuSetReturnKeyShortcutElement(&jkGuiMultiFriends_menu, &jkGuiMultiFriends_buttons[MULTI_FRIENDS_INVITE_BTN]);
		jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiMultiFriends_menu, &jkGuiMultiFriends_buttons[MULTI_FRIENDS_DONE_BTN]);

		result = jkGuiRend_DisplayAndReturnClicked(&jkGuiMultiFriends_menu);
		switch (result)
		{
		case GUI_DONE:
			break;
		case GUI_INVITE:
		{
			int friendIdx = jkGuiMultiFriends_buttons[MULTI_FRIENDS_LISTBOX].selectedTextEntry;
			DirectPlay_apFriends[friendIdx].invited = stdComm_Invite(DirectPlay_apFriends[friendIdx].dpId);
			continue;
		}
		default:
			continue;
		}
		break;
	};

	jkGuiRend_DarrayFree(&jkGuiMultiFriends_stringArray);
	jkGui_SetModeGame();
	return result;
}

void jkGuiMultiFriends_Startup()
{
#ifdef MENU_16BIT
	jkGui_InitMenu(&jkGuiMultiFriends_menu, jkGui_stdBitmaps[JKGUI_BM_BK_BUILD_LOAD], jkGui_stdBitmaps16[JKGUI_BM_BK_BUILD_LOAD]);
#else
	jkGui_InitMenu(&jkGuiMultiFriends_menu, jkGui_stdBitmaps[JKGUI_BM_BK_BUILD_LOAD]);
#endif
}

void jkGuiMultiFriends_Shutdown()
{
#ifdef MENU_16BIT
	jkGuiMultiFriends_menu.bkBm16 = NULL;
#endif
}

void jkGuiMultiFriends_Refresh(jkGuiMenu* pMenu)
{
	int timeDelta = stdPlatform_GetTimeMsec() - jkGuiMultiFriends_msStart;
    if ( timeDelta > 5000 )
    {
		jkGuiMultiFriends_msStart = stdPlatform_GetTimeMsec();
		jkGuiMultiFriends_PopulateFriendList(&jkGuiMultiFriends_stringArray, &jkGuiMultiFriends_buttons[MULTI_FRIENDS_LISTBOX]);
	}
}

extern int jkGuiRend_dword_85620C;

void jkGuiMultiFriends_ListDraw(jkGuiElement* element_, jkGuiMenu* menu, stdVBuffer* vbuf, int redraw)
{
	if (redraw)
		jkGuiRend_CopyVBuffer(menu, &element_->texInfo.rect);

	//if (menu->focusedElement == element_)
	//	jkGuiRend_DrawRect(vbuf, &element_->texInfo.rect, menu->fillColor);
//	jkGuiRend_DrawRect(vbuf, &element_->rect, menu->fillColor);

	jkGuiRend_sub_510C60(element_);
	if (element_->texInfo.numTextEntries > 0)
	{
		stdBitmap* topArrowBitmap = menu->ui_structs[element_->uiBitmaps[0]];
		stdBitmap* bottomArrowBitmap = menu->ui_structs[element_->uiBitmaps[1]];

		int x = element_->rect.x + 6;
		int y = element_->rect.y + 3;
		int element = element_->texInfo.textScrollY + element_->texInfo.maxTextEntries - 1;
		if (element_->texInfo.numTextEntries > element_->texInfo.maxTextEntries)
			element = element_->texInfo.textScrollY + element_->texInfo.maxTextEntries - 3;
		if (element >= element_->texInfo.numTextEntries - 1)
			element = element_->texInfo.numTextEntries - 1;
		if (element_->texInfo.numTextEntries > element_->texInfo.maxTextEntries)
		{
			int mipLevel;
			if (element_->texInfo.textScrollY)
				mipLevel = (jkGuiRend_dword_85620C < 0) + 1;
			else
				mipLevel = 0;

			mipLevel = stdMath_ClampInt(mipLevel, 0, topArrowBitmap->numMips - 1);

			rdRect renderRect; // [esp+20h] [ebp-10h]
			renderRect.y = 0;
			renderRect.x = 0;
			renderRect.width = topArrowBitmap->mipSurfaces[mipLevel]->format.width;
			renderRect.height = topArrowBitmap->mipSurfaces[mipLevel]->format.height;
			int height = element_->texInfo.textHeight - renderRect.height;
			stdDisplay_VBufferCopy(vbuf, topArrowBitmap->mipSurfaces[mipLevel], x + (element_->rect.width - renderRect.width) / 2, y + height / 2, &renderRect, 1);
			y += element_->texInfo.textHeight;
		}
		for (int i = element_->texInfo.textScrollY; i <= element; i++)
		{
			rdRect elementRect;
			elementRect.x = element_->rect.x;
			elementRect.y = y - 7;
			elementRect.width = element_->rect.width;
			elementRect.height = 35;
			jkGuiRend_DrawRect(vbuf, &elementRect, menu->fillColor);

			if (i == element_->selectedTextEntry)
			{
				elementRect.x += 4;
				elementRect.y += 4;
				elementRect.width -= 4;
				elementRect.height -= 4;
				jkGuiRend_DrawRect(vbuf, &element_->texInfo.rect, menu->fillColor);
			}
	 
			//stdBitmap* thumbnail = DirectPlay_apFriends[element_->unistr[i].id].thumbnail;
			rdColor24* pal24 = (rdColor24*)jkGui_stdBitmaps[JKGUI_BM_BK_BUILD_LOAD]->palette;//(rdColor24*)stdDisplay_gammaPalette;

			stdVBuffer* thumbnail = DirectPlay_GetFriendAvatarThumbnail(element_->unistr[i].id, pal24);
			if(thumbnail)
			{
				//float screenW = Video_menuBuffer.format.width;
				//float screenH = Video_menuBuffer.format.height;
				//
				//float menu_w = menu->bkBm16->mipSurfaces[0]->format.width;
				//float menu_h = menu->bkBm16->mipSurfaces[0]->format.height;
				//
				//float menu_x = (menu_w - (menu_h * (screenW / screenH))) / 2.0;
				//menu_w = (menu_h * (screenW / screenH));
				//
				//float scaleX = screenW / menu_w;
				//float scaleY = screenH / menu_h;
				//
				//rdRect rect = { 0, 0, 32, 32 };
				//std3D_DrawUIBitmapZ(thumbnail, 0, menu_x + x, v12, &renderRect, scaleX, scaleY, 0, 0.1);

				stdDisplay_VBufferCopy(vbuf, thumbnail, x, y - 6, NULL, 0);
			}

			stdFont* nameFont = menu->fonts[element_->textType + (i == element_->selectedTextEntry)];
			int textHeight = stdFont_GetHeight(nameFont);

			jkGuiStringEntry* pStringEntry = &element_->unistr[i];
			stdFont_sub_434EC0(
				vbuf,
				nameFont,
				x + 32 + 6,
				y,
				element_->rect.width - 6,
				(int*)menu->paddings,
				pStringEntry->str,
				1);

			const wchar_t* statusText = jkStrings_GetUniStringWithFallback(jkGuiMultiFriends_statusStrings[DirectPlay_apFriends[pStringEntry->id].state]);

			stdFont_sub_434EC0(
				vbuf,
				menu->fonts[0 + (i == element_->selectedTextEntry)],
				x + element_->rect.width - 75 - 6,
				y,
				element_->rect.width - 6,
				(int*)menu->paddings,
				statusText,
				1);

			y += element_->texInfo.textHeight;
		}
		if (element_->texInfo.numTextEntries > element_->texInfo.maxTextEntries)
		{
			int mipLevel;
			if (element == element_->texInfo.numTextEntries - 1)
				mipLevel = 0;
			else
				mipLevel = (jkGuiRend_dword_85620C > 0) + 1;

			mipLevel = stdMath_ClampInt(mipLevel, 0, bottomArrowBitmap->numMips - 1);

			rdRect renderRect;
			renderRect.y = 0;
			renderRect.x = 0;
			renderRect.width = bottomArrowBitmap->mipSurfaces[mipLevel]->format.width;
			renderRect.height = bottomArrowBitmap->mipSurfaces[mipLevel]->format.height;

			int height = element_->texInfo.textHeight - bottomArrowBitmap->mipSurfaces[mipLevel]->format.height;
			stdDisplay_VBufferCopy(vbuf, bottomArrowBitmap->mipSurfaces[mipLevel], x + (element_->rect.width - renderRect.width) / 2, y + height / 2, &renderRect, 1);
		}
	}
}


int jkGuiMultiFriends_ListEventHandler(jkGuiElement* element, jkGuiMenu* menu, int eventType, int eventParam)
{
	signed int result; // eax
	jkGuiElement* element_; // esi
	int v6; // ecx
	int v7; // eax
	int v9; // edx
	int v11; // ebp
	int v12; // ebx
	int selectedIdx; // eax
	int v18; // edx
	signed int v19; // edi
	stdFont** v20; // esi
	int v21; // eax
	int v22; // esi
	int v23; // eax
	rdRect* v24; // eax
	int v25; // edx
	int v26; // edx
	int v27; // esi
	int v28; // eax
	int a1a; // [esp+14h] [ebp+4h]
	int mouseX, mouseY;

	if (eventType == JKGUI_EVENT_INIT)
	{
		element->texInfo.textHeight = 32 + 6;
		v19 = 2;
		v22 = element->texInfo.textHeight;
		v23 = (element->rect.height - 3) / v22;
		element->texInfo.maxTextEntries = v23;
		element->rect.height = v22 * v23 + 6;
		v24 = &element->texInfo.rect;
		v24->x = element->rect.x;
		v24->y = element->rect.y;
		v25 = element->rect.height;
		v24->width = element->rect.width;
		v24->height = v25;
		v26 = element->texInfo.rect.width;
		v27 = element->texInfo.rect.y - 2;
		v24->x = element->texInfo.rect.x - 2;
		v28 = element->texInfo.rect.height;
		element->texInfo.rect.y = v27;
		element->texInfo.rect.height = v28 + 4;
		element->texInfo.rect.width = v26 + 4;
		return 1;
	}
	else if (eventType == JKGUI_EVENT_MOUSEDOWN)
	{
		jkGuiRend_GetMousePos(&mouseX, &mouseY);
		selectedIdx = (mouseY - element->rect.y - 3) / element->texInfo.textHeight;
		if (selectedIdx >= 0)
		{
			if (selectedIdx < element->texInfo.maxTextEntries)
			{
				v18 = selectedIdx + element->texInfo.textScrollY;
				if (element->texInfo.numTextEntries > element->texInfo.maxTextEntries)
				{
					if (!selectedIdx)
					{
						jkGuiRend_ClickableHover(menu, element, -1);
						jkGuiRend_ResetMouseLatestMs();
						return 0;
					}
					if (selectedIdx == element->texInfo.maxTextEntries - 1)
					{
						jkGuiRend_ClickableHover(menu, element, 1);
						jkGuiRend_ResetMouseLatestMs();
						return 0;
					}
					--v18;
				}
				element->selectedTextEntry = v18;
				jkGuiRend_UpdateAndDrawClickable(element, menu, 1);
				jkGuiRend_PlayWav(menu->soundHover);
			}
		}
	}
	else if (eventType == JKGUI_EVENT_KEYDOWN)
	{
		element_ = element;
		v6 = element->selectedTextEntry;
		v7 = element->texInfo.textHeight;
		v9 = v7 * (element->selectedTextEntry - element->texInfo.textScrollY);
		a1a = element->selectedTextEntry;
		v11 = v9 + element->rect.y + 4;
		v12 = element->rect.x + 1;
		if (element_->texInfo.numTextEntries > element_->texInfo.maxTextEntries)
			v11 += v7;
		switch (eventParam)
		{
		case VK_RETURN:
			if (element_->clickHandlerFunc)
				menu->lastClicked = element_->clickHandlerFunc(element_, menu, v12, v11, 1);
			break;
		case VK_ESCAPE:
			if (element_->clickHandlerFunc)
			{
				element_->texInfo.anonymous_18 = 1;
				menu->lastClicked = element_->clickHandlerFunc(element_, menu, v12, v11, 0);
				element_->texInfo.anonymous_18 = 0;
			}
			break;
		case VK_PRIOR:
			jkGuiRend_ClickableHover(menu, element_, -1);
			break;
		case VK_NEXT:
			jkGuiRend_ClickableHover(menu, element_, 1);
			break;
		case VK_UP:
			element_->selectedTextEntry = v6 - 1;
			jkGuiRend_UpdateAndDrawClickable(element_, menu, 1);
			jkGuiRend_PlayWav(menu->soundHover);
			break;
		case VK_DOWN:
			element_->selectedTextEntry = v6 + 1;
			jkGuiRend_UpdateAndDrawClickable(element_, menu, 1);
			jkGuiRend_PlayWav(menu->soundHover);
			break;
		default:
			break;
		}
		if (element_->selectedTextEntry != a1a)
		{
			if (element_->clickHandlerFunc)
			{
				menu->lastClicked = element_->clickHandlerFunc(element_, menu, v12, v11, 0);
				return 0;
			}
		}
	}
	return 0;
}

int jkGuiMultiFriends_ListClick(jkGuiElement* pElement, jkGuiMenu* pMenu, int mouseX, int mouseY, BOOL redraw)
{
	int friendIdx = jkGuiMultiFriends_buttons[MULTI_FRIENDS_LISTBOX].selectedTextEntry;
	jkGuiMultiFriends_buttons[MULTI_FRIENDS_INVITE_BTN].bIsVisible = !DirectPlay_apFriends[friendIdx].invited;
	
	return 0;
}

#endif
