#include "Window.h"

#include "Win95/stdGdi.h"
#include "Platform/std3D.h"
#include "Main/Main.h"
#include "Main/jkMain.h"
#include "Main/jkGame.h"
#include "Gui/jkGUI.h"
#include "Win95/stdDisplay.h"
#include "World/jkPlayer.h"
#include "Platform/stdControl.h"
#include "stdPlatform.h"
#include "Devices/sithConsole.h"
#include "Platform/wuRegistry.h"
#include "Main/jkQuakeConsole.h"
#include "Modules/std/stdProfiler.h"
#include "General/stdColor.h"

#include "jk.h"

#ifdef ARCH_WASM
#include <emscripten.h>
#endif

#ifdef SDL2_RENDER

#include <fcntl.h> 
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif //!_WIN32

#if !defined(WIN64_MINGW) && !defined(_WIN32)
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#else
#include <conio.h>
#endif
//#include <stropts.h>

#include "SDL2_helper.h"

#include <string.h>

//#include <GL/glew.h>
#ifdef MACOS
#include "Platform/macOS/SDL_fix.h"
#else
//#include <GL/gl.h>
#endif
#include "Win95/Video.h"

#if defined(MACOS)
#include <stdbool.h>
#import <Carbon/Carbon.h>
#endif

extern int Window_xPos, Window_yPos;
#endif // SDL2_RENDER

int Window_xSize = WINDOW_DEFAULT_WIDTH;
int Window_ySize = WINDOW_DEFAULT_HEIGHT;
int Window_screenXSize = WINDOW_DEFAULT_WIDTH;
int Window_screenYSize = WINDOW_DEFAULT_HEIGHT;
int Window_isHiDpi = 0;
int Window_isFullscreen = 0;
int Window_needsRecreate = 0;

void Window_UpdateDefaultWindowSize()
{
	Window_xSize = WINDOW_DEFAULT_WIDTH;
	Window_ySize = WINDOW_DEFAULT_HEIGHT;
#ifdef SDL2_RENDER
	SDL_DisplayMode mode;
	SDL_GetDesktopDisplayMode(0, &mode);
	if (mode.w >= 3840) // for 4k, make the default size bigger
	{
		Window_xSize = WINDOW_DEFAULT_WIDTH_4K;
		Window_ySize = WINDOW_DEFAULT_HEIGHT_4K;
	}
#endif
	Window_screenXSize = Window_xSize;
	Window_screenYSize = Window_ySize;
}

void Window_SetHiDpi(int val)
{
    if (Window_isHiDpi != val)
    {
        Window_isHiDpi = val;

        Window_needsRecreate = 1;
    }

    wuRegistry_SaveBool("Window_isHiDpi", Window_isHiDpi);
}

void Window_SetFullscreen(int val)
{
    if (Window_isFullscreen != val)
    {
        // Reset window when exiting fullscreen
        // TODO: Add settings for these sizes maybe?
        if (Window_isFullscreen && !val)
		{
			Window_UpdateDefaultWindowSize();
#ifdef SDL2_RENDER
            Window_xPos = SDL_WINDOWPOS_CENTERED;
            Window_yPos = SDL_WINDOWPOS_CENTERED;
#endif
        }

        Window_isFullscreen = val;
        Window_needsRecreate = 1;
    }

    wuRegistry_SaveBool("Window_isFullscreen", Window_isFullscreen);
    
}

//static wm_handler Window_ext_handlers[16] = {0};

int Window_AddMsgHandler(WindowHandler_t a1)
{
    int i = 0;

    // Added: no duplicates
    for (i = 0; i < 16; i++)
    {
        if (Window_ext_handlers[i].exists && Window_ext_handlers[i].handler == a1)
            return 1;
    }

    for (i = 0; i < 16; i++)
    {
        if ( !Window_ext_handlers[i].exists )
            break;
    }
    
    // Added: no OOB
    if (i >= 16) return 1;

    Window_ext_handlers[i].handler = a1;
    Window_ext_handlers[i].exists = 1;
    ++g_handler_count;
    return 1;
}

int Window_RemoveMsgHandler(WindowHandler_t a1)
{
    int i = 0;

    // Added: the original would still decrement on missing handlers
    for (i = 0; i < 16; i++)
    {
        if ( Window_ext_handlers[i].handler == a1 )
        {
            Window_ext_handlers[i].handler = 0;
            Window_ext_handlers[i].exists = 0;
            g_handler_count -= 1; // doing g_handler_count-- changes behavior???
            return 1;
        }
    }

    return 1;
}

int Window_AddDialogHwnd(HWND a1)
{
    int v1; // eax

    v1 = g_thing_two_some_dialog_count;
    if ( (unsigned int)g_thing_two_some_dialog_count >= 0x10 )
        return 0;
    Window_aDialogHwnds[g_thing_two_some_dialog_count] = a1;
    g_thing_two_some_dialog_count = v1 + 1;
    return 1;
}

#if !defined(SDL2_RENDER) && defined(WIN32)
#define dword_855E98 (*(int*)0x855E98)
#define dword_855DE4 (*(int*)0x855DE4)
#else
static int dword_855E98 = 0;
static int dword_855DE4 = 0;
#endif // SDL2_RENDER

int Window_msg_main_handler(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    int handler_count; // ebx
    struct wm_handler *ext_handler; // esi
    DWORD dwProcessId; // [esp+10h] [ebp-8h] BYREF
    LRESULT v10; // [esp+14h] [ebp-4h] BYREF

    switch ( Msg )
    {
        case WM_CREATE:
            g_app_active = 0;
            g_window_active = 0;
            break;
        case WM_DESTROY:
            g_window_not_destroyed = 0;
            Main_Shutdown();
            break;
        case WM_ACTIVATE:
            if ( (uint16_t)wParam == 2 || (uint16_t)wParam == 1 )// WA_ACTIVE or WA_CLICKACTIVE
            {
                g_window_active = 1;
                if ( dword_855E98 )
                {
                    dword_855E98 = 0;
                    if ( Window_setCooperativeLevel )
                        Window_setCooperativeLevel(0);
                }
#ifdef WIN32_BLOBS
                jk_SetFocus(g_hWnd);
#endif
            }
            else
            {
                if ( dword_855DE4 == 1 && g_window_not_destroyed && g_app_active && !dword_855E98 )
                {
                    dwProcessId = 0;
                    lParam = 1;
                    if ( lParam )
                    {
#ifdef WIN32_BLOBS
                        jk_GetWindowThreadProcessId((HWND)lParam, (LPDWORD)&lParam);
                        jk_GetWindowThreadProcessId(hWnd, &dwProcessId);
#endif
                    }
                    if ( dwProcessId == lParam )
                    {
                        dword_855E98 = 1;
                        if ( Window_drawAndFlip )
                            Window_drawAndFlip(0);
                    }
                }
                g_window_active = 0;
            }
            break;
        case WM_ACTIVATEAPP:
            g_app_active = wParam != 0;
            break;
        default:
            break;
    }

    if ( !g_app_active || (g_app_suspended = 1, !g_window_active) )
        g_app_suspended = 0;
    handler_count = 0;

    if ( g_handler_count <= 0 )
        return Window_DefaultHandler(hWnd, Msg, wParam, lParam, NULL);

    for ( ext_handler = Window_ext_handlers; !ext_handler->exists || !ext_handler->handler(hWnd, Msg, wParam, lParam, &v10); ++ext_handler )
    {
        if ( ++handler_count >= g_handler_count )
            return Window_DefaultHandler(hWnd, Msg, wParam, lParam, NULL);
    }
    return v10;
}

#if !defined(SDL2_RENDER) && defined(WIN32)

int Window_Main(HINSTANCE hInstance, int a2, char *lpCmdLine, int nShowCmd, LPCSTR lpWindowName)
{
    int result;
    WNDCLASSEXA wndClass;
    MSG msg;

    g_handler_count = 0;
    g_thing_two_some_dialog_count = 0;
    g_should_exit = 0;
    g_window_not_destroyed = 0;
    g_hInstance = hInstance;
    g_nShowCmd = nShowCmd;

    wndClass.cbSize = 48;
    wndClass.hInstance = hInstance;
    wndClass.lpszClassName = "wKernel";
    wndClass.lpszMenuName = 0;
    wndClass.lpfnWndProc = Window_msg_main_handler;
    wndClass.style = 3;
    wndClass.hIcon = jk_LoadIconA(hInstance, "APPICON");
    if ( !wndClass.hIcon )
        wndClass.hIcon = jk_LoadIconA(0, (void*)32512);
    wndClass.hIconSm = jk_LoadIconA(hInstance, "APPICON");
    if ( !wndClass.hIconSm )
        wndClass.hIconSm = jk_LoadIconA(0, (void*)32512);
    wndClass.hCursor = jk_LoadCursorA(0, (void*)0x7F00);
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hbrBackground = jk_GetStockObject(4);

    if (jk_RegisterClassExA(&wndClass))
    {
        if ( jk_FindWindowA("wKernel", lpWindowName) )
            jk_exit(-1);

        uint32_t hres = jk_GetSystemMetrics(1);
        uint32_t vres = jk_GetSystemMetrics(0);
        g_hWnd = jk_CreateWindowExA(0x40000u, "wKernel", lpWindowName, 0x90000000, 0, 0, vres, hres, 0, 0, hInstance, 0);

        if (g_hWnd)
        {
            g_hInstance = hInstance;
            jk_ShowWindow(g_hWnd, 1);
            jk_UpdateWindow(g_hWnd);
        }
    }

    stdGdi_SetHwnd(g_hWnd);
    stdGdi_SetHInstance(g_hInstance);
    jk_InitCommonControls();

    g_855E8C = 2 * jk_GetSystemMetrics(32);
    uint32_t metrics_32 = jk_GetSystemMetrics(32);
    g_855E90 = jk_GetSystemMetrics(15) + 2 * metrics_32;
    result = Main_Startup(lpCmdLine);

    if (!result) return result;

    
    g_window_not_destroyed = 1;

    while (1)
    {
        if (jk_PeekMessageA(&msg, 0, 0, 0, 0))
        {
            if (!jk_GetMessageA(&msg, 0, 0, 0))
            {
                result = msg.wParam;
                g_should_exit = 1;
                break;
            }

            uint32_t some_cnt = 0;
            if (g_thing_two_some_dialog_count > 0)
            {
#if 0
                v16 = &thing_three;
                do
                {
                    //TODO if ( jk_IsDialogMessageA(*v16, &msg) )
                    //  break;
                    ++some_cnt;
                    ++v16;
                }
                while ( some_cnt < g_thing_two_some_dialog_count );
#endif
            }

            if (some_cnt == g_thing_two_some_dialog_count)
            {
                jk_TranslateMessage(&msg);
                jk_DispatchMessageA(&msg);
            }

            if (!jk_PeekMessageA(&msg, 0, 0, 0, 0))
            {
                result = 0;
                if ( g_should_exit )
                    return result;
            }
        }

        //if (user32->stopping) break;

        jkMain_GuiAdvance();
    }

    return result;
}

