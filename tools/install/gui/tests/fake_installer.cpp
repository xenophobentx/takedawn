// A stand-in for the game's executable that answers the three command lines the
// setup window uses with the installer's line protocol (docs/installer.md), so the
// window can be tested without a disc, without 2.5 GB of copying and without the
// game. It copies nothing and plays nothing.
//
//   fake_installer --data-status [--data <dir>]
//   fake_installer --install <image-or-folder> [--dest <dir>] [--remember] [--check]
//   fake_installer --build-shaders
//   fake_installer --upscale-art [--quality q] | --upscale-art --dismiss
//   fake_installer [--data <dir>]          "the game": writes <dest>/played, status 0
//
// What it answers is told by the source's file name, so one window can be shown
// every outcome by choosing other "discs":
//
//   <code>.iso            the disc check fails with <code> (any of the installer's
//                         failure codes: wrong-version.iso, xbox360-disc.iso, ...)
//   install-<code>.iso    the check passes, the install fails with <code> at 40 %
//   hold.iso              the install stops at 60 % and waits to be cancelled
//   crash.iso             the install ends without a RESULT line (status 9)
//   nostart.iso           the program never gets going, as with a missing library: one line
//                         on stderr, no protocol line, an abort
//   europe-<x>.iso        a good European disc: the check is its own program's, with
//                         another data root ("<default root> PAL") and size
//   anything else         a good disc; the install runs to the end
//
// The install's state is kept in <dest> as the real installer keeps it: a
// "fake.journal" with how far it got (a second run goes on from there) and
// ".complete" last. A folder with "Burnout 3 Takedown/default.xbe" and no
// ".complete" is "unverified" for --data-status; installing from that folder
// checks it in place (and fails with damaged-source when the folder holds a file
// named "damaged").
//
// --remember keeps <dest> in "<default root>.location" (the game's data-folder.txt),
// or removes that file when <dest> is the default root; without --data or --dest the
// folder named there is the root, as the game's case 4 (install.h).
//
// Environment: TAKEDAWN_FAKE_ROOT is the default data root (else the working
// directory's "fake-root"), TAKEDAWN_FAKE_PACE the milliseconds between progress
// lines (default 4), TAKEDAWN_FAKE_FREE_NEEDED the bytes it says must be free (default 3000000000;
// a huge number makes any disk too small), TAKEDAWN_FAKE_SHADERS = ok | fail | hold | nogpu
// (nogpu: finishes, but warns that the scenes ran on the CPU), TAKEDAWN_FAKE_UPSCALE = on | off
// (--data-status offers the menu picture upscale, ticked or not; unset: an older game that
// says nothing), TAKEDAWN_FAKE_UPSCALE_RUN = ok | fail | hold. The upscale keeps its state in
// the root: fake-upscale-made (the pack) and fake-upscale-declined (--dismiss).
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* const kCodes[] = {"no-source", "source-unreadable", "not-xbox-image", "xbox360-disc", "no-xbe",
                              "wrong-game", "wrong-version", "truncated-image", "damaged-source", "disk-full",
                              "no-permission", "write-failed", "cancelled", "internal"};
const char kGameDir[] = "Burnout 3 Takedown";
const unsigned kFiles = 716;
const unsigned long long kBytes = 2476107072ull;

volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }

void Out(const std::string& line) {
  std::fputs((line + "\n").c_str(), stdout);
  std::fflush(stdout);
}

int Pace() {
  const char* p = std::getenv("TAKEDAWN_FAKE_PACE");
  return p && *p ? std::atoi(p) : 4;
}

void Nap(int times = 1) { std::this_thread::sleep_for(std::chrono::milliseconds(Pace() * times)); }

std::string DefaultRoot() {
  const char* r = std::getenv("TAKEDAWN_FAKE_ROOT");
  return r && *r ? r : (fs::current_path() / "fake-root").string();
}

std::string LocationFile() { return DefaultRoot() + ".location"; }

std::string SavedRoot() {
  std::ifstream in(LocationFile());
  std::string line;
  std::getline(in, line);
  return line;
}

