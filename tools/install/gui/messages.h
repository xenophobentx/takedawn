#pragma once

#include <QString>
#include <QStringList>

// Plain-language wording for the codes the installer reports (docs/installer.md).
// The installer's own message (English, with the file or the path in it) stays
// available as the detail; these are what the window says first.
namespace messages {

struct Explained {
  QString headline;  // what happened
  QString advice;    // what to do about it; may be empty
  bool known = true; // false: the code is not one this window has words for
};

// A step's name, as the progress rows show it.
QString StepTitle(const QString& step);

// Every failure code of `--install`, in the order the installer documents them.
const QStringList& InstallCodes();
// True for the codes that say "this is not a disc that can be used" (the disc card
// shows them); the others are about this computer or this run.
bool IsDiscProblem(const QString& code);

// What the window knows when it explains an `--install` failure.
struct InstallFacts {
  bool in_place = false;  // it was checking an older install where it lies, not copying from a disc
  QString source;         // the image or folder that was chosen (may be empty)
  bool kept = true;       // files from this or an earlier run are in the data folder
  qint64 needed = 0;      // "disk-full": bytes that must be free, the game's own bytes, the bytes free now
  qint64 data = 0;        //  (0 / -1: not known)
  qint64 free = -1;
};

// True when the file looks like a compressed archive (.zip, .7z, .rar): by its first
// bytes, or by its name. A folder or an unreadable path is not.
bool LooksLikeArchive(const QString& path);

// The disc's file a "damaged-source" message names ("<file> in the disc image is not what the
// disc holds ...", "<file> is missing from ...", "<file> has N bytes in ..."), or empty.
QString DamagedFile(const QString& message);

// `--install` (and its `--check`) failed. The words name the discs every package takes.
Explained ExplainInstall(const QString& code, const InstallFacts& facts = {});
// The window's own failures: the game's program is missing, could not be started,
// ended without an answer ("program-missing", "program-start", "program-crashed",
// "program-killed", "program-no-result").
Explained ExplainProgram(const QString& code);
// The code for a program that ended before it said anything: Windows' loader
// stops a program whose imports it cannot satisfy with these statuses
// (STATUS_ENTRYPOINT_NOT_FOUND, STATUS_DLL_NOT_FOUND), and the status says
// more than "failed". Anything else is "program-failed".
QString ProgramFailedCode(int exit_code);
// The optional shader build did not work: a warning, never a failed install.
Explained ExplainShaders(const QString& code);
// The optional menu picture upscale (BO-311): what went wrong, in plain words.
Explained ExplainUpscale(const QString& code);
// The graphics preparation (and so the game) found no graphics device it can use and runs
// on the CPU: a warning, never a stop.
Explained ExplainGpu();

}  // namespace messages