int Window_DefaultHandler(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, void* unused)
{
    return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

#endif

#ifdef SDL2_RENDER

SDL_Window* displayWindow = NULL;
SDL_Event event;
SDL_GLContext glWindowContext;

int Window_lastXRel = 0;
int Window_lastYRel = 0;
int Window_lastSampleTime = 0;
int Window_lastSampleMs = 0;
int Window_bMouseLeft = 0;
int Window_bMouseRight = 0;
int Window_resized = 0;
int Window_mouseX = 0;
int Window_mouseY = 0;
int Window_mouseWheelX = 0;
int Window_mouseWheelY = 0;
int Window_lastMouseX = 0;
int Window_lastMouseY = 0;
int Window_xPos = SDL_WINDOWPOS_CENTERED;
int Window_yPos = SDL_WINDOWPOS_CENTERED;
int last_jkGame_isDDraw = 0;
int last_jkQuakeConsole_bOpen = 0;
int Window_menu_mouseX = 0;
int Window_menu_mouseY = 0;

int Window_GL4 = 0;

extern int jkGuiBuildMulti_bRendering;

void Window_HandleMouseMove(SDL_MouseMotionEvent *event)
{
    int x = event->x;
    int y = event->y;

    Window_lastMouseX = Window_mouseX;
    Window_lastMouseY = Window_mouseY;

    if (!jkGame_isDDraw)
    {
        // FLEXTODO
        flex_t fX = (flex_t)x;
        flex_t fY = (flex_t)y;

        // Keep 4:3 aspect
        flex_t menu_x = ((flex_t)Window_screenXSize - ((flex_t)Window_screenYSize * (640.0 / 480.0))) / 2.0;
        flex_t menu_w = ((flex_t)Window_screenYSize * (640.0 / 480.0));

        Window_mouseX = (int)(((fX - menu_x) / (flex_t)menu_w) * 640.0);
        Window_mouseY = (int)((fY / (flex_t)Window_screenYSize) * 480.0);
        //printf("%d %d\n", Window_mouseX, Window_mouseY);
    }
    else
    {
        Window_mouseX = x;
        Window_mouseY = y;// - (Window_ySize - 480);
    }

    if (Window_mouseX < 0)
        Window_mouseX = 0;

    if (jkQuakeConsole_bOpen) return; // Hijack all input to console

    uint32_t pos = ((Window_mouseX) & 0xFFFF) | (((Window_mouseY) << 16) & 0xFFFF0000);
    
    Window_lastSampleMs = event->timestamp - Window_lastSampleTime;
    //Window_lastSampleTime = event->timestamp;
    Window_lastXRel += event->xrel;
    Window_lastYRel += event->yrel;

    Window_msg_main_handler(g_hWnd, WM_MOUSEMOVE, 0, pos);
}

void Window_HandleWindowEvent(SDL_Event* event)
{
    switch (event->window.event) 
    {
        case SDL_WINDOWEVENT_SHOWN:
#ifdef MACOS
            {
                static int bMacosOnlyOncePerProcessLifetimeTriggerTheStupidDylibLoad = 0;
                if (!bMacosOnlyOncePerProcessLifetimeTriggerTheStupidDylibLoad)
                {
                    CGEventRef ref = CGEventCreateKeyboardEvent(NULL, 0x72 /* help */, 1);
                    CGEventSetFlags( ref, kCGEventFlagMaskNumericPad );
                    CGEventSetFlags( ref, kCGEventFlagMaskSecondaryFn );
                    CGEventPost(kCGHIDEventTap, ref);
                    CFRelease(ref);
                    bMacosOnlyOncePerProcessLifetimeTriggerTheStupidDylibLoad = 1;
                }
            }
#endif
            //printf("Window %d shown", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_HIDDEN:
            //printf("Window %d hidden", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_EXPOSED:
            //printf("Window %d exposed", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_MOVED:
            /*printf("Window %d moved to %d,%d",
                    event->window.windowID, event->window.data1,
                    event->window.data2);*/
            Window_xPos = event->window.data1;
            Window_yPos = event->window.data2;
            break;
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            if (Window_xSize != event->window.data1 || Window_ySize != event->window.data2)
                Window_resized = 1;

            //Window_xSize = event->window.data1;
            //Window_ySize = event->window.data2;
#ifndef TILE_SW_RASTER
            SDL_GL_GetDrawableSize(displayWindow, &Window_xSize, &Window_ySize);
#endif
            SDL_GetWindowSize(displayWindow, &Window_screenXSize, &Window_screenYSize);

            if (Window_xSize < 640) Window_xSize = 640;
            if (Window_ySize < 480) Window_ySize = 480;
            //printf("%u %u\n", Window_xSize, Window_ySize);
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
            //printf("Window %d minimized", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_MAXIMIZED:
            //printf("Window %d maximized", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_RESTORED:
            //printf("Window %d restored", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_ENTER:
            //printf("Mouse entered window %d\n", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_LEAVE:
            //printf("Mouse left window %d\n", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            //printf("Window %d gained keyboard focus", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            //printf("Window %d lost keyboard focus", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_CLOSE:
            //printf("Window %d closed", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            //printf("Window %d is offered a focus", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_HIT_TEST:
            //printf("Window %d has a special hit test", event->window.windowID);
            break;
    }
}

#if defined(WIN64_MINGW) || defined(_WIN32)
CHAR my_getch() {
    DWORD mode, cc;
    DWORD num;
    INPUT_RECORD irInBuf[1];
    HANDLE h = GetStdHandle( STD_INPUT_HANDLE );

    if (h == NULL) {
        return 0; // console not found
    }

    GetConsoleMode( h, &mode );
    SetConsoleMode( h, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT) );
    TCHAR c = 0;
    GetNumberOfConsoleInputEvents(h, &num);
    if (num)
    {
        if (!ReadConsoleInput(
            h,      // input buffer handle 
            irInBuf,     // buffer to read into 
            1,         // size of read buffer 
            &num))
        {

        }
        else
        {
            if (irInBuf[0].EventType == KEY_EVENT && irInBuf[0].Event.KeyEvent.bKeyDown) {
                c = irInBuf[0].Event.KeyEvent.uChar.AsciiChar;
            }
        }
    }
    SetConsoleMode( h, mode );
    return c;
}