// The real installer's wording for each code, shortened.
std::string Message(const std::string& code, const std::string& source, const std::string& dest) {
  if (code == "no-source") return source + " does not exist.";
  if (code == "source-unreadable") return source + ": Permission denied.";
  if (code == "not-xbox-image") return source + " is not an Xbox disc image: it has no Xbox volume.";
  if (code == "xbox360-disc") return "This is an Xbox 360 disc (it has a default.xex).";
  if (code == "no-xbe") return "The disc image has no default.xbe: it is not a game disc.";
  if (code == "wrong-game") return "This is the disc of \"Burnout 2: Point of Impact\" (title id 0x41560016).";
  if (code == "wrong-version")
    return "This is Burnout 3: Takedown, but not the version this program was made for (sha256 0a1b...).";
  if (code == "truncated-image") return "The disc image is incomplete: it ends at 1.2 of 6.9 GB. Make a new image of your disc.";
  if (code == "damaged-source")
    return "Tracks/crash1/static.dat in the disc image is not what the disc holds (its SHA-256 differs).";
  if (code == "disk-full")
    return "Not enough free space in " + dest + ": Takedawn needs 3.0 GB free (the game uses 2.5 GB), and 2.1 GB is free. "
           "Free up space on this disk.";
  if (code == "no-permission") return "No permission to write " + dest + ". Choose another folder.";
  if (code == "write-failed") return "Writing " + dest + "/" + kGameDir + "/default.xbe.part failed: Input/output error.";
  if (code == "cancelled") return "Cancelled. What was copied is kept: the next run goes on from there.";
  return "Out of memory.";
}

bool IsCode(const std::string& s) {
  for (const char* c : kCodes)
    if (s == c) return true;
  return false;
}

int Fail(const std::string& step, const std::string& code, const std::string& text) {
  Out("STEP " + step + " FAIL " + code + " " + text);
  Out("RESULT FAIL " + step + " " + code);
  return 1;
}

bool Cancelled(const fs::path& cancel_file) {
  std::error_code ec;
  return g_stop || fs::exists(cancel_file, ec);
}

int DataStatus(const std::string& root, const std::string& place) {
  std::error_code ec;
  Out("PROTOCOL 1");
  Out("PLAN data");
  Out("LOG data root " + root);
  Out("LOG data place " + place);
  Out("LOG data default " + DefaultRoot());
  Out("LOG data disc Burnout 3: Takedown, North America (NTSC-U) retail");
  Out("LOG data files " + std::to_string(kFiles));
  Out("LOG data bytes " + std::to_string(kBytes));
  const char* needed = std::getenv("TAKEDAWN_FAKE_FREE_NEEDED");  // a test makes the disk look too small
  Out(std::string("LOG data free-needed ") + (needed && *needed ? needed : "3000000000"));
  Out("LOG data version Takedawn 0.1.0 \"Lazy Generation\"");
  Out("LOG data build ntsc-u-retail");
  if (const char* up = std::getenv("TAKEDAWN_FAKE_UPSCALE"); up && *up) {
    Out(std::string("LOG data upscale ") + up + (std::string(up) == "on" ? " best a graphics card" : " fast built-in graphics"));
    Out(std::string("LOG data upscale-pack ") +
        (fs::exists(fs::path(root) / "fake-upscale-made", ec) ? "made"
         : fs::exists(fs::path(root) / "fake-upscale-declined", ec) ? "declined" : "none"));
  }
  if (fs::exists(fs::path(root) / ".complete", ec)) {
    Out("STEP data OK installed by 0.1.0, manifest 81dc20fd647fb0b1");
    Out("RESULT OK " + root);
    return 0;
  }
  if (fs::exists(fs::path(root) / kGameDir / "default.xbe", ec)) {
    Out("STEP data FAIL unverified There are game files in " + root +
        ", but this program has not checked them. Installing from that folder checks them in place; nothing is copied.");
    Out("RESULT FAIL data unverified");
    return 3;
  }
  Out("STEP data FAIL not-installed The game's data is not installed in " + root + ".");
  Out("RESULT FAIL data not-installed");
  return 3;
}

