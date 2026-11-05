#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#ifdef Q_OS_MACOS
#include <ApplicationServices/ApplicationServices.h>
#endif
#ifdef Q_OS_UNIX
#include <execinfo.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <dlfcn.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app_icon.h"
#include "installer_window.h"
#include "shots.h"
#include "steam_artwork.h"
#include "steam_shortcut.h"
#include "theme.h"

#ifndef TAKEDAWN_SETUP_VERSION
#define TAKEDAWN_SETUP_VERSION "0.1.0"
#endif

// The program's name in the window titles.
#define TAKEDAWN_APP_TITLE "Takedawn"

// Test and unattended runs must never raise the operating system's crash dialog
// on someone's screen: a fatal Qt message (no platform plugin, say) or a crash
// ends the process by itself, with a message and a status (3 fatal message, 4
// crash). Armed before QApplication exists, by TAKEDAWN_SETUP_TEST=1 or by any
// of the test options.
static void QuietCrash(int) {
  static const char message[] = "takedawn-setup: crashed (test mode)\n";
#ifdef Q_OS_UNIX
  (void)!write(2, message, sizeof(message) - 1);
  void* frames[24];
  const int n = backtrace(frames, 24);  // where it happened, for the log
  backtrace_symbols_fd(frames, n, 2);
  _exit(4);
#else
  std::_Exit(4);
#endif
}

static void QuietMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
  const QByteArray text = message.toLocal8Bit() + "\n";
#ifdef Q_OS_UNIX
  (void)!write(2, text.constData(), size_t(text.size()));
  if (type == QtFatalMsg) _exit(3);
#else
  fwrite(text.constData(), 1, size_t(text.size()), stderr);
  if (type == QtFatalMsg) std::_Exit(3);
#endif
}

static void ArmQuietFailures(int argc, char** argv) {
  bool armed = qEnvironmentVariableIntValue("TAKEDAWN_SETUP_TEST") != 0;
  for (int i = 1; i < argc && !armed; ++i) {
    const QByteArray a(argv[i]);
    armed = a == "--shots" || a == "--check-shot" || a == "--e2e-shots" || a == "--show-grab" || a == "--pad-shots";
  }
  if (!armed) return;
#ifdef Q_OS_MACOS
  // Test runs have no window: no Dock icon or menu bar either, so nobody can "quit"
  // them by accident (a Quit from the Dock ends a Cocoa app with status 0).
  bool visible = false;
  for (int i = 1; i < argc; ++i) visible = visible || QByteArray(argv[i]) == "--show-grab";
  if (!visible) {
    ProcessSerialNumber psn = {0, kCurrentProcess};
    TransformProcessType(&psn, kProcessTransformToUIElementApplication);
  }
#endif
  qInstallMessageHandler(QuietMessageHandler);
  for (int sig : {SIGSEGV, SIGABRT, SIGILL, SIGFPE
#ifdef SIGBUS
                  , SIGBUS
#endif
       }) {
    std::signal(sig, QuietCrash);
  }
}

// "Couch" mode: there may be no mouse and no keyboard, only a controller and a
// touch screen. That is so under gamescope (a Steam Deck in Game Mode, Bazzite's
// and SteamOS's game session); --couch and TAKEDAWN_SETUP_COUCH=1/0 say it outright.
static bool CouchWanted(int argc, char** argv) {
  const QByteArray forced = qgetenv("TAKEDAWN_SETUP_COUCH");
  if (forced == "1") return true;
  if (forced == "0") return false;
  for (int i = 1; i < argc; ++i) {
    if (QByteArray(argv[i]) == "--couch") return true;
  }
#ifdef Q_OS_LINUX
  return !qEnvironmentVariableIsEmpty("GAMESCOPE_WAYLAND_DISPLAY") || qgetenv("XDG_CURRENT_DESKTOP") == "gamescope";
#else
  return false;
#endif
}

// Under gamescope the window is the screen, whatever its size in pixels: the
// whole window is scaled so that it is laid out 640 units high (1.25 on the
// Deck's 1280x800 panel), which is what makes the text readable at arm's length
// and the buttons finger sized. A QT_SCALE_FACTOR already set is left alone.
// The screen's height is asked of the X server directly, because the factor
// must be known before Qt starts.
static void SetCouchScale() {
  if (!qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) return;
  int height = 0;
#ifdef Q_OS_LINUX
  if (void* x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL)) {
    using Open = void* (*)(const char*);
    using Screen = int (*)(void*);
    using Height = int (*)(void*, int);
    using Close = int (*)(void*);
    const auto open = reinterpret_cast<Open>(dlsym(x11, "XOpenDisplay"));
    const auto screen = reinterpret_cast<Screen>(dlsym(x11, "XDefaultScreen"));
    const auto display_height = reinterpret_cast<Height>(dlsym(x11, "XDisplayHeight"));
    const auto close = reinterpret_cast<Close>(dlsym(x11, "XCloseDisplay"));
    if (open && screen && display_height && close) {
      if (void* display = open(nullptr)) {
        height = display_height(display, screen(display));
        close(display);
      }
    }
    // (left loaded: Qt's own platform plugin uses the same library)
  }