int my_kbhit() {
    DWORD num;
    DWORD mode, cc;
    HANDLE h = GetStdHandle( STD_INPUT_HANDLE );
    if (h == NULL) {
        return 0; // console not found
    }

    GetConsoleMode( h, &mode );
    SetConsoleMode( h, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT) );
    
    GetNumberOfConsoleInputEvents(h, &num);
    SetConsoleMode( h, mode );

    return num;
}
#else
int my_kbhit() {
    static const int STDIN = 0;
    static int initialized = 0;

    if (! initialized) {
        // Use termios to turn off line buffering
        struct termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        term.c_lflag &= ~ECHO;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = 1;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}
#endif

static char Window_headlessBuffer[256];

#ifdef TILE_SW_RASTER

#define USE_JOBS

static ALIGNED_(16) uint32_t paletteCache[256];

// no palette branch check version
inline Uint32 SDL_MapRGBFast(const SDL_PixelFormat* format, Uint8 r, Uint8 g, Uint8 b)
{
	return  (r >> format->Rloss) << format->Rshift
		| (g >> format->Gloss) << format->Gshift
		| (b >> format->Bloss) << format->Bshift | format->Amask;
}

#ifdef TARGET_SSE
__m128i SDL_MapRGBFastSIMD(const SDL_PixelFormat* fmt, __m128i r, __m128i g, __m128i b) {
	// Right shift by loss bits
	r = _mm_srli_epi32(r, fmt->Rloss);
	g = _mm_srli_epi32(g, fmt->Gloss);
	b = _mm_srli_epi32(b, fmt->Bloss);

	// Shift to correct position
	r = _mm_slli_epi32(r, fmt->Rshift);
	g = _mm_slli_epi32(g, fmt->Gshift);
	b = _mm_slli_epi32(b, fmt->Bshift);

	__m128i rgb = _mm_or_si128(r, _mm_or_si128(g, b));
	// Add alpha mask if necessary
	rgb = _mm_or_si128(rgb, _mm_set1_epi32(fmt->Amask));

	return rgb;
}

#endif

#ifdef TARGET_AVX2
__m256i SDL_MapRGBFastSIMD_AVX2(const SDL_PixelFormat* fmt, __m256i r, __m256i g, __m256i b)
{
	r = _mm256_srli_epi32(r, fmt->Rloss);
	g = _mm256_srli_epi32(g, fmt->Gloss);
	b = _mm256_srli_epi32(b, fmt->Bloss);

	r = _mm256_slli_epi32(r, fmt->Rshift);
	g = _mm256_slli_epi32(g, fmt->Gshift);
	b = _mm256_slli_epi32(b, fmt->Bshift);

	__m256i rgb = _mm256_or_si256(r, _mm256_or_si256(g, b));
	rgb = _mm256_or_si256(rgb, _mm256_set1_epi32(fmt->Amask));
	return rgb;
}
#endif

#ifdef USE_JOBS
#include "Modules/std/stdJob.h"

static SDL_Rect srcRect;
static SDL_Rect dstRect;

SDL_Surface* windowSurf = NULL;

static int copyWidth = 0;
static int copyHeight = 0;

uint8_t* srcPixels;
int srcPitch;

uint8_t* dstPixels;
int dstPitch;

float dstScaleW = 1.0f;
float dstScaleH = 1.0f;

const int fixed_shift = 16; // Bits of fractional precision
int scale_x_fp;
int scale_y_fp;
__m128i scale_x_fp_vec;
__m128i x_offset_vec;

static void SwapWindowJob(uint32_t jobIndex, uint32_t groupIndex)
{
	if (jobIndex >= copyHeight)
		return;

	// Row outside vertical copy area, fill entire destination row with black
	if (jobIndex < dstRect.y || jobIndex >= dstRect.y + copyHeight)
	{
		int dstRowWidth = dstRect.w + dstRect.x;
		uint32_t* dstRow = (uint32_t*)(dstPixels + jobIndex * dstPitch);
		memset(dstRow, 0, dstRowWidth * sizeof(uint32_t));
		return;
	}

	int y = jobIndex;

	int srcY = srcRect.y + ((y * scale_y_fp) >> fixed_shift);
	uint8_t* srcRow = srcPixels + srcY * srcPitch;

	uint32_t* dstRowStart = (uint32_t*)(dstPixels + (dstRect.y + y) * dstPitch);
	uint32_t* dstRow = dstRowStart + dstRect.x;

	if (dstRect.x > 0)
		memset(dstRowStart, 0, dstRect.x * sizeof(uint32_t));

#ifdef TARGET_AVX2
	int x = 0;
	__m256i scale_x_fp_vec8 = _mm256_set1_epi32(scale_x_fp);
	__m256i x_offset_vec8 = _mm256_set1_epi32(srcRect.x << fixed_shift);
	for (; x <= copyWidth - 8; x += 8)
	{
		__m256i x_vec = _mm256_setr_epi32(x, x+1, x+2, x+3, x+4, x+5, x+6, x+7);
		__m256i srcX_fp = _mm256_add_epi32(x_offset_vec8, _mm256_mullo_epi32(x_vec, scale_x_fp_vec8));
		__m256i srcX = _mm256_srli_epi32(srcX_fp, fixed_shift);

		int srcX_arr[8];
		_mm256_storeu_si256((__m256i*)srcX_arr, srcX);

		uint8_t indices[8] = {
			srcRow[srcX_arr[0]], srcRow[srcX_arr[1]], srcRow[srcX_arr[2]], srcRow[srcX_arr[3]],
			srcRow[srcX_arr[4]], srcRow[srcX_arr[5]], srcRow[srcX_arr[6]], srcRow[srcX_arr[7]]
		};

		__m256i pixels = _mm256_setr_epi32(
			paletteCache[indices[0]], paletteCache[indices[1]],
			paletteCache[indices[2]], paletteCache[indices[3]],
			paletteCache[indices[4]], paletteCache[indices[5]],
			paletteCache[indices[6]], paletteCache[indices[7]]
		);
		_mm256_storeu_si256((__m256i*)&dstRow[x], pixels);
	}
	for (; x < copyWidth; ++x)
	{
		int srcX = srcRect.x + (x * srcRect.w) * dstScaleW;
		uint8_t index = srcRow[srcX];
		dstRow[x] = paletteCache[index];
	}
#elif defined(TARGET_SSE)
	int x = 0;
	for (; x <= copyWidth - 4; x += 4)
	{
		// Create x positions: x, x+1, x+2, x+3
		__m128i x_vec = _mm_setr_epi32(x, x + 1, x + 2, x + 3);

		// Scale dst X to src X
		__m128i srcX_fp = _mm_add_epi32(
			x_offset_vec,
			_mm_mullo_epi32(x_vec, scale_x_fp_vec)
		);

		// Convert back to integer
		__m128i srcX = _mm_srli_epi32(srcX_fp, fixed_shift);

		// Extract the 4 indices
		int srcX_arr[4];
		_mm_store_si128((__m128i*)srcX_arr, srcX);

		uint8_t indices[4] = {
			srcRow[srcX_arr[0]],
			srcRow[srcX_arr[1]],
			srcRow[srcX_arr[2]],
			srcRow[srcX_arr[3]]
		};

		// is this really the best way to do this? would writing directly be faster?
		__m128i pixels = _mm_setr_epi32(
			paletteCache[indices[0]],
			paletteCache[indices[1]],
			paletteCache[indices[2]],
			paletteCache[indices[3]]
		);
		_mm_storeu_si128((__m128i*)&dstRow[x], pixels);
	}

	for (; x < copyWidth; ++x)
	{
		int srcX = srcRect.x + (x * srcRect.w) * dstScaleW;
		uint8_t index = srcRow[srcX];
		dstRow[x] = paletteCache[index];
	}	
#else
	for (int x = 0; x < copyWidth; ++x)
	{
		// Scale dst X to src X
		int srcX = srcRect.x + (x * srcRect.w) * dstScaleW;
		uint8_t index = srcRow[srcX];
		dstRow[x] = paletteCache[index];
	}
#endif

	int fullRowWidth = dstPitch / 4;  // full row pixel count
	int suffixLength = windowSurf->w - (dstRect.x + copyWidth);
	if (suffixLength > 0)
		memset(dstRow + copyWidth, 0, suffixLength * sizeof(uint32_t));
}

static void SwapWindowJobRGB16(uint32_t jobIndex, uint32_t groupIndex)
{
	if (jobIndex >= copyHeight)
		return;

	// Row outside vertical copy area, fill entire destination row with black
	if (jobIndex < dstRect.y || jobIndex >= dstRect.y + copyHeight)
	{
		int dstRowWidth = dstRect.w + dstRect.x;
		uint32_t* dstRow = (uint32_t*)(dstPixels + jobIndex * dstPitch);
		memset(dstRow, 0, dstRowWidth * sizeof(uint32_t));
		return;
	}

	int y = jobIndex;
	int srcY = srcRect.y + (int)(((int64_t)y * scale_y_fp) >> fixed_shift);
	uint8_t* srcRowBase = srcPixels + srcY * srcPitch;

	uint32_t* dstRowStart = (uint32_t*)(dstPixels + (dstRect.y + y) * dstPitch);
	uint32_t* dstRow = dstRowStart + dstRect.x;

	if (dstRect.x > 0)
		memset(dstRowStart, 0, dstRect.x * sizeof(uint32_t));

	int srcFormatIs16Bit = Video_pOtherBuf->format.format.bpp == 16;
	const rdTexformat* srcFmt = &Video_pOtherBuf->format.format;
	const SDL_PixelFormat* dstFmt = windowSurf->format;

	const int fx = stdPalEffects_state.effect.filter.x;
	const int fy = stdPalEffects_state.effect.filter.y;
	const int fz = stdPalEffects_state.effect.filter.z;
	const int useFilter = fx || fy || fz;

	const flex_t tx = stdPalEffects_state.effect.tint.x;
	const flex_t ty = stdPalEffects_state.effect.tint.y;
	const flex_t tz = stdPalEffects_state.effect.tint.z;
	const int useTint = tx != 0.0f || ty != 0.0f || tz != 0.0f;

	const flex_t halfR = tx * 0.5f;
	const flex_t halfG = ty * 0.5f;
	const flex_t halfB = tz * 0.5f;

	const __m128 mulR = _mm_set1_ps(tx - (halfB + halfG));
	const __m128 mulG = _mm_set1_ps(ty - (halfR + halfB));
	const __m128 mulB = _mm_set1_ps(tz - (halfR + halfG));
	const __m128 halfOffset = _mm_set1_ps(0.5f);

	const int ax = stdPalEffects_state.effect.add.x;
	const int ay = stdPalEffects_state.effect.add.y;
	const int az = stdPalEffects_state.effect.add.z;

	const int useAdd = (ax || ay || az);

	const __m128i addR = _mm_set1_epi32(ax);
	const __m128i addG = _mm_set1_epi32(ay);
	const __m128i addB = _mm_set1_epi32(az);
	
	const __m128 fade = _mm_set1_ps(stdPalEffects_state.effect.fade);
	const int useFade = stdPalEffects_state.effect.fade < 1.0;

#ifdef TARGET_AVX2

	__m256i step8 = _mm256_set1_epi32(scale_x_fp);
	__m256i srcRectX_v8 = _mm256_set1_epi32(srcRect.x);
	const __m256 mulR8 = _mm256_set1_ps(tx - (halfB + halfG));
	const __m256 mulG8 = _mm256_set1_ps(ty - (halfR + halfB));
	const __m256 mulB8 = _mm256_set1_ps(tz - (halfR + halfG));
	const __m256 halfOffset8 = _mm256_set1_ps(0.5f);
	const __m256i addR8 = _mm256_set1_epi32(ax);
	const __m256i addG8 = _mm256_set1_epi32(ay);
	const __m256i addB8 = _mm256_set1_epi32(az);
	const __m256 fade8 = _mm256_set1_ps(stdPalEffects_state.effect.fade);

	for (int x = 0; x < copyWidth; x += 8)
	{
		__m256i x_vec = _mm256_setr_epi32(x, x+1, x+2, x+3, x+4, x+5, x+6, x+7);
		__m256i scaled_fp = _mm256_mullo_epi32(x_vec, step8);
		__m256i srcX_fp = _mm256_srai_epi32(scaled_fp, fixed_shift);
		__m256i srcX = _mm256_add_epi32(srcX_fp, srcRectX_v8);

		int srcXs[8];
		_mm256_storeu_si256((__m256i*)srcXs, srcX);

		__m256i pixels;
		if (srcFormatIs16Bit)
		{
			uint16_t pix[8] = {
				*(uint16_t*)(srcRowBase + srcXs[0] * 2),
				*(uint16_t*)(srcRowBase + srcXs[1] * 2),
				*(uint16_t*)(srcRowBase + srcXs[2] * 2),
				*(uint16_t*)(srcRowBase + srcXs[3] * 2),
				*(uint16_t*)(srcRowBase + srcXs[4] * 2),
				*(uint16_t*)(srcRowBase + srcXs[5] * 2),
				*(uint16_t*)(srcRowBase + srcXs[6] * 2),
				*(uint16_t*)(srcRowBase + srcXs[7] * 2),
			};
			__m128i pixels16 = _mm_loadu_si128((__m128i*)pix); // 8x16-bit packed
			pixels = _mm256_cvtepu16_epi32(pixels16);           // expand to 8x32-bit
		}
		else
		{
			uint32_t pix[8] = {
				*(uint32_t*)(srcRowBase + srcXs[0] * 4),
				*(uint32_t*)(srcRowBase + srcXs[1] * 4),
				*(uint32_t*)(srcRowBase + srcXs[2] * 4),
				*(uint32_t*)(srcRowBase + srcXs[3] * 4),
				*(uint32_t*)(srcRowBase + srcXs[4] * 4),
				*(uint32_t*)(srcRowBase + srcXs[5] * 4),
				*(uint32_t*)(srcRowBase + srcXs[6] * 4),
				*(uint32_t*)(srcRowBase + srcXs[7] * 4),
			};
			pixels = _mm256_loadu_si256((__m256i*)pix);
		}

		__m256i r, g, b;
		stdColor_DecodeRGBSIMD_AVX2(pixels, srcFmt, &r, &g, &b);

		if (useFilter)
		{
			if (!fx) r = _mm256_setzero_si256();
			if (!fy) g = _mm256_setzero_si256();
			if (!fz) b = _mm256_setzero_si256();
		}

		if (useTint)
		{
			r = _mm256_add_epi32(r, _mm256_cvtps_epi32(_mm256_fmadd_ps(_mm256_cvtepi32_ps(r), mulR8, halfOffset8)));
			g = _mm256_add_epi32(g, _mm256_cvtps_epi32(_mm256_fmadd_ps(_mm256_cvtepi32_ps(g), mulG8, halfOffset8)));
			b = _mm256_add_epi32(b, _mm256_cvtps_epi32(_mm256_fmadd_ps(_mm256_cvtepi32_ps(b), mulB8, halfOffset8)));
		}

		if (useAdd)
		{
			r = _mm256_add_epi32(r, addR8);
			g = _mm256_add_epi32(g, addG8);
			b = _mm256_add_epi32(b, addB8);
		}

		if (useFade)
		{
			r = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_cvtepi32_ps(r), fade8));
			g = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_cvtepi32_ps(g), fade8));
			b = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_cvtepi32_ps(b), fade8));
		}

		r = _mm256_max_epi32(_mm256_min_epi32(r, _mm256_set1_epi32(255)), _mm256_setzero_si256());
		g = _mm256_max_epi32(_mm256_min_epi32(g, _mm256_set1_epi32(255)), _mm256_setzero_si256());
		b = _mm256_max_epi32(_mm256_min_epi32(b, _mm256_set1_epi32(255)), _mm256_setzero_si256());

		__m256i mapped = SDL_MapRGBFastSIMD_AVX2(dstFmt, r, g, b);
		_mm256_storeu_si256((__m256i*)&dstRow[x], mapped);
	}
