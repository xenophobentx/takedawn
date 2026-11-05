#include "messages.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace messages {

namespace {
QString T(const char* text) { return QCoreApplication::translate("Messages", text); }

// "3" for 3 000 000 000, "2.5" for 2 500 000 000, "25" for 25 GB.
QString Gb(qint64 bytes) {
  const double gb = bytes / 1e9;
  QString text = QString::number(gb, 'f', gb < 10 ? 1 : 0);
  if (text.endsWith(".0")) text.chop(2);
  return text;
}

// Every package takes the three retail discs (src/game/package.h).
QString DiscName() { return T("Burnout 3: Takedown disc"); }
}  // namespace

bool LooksLikeArchive(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) return false;
  const QString suffix = info.suffix().toLower();
  if (suffix == "zip" || suffix == "7z" || suffix == "rar") return true;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QByteArray head = file.read(8);
  return head.startsWith("PK\x03\x04") || head.startsWith("PK\x05\x06") ||  // zip (the second: an empty one)
         head.startsWith("7z\xBC\xAF\x27\x1C") || head.startsWith("Rar!\x1A\x07");
}

QString StepTitle(const QString& step) {
  if (step == "disc") return T("Game data");
  if (step == "verify") return T("Checking the installed data");
  if (step == "shaders") return T("Graphics preparation (optional)");
  if (step == "upscale") return T("Sharper menu pictures (optional)");
  if (step == "steam") return T("Steam library");
  return step;
}

const QStringList& InstallCodes() {
  static const QStringList codes{"no-source",     "source-unreadable", "not-xbox-image", "xbox360-disc",
                                 "no-xbe",        "wrong-game",        "wrong-version",  "truncated-image",
                                 "damaged-source", "disk-full",        "no-permission",  "write-failed",
                                 "cancelled",     "internal"};
  return codes;
}

bool IsDiscProblem(const QString& code) {
  static const QStringList disc{"no-source",  "source-unreadable", "not-xbox-image",  "xbox360-disc", "no-xbe",
                                "wrong-game", "wrong-version",     "truncated-image", "damaged-source"};
  return disc.contains(code);
}

QString DamagedFile(const QString& message) {
  static const QRegularExpression re(
      R"(^(\S.*?) (?:in the (?:disc image|folder) is not what the disc holds|is missing from the|has \d+ bytes in the) )");
  const auto match = re.match(message);
  return match.hasMatch() ? match.captured(1) : QString();
}

Explained ExplainInstall(const QString& code, const InstallFacts& facts) {
  if (code == "no-source") {
    return {T("That file or folder is not there any more."),
            T("It may have been moved, or the drive it is on was unplugged. Choose the disc image or the folder again.")};
  }
  if (code == "source-unreadable") {
    return {T("The disc image or folder cannot be read."),
            T("Check that you may open it and that the drive it is on is connected. On macOS a file in Downloads, "
              "Documents or on an external drive may need your permission first: choose it with the buttons here.")};
  }
  if (code == "not-xbox-image") {
    QString advice = T("Takedawn needs the .iso of the original Xbox disc of Burnout 3: Takedown, or the folder its files "
                       "were extracted to.");
    if (LooksLikeArchive(facts.source)) advice += " " + T("A compressed archive (.zip, .7z, .rar) has to be unpacked first.");
    return {T("This file is not an Xbox disc image."), advice};
  }
  if (code == "xbox360-disc") {
    return {T("This is an Xbox 360 disc."),
            T("Takedawn runs the original Xbox game. Use the %1 for the original Xbox.").arg(DiscName())};
  }
  if (code == "no-xbe") {
    return {T("There is no Xbox game here."),
            T("The image or folder has no default.xbe. Choose the .iso of the game disc, or the folder that holds "
              "the disc's files (the one with default.xbe in it).")};
  }
  if (code == "wrong-game") {
    return {T("This is the disc of another game."),
            T("Takedawn needs the %1 for the original Xbox.").arg(DiscName())};
  }
  if (code == "wrong-version") {
    return {T("This is Burnout 3: Takedown, but not the version Takedawn supports."),
            T("Three retail discs work: the North America (NTSC-U) one, and the two European (PAL) ones, French, "
              "German and Italian, and English, Spanish, Dutch and Swedish. The Japanese disc and patched or modified "
              "copies hold different code and cannot be used.")};
  }
  if (code == "truncated-image") {
    return {T("The disc image is incomplete."),
            T("The file ends before the disc does: a download or a copy that was cut short. Make a new image of your disc.")};
  }
  if (code == "damaged-source") {
    if (facts.in_place) {
      return {T("The installed game files are not complete."),
              T("A file is missing or is not what the disc holds. Choose your disc image or folder below: only what "
                "is wrong is copied again.")};
    }
    return {T("The disc image or folder is damaged."),
            T("A file is missing or is not what the disc holds. Choose another copy of the disc image, or make a new "
              "image of your disc. Resume only helps after the file is replaced.")};
  }
  if (code == "disk-full") {
    QString advice;
    if (facts.needed > 0 && facts.data > 0 && facts.free >= 0) {
      advice = T("Takedawn needs %1 GB free (the game uses %2 GB). You have %3 GB. Free up space on this disk, then press "
                 "Resume.").arg(Gb(facts.needed), Gb(facts.data), Gb(facts.free));
    } else {
      advice = T("Takedawn needs 3 GB free (the game uses 2.5 GB). Free up space on this disk, then press Resume.");
    }
    if (facts.kept) advice += " " + T("What was copied is kept.");
    return {T("There is not enough free space."), advice};
  }
  if (code == "no-permission") {
    return {T("Takedawn may not write into its data folder."),
            T("The folder shown under \"Installs to\" cannot be written. Change its permissions (or close the "
              "program that holds it), then press Resume.")};
  }
  if (code == "write-failed") {
    QString advice = T("The disk reported an error; the details name the file. Check the disk, then press Resume.");
    if (facts.kept) advice += " " + T("What was copied is kept.");
    return {T("Writing the game's data failed."), advice};
  }
  if (code == "cancelled") {
    return {T("Stopped."), facts.kept ? T("What was copied is kept. Resume goes on from where it stopped.")
                                      : T("Resume starts again.")};
  }
  if (code == "internal") {
    return {T("Takedawn ran into a problem of its own."),
            T("This should not happen. Press Resume to try again; if it happens again, copy the details and report them.")};
  }
  return {T("The installation failed."),
          T("The details say what happened. Press Resume to try again."), false};
}

