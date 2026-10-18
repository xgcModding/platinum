// Qt owns the native menubar and shell; SDL keeps the game surface and input.
// Wayland interop follows SDL's documented external wl_display/wl_surface API.
#include <SDL2/SDL.h>
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QScreen>
#include <QSurfaceFormat>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

extern "C" unsigned SIM_GetWindowSizeMode(void) __attribute__((weak));
extern "C" void SIM_RequestWindowSizeMode(unsigned) __attribute__((weak));

namespace {
QApplication *application;
QMainWindow *shell;
QWindow *viewport;
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
    QObject::connect(importAction, &QAction::triggered, [] { pendingAction = 1; });
    auto *input = shell->menuBar()->addMenu("&Input");
    auto *controls = input->addAction("Configure Controls…");
    QObject::connect(controls, &QAction::triggered, [] { pendingAction = 2; });
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
    auto *container = QWidget::createWindowContainer(viewport, shell);
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
    fprintf(stderr, "[native-menu] Qt %s: File > Import ROM; Input > Configure Controls\n", qPrintable(application->platformName()));
    return gameWindow;
}

extern "C" int PlatformMenu_Update(int canImport)
{
    if (!shell) return 0;
    importAction->setEnabled(canImport != 0);
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
    return action;
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