#elif defined(TARGET_SSE)

	__m128i step = _mm_set1_epi32(scale_x_fp);
	__m128i fixed_shift_v = _mm_set1_epi32(fixed_shift);
	__m128i srcRectX_v = _mm_set1_epi32(srcRect.x);

	for (int x = 0; x < copyWidth; x += 4)
	{
		// x: [x+0, x+1, x+2, x+3]
		__m128i x_vec = _mm_setr_epi32(x + 0, x + 1, x + 2, x + 3);
		__m128i scaled_fp = _mm_mullo_epi32(x_vec, step);               // x * scale_x_fp
		__m128i srcX_fp = _mm_srai_epi32(scaled_fp, fixed_shift);       // >> fixed_shift
		__m128i srcX = _mm_add_epi32(srcX_fp, srcRectX_v);              // + srcRect.x

		// Gather manually (SSE2 workaround)
		int srcXs[4];
		_mm_storeu_si128((__m128i*)srcXs, srcX);

		__m128i pixels;
		if (srcFormatIs16Bit)
		{
			uint16_t pix[4] = {
				*(uint16_t*)(srcRowBase + srcXs[0] * 2),
				*(uint16_t*)(srcRowBase + srcXs[1] * 2),
				*(uint16_t*)(srcRowBase + srcXs[2] * 2),
				*(uint16_t*)(srcRowBase + srcXs[3] * 2),
			};

			__m128i pixels16 = _mm_loadl_epi64((__m128i*)pix); // load 4x16-bit packed
			pixels = _mm_cvtepu16_epi32(pixels16);   // expand to 4x32-bit ints
		}
		else
		{
			uint32_t pix[4] = {
				*(uint32_t*)(srcRowBase + srcXs[0] * 4),
				*(uint32_t*)(srcRowBase + srcXs[1] * 4),
				*(uint32_t*)(srcRowBase + srcXs[2] * 4),
				*(uint32_t*)(srcRowBase + srcXs[3] * 4),
			};
			pixels = _mm_loadu_si128((__m128i*)pix);
		}

		__m128i r, g, b;
		stdColor_DecodeRGBSIMD(pixels, srcFmt, &r, &g, &b);

		// todo: can use cube palette here when the palette effects are extended to do more complex math
		// but as of right now the memory access overhead isn't worth it (filter + add + fade + tint are just a few math ops)
		if (useFilter)
		{
			if (!fx) r = _mm_setzero_si128();
			if (!fy) g = _mm_setzero_si128();
			if (!fz) b = _mm_setzero_si128();
		}

		if (useTint)
		{
			// x + (int)((float)x * mulR + 0.5)
			r = _mm_add_epi32(r, _mm_cvtps_epi32(_mm_fmadd_ps(_mm_cvtepi32_ps(r), mulR, halfOffset)));
			g = _mm_add_epi32(g, _mm_cvtps_epi32(_mm_fmadd_ps(_mm_cvtepi32_ps(g), mulG, halfOffset)));
			b = _mm_add_epi32(b, _mm_cvtps_epi32(_mm_fmadd_ps(_mm_cvtepi32_ps(b), mulB, halfOffset)));
		}

		if (useAdd)
		{
			r = _mm_add_epi32(r, addR);
			g = _mm_add_epi32(g, addR);
			b = _mm_add_epi32(b, addR);
		}

		if (useFade)
		{
			r = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(r), fade));
			g = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(g), fade));
			b = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(b), fade));
		}

		// note: normally the clamping is done after both tint + add which will have different behavior when oversaturating
		r = _mm_max_epi32(_mm_min_epi32(r, _mm_set1_epi32(255)), _mm_setzero_si128());
		g = _mm_max_epi32(_mm_min_epi32(g, _mm_set1_epi32(255)), _mm_setzero_si128());
		b = _mm_max_epi32(_mm_min_epi32(b, _mm_set1_epi32(255)), _mm_setzero_si128());

		__m128i mapped = SDL_MapRGBFastSIMD(dstFmt, r, g, b);
		_mm_storeu_si128((__m128i*) & dstRow[x], mapped);
	}
#else
	for (int x = 0; x < copyWidth; ++x)
	{
		int srcX = srcRect.x + (int)(((int64_t)x * scale_x_fp) >> fixed_shift);
		int bpp = Video_pOtherBuf->format.format.bpp;
		int byteOffset = srcX * (bpp / 8);

		if (srcFormatIs16Bit)
		{
			uint16_t pix = srcRowBase[byteOffset] | (srcRowBase[byteOffset + 1] << 8);
			uint8_t r, g, b;
			stdColor_DecodeRGB(pix, &Video_pOtherBuf->format.format, &r, &g, &b);
			dstRow[x] = SDL_MapRGBFast(windowSurf->format, r, g, b);
		}
		else
		{
			uint32_t pix = srcRowBase[byteOffset] |
				(srcRowBase[byteOffset + 1] << 8) |
				(srcRowBase[byteOffset + 2] << 16) |
				(srcRowBase[byteOffset + 3] << 24);

			uint8_t r, g, b;
			stdColor_DecodeRGB(pix, &Video_pOtherBuf->format.format, &r, &g, &b);
			dstRow[x] = SDL_MapRGBFast(windowSurf->format, r, g, b);
		}
	}
