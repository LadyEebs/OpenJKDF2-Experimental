#include "Gui/jkGUIDisplay.h"

#include "General/stdBitmap.h"
#include "General/stdFont.h"
#include "General/stdString.h"
#include "Engine/rdMaterial.h" // TODO move stdVBuffer
#include "stdPlatform.h"
#include "jk.h"
#include "Gui/jkGUIRend.h"
#include "Gui/jkGUI.h"
#include "Gui/jkGUISetup.h"
#include "World/jkPlayer.h"
#include "Win95/Window.h"
#include "Platform/std3D.h"
#include "General/stdMath.h"

#include "jk.h"

#ifndef TILE_SW_RASTER

#if defined(RENDER_DROID2)
	#define NEW_DISPLAY_GUI
#endif

enum jkGuiDecisionButton_t
{
    GUI_GENERAL = 100,
    GUI_GAMEPLAY = 101,
    GUI_DISPLAY = 102,
    GUI_SOUND = 103,
    GUI_CONTROLS = 104,

    GUI_ADVANCED = 105,
};

enum jkGuiElementIds_t
{
	ID_GUI_SETUP = 1,
	ID_GUI_GENERAL,
	ID_GUI_GAMEPLAY,
	ID_GUI_DISPLAY,
	ID_GUI_SOUND,  
	ID_GUI_CONTROLS,
	ID_GUI_OK,
	ID_GUI_CANCEL,

	ID_GUI_FOV,
	ID_GUI_FOV_SLIDER,
	ID_GUI_FOV_TEXT,

#ifndef NEW_DISPLAY_GUI
	ID_GUI_FOV_VERTICAL,
#endif
	ID_GUI_FULLSCREEN,
	ID_GUI_HIDPI,
	ID_GUI_TEXTURE_FILTERING,
#ifndef NEW_DISPLAY_GUI
	ID_GUI_SQUARE_ASPECT,
#endif

	ID_GUI_VSYNC,
	ID_GUI_SSAA,
	ID_GUI_SSAA_TEXT,
	ID_GUI_GAMMA,
	ID_GUI_GAMMA_TEXT,

	ID_GUI_COLOR_DEPTH,
	ID_GUI_COLOR_DEPTH_TEXT,
	ID_GUI_COLOR_DEPTH_LARROW,
	ID_GUI_COLOR_DEPTH_RARROW,

	ID_GUI_SAMPLES,
	ID_GUI_SAMPLES_TEXT,
	ID_GUI_SAMPLES_LARROW,
	ID_GUI_SAMPLES_RARROW,

	ID_GUI_DITHER,

#ifndef NEW_DISPLAY_GUI
	ID_GUI_HUD_SCALE,
	ID_GUI_HUD_SCALE_VALUE,
#endif

};

static wchar_t render_level[256] = {0};
static wchar_t gamma_level[256] = {0};
static wchar_t hud_level[256] = {0};

static wchar_t slider_val_text[5] = {0};
static wchar_t slider_val_text_2[5] = {0};

static int slider_images[2] = {JKGUI_BM_SLIDER_BACK, JKGUI_BM_SLIDER_THUMB};

static wchar_t colordepth_text[8];
static wchar_t samples_text[16];

void jkGuiDisplay_FovDraw(jkGuiElement *element, jkGuiMenu *menu, stdVBuffer *vbuf, int redraw);
void jkGuiDisplay_FramelimitDraw(jkGuiElement *element, jkGuiMenu *menu, stdVBuffer *vbuf, int redraw);
int jkGuiDisplay_ColorDepthArrowButtonClickHandler(jkGuiElement* pElement, jkGuiMenu* pMenu, int mouseX, int mouseY, BOOL a5);
int jkGuiDisplay_SamplesArrowButtonClickHandler(jkGuiElement* pElement, jkGuiMenu* pMenu, int mouseX, int mouseY, BOOL a5);

