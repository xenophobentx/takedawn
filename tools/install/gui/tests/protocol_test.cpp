#include <QtTest>

#include <QTemporaryDir>

#include "messages.h"
#include "protocol.h"

using K = Event::Kind;

// The line protocol of the game's installer (docs/installer.md) and the words the
// window has for its codes. No window, no process.
class ProtocolTest : public QObject {
  Q_OBJECT

  static messages::InstallFacts InPlace() {
    messages::InstallFacts facts;
    facts.in_place = true;
    return facts;
  }
  static bool Write(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
  }

 private slots:
  void protocolAndPlan() {
    Event e = ParseLine("PROTOCOL 1");
    QCOMPARE(e.kind, K::Protocol);
    QCOMPARE(e.version, 1);
    e = ParseLine("PLAN disc");
    QCOMPARE(e.kind, K::Plan);
    QCOMPARE(e.steps, QStringList{"disc"});
  }

  void stepEvents() {
    Event e = ParseLine("STEP disc BEGIN Checking the disc and copying the game's data (2.5 GB)");
    QCOMPARE(e.kind, K::Begin);
    QCOMPARE(e.step, QString("disc"));
    QCOMPARE(e.text, QString("Checking the disc and copying the game's data (2.5 GB)"));
    e = ParseLine("STEP disc SKIP already installed (716 files, manifest 81dc20fd647fb0b1)");
    QCOMPARE(e.kind, K::Skip);
    QCOMPARE(e.text, QString("already installed (716 files, manifest 81dc20fd647fb0b1)"));
    e = ParseLine("STEP disc OK 716 files, 2476107072 bytes; 716 copied, 0 checked in place, 0 kept from an earlier run; 13 s");
    QCOMPARE(e.kind, K::Ok);
    QVERIFY(e.text.startsWith("716 files"));
    e = ParseLine("STEP disc FAIL wrong-version This is Burnout 3: Takedown, but not the version this program was made for");
    QCOMPARE(e.kind, K::Fail);
    QCOMPARE(e.code, QString("wrong-version"));
    QVERIFY(e.text.startsWith("This is Burnout 3"));
    e = ParseLine("STEP data FAIL unverified There are game files in /a b/Game, but this program has not checked them.");
    QCOMPARE(e.kind, K::Fail);
    QCOMPARE(e.step, QString("data"));
    QCOMPARE(e.code, QString("unverified"));
  }

  void progress() {
    Event e = ParseLine("PROGRESS disc 0.884 Tracks/eu/italy/static.dat");
    QCOMPARE(e.kind, K::Progress);
    QCOMPARE(e.step, QString("disc"));
    QVERIFY(qAbs(e.fraction - 0.884) < 1e-9);
    QCOMPARE(e.text, QString("Tracks/eu/italy/static.dat"));
    e = ParseLine("PROGRESS disc 1.000 ");
    QCOMPARE(e.kind, K::Progress);
    QCOMPARE(e.text, QString());
    e = ParseLine("PROGRESS disc nonsense");
    QCOMPARE(e.kind, K::Unknown);
  }

  void logs() {
    Event e = ParseLine("LOG data root /Users/a b/Library/Application Support/Takedawn/Game");
    QCOMPARE(e.kind, K::Log);
    QCOMPARE(e.step, QString("data"));
    QCOMPARE(e.text, QString("root /Users/a b/Library/Application Support/Takedawn/Game"));
    e = ParseLine("WARN shaders something odd");
    QCOMPARE(e.kind, K::Warn);
  }

  void results() {
    Event e = ParseLine("RESULT OK /Users/a b/Library/Application Support/Takedawn/Game");
    QCOMPARE(e.kind, K::ResultOk);
    QCOMPARE(e.text, QString("/Users/a b/Library/Application Support/Takedawn/Game"));
    e = ParseLine("RESULT FAIL disc disk-full");
    QCOMPARE(e.kind, K::ResultFail);
    QCOMPARE(e.step, QString("disc"));
    QCOMPARE(e.code, QString("disk-full"));
  }

  void junk() {
    QCOMPARE(ParseLine("").kind, K::Unknown);
    QCOMPARE(ParseLine("Segmentation fault").kind, K::Unknown);
    QCOMPARE(ParseLine("STEP").kind, K::Unknown);
    QCOMPARE(ParseLine("STEP x WHAT y").kind, K::Unknown);
  }

