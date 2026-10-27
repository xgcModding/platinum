#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>

static HWND gameWindow;
static SDL_Window *sdlWindow;
static HMENU menuBar;
static LONG pendingAction;
static Uint32 gameWindowId;
static unsigned windowedMode = 2;

extern unsigned SIM_GetWindowSizeMode(void) __attribute__((weak));
extern void SIM_RequestWindowSizeMode(unsigned mode) __attribute__((weak));

static void UpdateMenuVisibility(void)
{
    if (!sdlWindow || !menuBar) return;
    HMENU visibleMenu = (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN) ? NULL : menuBar;
    if (GetMenu(gameWindow) != visibleMenu) {
        SetMenu(gameWindow, visibleMenu);
        DrawMenuBar(gameWindow);
    }
}

static int WatchMenu(void *unused, SDL_Event *event)
{
    (void)unused;
    if (event->type == SDL_KEYDOWN && event->key.windowID == gameWindowId
        && event->key.keysym.sym == SDLK_F11 && !event->key.repeat
        && SIM_GetWindowSizeMode && SIM_RequestWindowSizeMode
        && !(event->key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI))) {
        unsigned mode = SIM_GetWindowSizeMode();
        if (mode >= 1 && mode <= 4) windowedMode = mode;
        SIM_RequestWindowSizeMode(mode == 5 ? windowedMode : 5);
    }
    if (event->type == SDL_SYSWMEVENT && event->syswm.msg->subsystem == SDL_SYSWM_WINDOWS) {
        SDL_SysWMmsg *message = event->syswm.msg;
        if (message->msg.win.hwnd == gameWindow && message->msg.win.msg == WM_COMMAND) {
            unsigned command = LOWORD(message->msg.win.wParam);
            if (command == 1001 || command == 1002)
                InterlockedExchange(&pendingAction, command - 1000);
        }
    }
    return 1;
}

void PlatformWindow_ConfigureWindows(void *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return;
    sdlWindow = window;
    gameWindow = info.info.win.window;
    gameWindowId = SDL_GetWindowID(window);
    SetWindowTextW(gameWindow, L"Pok\u00e9mon Platinum");
    HINSTANCE instance = GetModuleHandleW(NULL);
    HICON large = LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    HICON small = LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    SendMessageW(gameWindow, WM_SETICON, ICON_BIG, (LPARAM)large);
    SendMessageW(gameWindow, WM_SETICON, ICON_SMALL, (LPARAM)small);
    menuBar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU input = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, 1001, L"Import ROM...");
    AppendMenuW(input, MF_STRING, 1002, L"Configure Controls...");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)input, L"&Input");
    UpdateMenuVisibility();
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    SDL_AddEventWatch(WatchMenu, NULL);
}

int PlatformMenu_Update(int canImport)
{
    UpdateMenuVisibility();
    if (menuBar) EnableMenuItem(menuBar, 1001, MF_BYCOMMAND | (canImport ? MF_ENABLED : MF_GRAYED));
    return (int)InterlockedExchange(&pendingAction, 0);
}