#endif


	int fullRowWidth = dstPitch / 4;  // full row pixel count
	int suffixLength = windowSurf->w - (dstRect.x + copyWidth);
	if (suffixLength > 0)
		memset(dstRow + copyWidth, 0, suffixLength * sizeof(uint32_t));
}

#endif

// TILETODO move me
void SwapWindow(SDL_Window* window)
{
	stdVBuffer* buffer = Video_pOtherBuf;//Video_pMenuBuffer
	if (buffer)
	{
#ifndef USE_JOBS
		SDL_Rect srcRect;
		SDL_Rect dstRect;

		SDL_Surface* windowSurf = NULL;
		uint8_t* srcPixels;
		int srcPitch;

		uint8_t* dstPixels;
		int dstPitch;

		int copyWidth;
		int copyHeight;
#endif
		windowSurf = SDL_GetWindowSurface(window);
		SDL_LockSurface(windowSurf);

		//SDL_FillRect(windowSurf, NULL, 0);

		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.w = buffer->format.width;
		srcRect.h = buffer->format.height;

		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.w = Window_screenXSize;
		dstRect.h = Window_screenYSize;

		float srcAspect = (float)srcRect.w / (float)srcRect.h;
		float dstAspect = (float)Window_screenXSize / (float)Window_screenYSize;

		if (dstAspect > srcAspect)
		{
			// Destination is wider than source — scale by height
			dstRect.h = Window_screenYSize;
			dstRect.w = (int)(Window_screenYSize * srcAspect);
			dstRect.x = (Window_screenXSize - dstRect.w) / 2;
			dstRect.y = 0;
		}
		else
		{
			// Destination is taller than source — scale by width
			dstRect.w = Window_screenXSize;
			dstRect.h = (int)(Window_screenXSize / srcAspect);
			dstRect.x = 0;
			dstRect.y = (Window_screenYSize - dstRect.h) / 2;
		}
		
		if (stdDisplay_pCurDevice->video_device[0].device_active)
		{
			std3D_Present(buffer->gpuHandle, buffer->format.width, buffer->format.height, buffer->format.format.bpp >> 3, &dstRect);
			//SDL_UpdateWindowSurface(displayWindow);
			return;
		}

		//SDL_LockSurface(buffer->sdlSurface);
		
		srcPixels = (uint8_t*)buffer->surface_lock_alloc;//sdlSurface->pixels;
		srcPitch = buffer->format.width_in_bytes;// buffer->sdlSurface->pitch;

		dstPixels = (uint8_t*)windowSurf->pixels;
		dstPitch = windowSurf->pitch;

		copyWidth = SDL_min(dstRect.w, windowSurf->w - dstRect.x);
		copyHeight = SDL_min(dstRect.h, windowSurf->h - dstRect.y);

		// Perhaps we should do palette indexing/RGB format resolving to an intermediate buffer that matches the size of the source
		// and then rescale that, instead of performing the indexing/resolving per pixel when scaling (pretty redundant for smaller formats)

		if (buffer->format.format.colorMode == STDCOLOR_PAL)
		{
			rdColor24* pal_master = (rdColor24*)stdDisplay_masterPalette;
			for (int i = 0; i < 256; i+= 4) // lazy hoping that the compiler will auto vectorize...
			{
				int i0 = i;
				int i1 = i + 1;
				int i2 = i + 2;
				int i3 = i + 3;
				paletteCache[i0] = SDL_MapRGBFast(windowSurf->format, pal_master[i0].r, pal_master[i0].g, pal_master[i0].b);
				paletteCache[i1] = SDL_MapRGBFast(windowSurf->format, pal_master[i1].r, pal_master[i1].g, pal_master[i1].b);
				paletteCache[i2] = SDL_MapRGBFast(windowSurf->format, pal_master[i2].r, pal_master[i2].g, pal_master[i2].b);
				paletteCache[i3] = SDL_MapRGBFast(windowSurf->format, pal_master[i3].r, pal_master[i3].g, pal_master[i3].b);
			}
		}

		dstScaleW = 1.0f / (float)dstRect.w;
		dstScaleH = 1.0f / (float)dstRect.h;

	#ifdef USE_JOBS

		scale_x_fp = (srcRect.w * (1 << fixed_shift)) / dstRect.w;
		scale_y_fp = (srcRect.h * (1 << fixed_shift)) / dstRect.h;
		scale_x_fp_vec = _mm_set1_epi32(scale_x_fp);
		x_offset_vec = _mm_set1_epi32(srcRect.x << fixed_shift);

		// eebs: tried tiling but it seems that dispatching by row seems to be the fastest option?
		if (buffer->format.format.colorMode)
			stdJob_Dispatch(copyHeight, 8, SwapWindowJobRGB16);
		else
			stdJob_Dispatch(copyHeight, 8, SwapWindowJob);
		stdJob_Wait();

	#else
#ifdef TARGET_AVX2
		const int fixed_shift = 16;
		const int scale_x_fp = (srcRect.w * (1 << fixed_shift)) / dstRect.w;
		const int scale_y_fp = (srcRect.h * (1 << fixed_shift)) / dstRect.h;
		const __m256i scale_x_fp_vec8 = _mm256_set1_epi32(scale_x_fp);
		const __m256i x_offset_vec8 = _mm256_set1_epi32(srcRect.x << fixed_shift);

		for (int y = 0; y < copyHeight; ++y)
		{
			int srcY = srcRect.y + ((y * scale_y_fp) >> fixed_shift);
			uint8_t* srcRow = srcPixels + srcY * srcPitch;
			uint32_t* dstRow = (uint32_t*)(dstPixels + (dstRect.y + y) * dstPitch + dstRect.x * 4);

			int x = 0;
			for (; x + 7 < copyWidth; x += 8)
			{
				__m256i x_vec = _mm256_setr_epi32(x, x+1, x+2, x+3, x+4, x+5, x+6, x+7);
				__m256i srcX_fp = _mm256_add_epi32(x_offset_vec8, _mm256_mullo_epi32(x_vec, scale_x_fp_vec8));
				__m256i srcX = _mm256_srli_epi32(srcX_fp, fixed_shift);

				int srcX_arr[8];
				_mm256_storeu_si256((__m256i*)srcX_arr, srcX);

				uint8_t indices[8] = {
					srcRow[srcX_arr[0]], srcRow[srcX_arr[1]], srcRow[srcX_arr[2]], srcRow[srcX_arr[3]],
					srcRow[srcX_arr[4]], srcRow[srcX_arr[5]], srcRow[srcX_arr[6]], srcRow[srcX_arr[7]]
				};

				__m256i pixel_vec = _mm256_setr_epi32(
					paletteCache[indices[0]], paletteCache[indices[1]],
					paletteCache[indices[2]], paletteCache[indices[3]],
					paletteCache[indices[4]], paletteCache[indices[5]],
					paletteCache[indices[6]], paletteCache[indices[7]]
				);
				_mm256_storeu_si256((__m256i*)&dstRow[x], pixel_vec);
			}

			for (; x < copyWidth; ++x)
			{
				int srcX = srcRect.x + (x * srcRect.w) / dstRect.w;
				uint8_t index = srcRow[srcX];
				rdColor24* color = &pal_master[index];
				dstRow[x] = SDL_MapRGBFast(windowSurf->format, color->r, color->g, color->b);
			}
		}
#elif defined(TARGET_SSE)
		const int fixed_shift = 16; // Bits of fractional precision
		const int scale_x_fp = (srcRect.w * (1 << fixed_shift)) / dstRect.w;
		const int scale_y_fp = (srcRect.h * (1 << fixed_shift)) / dstRect.h;
		const __m128i scale_x_fp_vec = _mm_set1_epi32(scale_x_fp);
		const __m128i x_offset_vec = _mm_set1_epi32(srcRect.x << fixed_shift);

		for (int y = 0; y < copyHeight; ++y)
		{
			// Scale dst Y to src Y
			int srcY = srcRect.y + ((y * scale_y_fp) >> fixed_shift);
			uint8_t* srcRow = srcPixels + srcY * srcPitch;

			uint32_t* dstRow = (uint32_t*)(dstPixels + (dstRect.y + y) * dstPitch + dstRect.x * 4);

			int x = 0;
			// Process 4 pixels at a time
			for (; x + 3 < copyWidth; x += 4)
			{
				// Create x positions: x, x+1, x+2, x+3
				__m128i x_vec = _mm_setr_epi32(x, x + 1, x + 2, x + 3);

				// Scale dst X to src X
				__m128i srcX_fp = _mm_add_epi32(
					x_offset_vec,
					_mm_mullo_epi32(x_vec, scale_x_fp_vec)
				);

				// Convert back to integer
				__m128i srcX = _mm_srli_epi32(srcX_fp, fixed_shift);

				// Extract the 4 indices
				int srcX_arr[4];
				_mm_store_si128((__m128i*)srcX_arr, srcX);

				uint8_t indices[4] = {
					srcRow[srcX_arr[0]],
					srcRow[srcX_arr[1]],
					srcRow[srcX_arr[2]],
					srcRow[srcX_arr[3]]
				};

				// Map to destination format
				__m128i pixel_vec = _mm_setr_epi32(
					paletteCache[indices[0]],
					paletteCache[indices[1]],
					paletteCache[indices[2]],
					paletteCache[indices[3]]
				);

				// Store 4 pixels
				_mm_storeu_si128((__m128i*)&dstRow[x], pixel_vec);
			}

			// Handle remaining pixels
			for (; x < copyWidth; ++x)
			{
				int srcX = srcRect.x + (x * srcRect.w) / dstRect.w;
				uint8_t index = srcRow[srcX];
				rdColor24* color = &pal_master[index];
				dstRow[x] = SDL_MapRGBFast(windowSurf->format, color->r, color->g, color->b);
			}
		}
#else
		for (int y = 0; y < copyHeight; ++y)
		{
			// Scale dst Y to src Y
			int srcY = srcRect.y + (y * srcRect.h) / dstRect.h;
			uint8_t* srcRow = srcPixels + srcY * srcPitch;
		
			uint32_t* dstRow = (uint32_t*)(dstPixels + (dstRect.y + y) * dstPitch + dstRect.x * 4);
		
			for (int x = 0; x < copyWidth; ++x)
			{
				// Scale dst X to src X
				int srcX = srcRect.x + (x * srcRect.w) / dstRect.w;
		
				uint8_t index = srcRow[srcX];
				dstRow[x] = paletteCache[index];
			}
		}
	#endif
	#endif

		//SDL_UnlockSurface(buffer->sdlSurface);
		SDL_UnlockSurface(windowSurf);

		SDL_UpdateWindowSurface(displayWindow);
	}

}
#endif