  // Every code the installer documents has words of its own, and no two share a headline.
  void everyCodeIsExplained() {
    QCOMPARE(messages::InstallCodes().size(), 14);
    QSet<QString> headlines;
    for (const QString& code : messages::InstallCodes()) {
      const auto why = messages::ExplainInstall(code);
      QVERIFY2(why.known, qPrintable(code));
      QVERIFY2(!why.headline.isEmpty() && !why.advice.isEmpty(), qPrintable(code));
      QVERIFY2(!headlines.contains(why.headline), qPrintable(code));
      headlines.insert(why.headline);
    }
    QVERIFY(!messages::ExplainInstall("a-code-from-the-future").known);
    QVERIFY(messages::ExplainInstall("damaged-source", InPlace()).headline != messages::ExplainInstall("damaged-source").headline);
    for (const char* code : {"program-missing", "program-start", "program-crashed", "program-failed", "program-killed", "program-no-result",
                              "program-entrypoint", "program-dll"}) {
      QVERIFY2(!messages::ExplainProgram(code).headline.isEmpty(), code);
    }
    // BO-231 (GitHub #37): Windows' loader statuses are told apart from any other stop, and the
    // entry-point one names the graphics driver.
    QCOMPARE(messages::ProgramFailedCode(-1073741511), QString("program-entrypoint"));  // 0xC0000139
    QCOMPARE(messages::ProgramFailedCode(-1073741515), QString("program-dll"));         // 0xC0000135
    QCOMPARE(messages::ProgramFailedCode(1), QString("program-failed"));
    QVERIFY(messages::ExplainProgram("program-entrypoint").advice.contains("Vulkan 1.3"));
    for (const char* code : {"cache-off", "no-game-data", "scene-failed", "cancelled", "game-failed"}) {
      QVERIFY2(!messages::ExplainShaders(code).advice.isEmpty(), code);
    }
  }

  // The window's words say Takedawn; the game's title appears only to say which disc.
  void wording() {
    for (const QString& code : messages::InstallCodes()) {
      const auto why = messages::ExplainInstall(code);
      QVERIFY2(!(why.headline + why.advice).contains("installer", Qt::CaseInsensitive), qPrintable(code));
      QVERIFY2(!(why.headline + why.advice).contains("Python"), qPrintable(code));
    }
    QVERIFY(messages::IsDiscProblem("wrong-version"));
    QVERIFY(!messages::IsDiscProblem("disk-full"));
  }

  // BO-321, BO-341: one package for all three retail discs; the disc errors name them all and no
  // region alone.
  void discNamesOnePackage() {
    QCOMPARE(messages::ExplainInstall("wrong-version").advice,
             QString("Three retail discs work: the North America (NTSC-U) one, and the two European (PAL) ones, "
                     "French, German and Italian, and English, Spanish, Dutch and Swedish. The Japanese disc and "
                     "patched or modified copies hold different code and cannot be used."));
    QCOMPARE(messages::ExplainInstall("wrong-game").advice,
             QString("Takedawn needs the Burnout 3: Takedown disc for the original Xbox."));
    QCOMPARE(messages::ExplainInstall("xbox360-disc").advice,
             QString("Takedawn runs the original Xbox game. Use the Burnout 3: Takedown disc for the original Xbox."));
    for (const QString& code : messages::InstallCodes()) {
      const auto e = messages::ExplainInstall(code);
      if (code != "wrong-version") {
        QVERIFY2(!(e.headline + e.advice).contains("NTSC"), qPrintable(code));
        QVERIFY2(!(e.headline + e.advice).contains("PAL"), qPrintable(code));
      }
    }
  }

  // BO-130: nothing in the window says "dump", offers another folder where there is no folder
  // picker, or sends the person to Resume on the same bad image.
  void plainWords() {
    for (const QString& code : messages::InstallCodes()) {
      const auto why = messages::ExplainInstall(code);
      const QString all = why.headline + " " + why.advice;
      QVERIFY2(!all.contains("dump", Qt::CaseInsensitive), qPrintable(code));
      QVERIFY2(!all.contains("another folder", Qt::CaseInsensitive), qPrintable(code));
      QVERIFY2(!all.contains("Resume to try this one", Qt::CaseInsensitive), qPrintable(code));
    }
    QVERIFY(messages::ExplainInstall("truncated-image").advice.contains("Make a new image of your disc"));
    const auto damaged = messages::ExplainInstall("damaged-source");
    QVERIFY(damaged.advice.startsWith("A file is missing or is not what the disc holds. Choose another copy"));
    QVERIFY(damaged.advice.contains("Resume only helps after the file is replaced"));
    QVERIFY(messages::ExplainInstall("no-permission").advice.contains("Change its permissions"));
  }

