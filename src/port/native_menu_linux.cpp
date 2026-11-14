// Qt owns the native menubar and shell; SDL keeps the game surface and input.
// Wayland interop follows SDL's documented external wl_display/wl_surface API.
#include <SDL2/SDL.h>
#include "app_menu_actions.h"
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QScreen>
#include <QSurfaceFormat>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" unsigned SIM_GetWindowSizeMode(void) __attribute__((weak));
extern "C" void SIM_RequestWindowSizeMode(unsigned) __attribute__((weak));

namespace {
QApplication *application;
QMainWindow *shell;
QWindow *viewport;
QWidget *viewportContainer;
SDL_Window *gameWindow;
QAction *importAction;
int pendingAction;
unsigned windowedMode = 2;

// SDL2-compat forwards SDL_Window pointers to SDL3. Load the properties API
// explicitly so SDL2 and SDL3 entry points with different signatures never mix.
struct SDL3API {
    unsigned (*getGlobalProperties)();
    unsigned (*createProperties)();
    void (*destroyProperties)(unsigned);
    bool (*setPointer)(unsigned, const char *, void *);
    bool (*setBoolean)(unsigned, const char *, bool);
    bool (*setNumber)(unsigned, const char *, long long);
    SDL_Window *(*createWindow)(unsigned);
} api;

bool loadSDL3()
{
    void *library = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
#define LOAD(member, symbol) api.member = reinterpret_cast<decltype(api.member)>(dlsym(library, symbol)); if (!api.member) return false
    LOAD(getGlobalProperties, "SDL_GetGlobalProperties");
    LOAD(createProperties, "SDL_CreateProperties");
    LOAD(destroyProperties, "SDL_DestroyProperties");
    LOAD(setPointer, "SDL_SetPointerProperty");
    LOAD(setBoolean, "SDL_SetBooleanProperty");
    LOAD(setNumber, "SDL_SetNumberProperty");
    LOAD(createWindow, "SDL_CreateWindowWithProperties");
#undef LOAD
    return true;
}

class GameShell : public QMainWindow {
    void changeEvent(QEvent *event) override
    {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange) {
            menuBar()->setVisible(!isFullScreen());
        }
    }

    void closeEvent(QCloseEvent *event) override
    {
        // Let the game shut down its render threads before Qt destroys surfaces.
        event->ignore();
        SDL_Event quit = {};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
};

class WindowKeys : public QObject {
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (!shell || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_F11 || key->modifiers() != Qt::NoModifier) return false;
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()
            && SIM_GetWindowSizeMode && SIM_RequestWindowSizeMode) {
            unsigned mode = SIM_GetWindowSizeMode();
            if (mode >= 1 && mode <= 4) windowedMode = mode;
            SIM_RequestWindowSizeMode(mode == 5 ? windowedMode : 5);
        }
        return true;
    }
};
WindowKeys windowKeys;

// The Qt-created child window belongs to Qt's X11/Wayland event dispatcher.
// SDL2-compat may not receive pointer events for that *foreign* surface under
// Gamescope. Translate Qt's viewport events to SDL's normal input queue so
// ImGui and the emulated touch panel receive the same clicks.
//
// SDL_PushEvent deliberately does not change SDL_GetMouseState(). The simulator
// therefore also queries PlatformWindow_GetMousePosition() (libntr patch).
// Keeping both pieces is essential for correct touch coordinates.
int qtMouseX = 0;
int qtMouseY = 0;
bool qtMouseValid = false;
Uint32 qtMouseButtons = 0;

bool MouseBridgeEnabled()
{
    return qEnvironmentVariable("POKEPLATINUM_QT_MOUSE_BRIDGE") != "0";
}

bool MouseBridgeDebug()
{
    return qEnvironmentVariable("POKEPLATINUM_MOUSE_DEBUG") == "1";
}

Uint8 SDLButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:   return SDL_BUTTON_LEFT;
    case Qt::MiddleButton: return SDL_BUTTON_MIDDLE;
    case Qt::RightButton:  return SDL_BUTTON_RIGHT;
    case Qt::BackButton:   return SDL_BUTTON_X1;
    case Qt::ForwardButton:return SDL_BUTTON_X2;
    default:               return 0;
    }
}