QString ProgramFailedCode(int exit_code) {
  switch (static_cast<unsigned int>(exit_code)) {
    case 0xC0000139u: return "program-entrypoint";
    case 0xC0000135u: return "program-dll";
    default: return "program-failed";
  }
}

Explained ExplainProgram(const QString& code) {
  if (code == "program-missing") {
    return {T("The game's program was not found beside this setup window."),
            T("This copy of Takedawn is incomplete. Download it again.")};
  }
  if (code == "program-start") {
    return {T("The game's program could not be started."),
            T("This copy of Takedawn may be damaged, or the system refused to run it. Download it again; on macOS, "
              "open Takedawn once with right-click, Open.")};
  }
  if (code == "program-failed") {
    return {T("The game's program could not run."),
            T("It stopped before it said anything, so nothing can be installed with it. The last lines it printed are "
              "below and in the details; a missing library means this download is damaged or incomplete. Download it "
              "again, and if it happens again report it with the details.")};
  }
  if (code == "program-entrypoint") {
    return {T("The game's program could not be loaded: a system library is too old."),
            T("Windows stopped it before it started, because a library it uses lacks a function it calls. That is "
              "usually the graphics driver: Takedawn needs a Vulkan 1.3 graphics driver. Update the driver from "
              "Intel, AMD or NVIDIA, then try again. If it happens again after that, report it with the details.")};
  }
  if (code == "program-dll") {
    return {T("The game's program could not be loaded: a library is missing."),
            T("Windows stopped it before it started, because a library it needs was not found beside it or in the "
              "system. This download is damaged or incomplete: download it again, and if it happens again report it "
              "with the details.")};
  }
  if (code == "program-killed") {
    return {T("The installation was stopped from outside."),
            T("The program was ended before it finished. Press Resume: what was copied is kept.")};
  }
  if (code == "program-no-result") {
    return {T("The installation ended without saying whether it worked."),
            T("Press Resume to run it again: what was copied is kept. The last lines of its output are in the details.")};
  }
  return {T("The installation stopped unexpectedly."),
          T("Press Resume to run it again: what was copied is kept. The last lines of its output are in the details; "
            "copy them if you report this.")};
}

Explained ExplainGpu() {
#ifdef Q_OS_LINUX
  return {T("No GPU access, running on the CPU."),
          T("Check that your user can use /dev/dri (render group): on most systems, add yourself to the render group "
            "and log in again. Until then the game runs on the CPU, too slowly to play well, and the graphics "
            "preparation takes many times longer.")};
#else
  return {T("No GPU access, running on the CPU."),
          T("Check that the graphics driver is installed and up to date. Until then the game runs on the CPU, "
            "too slowly to play well, and the graphics preparation takes many times longer.")};
#endif
}

Explained ExplainShaders(const QString& code) {
  if (code == "cache-off") {
    return {T("The graphics cache is switched off."),
            T("Turn it on in the game's settings (Graphics), then use \"Build shaders now\" there.")};
  }
  if (code == "no-game-data") {
    return {T("The graphics could not be prepared: the game's data was not found."),
            T("Start the game once, then use \"Build shaders now\" in its settings (Graphics).")};
  }
  if (code == "scene-failed") {
    return {T("Preparing the graphics did not finish."),
            T("The game works without it. You can try again with \"Build shaders now\" in the game's settings (Graphics).")};
  }
  if (code == "cancelled") {
    return {T("Preparing the graphics was stopped."),
            T("What it finished is kept. You can do the rest later with \"Build shaders now\" in the game's settings (Graphics).")};
  }
  return {T("Preparing the graphics did not finish."),
          T("The game works without it; it prepares what it needs while you play, so the first races can stutter a "
            "little. You can try again with \"Build shaders now\" in the game's settings (Graphics).")};
}

Explained ExplainUpscale(const QString& code) {
  const QString later = T("You can try again with \"Upscale menu pictures with AI\" in the game's settings (Mods).");
  if (code == "cancelled") {
    return {T("Upscaling the menu pictures was stopped."),
            T("What it finished is kept. You can do the rest later with \"Upscale menu pictures with AI\" in the "
              "game's settings (Mods).")};
  }
  if (code == "no-engine") {
    return {T("This copy of Takedawn has no upscaler."), T("The release packages carry it; a build of your own needs it "
                                                          "beside the program (docs/upscale-art.md).")};
  }
  if (code == "gpu-lost") {
    return {T("The graphics card stopped while upscaling (it may be too busy or too small for the model)."),
            T("The game is not affected; its menu pictures stay as they are. ") + later};
  }
  if (code == "no-gpu") {
    return {T("The upscaler found no graphics card it can use (it needs Vulkan, or Metal on a Mac)."),
            T("The game is not affected; its menu pictures stay as they are.")};
  }
  if (code == "disk") {
    return {T("The upscaled pictures could not be written (is the disk full?)."), later};
  }
  return {T("Upscaling the menu pictures did not finish."),
          T("The game works without it; its menu pictures stay as they are. ") + later};
}

}  // namespace messages