void Window_UpdateHeadless()
{
    char buffer[32];
    size_t bytes_read = 0;

    if (my_kbhit() > 0) {
#if defined(WIN64_MINGW) || (_WIN32)
        buffer[0] = my_getch();
        buffer[1] = 0;
        bytes_read = 1;
#else
        int fd = STDIN_FILENO;
        bytes_read = read(fd, buffer, sizeof(buffer)-1);
        buffer[bytes_read] = 0;
#endif

        for (int i = 0; i < bytes_read; i++)
        {
            if (buffer[i] == '\n' || buffer[i] == '\r') {
                printf("\r> %s\n", Window_headlessBuffer);
                sithConsole_TryCommand(Window_headlessBuffer);
                memset(Window_headlessBuffer, 0, sizeof(Window_headlessBuffer));
                continue;
            }
            else if (buffer[i] == 0x7F && strlen(Window_headlessBuffer)) {
                Window_headlessBuffer[strlen(Window_headlessBuffer)-1] = 0;
                printf("\r> %s ", Window_headlessBuffer);
                continue;
            }
            else if (buffer[i] < ' ' || buffer[i] > '~')
            {
                continue;
            }

            char tmp[2] = {buffer[i], 0};
            strncat(Window_headlessBuffer, tmp, 255);
        }
    }
    
    printf("\r> %s", Window_headlessBuffer);
    //printf("> %x %x %s\n", buffer[0], my_kbhit(), Window_headlessBuffer);
    fflush(stdout);

    if (Window_resized)
    {
	#ifdef TILE_SW_RASTER
		std3D_ResizeViewport(Window_screenXSize, Window_screenYSize);
	#else
        jkMain_FixRes();
        if (!jkGui_SetModeMenu(0))
        {
            stdDisplay_SetMode(0, 0, 0);
            //jkMain_FixRes();
        }
    #endif 
        Window_resized = 0;
    }
    
    int sampleTime_roundtrip = SDL_GetTicks() - Window_lastSampleTime;
    //printf("%u\n", sampleTime_roundtrip);
    Window_lastSampleTime = SDL_GetTicks();

    static int sampleTime_delay = 0;
    int menu_framelimit_amt_ms = 6;

    if (!jkGame_isDDraw)
    {
	#ifndef TILE_SW_RASTER
        if (!jkGuiBuildMulti_bRendering) {
            std3D_StartScene();
            jkQuakeConsole_Render();
            std3D_DrawMenu();
            std3D_EndScene();
            //SDL_GL_SwapWindow(displayWindow);
        }
        else {
            jkQuakeConsole_Render();
            std3D_DrawMenu();
            //SDL_GL_SwapWindow(displayWindow);
            //menu_framelimit_amt_ms = 64;
        }
	#endif
    }
    else
    {
        // Save mouse position for menu
        if (jkGame_isDDraw != last_jkGame_isDDraw) {
            Window_menu_mouseX = Window_mouseX;
            Window_menu_mouseY = Window_mouseY;
            Window_lastXRel = 0;
            Window_lastYRel = 0;
        }
    }

    // Keep entire loop at 6ms (150FPS)
    if (sampleTime_roundtrip < menu_framelimit_amt_ms) {
        sampleTime_delay++;
    }
    else {
        sampleTime_delay--;
    }
    if (sampleTime_delay <= 0) {
        sampleTime_delay = 1;
    }
    if (sampleTime_delay >= menu_framelimit_amt_ms) {
        sampleTime_delay = menu_framelimit_amt_ms;
    }
    SDL_Delay(sampleTime_delay);

    last_jkGame_isDDraw = jkGame_isDDraw;
    last_jkQuakeConsole_bOpen = jkQuakeConsole_bOpen;
}