class GameMouseBridge : public QObject {
    std::vector<SDL_Event> pendingReleases;
    // Qt may deliver a native click to both QWindow and its QWidget holder.
    // Avoid forwarding the same native event twice.
    ulong lastClickTimestamp = 0;
    QEvent::Type lastClickType = QEvent::None;
    Qt::MouseButton lastClickButton = Qt::NoButton;
    QPointF lastClickPosition;

    void sendMotion(const QPointF &point)
    {
        // Qt uses logical viewport coordinates; SDL's wrapped window uses the
        // actual SDL window dimensions (which may differ on a scaled display).
        int width = 0, height = 0;
        SDL_GetWindowSize(gameWindow, &width, &height);
        if (width <= 0 || height <= 0 || viewport->width() <= 0 || viewport->height() <= 0) return;
        const int x = qRound(point.x() * width / viewport->width());
        const int y = qRound(point.y() * height / viewport->height());
        SDL_Event motion = {};
        motion.type = SDL_MOUSEMOTION;
        motion.motion.windowID = SDL_GetWindowID(gameWindow);
        motion.motion.state = qtMouseButtons;
        motion.motion.x = x;
        motion.motion.y = y;
        motion.motion.xrel = qtMouseValid ? x - qtMouseX : 0;
        motion.motion.yrel = qtMouseValid ? y - qtMouseY : 0;
        qtMouseX = x;
        qtMouseY = y;
        qtMouseValid = true;
        SDL_PushEvent(&motion);
    }

    void sendButton(Uint8 button, bool down, Uint8 clicks)
    {
        if (!button) return;
        if (down) qtMouseButtons |= SDL_BUTTON(button);
        else qtMouseButtons &= ~SDL_BUTTON(button);
        SDL_Event event = {};
        event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.windowID = SDL_GetWindowID(gameWindow);
        event.button.button = button;
        event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.button.clicks = clicks;
        event.button.x = qtMouseX;
        event.button.y = qtMouseY;
        // Qt pumps after the simulator's SDL_PollEvent loop. If press and
        // release are both delivered in one Qt pump, the game would consume
        // both before sampling its DS touch state, losing short taps. Put a
        // release onto SDL's queue in the *following* Qt pump instead.
        if (!down) {
            pendingReleases.push_back(event);
            return;
        }
        const int queued = SDL_PushEvent(&event);
        if (MouseBridgeDebug()) {
            fprintf(stderr, "[qt-sdl-mouse] down button=%u x=%d y=%d queued=%d\n",
                    button, qtMouseX, qtMouseY, queued);
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((watched != viewport && watched != viewportContainer)
            || !gameWindow || !MouseBridgeEnabled()) return false;
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (event->type() != QEvent::MouseMove && mouse->timestamp() != 0) {
                if (mouse->timestamp() == lastClickTimestamp
                    && event->type() == lastClickType
                    && mouse->button() == lastClickButton
                    && mouse->position() == lastClickPosition) return false;
                lastClickTimestamp = mouse->timestamp();
                lastClickType = event->type();
                lastClickButton = mouse->button();
                lastClickPosition = mouse->position();
            }
            sendMotion(mouse->position());
            if (event->type() != QEvent::MouseMove) {
                sendButton(SDLButton(mouse->button()),
                           event->type() != QEvent::MouseButtonRelease,
                           event->type() == QEvent::MouseButtonDblClick ? 2 : 1);
            }
            // Do not consume the Qt event: Qt still owns focus and the shell.
            return false;
        }
        case QEvent::FocusOut:
        case QEvent::Hide:
            // A gamepad/Steam overlay can steal focus mid-drag. Avoid a stuck
            // stylus or stuck ImGui button when the release is sent elsewhere.
            for (Uint8 button : {SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE,
                                 SDL_BUTTON_RIGHT, SDL_BUTTON_X1, SDL_BUTTON_X2}) {
                if (qtMouseButtons & SDL_BUTTON(button)) sendButton(button, false, 1);
            }
            qtMouseValid = false;
            return false;
        default:
            return false;
        }
    }