  void damagedFileName() {
    QCOMPARE(messages::DamagedFile("Tracks/AS/M1_V1/streamed.dat in the disc image is not what the disc holds (its SHA-256 is "
                                   "0a1b..., the disc's 9f8e...): the copy is damaged or was changed. Make the image again from "
                                   "the disc."),
             QString("Tracks/AS/M1_V1/streamed.dat"));
    QCOMPARE(messages::DamagedFile("dashupdate.xbe is missing from the folder: it is not a complete copy of the disc."),
             QString("dashupdate.xbe"));
    QCOMPARE(messages::DamagedFile("sound/v_eng.rws has 12 bytes in the disc image and 99 on the disc: the copy is damaged."),
             QString("sound/v_eng.rws"));
    QCOMPARE(messages::DamagedFile("The disc image is damaged: bad volume descriptor."), QString());
    QCOMPARE(messages::DamagedFile(QString()), QString());
  }

  // The "unpack the archive" sentence is for a file that looks like an archive (by its first bytes
  // or its name), not for, say, a PlayStation 2 image.
  void archiveHintOnlyForArchives() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = [&](const char* name) { return dir.filePath(name); };
    QVERIFY(Write(path("renamed.bin"), QByteArray("PK\x03\x04", 4) + QByteArray(64, 'x')));  // a zip with another name
    QVERIFY(Write(path("seven.dat"), QByteArray("7z\xBC\xAF\x27\x1C", 6) + QByteArray(64, 'x')));
    QVERIFY(Write(path("rar.dat"), QByteArray("Rar!\x1A\x07\x00", 7) + QByteArray(64, 'x')));
    QVERIFY(Write(path("named.zip"), QByteArray(64, 'x')));                                  // by its name only
    QVERIFY(Write(path("Pack.7Z"), QByteArray(64, 'x')));
    QVERIFY(Write(path("ps2.iso"), QByteArray(64, '\0') + "SYSTEM.CNF;1"));
    QVERIFY(Write(path("empty.iso"), QByteArray()));
    QVERIFY(QDir().mkpath(path("a folder.zip")));  // a folder is not an archive, whatever it is called

    for (const char* name : {"renamed.bin", "seven.dat", "rar.dat", "named.zip", "Pack.7Z"}) {
      QVERIFY2(messages::LooksLikeArchive(path(name)), name);
      messages::InstallFacts facts;
      facts.source = path(name);
      QVERIFY2(messages::ExplainInstall("not-xbox-image", facts).advice.contains("compressed archive (.zip, .7z, .rar)"), name);
    }
    for (const char* name : {"ps2.iso", "empty.iso", "a folder.zip", "missing.iso"}) {
      QVERIFY2(!messages::LooksLikeArchive(path(name)), name);
      messages::InstallFacts facts;
      facts.source = path(name);
      const auto why = messages::ExplainInstall("not-xbox-image", facts);
      QVERIFY2(!why.advice.contains("archive"), name);
      QVERIFY2(why.advice.contains("Takedawn needs the .iso"), name);  // the rest of the advice stays
    }
    QVERIFY(!messages::ExplainInstall("not-xbox-image").advice.contains("archive"));
  }

  // The disk-space words use the 3 GB free the installer asks for and the 2.5 GB the game uses
  // as two different numbers, and say "kept" only when something was copied.
  void diskFullWords() {
    messages::InstallFacts facts;
    facts.needed = 3000000000;
    facts.data = 2476107072;
    facts.free = 2100000000;
    facts.kept = false;
    auto why = messages::ExplainInstall("disk-full", facts);
    QCOMPARE(why.advice, QString("Takedawn needs 3 GB free (the game uses 2.5 GB). You have 2.1 GB. Free up space on this "
                                 "disk, then press Resume."));
    facts.kept = true;
    why = messages::ExplainInstall("disk-full", facts);
    QVERIFY(why.advice.endsWith("then press Resume. What was copied is kept."));
    why = messages::ExplainInstall("disk-full");  // numbers not known
    QVERIFY(why.advice.contains("3 GB free (the game uses 2.5 GB)"));
    for (const char* code : {"disk-full", "write-failed", "cancelled"}) {
      messages::InstallFacts none;
      none.kept = false;
      QVERIFY2(!messages::ExplainInstall(code, none).advice.contains("kept"), code);
      QVERIFY2(messages::ExplainInstall(code).advice.contains("kept"), code);
    }
  }
};

QTEST_APPLESS_MAIN(ProtocolTest)
#include "protocol_test.moc"