void Window_SdlUpdate()
{
    if (Main_bHeadless)
    {
        Window_UpdateHeadless();
        return;
    }

    uint16_t left, right;
    uint32_t pos, msgl, msgr;
    int hasLeft, hasRight;
    SDL_Event event;
    SDL_MouseButtonEvent* mevent;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_JOYDEVICEADDED: {
                stdControl_InitSdlJoysticks();
                break;
            }
            case SDL_JOYDEVICEREMOVED: {
                stdControl_InitSdlJoysticks();
                break;
            }

            case SDL_TEXTINPUT:
                for (int i = 0; i < _strlen(event.text.text); i++)
                {
                    Window_msg_main_handler(g_hWnd, WM_CHAR, event.text.text[i], 0);
                }
                break;
            case SDL_WINDOWEVENT:
                Window_HandleWindowEvent(&event);
                break;
            case SDL_KEYDOWN:
                //handleKey(&event.key.keysym, WM_KEYDOWN, 0x1);
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_ESCAPE, event.key.repeat & 0xFFFF);
                    Window_msg_main_handler(g_hWnd, WM_CHAR, VK_ESCAPE, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_PAGEUP)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_PRIOR, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_PAGEDOWN)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_NEXT, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_LEFT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_LEFT, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_RIGHT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_RIGHT, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_UP)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_UP, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_DOWN)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_DOWN, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_BACKSPACE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_BACK, event.key.repeat & 0xFFFF);
                    Window_msg_main_handler(g_hWnd, WM_CHAR, VK_BACK, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_DELETE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_DELETE, event.key.repeat & 0xFFFF);
                    //Window_msg_main_handler(g_hWnd, WM_CHAR, VK_DELETE, 0);
                }
                else if (event.key.keysym.sym == SDLK_INSERT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_INSERT, event.key.repeat & 0xFFFF);
                    Window_msg_main_handler(g_hWnd, WM_CHAR, VK_INSERT, 0);
                }
                else if (event.key.keysym.sym == SDLK_RETURN)
                {
                    // HACK apparently Windows buffers these events in some way, but to replicate the behavior in jkGUI we just spam KEYFIRST
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_RETURN, event.key.repeat & 0xFFFF);
                    Window_msg_main_handler(g_hWnd, WM_CHAR, VK_RETURN, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_LSHIFT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_LSHIFT, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_RSHIFT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_RSHIFT, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_TAB)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_TAB, event.key.repeat & 0xFFFF);
                    Window_msg_main_handler(g_hWnd, WM_CHAR, VK_TAB, event.key.repeat & 0xFFFF);
                }
                else if (event.key.keysym.sym == SDLK_END)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_END, event.key.repeat & 0xFFFF);
                    //Window_msg_main_handler(g_hWnd, WM_CHAR, 0x23, 0);
                }
                else if (event.key.keysym.sym == SDLK_HOME)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_HOME, event.key.repeat & 0xFFFF);
                    //Window_msg_main_handler(g_hWnd, WM_CHAR, 0x24, 0);
                }
                else if (event.key.keysym.sym == SDLK_BACKQUOTE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYFIRST, VK_OEM_3, event.key.repeat & 0xFFFF);
                }

                //if (!event.key.repeat)
                //    stdControl_SetSDLKeydown(event.key.keysym.scancode, 1, event.key.timestamp);
                break;
            case SDL_KEYUP:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_ESCAPE, 0);
                }
                else if (event.key.keysym.sym == SDLK_PAGEUP)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_PRIOR, 0);
                }
                else if (event.key.keysym.sym == SDLK_PAGEDOWN)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_NEXT, 0);
                }
                else if (event.key.keysym.sym == SDLK_LEFT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_LEFT, 0);
                }
                else if (event.key.keysym.sym == SDLK_RIGHT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_RIGHT, 0);
                }
                else if (event.key.keysym.sym == SDLK_UP)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_UP, 0);
                }
                else if (event.key.keysym.sym == SDLK_DOWN)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_DOWN, 0);
                }
                else if (event.key.keysym.sym == SDLK_BACKSPACE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_BACK, 0);
                }
                else if (event.key.keysym.sym == SDLK_DELETE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_DELETE, 0);
                }
                else if (event.key.keysym.sym == SDLK_INSERT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_INSERT, 0);
                }
                else if (event.key.keysym.sym == SDLK_RETURN)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_RETURN, 0); // 0xB?
                }
                else if (event.key.keysym.sym == SDLK_LSHIFT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_LSHIFT, 0);
                }
                else if (event.key.keysym.sym == SDLK_RSHIFT)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_RSHIFT, 0);
                }
                else if (event.key.keysym.sym == SDLK_TAB)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_TAB, 0);
                }
                else if (event.key.keysym.sym == SDLK_END)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_END, 0);
                }
                else if (event.key.keysym.sym == SDLK_HOME)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_HOME, 0);
                }
                else if (event.key.keysym.sym == SDLK_BACKQUOTE)
                {
                    Window_msg_main_handler(g_hWnd, WM_KEYUP, VK_OEM_3, 0);
                }
                //handleKey(&event.key.keysym, WM_KEYUP, 0xc0000001);

                if (jkQuakeConsole_bOpen) break; // Hijack all input to console

                stdControl_SetSDLKeydown(event.key.keysym.scancode, 0, event.key.timestamp);
                break;
            case SDL_MOUSEMOTION:
                Window_HandleMouseMove(&event.motion);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:

                mevent = (SDL_MouseButtonEvent*)&event;
                left = 0;
                right = 0;
                hasLeft = 0;
                hasRight = 0;
                if (event.type == SDL_MOUSEBUTTONDOWN)
                {
                    left = (mevent->button == SDL_BUTTON_LEFT ? 1 : 0);
                    right = (mevent->button == SDL_BUTTON_RIGHT ? 2 : 0);
                    
                    if (left)
                        hasLeft = 1;
                    if (right)
                        hasRight = 1;
                }
                else if (event.type == SDL_MOUSEBUTTONUP)
                {
                    left = (mevent->button == SDL_BUTTON_LEFT ? 0 : 1);
                    right = (mevent->button == SDL_BUTTON_RIGHT ? 0 : 2);
                    
                    if (!left)
                        hasLeft = 1;
                    if (!right)
                        hasRight = 1;
                }
                
                if (hasLeft)
                    Window_bMouseLeft = left;
                if (hasRight)
                    Window_bMouseRight = right;

                Window_mouseX = mevent->x;
                Window_mouseY = mevent->y;// - (Window_ySize - 480);

                pos = ((Window_mouseX) & 0xFFFF) | (((Window_mouseY) << 16) & 0xFFFF0000);
                msgl = (event.type == SDL_MOUSEBUTTONDOWN ? WM_LBUTTONDOWN : WM_LBUTTONUP);
                msgr = (event.type == SDL_MOUSEBUTTONDOWN ? WM_RBUTTONDOWN : WM_RBUTTONUP);

                if (jkQuakeConsole_bOpen) break; // Hijack all input to console
                
                if (hasLeft)
                    Window_msg_main_handler(g_hWnd, msgl, left | right, pos);
                if (hasRight)
                    Window_msg_main_handler(g_hWnd, msgr, left | right, pos);

                //stdControl_SetKeydown(KEY_MOUSE_B1, Window_bMouseLeft, mevent->timestamp);
                //stdControl_SetKeydown(KEY_MOUSE_B2, Window_bMouseRight, mevent->timestamp);

                break;
            case SDL_MOUSEWHEEL:
                Window_mouseWheelY = event.wheel.y;
                Window_mouseWheelX = event.wheel.x;

                if (jkQuakeConsole_bOpen) break; // Hijack all input to console
                break;
            case SDL_QUIT:
                stdPlatform_Printf("Quit!\n");

                // Added
                if (jkPlayer_bHasLoadedSettingsOnce) {
                    jkPlayer_WriteConf(jkPlayer_playerShortName);
                }
                
                exit(-1);
                break;
            default:
                break;
        }
    }
    
    if (Window_resized)
    {
#ifndef TILE_SW_RASTER
		jkMain_FixRes();
        if (!jkGui_SetModeMenu(0))
        {
            stdDisplay_SetMode(0, 0, 0);
            //jkMain_FixRes();
        }
#endif
        
        Window_resized = 0;
    }
    
    static int sampleTime_delay = 0;
    int sampleTime_roundtrip = SDL_GetTicks() - Window_lastSampleTime;
    //printf("%u\n", sampleTime_roundtrip);
    Window_lastSampleTime = SDL_GetTicks();

    static int jkPlayer_enableVsync_last = 0;
    int menu_framelimit_amt_ms = 16;

    if (jkPlayer_enableVsync_last != jkPlayer_enableVsync)
    {
        SDL_GL_SetSwapInterval(jkPlayer_enableVsync);
    }

	// Added
	stdProfiler_Tick();

    if (!jkGame_isDDraw)
    {
        // Restore menu mouse position
        if (jkGame_isDDraw != last_jkGame_isDDraw) {
            SDL_WarpMouseInWindow(displayWindow, Window_menu_mouseX, Window_menu_mouseY);
        }

        SDL_SetRelativeMouseMode(SDL_FALSE);

	#ifdef TILE_SW_RASTER
		//SwapWindow(displayWindow);
	#else
        if (!jkGuiBuildMulti_bRendering) {
            std3D_StartScene();
            jkQuakeConsole_Render();
            std3D_DrawMenu();
            std3D_EndScene();
            SDL_GL_SwapWindow(displayWindow);
        }
        else {
            jkQuakeConsole_Render();
            std3D_DrawMenu();
            SDL_GL_SwapWindow(displayWindow);
            //menu_framelimit_amt_ms = 64;
        }
	#endif

        if (Window_needsRecreate) {
	#ifndef TILE_SW_RASTER
            std3D_PurgeEntireTextureCache();
	#endif
            Window_RecreateSDL2Window();
        }
        
#ifndef TILE_SW_RASTER
  // Keep menu FPS at 60FPS, to avoid cranking the GPU unnecessarily.
        if (sampleTime_roundtrip < menu_framelimit_amt_ms) {
            sampleTime_delay++;
        }
        else {
            sampleTime_delay--;
        }
        if (sampleTime_delay <= 0) {
            sampleTime_delay = 1;
        }
        if (sampleTime_delay >= menu_framelimit_amt_ms) {
            sampleTime_delay = menu_framelimit_amt_ms;
        }
        SDL_Delay(sampleTime_delay);
#endif
    }
    else
    {
        // Save mouse position for menu
        if (jkGame_isDDraw != last_jkGame_isDDraw) {
            Window_menu_mouseX = Window_mouseX;
            Window_menu_mouseY = Window_mouseY;
            Window_lastXRel = 0;
            Window_lastYRel = 0;
        }

        if (jkQuakeConsole_bOpen && jkQuakeConsole_bOpen != last_jkQuakeConsole_bOpen) {
            SDL_WarpMouseInWindow(displayWindow, Window_menu_mouseX, Window_menu_mouseY);
        }
        else if (!jkQuakeConsole_bOpen && jkQuakeConsole_bOpen != last_jkQuakeConsole_bOpen) {
            Window_menu_mouseX = Window_mouseX;
            Window_menu_mouseY = Window_mouseY;
            Window_lastXRel = 0;
            Window_lastYRel = 0;
        }

        if (jkQuakeConsole_bOpen)
        {
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }

        if (!jkQuakeConsole_bOpen && SDL_GetWindowFlags(displayWindow) & SDL_WINDOW_MOUSE_FOCUS) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            //SDL_WarpMouseInWindow(displayWindow, 100, 100);
        }
        else
        {
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
    }

    jkPlayer_enableVsync_last = jkPlayer_enableVsync;

    last_jkGame_isDDraw = jkGame_isDDraw;
    last_jkQuakeConsole_bOpen = jkQuakeConsole_bOpen;
}

void Window_SdlVblank()
{
    if (Main_bHeadless) return;

    //static uint32_t roundtrip = 0;
    //uint32_t before = stdPlatform_GetTimeMsec();
#ifdef ARCH_WASM
    if (!jkGuiBuildMulti_bRendering)
#endif
#ifdef TILE_SW_RASTER
	//SwapWindow(displayWindow);
#else
    SDL_GL_SwapWindow(displayWindow);
#endif
	//uint32_t after = stdPlatform_GetTimeMsec();
    //printf("%u %u\n", after-before, before-roundtrip);

    //roundtrip = before;

    if (Window_needsRecreate)
        Window_RecreateSDL2Window();

#ifdef ARCH_WASM
    //emscripten_sleep(1);
#endif
}

#ifdef ARCH_WASM
EM_JS(int, canvas_get_width, (), {
  return canvas.width;
});

EM_JS(int, canvas_get_height, (), {
  return canvas.height;
});
#endif