int Install(const std::string& source, const std::string& dest, bool check, bool remember) {
  const std::string name = fs::path(source).stem().string();
  std::error_code ec;
  const fs::path cancel_file = fs::path(dest) / "install.cancel";
  fs::remove(cancel_file, ec);
  if (name == "nostart") {
    std::fputs("Failed loading SDL3 library.\n", stderr);
    std::fflush(stderr);
    std::abort();
  }
  Out("PROTOCOL 1");
  Out("PLAN disc");
  Out(check ? "STEP disc BEGIN Checking the disc" : "STEP disc BEGIN Checking the disc and copying the game's data (2.5 GB)");
  Out("LOG disc source " + source);
  if (!check) Out("LOG disc destination " + dest);
  if (remember && !check) {
    if (fs::path(dest).lexically_normal() == fs::path(DefaultRoot()).lexically_normal()) {
      fs::remove(LocationFile(), ec);
      Out("LOG disc remembered: the game uses its default folder");
    } else {
      fs::create_directories(fs::path(LocationFile()).parent_path(), ec);
      std::ofstream(LocationFile()) << dest << "\n";
      Out("LOG disc remembered: the game uses " + dest + " from now on");
    }
  }
  Nap(10);
  if (IsCode(name)) return Fail("disc", name, Message(name, source, dest));
  const bool in_place = fs::equivalent(fs::path(source), fs::path(dest) / kGameDir, ec);
  if (check) {
    // The package's programs (src/game/package.h): a disc named europe-*.iso is a
    // European one, answered by its own program, which keeps its data elsewhere.
    if (name.rfind("europe-", 0) == 0) {
      Out("LOG disc build pal-retail");
      Out("LOG disc root " + DefaultRoot() + " PAL");
      Out("LOG disc place default");
      Out("LOG disc default " + DefaultRoot() + " PAL");
      Out("LOG disc bytes 2966033036");
      Out("LOG disc free-needed 3400000000");
      Out("STEP disc OK Burnout 3: Takedown, Europe (PAL, Fr/De/It) retail (a disc image, game partition at 0x0)");
      Out("RESULT OK " + source);
      return 0;
    }
    Out("LOG disc build ntsc-u-retail");
    Out("LOG disc root " + dest);
    Out(fs::is_directory(source, ec)
            ? "STEP disc OK Burnout 3: Takedown, North America (NTSC-U) retail (an extracted folder)"
            : "STEP disc OK Burnout 3: Takedown, North America (NTSC-U) retail (a disc image, game partition at 0x0)");
    Out("RESULT OK " + source);
    return 0;
  }
  if (fs::exists(fs::path(dest) / ".complete", ec)) {
    Out("STEP disc SKIP already installed (716 files, manifest 81dc20fd647fb0b1)");
    Out("RESULT OK " + dest);
    return 0;
  }
  std::string fail_at_40;
  if (name.rfind("install-", 0) == 0 && IsCode(name.substr(8))) fail_at_40 = name.substr(8);
  if (in_place && fs::exists(fs::path(source) / "damaged", ec)) fail_at_40 = "damaged-source";

  fs::create_directories(dest, ec);
  if (ec) return Fail("disc", "no-permission", Message("no-permission", source, dest));
  const fs::path journal = fs::path(dest) / "fake.journal";
  unsigned done = 0;
  {
    std::ifstream in(journal);
    in >> done;
  }
  const unsigned kept = done;
  if (kept) Out("LOG disc " + std::to_string(kept) + " files of an earlier run are kept");
  const unsigned step = 4;
  for (; done < kFiles; done += step) {
    const double f = double(done) / kFiles;
    if (Cancelled(cancel_file)) {
      std::ofstream(journal) << done;
      fs::remove(cancel_file, ec);
      return Fail("disc", "cancelled", Message("cancelled", source, dest));
    }
    if (!fail_at_40.empty() && f >= 0.4) {
      std::ofstream(journal) << done;
      return Fail("disc", fail_at_40, Message(fail_at_40, source, dest));
    }
    if (name == "crash" && f >= 0.3) {
      std::fputs("fake_installer: segmentation fault (pretend)\n", stderr);
      return 9;
    }
    if (name == "hold" && f >= 0.6) {
      std::ofstream(journal) << done;
      char line[160];
      std::snprintf(line, sizeof line, "PROGRESS disc %.3f Tracks/eu/italy/static.dat", f);
      Out(line);
      while (!Cancelled(cancel_file)) Nap(5);
      continue;
    }
    char line[160];
    std::snprintf(line, sizeof line, "PROGRESS disc %.3f Tracks/track%02u/streamed%03u.dat", f, done / 40, done % 40);
    Out(line);
    Nap();
  }
  fs::remove(journal, ec);
  if (!in_place) {
    fs::create_directories(fs::path(dest) / kGameDir, ec);
    std::ofstream(fs::path(dest) / kGameDir / "default.xbe") << "fake";
  }
  std::ofstream(fs::path(dest) / ".complete") << "manifest 81dc20fd647fb0b1\nversion 0.1.0\n";
  Out("PROGRESS disc 1.000 ");
  if (in_place) {
    Out("STEP disc OK 716 files, 2476107072 bytes; 0 copied, 716 checked in place, 0 kept from an earlier run; 19 s");
  } else {
    Out("STEP disc OK 716 files, 2476107072 bytes; " + std::to_string(kFiles - kept) + " copied, 0 checked in place, " +
        std::to_string(kept) + " kept from an earlier run; 13 s");
  }
  Out("RESULT OK " + dest);
  return 0;
}