#endif
  if (height <= 0) height = 800;
  const double scale = std::clamp(std::round(height / 640.0 * 20.0) / 20.0, 1.0, 4.0);
  qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));
}

// --steam-art: the library art for the Takedawn shortcuts Steam already has, with no
// window (the game's overlay runs it: "Add Steam library art"). The art comes from the
// game's files under --dest (the data root; else the one `--program --data-status` names),
// Takedawn's own when they cannot be read. One line on stdout for the person, the details
// on stderr; status 0 when files were written, 1 when not.
static int SteamArtMain(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");  // nothing is shown, whatever display there is
  QGuiApplication app(argc, argv);
  QString program, dest;
  for (int i = 1; i + 1 < argc; ++i) {
    if (QByteArray(argv[i]) == "--program") program = QString::fromLocal8Bit(argv[i + 1]);
    if (QByteArray(argv[i]) == "--dest") dest = QString::fromLocal8Bit(argv[i + 1]);
  }
  if (dest.isEmpty() && !program.isEmpty()) {
    QProcess query;
    query.start(program, {"--data-status"});
    if (query.waitForFinished(20000)) {
      for (const QByteArray& line : query.readAllStandardOutput().split('\n'))
        if (line.startsWith("LOG data root ")) dest = QString::fromUtf8(line.mid(14)).trimmed();
    }
  }
  const QString home = QDir::homePath();
  std::vector<steam::User> users = steam::UserdataConfigs(home.toStdString());
  // A test names the config folder outright, as the setup window's Add to Steam allows.
  if (qEnvironmentVariableIsSet("TAKEDAWN_STEAM_CONFIG"))
    users = {{"test", qEnvironmentVariable("TAKEDAWN_STEAM_CONFIG").toStdString(), {}}};
  QString note;
  const steamart::Art art = steamart::ForShortcut(dest.isEmpty() ? QString() : QDir(dest).filePath("Burnout 3 Takedown"),
                                                  QImage(":/takedawn/icon-512.png"), &note);
  QTextStream err(stderr);
  err << "[steam] " << note << "\n";
  const steamart::Existing done = steamart::WriteForExisting(users, art);
  for (const QString& line : done.log) err << line << "\n";
  QTextStream(stdout) << done.Line() << "\n";
  return done.files > 0 ? 0 : 1;
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (QByteArray(argv[i]) == "--steam-art") return SteamArtMain(argc, argv);
  ArmQuietFailures(argc, argv);
  const bool couch = CouchWanted(argc, argv);
  if (couch) SetCouchScale();
  QApplication app(argc, argv);
  QApplication::setApplicationName("Takedawn Setup");
  QApplication::setApplicationDisplayName(TAKEDAWN_APP_TITLE);
  QApplication::setApplicationVersion(TAKEDAWN_SETUP_VERSION);
  QApplication::setWindowIcon(AppIcon());

  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Takedawn's setup window: puts the game's data in place from your own Burnout 3: Takedown disc "
      "image or folder, then starts the game. The game opens it when its data is missing.");
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption program("program", "The game's executable (default: found beside this program).", "file");
  QCommandLineOption dest("dest", "The data folder the game was told to use (its --data); default: the game's own.", "directory");
  QCommandLineOption iso("iso", "Disc image or extracted folder to start with.", "path");
  QCommandLineOption shaders_opt("shaders", "Start with \"Prepare graphics\" ticked (it is the default).");
  QCommandLineOption no_shaders_opt("no-shaders", "Start with \"Prepare graphics\" unticked.");
  QCommandLineOption upscale_opt("upscale", "Start with \"Sharper menu pictures\" ticked (where the game offers it).");
  QCommandLineOption no_upscale_opt("no-upscale", "Start with \"Sharper menu pictures\" unticked.");
  QCommandLineOption size("size", "Window size, e.g. 780x680.", "WxH");
  QCommandLineOption platform("platform", "Test: show another system's options (macos, linux, windows).", "name");
  QCommandLineOption shots("shots", "Test: every state against the fake installer named by --program; PNGs here.", "directory");
  QCommandLineOption check_shot("check-shot", "Test: have the game check --iso; a PNG of the window here.", "directory");
  QCommandLineOption e2e_shots("e2e-shots", "Test: install from --iso with the real game (into --dest or the per-user "
                                            "folder), PNGs of each stage here; status 0 when installed.", "directory");
  QCommandLineOption show_grab("show-grab", "Test: show the window, write a PNG of it to this file after two seconds and quit.", "file");
  QCommandLineOption couch_opt("couch", "Fill the screen, finger-sized controls, controller hints (as under gamescope).");
  QCommandLineOption no_pad("no-pad", "Do not read the controllers.");
  QCommandLineOption pad_shots("pad-shots", "Test: the install driven by controller presses alone, against the fake "
                                            "installer named by --program and a made-up tree of folders; PNGs here.", "directory");
  parser.addOptions({program, dest, iso, shaders_opt, no_shaders_opt, upscale_opt, no_upscale_opt, size, platform, shots, check_shot, e2e_shots, show_grab, couch_opt,
                     no_pad, pad_shots});
  parser.process(app);

  LaunchConfig config;
  config.program = parser.value(program);
  config.dest = parser.value(dest);
  config.disc = parser.value(iso);
  config.platform = parser.isSet(platform) ? parser.value(platform) : qEnvironmentVariable("TAKEDAWN_SETUP_PLATFORM");
  config.shaders = !parser.isSet(no_shaders_opt) || parser.isSet(shaders_opt);
  config.upscale = parser.isSet(no_upscale_opt) ? 0 : parser.isSet(upscale_opt) ? 1 : -1;
  config.couch = couch;
  config.pad = !parser.isSet(no_pad);

  theme::Apply(app);
  QSize wanted;
  if (parser.isSet(size)) {
    const QStringList wh = parser.value(size).split('x');
    if (wh.size() == 2) wanted = QSize(wh[0].toInt(), wh[1].toInt());
  }

  if (parser.isSet(shots) || parser.isSet(check_shot) || parser.isSet(e2e_shots) || parser.isSet(pad_shots)) {
    LaunchConfig test = config;
    test.disc.clear();  // the runner chooses it
    if (parser.isSet(pad_shots)) {
      return ShotRunner(test, wanted, parser.value(pad_shots), "pad").RunPad() ? 0 : 1;
    }
    if (parser.isSet(shots)) {
      return ShotRunner(test, wanted, parser.value(shots), "setup").Run() ? 0 : 1;
    }
    if (parser.isSet(check_shot)) {
      return ShotRunner(test, wanted, parser.value(check_shot), "check").RunRealCheck(parser.value(iso)) ? 0 : 3;
    }
    return ShotRunner(test, wanted, parser.value(e2e_shots), "e2e").RunRealInstall(parser.value(iso)) ? 0 : 1;
  }

  InstallerWindow window(config);
  if (wanted.isValid()) window.resize(wanted);
  // RECOMP_DEBUG=window_display=<n>, as the game takes it: the window opens centred on
  // display n (0 = the main one), and Play hands the same setting on to the game. For
  // tests on a machine with more than one display.
  for (const QString& item : qEnvironmentVariable("RECOMP_DEBUG").split(QRegularExpression("[,; ]"), Qt::SkipEmptyParts)) {
    if (!item.startsWith("window_display=")) continue;
    QList<QScreen*> screens = QGuiApplication::screens();
    if (QScreen* main = QGuiApplication::primaryScreen()) {
      screens.removeAll(main);
      screens.prepend(main);
    }
    const int n = item.mid(int(strlen("window_display="))).toInt();
    if (n >= 0 && n < screens.size()) {
      const QRect area = screens[n]->availableGeometry();
      QRect frame(QPoint(), window.size().boundedTo(area.size()));
      frame.moveCenter(area.center());
      window.setGeometry(frame);
      if (qEnvironmentVariableIntValue("TAKEDAWN_SETUP_TRACE")) {
        QTextStream(stderr) << "[SETUP] display " << n << ": " << screens[n]->name() << Qt::endl;
      }
    }
  }
  if (parser.isSet(show_grab)) {
    // The one on-screen start test of the real platform plugin: up, grabbed, gone.
    if (couch && !wanted.isValid()) window.showFullScreen();
    else window.show();
    QTimer::singleShot(2000, &window, [&] {
      const bool ok = window.grab().save(parser.value(show_grab));
      QCoreApplication::exit(ok ? 0 : 1);
    });
    return app.exec();
  }
  if (couch && !wanted.isValid()) window.showFullScreen();
  else window.show();
  window.raise();
  window.activateWindow();
  // A test of the real program with a made-up controller (shots.h).
  if (!qEnvironmentVariableIsEmpty("TAKEDAWN_SETUP_PAD_SCRIPT")) {
    (new PadScript(&window, qEnvironmentVariable("TAKEDAWN_SETUP_PAD_SCRIPT")))->Begin();
  }
  return app.exec();
}