void Window_RecreateSDL2Window()
{
#ifdef ARCH_WASM
    static int onlyOnce = 0;
    if (onlyOnce) {
        return;
    }
    onlyOnce = 1;
#endif

    if (Main_bHeadless) return;

    stdPlatform_Printf("Recreating SDL2 Window!\n");
    Window_needsRecreate = 0;

    if (displayWindow) {
        std3D_FreeResources();
#ifdef TILE_SW_RASTER
		//std3D_FreeSwapChain();
#else TILE_SW_RASTER
		SDL_GL_DeleteContext(glWindowContext);
#endif
        SDL_DestroyWindow(displayWindow);
    }

#ifdef TILE_SW_RASTER
	int flags = SDL_WINDOW_RESIZABLE; 
#else
    int flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#endif
    if (displayWindow) {
        flags = SDL_GetWindowFlags(displayWindow);
        //std3D_FreeResources();
        //SDL_GL_DeleteContext(glWindowContext);
        //SDL_DestroyWindow(displayWindow);

#ifdef TILE_SW_RASTER
		flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
#else
		flags |= SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
#endif
    }

#ifdef WIN64_STANDALONE
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif

    if (Window_isHiDpi)
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    else
        flags &= ~SDL_WINDOW_ALLOW_HIGHDPI;

    if (Window_isFullscreen) {
        //flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    else {
        //flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

#if defined(ARCH_WASM)
    //flags &= ~SDL_WINDOW_RESIZABLE;
#endif

#ifdef TARGET_ANDROID
    flags = SDL_WINDOW_SHOWN;
#endif

#ifdef ARCH_WASM
    displayWindow = SDL_CreateWindow(Window_isHiDpi ? "OpenJKDF2 HiDPI" : "OpenJKDF2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, canvas_get_width(), canvas_get_height(), flags);
#elif defined(TARGET_ANDROID)
    displayWindow = SDL_CreateWindow(Window_isHiDpi ? "OpenJKDF2 HiDPI" : "OpenJKDF2", 0, 0, Window_screenXSize, Window_screenYSize, flags);
#else
    displayWindow = SDL_CreateWindow(Window_isHiDpi ? "OpenJKDF2 HiDPI" : "OpenJKDF2", Window_xPos, Window_yPos, Window_screenXSize, Window_screenYSize, flags);
#endif
    if (!displayWindow) {
        char errtmp[256];
        snprintf(errtmp, 256, "!! Failed to create SDL2 window !!\n%s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errtmp, NULL);
        exit (-1);
    }
    //SDL_SetRenderDrawBlendMode(displayRenderer, SDL_BLENDMODE_BLEND);

#if defined(MACOS) && defined(__aarch64__)
    //SDL_FixWindowMacOS(displayWindow);
#endif

    if (Window_isFullscreen) {
        SDL_SetWindowFullscreen(displayWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else {
        SDL_SetWindowFullscreen(displayWindow, 0);
    }

 #ifndef TILE_SW_RASTER
	glWindowContext = SDL_GL_CreateContext(displayWindow);
    
    // Retry with 3.30 instead
    if (glWindowContext == NULL)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG|SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        glWindowContext = SDL_GL_CreateContext(displayWindow);
		Window_GL4 = 0;
    }
    
    if (glWindowContext == NULL)
    {
        char errtmp[256];
        snprintf(errtmp, 256, "!! Failed to initialize SDL OpenGL context !!\n%s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errtmp, NULL);
        exit(-1);
    }

    SDL_GL_MakeCurrent(displayWindow, glWindowContext);
    SDL_GL_SetSwapInterval(jkPlayer_enableVsync); // Disable vsync
 #endif
	SDL_StartTextInput();

#ifndef TILE_SW_RASTER
    SDL_GL_GetDrawableSize(displayWindow, &Window_xSize, &Window_ySize);
#endif
    SDL_GetWindowSize(displayWindow, &Window_screenXSize, &Window_screenYSize);

    Window_resized = 1;

#ifdef TILE_SW_RASTER
	//std3D_CreateSwapChain();
#endif
}

void Window_Main_Loop()
{
    jkMain_GuiAdvance(); // TODO needed?
    Window_msg_main_handler(g_hWnd, WM_PAINT, 0, 0);
#ifdef TILE_SW_RASTER
	Window_SdlUpdate();
#endif 
    //Window_SdlUpdate();
}

int Window_Main_Linux(int argc, char** argv)
{
    char cmdLine[1024];
    int result;

    // Init SDL
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);

#if defined(MACOS)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
#else

#if defined(WIN64_STANDALONE)

#ifndef TILE_SW_RASTER
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG|SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	Window_GL4 = 1;

    // apitrace
#if 0
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
#endif
#elif defined(TARGET_ANDROID)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
#elif defined(ARCH_WASM)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

#endif

	// update the default size if needed
	Window_UpdateDefaultWindowSize();
    
    Window_RecreateSDL2Window();
#if !defined(TARGET_ANDROID) && !defined(ARCH_WASM) && !defined(TILE_SW_RASTER)
	glewExperimental = GL_TRUE;
    glewInit();
#endif
    
    //SDL_RenderClear(displayRenderer);
    //SDL_RenderPresent(displayRenderer);
    
    
    strcpy(cmdLine, "");
    
    g_handler_count = 0;
    g_thing_two_some_dialog_count = 0;
    g_should_exit = 0;
    g_window_not_destroyed = 0;
    g_hInstance = 0;//hInstance;
    g_nShowCmd = 0;//nShowCmd;
    
    for (int i = 1; i < argc; i++)
    {
        strcat(cmdLine, argv[i]);
        strcat(cmdLine, " ");
    }
    
    result = Main_Startup(cmdLine);

    int fullscreen = wuRegistry_GetBool("Window_isFullscreen", 0);
    int hidpi = wuRegistry_GetBool("Window_isHiDpi", 0);
    Window_SetFullscreen(fullscreen);
    Window_SetHiDpi(hidpi);
    Window_RecreateSDL2Window();

    if (!result) return result;

    if (Main_bHeadless)
    {
        if (displayWindow) {
            std3D_FreeResources();
#ifndef TILE_SW_RASTER
			SDL_GL_DeleteContext(glWindowContext);
#endif
            SDL_DestroyWindow(displayWindow);
        }
    }

    g_window_not_destroyed = 1;
    
    Window_msg_main_handler(g_hWnd, 0x1, 0, 0); // WM_CREATE
    Window_msg_main_handler(g_hWnd, 0x6, 2, 0); // WM_ACTIVATE
    Window_msg_main_handler(g_hWnd, 0x1C, 1, 0); // WM_ACTIVATEAPP
    Window_msg_main_handler(g_hWnd, 0x18, 0, 0); // WM_SHOWWINDOW
    Window_msg_main_handler(g_hWnd, WM_PAINT, 0, 0);


#ifdef ARCH_WASM
    //int fps = 0; // Use browser's requestAnimationFrame
    //emscripten_set_main_loop_arg(Window_Main_Loop, NULL, fps, 1);
    while (1)
    {
        Window_Main_Loop();
        if (g_should_exit) break;
    }
#else
    while (1)
    {
        Window_Main_Loop();
        if (g_should_exit) break;
    }
#endif

    // Added
    if (jkPlayer_bHasLoadedSettingsOnce) {
        jkPlayer_WriteConf(jkPlayer_playerShortName);
    }

    Main_Shutdown();
    return 1;
}

int Window_Main(HINSTANCE hInstance, int a2, char *lpCmdLine, int nShowCmd, LPCSTR lpWindowName)
{
    int result;

    g_handler_count = 0;
    g_thing_two_some_dialog_count = 0;
    g_should_exit = 0;
    g_window_not_destroyed = 0;
    g_hInstance = hInstance;
    g_nShowCmd = nShowCmd;
#if 0
    if (jk_RegisterClassExA(&wndClass))
    {
        if ( jk_FindWindowA("wKernel", lpWindowName) )
            jk_exit(-1);

        uint32_t hres = jk_GetSystemMetrics(1);
        uint32_t vres = jk_GetSystemMetrics(0);
        g_hWnd = jk_CreateWindowExA(0x40000u, "wKernel", lpWindowName, 0x90000000, 0, 0, vres, hres, 0, 0, hInstance, 0);

        if (g_hWnd)
        {
            g_hInstance = hInstance;
            jk_ShowWindow(g_hWnd, 1);
            jk_UpdateWindow(g_hWnd);
        }
    }

    stdGdi_SetHwnd(g_hWnd);
    stdGdi_SetHInstance(g_hInstance);
    jk_InitCommonControls();

    g_855E8C = 2 * jk_GetSystemMetrics(32);
    uint32_t metrics_32 = jk_GetSystemMetrics(32);
    g_855E90 = jk_GetSystemMetrics(15) + 2 * metrics_32;
    result = Main_Startup(lpCmdLine);

    if (!result) return result;

    
    g_window_not_destroyed = 1;

    while (1)
    {
        if (jk_PeekMessageA(&msg, 0, 0, 0, 0))
        {
            if (!jk_GetMessageA(&msg, 0, 0, 0))
            {
                result = msg.wParam;
                g_should_exit = 1;
                break;
            }

            uint32_t some_cnt = 0;
            if (g_thing_two_some_dialog_count > 0)
            {
#if 0
                v16 = &thing_three;
                do
                {
                    //TODO if ( jk_IsDialogMessageA(*v16, &msg) )
                    //  break;
                    ++some_cnt;
                    ++v16;
                }
                while ( some_cnt < g_thing_two_some_dialog_count );
#endif
            }

            if (some_cnt == g_thing_two_some_dialog_count)
            {
                jk_TranslateMessage(&msg);
                jk_DispatchMessageA(&msg);
            }

            if (!jk_PeekMessageA(&msg, 0, 0, 0, 0))
            {
                result = 0;
                if ( g_should_exit )
                    return result;
            }
        }

        //if (user32->stopping) break;

        jkMain_GuiAdvance();
    }
#endif
    result = 1;
    return result;
}

int Window_ShowCursorUnwindowed(int a1)
{
    return stdControl_ShowCursor(a1);
}

int Window_DefaultHandler(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, void* unused)
{
    return 0;
}

int Window_MessageLoop()
{
    jkMain_GuiAdvance();
    Window_msg_main_handler(g_hWnd, WM_PAINT, 0, 0);
    
#ifdef TILE_SW_RASTER
	Window_SdlUpdate();
#endif
   //Window_SdlUpdate();
    return 0;
}

#endif // SDL2_RENDER

void Window_SetDrawHandlers(WindowDrawHandler_t a1, WindowDrawHandler_t a2)
{
    Window_drawAndFlip = a1;
    Window_setCooperativeLevel = a2;
}

void Window_GetDrawHandlers(WindowDrawHandler_t *a1, WindowDrawHandler_t *a2)
{
    *a1 = Window_drawAndFlip;
    *a2 = Window_setCooperativeLevel;
}