public:
    void flushReleases()
    {
        for (const SDL_Event &event : pendingReleases) {
            SDL_Event copy = event;
            const int queued = SDL_PushEvent(&copy);
            if (MouseBridgeDebug()) {
                fprintf(stderr, "[qt-sdl-mouse] up button=%u x=%d y=%d queued=%d\n",
                        copy.button.button, copy.button.x, copy.button.y, queued);
            }
        }
        pendingReleases.clear();
    }
};
GameMouseBridge gameMouseBridge;
}

extern "C" void PlatformMenu_RequestAction(int action)
{
    if (action == LAKE_TRIO_MENU_IMPORT_ROM || action == LAKE_TRIO_MENU_CONFIGURE_CONTROLS)
        pendingAction = action;
}

extern "C" void PlatformWindow_Init()
{
    static int argc = 1;
    static char name[] = "pokeplatinum";
    static char *argv[] = { name, nullptr };
    // A packaged application must never mix its Qt with host Qt plugins.
    const QString appDir = qEnvironmentVariable("APPDIR");
    if (!appDir.isEmpty()) {
        QCoreApplication::setLibraryPaths({appDir + "/usr/plugins"});
    }
    // Use the same EGL backend for Qt's X11 visual and SDL's foreign window.
    // GLX independently chooses a framebuffer config and can fail with BadMatch.
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
    application = new QApplication(argc, argv);
    application->setApplicationName("pokeplatinum");
    application->setDesktopFileName("pokeplatinum");
    application->setWindowIcon(QIcon("icon.png"));
    application->setQuitOnLastWindowClosed(false);
    application->installEventFilter(&windowKeys);
    if (application->platformName().startsWith("wayland")) {
        SDL_version version;
        SDL_GetVersion(&version);
        if (version.minor < 32 || !loadSDL3()) {
            fprintf(stderr, "Native Wayland menus require SDL2-compat and SDL3.\n");
            exit(EXIT_FAILURE);
        }
        auto *native = application->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!native || !api.setPointer(api.getGlobalProperties(),
                "SDL.video.wayland.wl_display", native->display())) {
            fprintf(stderr, "Unable to share the native Wayland display with SDL.\n");
            exit(EXIT_FAILURE);
        }
        SDL_SetHint("SDL_VIDEODRIVER", "wayland");
        SDL_SetHint("SDL_VIDEO_DRIVER", "wayland");
    } else {
        SDL_SetHint("SDL_VIDEODRIVER", "x11");
        SDL_SetHint("SDL_VIDEO_DRIVER", "x11");
        SDL_SetHint("SDL_VIDEO_FORCE_EGL", "1");
    }
}