static jkGuiElement jkGuiDisplay_aElements[] = { 
    { ELEMENT_TEXT,        0,            0, NULL,                   3, {0, 410, 640, 20},   1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXT,        0,            6, "GUI_SETUP",            3, {20, 20, 600, 40},   1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_GENERAL,  2, "GUI_GENERAL",          3, {20, 80, 120, 40},   1, 0, "GUI_GENERAL_HINT",          0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_GAMEPLAY, 2, "GUI_GAMEPLAY",         3, {140, 80, 120, 40},  1, 0, "GUI_GAMEPLAY_HINT",         0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_DISPLAY,  2, "GUI_DISPLAY",          3, {260, 80, 120, 40},  1, 0, "GUI_DISPLAY_HINT",          0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_SOUND,    2, "GUI_SOUND",            3, {380, 80, 120, 40},  1, 0, "GUI_SOUND_HINT",            0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_CONTROLS, 2, "GUI_CONTROLS",         3, {500, 80, 120, 40},  1, 0, "GUI_CONTROLS_HINT",         0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  1,            2, "GUI_OK",               3, {440, 430, 200, 40}, 1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON, -1,            2, "GUI_CANCEL",           3, {0, 430, 200, 40},   1, 0, NULL,                        0, 0, 0, {0}, 0},

    // 9
    {ELEMENT_TEXT,         0,            0, "GUIEXT_FOV",                 3, {20, 130, 300, 30}, 1,  0, 0, 0, 0, 0, {0}, 0},
    {ELEMENT_SLIDER,       0,            0, (const char*)(FOV_MAX - FOV_MIN),                    0, {10, 160, 320, 30}, 1, 0, "GUIEXT_FOV_HINT", jkGuiDisplay_FovDraw, 0, slider_images, {0}, 0},
    {ELEMENT_TEXT,         0,            0, slider_val_text,        3, {20, 190, 300, 30}, 1,  0, 0, 0, 0, 0, {0}, 0},
#ifndef NEW_DISPLAY_GUI
	{ELEMENT_CHECKBOX,     0,            0, "GUIEXT_FOV_VERTICAL",    0, {20, 210, 200, 40}, 1,  0, NULL, 0, 0, 0, {0}, 0},
#endif
	{ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_FULLSCREEN",    0, {400, 250, 200, 30}, 1,  0, NULL, 0, 0, 0, {0}, 0},
    {ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_HIDPI",    0, {400, 310, 200, 30}, 1,  0, NULL, 0, 0, 0, {0}, 0},
    {ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_TEXTURE_FILTERING",    0, {400, 340, 200, 30}, 1,  0, NULL, 0, 0, 0, {0}, 0},
#ifndef NEW_DISPLAY_GUI
    {ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_SQUARE_ASPECT",    0, {20, 240, 300, 40}, 1,  0, NULL, 0, 0, 0, {0}, 0},
#endif

    // 17
    {ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_VSYNC",    0, {400, 280, 300, 30}, 1,  0, NULL, 0, 0, 0, {0}, 0},

    // 18
    { ELEMENT_TEXT,        0,            0, "GUIEXT_SSAA_MULT",            2, {20, 320, 140, 20},   1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBOX,      0,            0, NULL,    100, {170, 320, 80, 20}, 1,  0, NULL, 0, 0, 0, {0}, 0},
    
    // 20
    { ELEMENT_TEXT,        0,            0, "GUIEXT_GAMMA_VAL",            2, {20, 350, 140, 20},   1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBOX,      0,            0, NULL,    100, {170, 350, 80, 20}, 1,  0, NULL, 0, 0, 0, {0}, 0},

	// 22
	{ ELEMENT_TEXT,        0,            0, "GUIEXT_EN_COLORDEPTH",  3,  { 360, 160, 120, 25}, 1,  0, 0, 0, 0, 0, {0}, 0},
	{ ELEMENT_TEXT,        0,            0, NULL,                    3,  { 506, 160, 78, 30 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
	{ ELEMENT_PICBUTTON, 103,            0, NULL,                    33, { 480, 160, 24, 24 }, 1, 0, NULL, NULL, jkGuiDisplay_ColorDepthArrowButtonClickHandler, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
	{ ELEMENT_PICBUTTON, 104,            0, NULL,                    34, { 584, 160, 24, 24 }, 1, 0, NULL, NULL, jkGuiDisplay_ColorDepthArrowButtonClickHandler, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },

	// 26
	{ ELEMENT_TEXT,        0,            0, "GUIEXT_EN_SAMPLES",     3,  { 360, 190, 120, 25}, 1,  0, 0, 0, 0, 0, {0}, 0},
	{ ELEMENT_TEXT,        0,            0, NULL,                    3,  { 506, 190, 78, 30 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
	{ ELEMENT_PICBUTTON, 103,            0, NULL,                    33, { 480, 190, 24, 24 }, 1, 0, NULL, NULL, jkGuiDisplay_SamplesArrowButtonClickHandler, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
	{ ELEMENT_PICBUTTON, 104,            0, NULL,                    34, { 584, 190, 24, 24 }, 1, 0, NULL, NULL, jkGuiDisplay_SamplesArrowButtonClickHandler, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },

	// 30
	{ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_DITHER",    0, {400, 220, 200, 30}, 1,  0, NULL, 0, 0, 0, {0}, 0},

#ifndef NEW_DISPLAY_GUI
	// 31
	{ ELEMENT_TEXT,        0,            0, "GUIEXT_HUD_SCALE",            2, {20, 380, 140, 20},   1, 0, NULL,                        0, 0, 0, {0}, 0},
	{ ELEMENT_TEXTBOX,      0,            0, NULL,    100, {170, 380, 80, 20}, 1,  0, NULL, 0, 0, 0, {0}, 0},
#endif

    { ELEMENT_TEXTBUTTON,  GUI_ADVANCED, 2, "GUI_ADVANCED",               3, {220, 430, 200, 40}, 1, 0, NULL,                        0, 0, 0, {0}, 0},

    { ELEMENT_END,         0,            0, NULL,                   0, {0},                 0, 0, NULL,                        0, 0, 0, {0}, 0},
};

static jkGuiMenu jkGuiDisplay_menu = { jkGuiDisplay_aElements, 0, 0xFF, 0xE1, 0x0F, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, 0, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };

static jkGuiElement jkGuiDisplay_aElementsAdvanced[] = { 
    { ELEMENT_TEXT,        0,            0, NULL,                   3, {0, 410, 640, 20},   1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXT,        0,            6, "GUI_SETUP",            3, {20, 20, 600, 40},   1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_GENERAL,  2, "GUI_GENERAL",          3, {20, 80, 120, 40},   1, 0, "GUI_GENERAL_HINT",          0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_GAMEPLAY, 2, "GUI_GAMEPLAY",         3, {140, 80, 120, 40},  1, 0, "GUI_GAMEPLAY_HINT",         0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_DISPLAY,  2, "GUI_DISPLAY",          3, {260, 80, 120, 40},  1, 0, "GUI_DISPLAY_HINT",          0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_SOUND,    2, "GUI_SOUND",            3, {380, 80, 120, 40},  1, 0, "GUI_SOUND_HINT",            0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON,  GUI_CONTROLS, 2, "GUI_CONTROLS",         3, {500, 80, 120, 40},  1, 0, "GUI_CONTROLS_HINT",         0, 0, 0, {0}, 0},
    
    { ELEMENT_TEXTBUTTON,  1,            2, "GUI_OK",               3, {440, 430, 200, 40}, 1, 0, NULL,                        0, 0, 0, {0}, 0},
    { ELEMENT_TEXTBUTTON, -1,            2, "GUI_CANCEL",           3, {0, 430, 200, 40},   1, 0, NULL,                        0, 0, 0, {0}, 0},
  
    { ELEMENT_CHECKBOX,    0,            0, "GUIEXT_EN_JKGFXMOD",            0, {20, 150, 250, 40},  1, 0, "GUIEXT_EN_JKGFXMOD_HINT",          0, 0, 0, {0}, 0},
    { ELEMENT_CHECKBOX,    0,            0, "GUIEXT_EN_TEXTURE_PRECACHE",   0, {20, 190, 250, 40},  1, 0, "GUIEXT_EN_TEXTURE_PRECACHE_HINT",          0, 0, 0, {0}, 0},

	 // 11
	{ELEMENT_TEXT,         0,            0, "GUIEXT_FPS_LIMIT",                 3, {300, 150, 300, 30}, 1,  0, 0, 0, 0, 0, {0}, 0},
	{ELEMENT_SLIDER,       0,            0, (const char*)(FPS_LIMIT_MAX - FPS_LIMIT_MIN),                    0, {290, 180, 320, 30}, 1, 0, "GUIEXT_FPS_LIMIT_HINT", jkGuiDisplay_FramelimitDraw, 0, slider_images, {0}, 0},
	{ELEMENT_TEXT,         0,            0, slider_val_text_2,        3, {300, 210, 300, 30}, 1,  0, 0, 0, 0, 0, {0}, 0},

	// 14
	{ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_BLOOM",    0, {20, 230, 300, 40}, 1,  0, NULL, 0, 0, 0, {0}, 0},

	// 15
	{ELEMENT_CHECKBOX,     0,            0, "GUIEXT_EN_SSAO",    0, {20, 270, 300, 40}, 1,  0, NULL, 0, 0, 0, {0}, 0},

#if defined(SPHERE_AO) || defined(RENDER_DROID2)
	// 16
	{ ELEMENT_CHECKBOX,     0,           0, "GUIEXT_EN_SHADOWS",    0, {20, 310, 300, 40}, 1,  0, NULL, 0, 0, 0, {0}, 0},
#endif

#if defined(DECAL_RENDERING) || defined(RENDER_DROID2)
	// 17
	{ ELEMENT_CHECKBOX,     0,           0, "GUIEXT_EN_DECALS",    0, {20, 350, 300, 40}, 1,  0, NULL, 0, 0, 0, {0}, 0},
#endif

    { ELEMENT_END,         0,            0, NULL,                   0, {0},                 0, 0, NULL,                        0, 0, 0, {0}, 0},
};

static jkGuiMenu jkGuiDisplay_menuAdvanced = { jkGuiDisplay_aElementsAdvanced, 0, 0xFF, 0xE1, 0x0F, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, 0, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };


void jkGuiDisplay_UpdateSampleText()
{
	if (jkPlayer_multiSample == 0)
		jk_snwprintf(samples_text, 16u, L"None");
	else if (jkPlayer_multiSample < 0)
		jk_snwprintf(samples_text, 16u, jkPlayer_multiSample == SAMPLE_2x1 ? L"2x1 LRS" : L"2x2 LRS");
	else
		jk_snwprintf(samples_text, 16u, L"%dx MSAA", jkPlayer_multiSample << 1);

	jkGuiDisplay_aElements[ID_GUI_SAMPLES_TEXT].wstr = samples_text;
}

void jkGuiDisplay_Startup()
{
#ifdef MENU_16BIT
	jkGui_InitMenu(&jkGuiDisplay_menu, jkGui_stdBitmaps[JKGUI_BM_BK_SETUP], jkGui_stdBitmaps16[JKGUI_BM_BK_SETUP]);
	jkGui_InitMenu(&jkGuiDisplay_menuAdvanced, jkGui_stdBitmaps[JKGUI_BM_BK_SETUP], jkGui_stdBitmaps16[JKGUI_BM_BK_SETUP]);
#else
    jkGui_InitMenu(&jkGuiDisplay_menu, jkGui_stdBitmaps[JKGUI_BM_BK_SETUP]);
    jkGui_InitMenu(&jkGuiDisplay_menuAdvanced, jkGui_stdBitmaps[JKGUI_BM_BK_SETUP]);
#endif
    jkGuiDisplay_aElements[ID_GUI_SSAA_TEXT].wstr = render_level;

    jkGuiDisplay_aElements[ID_GUI_GAMMA_TEXT].wstr = gamma_level;

#ifndef NEW_DISPLAY_GUI
	jkGuiDisplay_aElements[ID_GUI_HUD_VALUE].wstr = hud_level;
#endif

	jk_snwprintf(colordepth_text, 8u, jkPlayer_enable32Bit ? L"32-bit" : L"16-bit");
	jkGuiDisplay_aElements[ID_GUI_COLOR_DEPTH_TEXT].wstr = colordepth_text;

	jkGuiDisplay_UpdateSampleText();

    flex32_t ftmp = jkPlayer_ssaaMultiple;
    jk_snwprintf(render_level, 255, L"%.2f", ftmp);
    ftmp = jkPlayer_gamma;
    jk_snwprintf(gamma_level, 255, L"%.2f", ftmp);
#ifndef NEW_DISPLAY_GUI
	ftmp = jkPlayer_hudScale;
    jk_snwprintf(hud_level, 255, L"%.2f", ftmp);
#endif

#ifdef TILE_SW_RASTER
	// todo: we're really going to want to recreate that original menu to get this working...
	Video_modeStruct.modeIdx = 0;
	Video_modeStruct.descIdx = 6;
#endif
}

void jkGuiDisplay_Shutdown()
{
    ;
}

void jkGuiDisplay_FovDraw(jkGuiElement *element, jkGuiMenu *menu, stdVBuffer *vbuf, int redraw)
{
    uint32_t tmp = FOV_MIN + jkGuiDisplay_aElements[ID_GUI_FOV_SLIDER].selectedTextEntry;
    
    jk_snwprintf(slider_val_text, 5, L"%u", tmp);
    jkGuiDisplay_aElements[ID_GUI_FOV_TEXT].wstr = slider_val_text;
    
    jkGuiRend_SliderDraw(element, menu, vbuf, redraw);
    
    jkGuiRend_UpdateAndDrawClickable(&jkGuiDisplay_aElements[ID_GUI_FOV_TEXT], menu, 1);
}

void jkGuiDisplay_FramelimitDraw(jkGuiElement *element, jkGuiMenu *menu, stdVBuffer *vbuf, int redraw)
{
    uint32_t tmp = FPS_LIMIT_MIN + jkGuiDisplay_aElementsAdvanced[12].selectedTextEntry;
    
    if (tmp)
        jk_snwprintf(slider_val_text_2, 5, L"%u", tmp);
    else
        jk_snwprintf(slider_val_text_2, 5, L"None");

	jkGuiDisplay_aElementsAdvanced[13].wstr = slider_val_text_2;
    
    jkGuiRend_SliderDraw(element, menu, vbuf, redraw);
    
    jkGuiRend_UpdateAndDrawClickable(&jkGuiDisplay_aElementsAdvanced[13], menu, 1);
}

int jkGuiDisplay_ShowAdvanced()
{
    int v0; // esi

    jkGui_sub_412E20(&jkGuiDisplay_menuAdvanced, GUI_DISPLAY, 104, GUI_DISPLAY);
    jkGuiDisplay_aElementsAdvanced[9].selectedTextEntry = jkPlayer_bEnableJkgm;
    jkGuiDisplay_aElementsAdvanced[10].selectedTextEntry = jkPlayer_bEnableTexturePrecache;
    
    jkGuiRend_MenuSetReturnKeyShortcutElement(&jkGuiDisplay_menuAdvanced, &jkGuiDisplay_aElementsAdvanced[7]);
    jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiDisplay_menuAdvanced, &jkGuiDisplay_aElementsAdvanced[8]);
    jkGuiSetup_sub_412EF0(&jkGuiDisplay_menuAdvanced, 0);

	jkGuiDisplay_aElementsAdvanced[12].selectedTextEntry = jkPlayer_fpslimit - FPS_LIMIT_MIN;

	jkGuiDisplay_aElementsAdvanced[14].selectedTextEntry = jkPlayer_enableBloom;
	jkGuiDisplay_aElementsAdvanced[15].selectedTextEntry = jkPlayer_enableSSAO;

	int id = 16;
#if defined(SPHERE_AO) || defined(RENDER_DROID2)
	jkGuiDisplay_aElementsAdvanced[id++].selectedTextEntry = jkPlayer_enableShadows;
#endif

#if defined(DECAL_RENDERING) || defined(RENDER_DROID2)
	jkGuiDisplay_aElementsAdvanced[id++].selectedTextEntry = jkPlayer_enableDecals;
#endif

    while (1)
    {
        v0 = jkGuiRend_DisplayAndReturnClicked(&jkGuiDisplay_menuAdvanced);

        if ( v0 != -1 )
        {
			jkPlayer_fpslimit = FPS_LIMIT_MIN + jkGuiDisplay_aElementsAdvanced[12].selectedTextEntry;

            jkPlayer_bEnableJkgm = jkGuiDisplay_aElementsAdvanced[9].selectedTextEntry;
            jkPlayer_bEnableTexturePrecache = jkGuiDisplay_aElementsAdvanced[10].selectedTextEntry;

			jkPlayer_enableBloom = jkGuiDisplay_aElementsAdvanced[14].selectedTextEntry;
			jkPlayer_enableSSAO = jkGuiDisplay_aElementsAdvanced[15].selectedTextEntry;

			id = 16;
#if defined(SPHERE_AO) || defined(RENDER_DROID2)
			jkPlayer_enableShadows = jkGuiDisplay_aElementsAdvanced[id++].selectedTextEntry;
#endif

#if defined(DECAL_RENDERING) || defined(RENDER_DROID2)
			jkPlayer_enableDecals = jkGuiDisplay_aElementsAdvanced[id++].selectedTextEntry;
#endif

            std3D_PurgeEntireTextureCache();

            jkPlayer_WriteConf(jkPlayer_playerShortName);
        }
        break;
    }
    return v0;
}

int jkGuiDisplay_Show()
{
    flex32_t ftmp;
    int v0; // esi

    jkGui_sub_412E20(&jkGuiDisplay_menu, GUI_DISPLAY, 104, GUI_DISPLAY);
    jkGuiRend_MenuSetReturnKeyShortcutElement(&jkGuiDisplay_menu, &jkGuiDisplay_aElements[ID_GUI_OK]);
    jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiDisplay_menu, &jkGuiDisplay_aElements[ID_GUI_CANCEL]);
    jkGuiSetup_sub_412EF0(&jkGuiDisplay_menu, 0);

    jkGuiDisplay_aElements[ID_GUI_FOV_SLIDER].selectedTextEntry = jkPlayer_fov - FOV_MIN;
#ifndef NEW_DISPLAY_GUI
    jkGuiDisplay_aElements[ID_GUI_FOV_VERTICAL].selectedTextEntry = jkPlayer_fovIsVertical;
	jkGuiDisplay_aElements[ID_GUI_SQUARE_ASPECT].selectedTextEntry = jkPlayer_enableOrigAspect;
#endif
    jkGuiDisplay_aElements[ID_GUI_FULLSCREEN].selectedTextEntry = Window_isFullscreen;
    jkGuiDisplay_aElements[ID_GUI_HIDPI].selectedTextEntry = Window_isHiDpi;
    jkGuiDisplay_aElements[ID_GUI_TEXTURE_FILTERING].selectedTextEntry = jkPlayer_enableTextureFilter;
	jkGuiDisplay_aElements[ID_GUI_DITHER].selectedTextEntry = jkPlayer_enableDithering;

    jkGuiDisplay_aElements[ID_GUI_VSYNC].selectedTextEntry = jkPlayer_enableVsync;

	jk_snwprintf(colordepth_text, 8u, jkPlayer_enable32Bit ? L"32-bit" : L"16-bit");
	jkGuiDisplay_aElements[ID_GUI_COLOR_DEPTH_TEXT].wstr = colordepth_text;

	jkGuiDisplay_UpdateSampleText();
	
    ftmp = jkPlayer_ssaaMultiple;
    jk_snwprintf(render_level, 255, L"%.2f", ftmp);
    ftmp = jkPlayer_gamma;
    jk_snwprintf(gamma_level, 255, L"%.2f", ftmp);

#ifndef NEW_DISPLAY_GUI
	ftmp = jkPlayer_hudScale;
    jk_snwprintf(hud_level, 255, L"%.2f", ftmp);
#endif

continue_menu:
    v0 = jkGuiRend_DisplayAndReturnClicked(&jkGuiDisplay_menu);
    if (v0 == GUI_ADVANCED)
    {
        jkGuiDisplay_ShowAdvanced();
        goto continue_menu;
    }
    else if ( v0 != -1 )
    {
        jkPlayer_fov = FOV_MIN + jkGuiDisplay_aElements[ID_GUI_FOV_SLIDER].selectedTextEntry;
#ifndef NEW_DISPLAY_GUI
		jkPlayer_fovIsVertical = jkGuiDisplay_aElements[ID_GUI_FOV_VERTICAL].selectedTextEntry;
		jkPlayer_enableOrigAspect = jkGuiDisplay_aElements[ID_GUI_SQUARE_ASPECT].selectedTextEntry;
#endif
		Window_SetFullscreen(jkGuiDisplay_aElements[ID_GUI_FULLSCREEN].selectedTextEntry);
        Window_SetHiDpi(jkGuiDisplay_aElements[ID_GUI_HIDPI].selectedTextEntry);
        jkPlayer_enableTextureFilter = jkGuiDisplay_aElements[ID_GUI_TEXTURE_FILTERING].selectedTextEntry;
        jkPlayer_enableVsync = jkGuiDisplay_aElements[ID_GUI_VSYNC].selectedTextEntry;
		jkPlayer_enableDithering = jkGuiDisplay_aElements[ID_GUI_DITHER].selectedTextEntry;

        char tmp[256];
        stdString_WcharToChar(tmp, render_level, 255);

        if(_sscanf(tmp, "%f", &ftmp) != 1) {
            jkPlayer_ssaaMultiple = 1.0;
        }
        else {
            jkPlayer_ssaaMultiple = ftmp;
        }

		jkPlayer_ssaaMultiple = stdMath_Clamp(jkPlayer_ssaaMultiple, 0.125f, 8.0f);
		jk_snwprintf(render_level, 255, L"%.3f", jkPlayer_ssaaMultiple);

        stdString_WcharToChar(tmp, gamma_level, 255);
        if(_sscanf(tmp, "%f", &ftmp) != 1) {
            jkPlayer_gamma = 1.0;
        }
        else {
            jkPlayer_gamma = ftmp;
        }

#ifndef NEW_DISPLAY_GUI
		stdString_WcharToChar(tmp, hud_level, 255);
        if(_sscanf(tmp, "%f", &ftmp) != 1) {
            jkPlayer_hudScale = 1.0;
        }
        else {
            jkPlayer_hudScale = ftmp;
        }

        if (jkPlayer_hudScale > 100.0) {
            jkPlayer_hudScale = 100.0;
        }
#endif

        jkPlayer_WriteConf(jkPlayer_playerShortName);

        // Make sure filter settings get applied
        std3D_UpdateSettings();
    }
    return v0;
}

void jkGuiDisplay_PrecalcViewSizes(){}


int jkGuiDisplay_ColorDepthArrowButtonClickHandler(jkGuiElement* pElement, jkGuiMenu* pMenu, int mouseX, int mouseY, BOOL a5)
{
	if (pElement->hoverId == 103)
	{
		--jkPlayer_enable32Bit;
		if (jkPlayer_enable32Bit < 0)
			jkPlayer_enable32Bit = 1;
	}
	else if (pElement->hoverId == 104)
	{
		++jkPlayer_enable32Bit;
		if (jkPlayer_enable32Bit > 1)
			jkPlayer_enable32Bit = 0;
	}

	jk_snwprintf(colordepth_text, 8u, jkPlayer_enable32Bit ? L"32-bit" : L"16-bit");
	jkGuiDisplay_aElements[ID_GUI_COLOR_DEPTH_TEXT].wstr = colordepth_text;
	jkGuiRend_UpdateAndDrawClickable(&jkGuiDisplay_aElements[ID_GUI_COLOR_DEPTH_TEXT], pMenu, 1);

	return 0;
}

#ifdef SDL2_RENDER
#include "SDL2_helper.h"

int jkGuiDisplay_SamplesArrowButtonClickHandler(jkGuiElement* pElement, jkGuiMenu* pMenu, int mouseX, int mouseY, BOOL a5)
{
	if (pElement->hoverId == 103)
		jkPlayer_multiSample = stdMath_ClampInt(jkPlayer_multiSample - 1, GL_ARB_sample_locations ? SAMPLE_MODE_MIN : SAMPLE_NONE, SAMPLE_MODE_MAX);
	else if (pElement->hoverId == 104)
		jkPlayer_multiSample = stdMath_ClampInt(jkPlayer_multiSample + 1, GL_ARB_sample_locations ? SAMPLE_MODE_MIN : SAMPLE_NONE, SAMPLE_MODE_MAX);

	jkGuiDisplay_UpdateSampleText();
	jkGuiRend_UpdateAndDrawClickable(&jkGuiDisplay_aElements[ID_GUI_SAMPLES_TEXT], pMenu, 1);

	return 0;
}
#endif

#endif