int Shaders(const std::string& root) {
  const char* m = std::getenv("TAKEDAWN_FAKE_SHADERS");
  const std::string mode = m && *m ? m : "ok";
  std::error_code ec;
  const fs::path cache = fs::path(root) / "fake-shader-cache";
  fs::create_directories(cache / "prebuild", ec);
  const fs::path cancel_file = cache / "prebuild" / "cancel";
  fs::remove(cancel_file, ec);
  Out("PROTOCOL 1");
  Out("PLAN shaders");
  Out("STEP shaders BEGIN Preparing graphics: the game builds what it needs without a window or sound (a few seconds)");
  Out("LOG shaders cache " + cache.string());
  if (mode == "nogpu") Out("WARN shaders no-gpu llvmpipe (LLVM 17.0.6, 256 bits), Vulkan 1.3.274");
  for (int i = 0; i <= 50; ++i) {
    if (Cancelled(cancel_file)) return Fail("shaders", "cancelled", "stopped before it finished");
    if (mode == "fail" && i == 20) return Fail("shaders", "scene-failed", "race: the game lost its way twice");
    char line[160];
    std::snprintf(line, sizeof line, "PROGRESS shaders %.3f %s", i / 50.0, i < 25 ? "race: 2/3 laps" : "crash: the junction");
    Out(line);
    if (mode == "hold" && i == 25) {
      while (!Cancelled(cancel_file)) Nap(5);
      continue;
    }
    Nap();
  }
  Out("STEP shaders OK 29 vertex, 329 fragment, 68 pipelines; 526887 bytes in " + cache.string() + "; 57 s");
  Out("RESULT OK " + cache.string());
  return 0;
}

int Upscale(const std::string& root, bool dismiss) {
  std::error_code ec;
  fs::create_directories(root, ec);
  Out("PROTOCOL 1");
  Out("PLAN upscale");
  if (dismiss) {
    std::ofstream(fs::path(root) / "fake-upscale-declined") << "declined\n";
    Out("RESULT OK declined");
    return 0;
  }
  const char* m = std::getenv("TAKEDAWN_FAKE_UPSCALE_RUN");
  const std::string mode = m && *m ? m : "ok";
  const fs::path work = fs::path(root) / "fake-upscale-work";
  fs::create_directories(work, ec);
  const fs::path cancel_file = work / "cancel";
  fs::remove(cancel_file, ec);
  Out("STEP upscale BEGIN Upscaling the menu pictures with Real-ESRGAN (best quality)");
  Out("LOG upscale work " + work.string());
  for (int i = 0; i <= 40; ++i) {
    if (Cancelled(cancel_file)) {
      fs::remove(cancel_file, ec);
      return Fail("upscale", "cancelled", "stopped; what was finished is kept");
    }
    if (mode == "fail" && i == 10) return Fail("upscale", "gpu-lost", "the graphics device failed while upscaling");
    char line[160];
    std::snprintf(line, sizeof line, "PROGRESS upscale %.3f upscaling: %d of 229 pictures", i / 40.0, i * 5);
    Out(line);
    if (mode == "hold" && i == 20) {
      while (!Cancelled(cancel_file)) Nap(5);
      continue;
    }
    Nap();
  }
  std::ofstream(fs::path(root) / "fake-upscale-made") << "made\n";
  Out("STEP upscale OK 229 pictures upscaled (best quality) into " + root + "/generated-mods; 307 s");
  Out("RESULT OK " + root);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  std::vector<std::string> args(argv + 1, argv + argc);
  std::string dest, source, mode = "play", place = "named";
  bool check = false, remember = false, dismiss = false;
  for (size_t i = 0; i < args.size(); ++i) {
    if ((args[i] == "--dest" || args[i] == "--data") && i + 1 < args.size()) dest = args[++i];
    else if (args[i] == "--check") check = true;
    else if (args[i] == "--remember") remember = true;
    else if (args[i] == "--install" || args[i] == "--data-status" || args[i] == "--build-shaders") mode = args[i].substr(2);
    else if (args[i] == "--no-setup") continue;
    else if (args[i] == "--upscale-art") mode = "upscale";
    else if (args[i] == "--dismiss") dismiss = true;
    else if (args[i] == "--quality" && i + 1 < args.size()) ++i;
    else if (args[i].rfind("--", 0) == 0) return 2;
    else source = args[i];
  }
  if (dest.empty()) {
    dest = SavedRoot();
    place = dest.empty() ? "default" : "saved";
    if (dest.empty()) dest = DefaultRoot();
  }
  if (mode == "data-status") return DataStatus(dest, place);
  if (mode == "install") return source.empty() ? 2 : Install(source, dest, check, remember);
  if (mode == "build-shaders") return Shaders(dest);
  if (mode == "upscale") return Upscale(dest, dismiss);
  std::error_code ec;
  fs::create_directories(dest, ec);
  std::ofstream(fs::path(dest) / "played") << "played\n";  // "the game was started"
  return 0;
}