extern "C" SDL_Window *PlatformWindow_Create(const char *title, int width, int height, int monitor, int fullscreen)
{
    shell = new GameShell;
    shell->setWindowTitle(QString::fromUtf8(title));
    // Keep File and Input in the application window, even with a global-menu applet.
    shell->menuBar()->setNativeMenuBar(false);
    auto *file = shell->menuBar()->addMenu("&File");
    importAction = file->addAction("Import ROM…");
    importAction->setShortcut(QKeySequence::Open);
    QObject::connect(importAction, &QAction::triggered, [] { PlatformMenu_RequestAction(LAKE_TRIO_MENU_IMPORT_ROM); });
    auto *input = shell->menuBar()->addMenu("&Input");
    auto *controls = input->addAction("Configure Controls…");
    QObject::connect(controls, &QAction::triggered, [] { PlatformMenu_RequestAction(LAKE_TRIO_MENU_CONFIGURE_CONTROLS); });
    viewport = new QWindow;
    viewport->setSurfaceType(QSurface::OpenGLSurface);
    // The X11 visual is chosen when Qt creates this window. Match the game's
    // framebuffer requirements before SDL attaches its OpenGL context.
    QSurfaceFormat format;
    format.setVersion(4, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(0);
    format.setAlphaBufferSize(0);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    viewport->setFormat(format);
    // A QWindow event filter sees mouse clicks even when SDL receives no
    // foreign-window events from the Gamescope compositor.
    viewport->installEventFilter(&gameMouseBridge);
    auto *container = QWidget::createWindowContainer(viewport, shell);
    viewportContainer = container;
    container->installEventFilter(&gameMouseBridge);
    container->setFocusPolicy(Qt::StrongFocus);
    shell->setCentralWidget(container);
    shell->resize(width, height + shell->menuBar()->sizeHint().height());
    auto screens = application->screens();
    if (monitor >= 0 && monitor < screens.size()) shell->setScreen(screens[monitor]);
    shell->menuBar()->setVisible(!fullscreen);
    if (fullscreen) shell->showFullScreen(); else shell->show();
    application->processEvents();
    viewport->create();
    if (application->platformName().startsWith("wayland")) {
        void *surface = application->platformNativeInterface()->nativeResourceForWindow("surface", viewport);
        unsigned props = api.createProperties();
        api.setPointer(props, "SDL.window.create.wayland.wl_surface", surface);
        api.setBoolean(props, "SDL.window.create.opengl", true);
        api.setNumber(props, "SDL.window.create.width", qRound(viewport->width() * viewport->devicePixelRatio()));
        api.setNumber(props, "SDL.window.create.height", qRound(viewport->height() * viewport->devicePixelRatio()));
        gameWindow = surface ? api.createWindow(props) : nullptr;
        api.destroyProperties(props);
    } else {
        SDL_SetHint("SDL_VIDEO_FOREIGN_WINDOW_OPENGL", "1");
        gameWindow = SDL_CreateWindowFrom(reinterpret_cast<void *>(viewport->winId()));
    }
    container->setFocus();
    fprintf(stderr, "[native-menu] Qt %s: File > Import ROM; Input > Configure Controls; Qt->SDL mouse bridge %s\n",
            qPrintable(application->platformName()), MouseBridgeEnabled() ? "enabled" : "disabled");
    return gameWindow;
}

extern "C" int PlatformMenu_Update(int canImport)
{
    if (!shell) return 0;
    importAction->setEnabled(canImport != 0);
    // Enqueue the previous pump's button releases only after an SDL frame has
    // had the opportunity to observe the matching button presses.
    gameMouseBridge.flushReleases();
    application->processEvents();
    int width = qRound(viewport->width() * viewport->devicePixelRatio());
    int height = qRound(viewport->height() * viewport->devicePixelRatio());
    int oldWidth, oldHeight;
    SDL_GetWindowSize(gameWindow, &oldWidth, &oldHeight);
    if (width > 0 && height > 0 && (width != oldWidth || height != oldHeight)) {
        SDL_SetWindowSize(gameWindow, width, height);
    }
    int action = pendingAction;
    pendingAction = 0;
    return (action == LAKE_TRIO_MENU_IMPORT_ROM && !canImport) ? LAKE_TRIO_MENU_NONE : action;
}

extern "C" int PlatformWindow_SetFullscreen(SDL_Window *window, unsigned flags)
{
    if (window != gameWindow) return SDL_SetWindowFullscreen(window, flags);
    if (flags && SIM_GetWindowSizeMode) {
        unsigned mode = SIM_GetWindowSizeMode();
        if (mode >= 1 && mode <= 4) windowedMode = mode;
    }
    shell->menuBar()->setVisible(flags == 0);
    if (flags) shell->showFullScreen(); else shell->showNormal();
    return 0;
}

extern "C" void PlatformWindow_SetSize(SDL_Window *window, int width, int height)
{
    if (window != gameWindow) { SDL_SetWindowSize(window, width, height); return; }
    shell->resize(width, height + shell->menuBar()->sizeHint().height());
}

extern "C" void PlatformWindow_SetPosition(SDL_Window *window, int x, int y)
{
    if (window != gameWindow) { SDL_SetWindowPosition(window, x, y); return; }
    if (SDL_WINDOWPOS_ISCENTERED(x)) {
        int monitor = x & 0xffff;
        auto screens = application->screens();
        if (monitor >= 0 && monitor < screens.size()) shell->setScreen(screens[monitor]);
    } else {
        shell->move(x, y);
    }
}

// Called by the Linux libntr touch-panel path after SDL_GetMouseState().
// Qt input events are handled on the same rendering/event-loop thread.
extern "C" bool PlatformWindow_GetMousePosition(int *x, int *y)
{
    if (!MouseBridgeEnabled() || !qtMouseValid) return false;
    if (x) *x = qtMouseX;
    if (y) *y = qtMouseY;
    return true;
}
