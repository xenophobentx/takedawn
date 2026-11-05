// The setup window against a fake installer process (fake_installer.cpp, built
// beside this test): the whole conversation, with every failure code the installer
// documents, the stop and the resume, an older install checked in place, and the
// Play button. Offscreen: QT_QPA_PLATFORM=offscreen (CTest sets it).
#include <QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocumentFragment>
#include <QTemporaryDir>
#include <QToolButton>

#include "elided_label.h"
#include "installer_window.h"
#include "messages.h"
#include "steam_artwork.h"
#include "steam_shortcut.h"
#include "theme.h"
#include "ui_widgets.h"

class WindowTest : public QObject {
  Q_OBJECT

  using PH = InstallerWindow::Phase;
  using DS = InstallerWindow::DiscState;
  using RS = InstallerWindow::RowStatus;

  std::unique_ptr<QTemporaryDir> tmp_;
  std::unique_ptr<InstallerWindow> w_;
  QString root_;

  static messages::InstallFacts InPlace() {
    messages::InstallFacts facts;
    facts.in_place = true;
    return facts;
  }

  static QString Plain(const QString& html) { return QTextDocumentFragment::fromHtml(html).toPlainText(); }

  // Most tests are about the data step: the "Prepare graphics" box starts unticked for them
  // (the window's own default is ticked, shadersAreOnByDefault checks that one).
  bool shaders_ = false;
  // What the fake game offers for the menu picture upscale ("": an older game that says nothing).
  QByteArray upscale_;

  // A fresh window over a data root of its own; `prepare` puts things there first.
  InstallerWindow* Make(const std::function<void(const QString&)>& prepare = {}, const QString& dest = QString()) {
    w_.reset();
    tmp_ = std::make_unique<QTemporaryDir>();
    root_ = tmp_->path() + "/Takedawn/Game";
    qputenv("TAKEDAWN_FAKE_ROOT", root_.toUtf8());
    qputenv("TAKEDAWN_FAKE_PACE", "0");
    qputenv("TAKEDAWN_FAKE_SHADERS", "ok");
    qputenv("TAKEDAWN_FAKE_UPSCALE", upscale_);
    qputenv("TAKEDAWN_FAKE_UPSCALE_RUN", "ok");
    if (prepare) prepare(dest.isEmpty() ? root_ : dest);
    LaunchConfig config;
    config.program = QCoreApplication::applicationDirPath() + "/fake_installer";
    config.dest = dest;
    config.test = true;
    config.shaders = shaders_;
    w_ = std::make_unique<InstallerWindow>(config);
    w_->setAttribute(Qt::WA_DontShowOnScreen);
    w_->show();
    return w_.get();
  }

  static void OldInstall(const QString& root) {
    QDir().mkpath(root + "/Burnout 3 Takedown");
    QFile f(root + "/Burnout 3 Takedown/default.xbe");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("old");
  }

  void Choose(InstallerWindow* w, const QString& name, DS wanted) {
    w->SetDisc("/discs/" + name);
    QTRY_COMPARE_WITH_TIMEOUT(w->disc_state_, wanted, 10000);
  }

 private slots:
  void emptyStart() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QCOMPARE(w->status_code_, QString("not-installed"));
    QCOMPARE(w->data_root_, QDir::cleanPath(root_));
    QCOMPARE(w->data_files_, 716);
    QCOMPARE(w->disc_state_, DS::Empty);
    QVERIFY(!w->primary_->isEnabled());
    QVERIFY(!w->play_->isVisible());
    QVERIFY(w->windowTitle().contains("Takedawn"));
    QVERIFY(w->dest_label_->FullText().contains("Takedawn"));
    QVERIFY2(w->space_label_->text().contains("GB free on this disk"), qPrintable(w->space_label_->text()));
    QVERIFY(!QFileInfo::exists(root_));  // asking wrote nothing
  }

  void installFromAGoodDisc() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "Burnout 3 - Takedown (USA).iso", DS::Valid);
    QVERIFY(w->primary_->isEnabled());
    QCOMPARE(w->primary_->text(), QString("Install"));
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(QFileInfo::exists(root_ + "/.complete"));
    QCOMPARE(w->Row("disc")->status, RS::Ok);
    QVERIFY(w->Row("disc")->detail.contains("716 copied"));
    QCOMPARE(w->progress_->value(), w->progress_->maximum());
    QVERIFY(w->play_->isVisible());
    QVERIFY(!w->primary_->isVisible());
    QVERIFY(!w->note_->isVisible());
    w->play_->click();
    QCOMPARE(w->play_count_, 1);
  }

  // BO-341: one package, a program per disc. A European disc's check is answered by its own
  // program, which keeps the data in its own folder and is another size: "Installs to" and
  // the space line follow it; a North America disc again goes back to the first answer's.
  void anotherDiscsProgramHasItsOwnFolder() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QCOMPARE(w->data_build_, QString("ntsc-u-retail"));
    const QString first = w->data_root_;
    Choose(w, "europe-frdeit.iso", DS::Valid);
    QCOMPARE(w->data_build_, QString("pal-retail"));
    QCOMPARE(w->data_root_, QDir::cleanPath(root_ + " PAL"));
    QCOMPARE(w->free_needed_, 3400000000ll);
    QCOMPARE(w->data_bytes_, 2966033036ll);
    QVERIFY(w->dest_label_->FullText().contains(QDir::toNativeSeparators(root_ + " PAL")));
    Choose(w, "Burnout 3 - Takedown (USA).iso", DS::Valid);
    QCOMPARE(w->data_build_, QString("ntsc-u-retail"));
    QCOMPARE(w->data_root_, first);
  }

  // Each failure code, as the check gives it (the disc card says it) or as the copy
  // ends with it (the note says it, Resume is offered).
  void everyFailureCode_data() {
    QTest::addColumn<QString>("code");
    for (const QString& code : messages::InstallCodes()) {
      if (code != "cancelled") QTest::newRow(qPrintable(code)) << code;
    }
  }

  void everyFailureCode() {
    QFETCH(QString, code);
    const auto why = messages::ExplainInstall(code);
    QVERIFY(why.known);
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);

    // At the check.
    Choose(w, code + ".iso", DS::Invalid);
    QVERIFY2(Plain(w->disc_status_->text()).contains(why.headline), qPrintable(w->disc_status_->text()));
    QVERIFY(Plain(w->disc_status_->text()).contains(why.advice.left(30)));
    QVERIFY(!w->disc_status_->toolTip().isEmpty());  // the installer's own words
    QVERIFY(!w->primary_->isEnabled());
    QCOMPARE(w->phase_, PH::Idle);
    QVERIFY(!QFileInfo::exists(root_));

    // In the copy.
    Choose(w, "install-" + code + ".iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    QCOMPARE(w->failure_.code, code);
    QVERIFY(!w->failure_.message.isEmpty());
    QVERIFY(w->note_->isVisible());
    if (messages::IsDiscProblem(code)) {
      // BO-130: said once, on the disc card; the note says where it stopped and what to do.
      QCOMPARE(Plain(w->disc_status_->text()).count(why.headline), 1);
      QVERIFY2(!Plain(w->note_text_->text()).contains(why.headline), qPrintable(w->note_text_->text()));
      QVERIFY(Plain(w->note_text_->text()).contains(why.advice.left(30)));
    } else {
      QVERIFY2(Plain(w->note_text_->text()).contains(why.headline), qPrintable(w->note_text_->text()));
    }
    QCOMPARE(w->status_->text(), why.headline);
    QCOMPARE(w->Row("disc")->status, RS::Failed);
    QCOMPARE(w->primary_->text(), QString("Resume"));
    QVERIFY(w->primary_->isEnabled());
    QVERIFY(!w->play_->isVisible());
    QCOMPARE(w->disc_state_, messages::IsDiscProblem(code) ? DS::Invalid : DS::Valid);
    QVERIFY(w->DetailsText().contains("Code: " + code));
    QVERIFY(!QFileInfo::exists(root_ + "/.complete"));

    // Another disc: the copy goes on from what is kept.
    Choose(w, "good.iso", DS::Valid);
    QVERIFY(!w->note_->isVisible());
    QCOMPARE(w->primary_->text(), QString("Install"));
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(w->Row("disc")->detail.contains("kept from an earlier run"));
    QVERIFY(!w->Row("disc")->detail.contains(" 0 kept"));
  }

  // BO-130. A damaged file mid-copy: the card says "damaged" once and the note names the file in
  // full; the row is short (the long sentence is its tooltip); the advice is not "Resume".
  void damagedFileIsNamedOnce() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "install-damaged-source.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    const QString headline = messages::ExplainInstall("damaged-source").headline;
    const QString card = Plain(w->disc_status_->text());
    const QString note = Plain(w->note_text_->text());
    QCOMPARE(card.count(headline), 1);
    QVERIFY2(!note.contains(headline), qPrintable(note));
    QVERIFY2(note.contains("Stopped at Tracks/crash1/static.dat."), qPrintable(note));  // not cut off
    QVERIFY2(note.contains("Choose another copy of the disc image"), qPrintable(note));
    QVERIFY2(note.contains("Resume only helps after the file is replaced"), qPrintable(note));
    QVERIFY2(!(card + note).contains("Resume to try"), qPrintable(card + note));
    QVERIFY2(!(card + note).contains("dump", Qt::CaseInsensitive), qPrintable(card + note));
    QCOMPARE(w->Row("disc")->info->FullText().left(10), QString("stopped at"));
    QVERIFY(w->Row("disc")->info->toolTip().contains("Tracks/crash1/static.dat in the disc image"));
    QVERIFY(w->disc_status_->toolTip().contains("static.dat"));  // the installer's own words stay on the card
  }

  // BO-130. The details box wraps and never opens scrolled to the right.
  void detailsWrapAndStayLeft() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    w->resize(700, 600);
    w->details_toggle_->setChecked(true);
    QCOMPARE(w->log_->lineWrapMode(), QPlainTextEdit::WidgetWidth);
    w->AppendLog("check: " + QString("a-very-long-line-without-any-break ").repeated(20), true);
    w->AppendLog("$ " + QString("/some/very/long/path/").repeated(30) + "takedawn --install x --check");
    QCoreApplication::processEvents();
    QCOMPARE(w->log_->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(w->log_->horizontalScrollBar()->value(), 0);
  }

  // BO-130. The card does not claim more than was checked (the layout, not the files).
  void validCardPromisesNoMore() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    const QString card = Plain(w->disc_status_->text());
    QVERIFY2(card.contains("Looks like the right disc. Every file is checked while copying."), qPrintable(card));
    QVERIFY2(!card.contains("Ready"), qPrintable(card));
    QVERIFY2(!w->status_->text().contains("Ready"), qPrintable(w->status_->text()));
  }

  // BO-130. Not enough room: Install waits (the warning is above it), the words are the ones with
  // both numbers, and there is no "choose another folder" (the window has no folder picker).
  void lowSpaceStopsInstall() {
    qputenv("TAKEDAWN_FAKE_FREE_NEEDED", "900000000000000");  // 900 TB
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);  // (the fake program has told what it wants by now)
    qunsetenv("TAKEDAWN_FAKE_FREE_NEEDED");
    QVERIFY(w->low_space_);
    const QString space = Plain(w->space_label_->text());
    QVERIFY2(space.contains("Takedawn needs 900000 GB free (the game uses 2.5 GB). You have "), qPrintable(space));
    QVERIFY2(space.contains("Free up space on this disk."), qPrintable(space));
    QVERIFY2(!space.contains("another folder"), qPrintable(space));
    Choose(w, "good.iso", DS::Valid);
    QCOMPARE(w->phase_, PH::Idle);
    QVERIFY(!w->primary_->isEnabled());
    QVERIFY2(w->status_->text().contains("Not enough free space"), qPrintable(w->status_->text()));
    // Room is made: the next look at the disk lets it go on.
    w->free_needed_ = 1000;
    w->RefreshSpace();
    QVERIFY(!w->low_space_);
    QVERIFY(w->primary_->isEnabled());
    QVERIFY(Plain(w->space_label_->text()).contains("GB free on this disk"));
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
  }

  // BO-130. A full disk in the middle of the copy: Resume stays (it is how to go on after making
  // room), the numbers are the real ones, and "what was copied is kept" only when files are there.
  void diskFullSaysKeptOnlyWhenFilesExist() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "install-disk-full.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    QCOMPARE(w->primary_->text(), QString("Resume"));
    QVERIFY(w->primary_->isEnabled());
    QString note = Plain(w->note_text_->text());
    QVERIFY2(note.contains("Takedawn needs 3 GB free (the game uses 2.5 GB). You have "), qPrintable(note));
    QVERIFY2(note.contains("Free up space on this disk, then press Resume."), qPrintable(note));
    QVERIFY2(!note.contains("kept"), qPrintable(note));
    QVERIFY2(!note.contains("another folder"), qPrintable(note));
    QVERIFY(w->Row("disc")->info->FullText().startsWith("stopped at"));

    w = Make([](const QString& root) {  // an earlier run left a file
      QDir().mkpath(root + "/Burnout 3 Takedown/sound");
      QFile f(root + "/Burnout 3 Takedown/sound/a.rws");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray(1000, 'x'));
    });
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "install-disk-full.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    note = Plain(w->note_text_->text());
    QVERIFY2(note.contains("What was copied is kept."), qPrintable(note));
  }

  // BO-130. The no-permission advice is about the folder's permissions only.
  void noPermissionAdviceIsAboutPermissions() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "install-no-permission.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    const QString note = Plain(w->note_text_->text());
    QVERIFY2(note.contains("Change its permissions"), qPrintable(note));
    QVERIFY2(!note.contains("another folder"), qPrintable(note));
  }

  void stopAndResume() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "hold.iso", DS::Valid);
    w->primary_->click();
    QTRY_VERIFY_WITH_TIMEOUT(w->Row("disc") && w->Row("disc")->fraction >= 0.6, 20000);
    QCOMPARE(w->phase_, PH::Running);
    QCOMPARE(w->primary_->text(), QString("Stop"));
    QVERIFY(!w->image_browse_->isEnabled());
    w->SetDisc("/discs/other.iso");  // not while it runs
    QCOMPARE(w->disc_path_, QString("/discs/hold.iso"));
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Paused, 10000);
    QCOMPARE(w->primary_->text(), QString("Resume"));
    QVERIFY(!QFileInfo::exists(root_ + "/install.cancel"));  // the installer took it
    QVERIFY(!w->note_->isVisible());                         // a stop is not an error
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(w->Row("disc")->detail.contains("kept from an earlier run"));
  }

  void programEndsWithoutAnAnswer() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "crash.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    QCOMPARE(w->failure_.code, QString("program-crashed"));
    QVERIFY(w->DetailsText().contains("segmentation fault"));
    QCOMPARE(w->primary_->text(), QString("Resume"));
  }

  // The game's program cannot run at all (a library missing): said as that, with what it
  // printed, at the check, in the search, and in the copy; never "not the disc".
  void programThatNeverGetsGoing() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    w->SetDisc("/discs/nostart.iso");
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Broken, 10000);
    QCOMPARE(w->failure_.code, QString("program-failed"));
    const auto why = messages::ExplainProgram("program-failed");
    QCOMPARE(w->status_->text(), why.headline);
    QVERIFY(Plain(w->note_text_->text()).contains("Failed loading SDL3 library"));
    QVERIFY(w->DetailsText().contains("Failed loading SDL3 library"));
    QVERIFY(!Plain(w->note_text_->text()).contains("not the disc"));
  }

  // The graphics preparation ran on the CPU: the install is whole and Play is there, with a
  // warning that says why it is slow.
  void graphicsOnTheCpu() {
    shaders_ = true;
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    qputenv("TAKEDAWN_FAKE_SHADERS", "nogpu");
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(w->play_->isVisible());
    QVERIFY(w->note_->isVisible());
    QVERIFY2(Plain(w->note_text_->text()).contains("No GPU access, running on the CPU"), qPrintable(w->note_text_->text()));
    QVERIFY(w->gpu_warning_.contains("llvmpipe"));
    shaders_ = false;
  }

  void noProgram() {
    w_.reset();
    LaunchConfig config;
    config.program = "/nowhere/Takedawn";
    config.test = true;
    InstallerWindow w(config);
    w.Start();
    QCOMPARE(w.phase_, PH::Broken);
    QCOMPARE(w.failure_.code, QString("program-missing"));
    QCOMPARE(w.primary_->text(), QString("Quit"));
    QVERIFY(!w.image_browse_->isEnabled());
  }

  // An install without the marker is checked where it lies, by itself, and said so.
  void unverifiedIsCheckedInPlace() {
    InstallerWindow* w = Make(OldInstall);
    QTRY_VERIFY(w->phase_ == PH::Running || w->phase_ == PH::Done);
    QVERIFY(w->verify_);
    QVERIFY(w->note_->isVisible());
    QVERIFY(Plain(w->note_text_->text()).contains("earlier install"));
    QVERIFY(w->last_command_.contains("--dest"));
    QCOMPARE(w->last_command_.value(2), QFileInfo(root_ + "/Burnout 3 Takedown").absoluteFilePath());
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(w->Row("disc")->detail.contains("716 checked in place"));
    QVERIFY(QFileInfo::exists(root_ + "/.complete"));
    QVERIFY(w->play_->isVisible());
  }

  void unverifiedAndDamagedAsksForTheDisc() {
    InstallerWindow* w = Make([](const QString& root) {
      OldInstall(root);
      QFile f(root + "/Burnout 3 Takedown/damaged");
      QVERIFY(f.open(QIODevice::WriteOnly));
    });
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Failed, 20000);
    QCOMPARE(w->failure_.code, QString("damaged-source"));
    QVERIFY(Plain(w->note_text_->text()).contains(messages::ExplainInstall("damaged-source", InPlace()).headline));
    QVERIFY(w->image_browse_->isEnabled());
    // The disc repairs it, into the same root.
    Choose(w, "good.iso", DS::Valid);
    QVERIFY(!w->verify_);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(QFileInfo::exists(root_ + "/.complete"));
  }

  void alreadyInstalled() {
    InstallerWindow* w = Make([](const QString& root) {
      QDir().mkpath(root);
      QFile f(root + "/.complete");
      QVERIFY(f.open(QIODevice::WriteOnly));
    });
    QTRY_COMPARE(w->phase_, PH::Done);
    QVERIFY(w->play_->isVisible());
    QVERIFY(!w->primary_->isVisible());
    QVERIFY(w->last_command_.isEmpty());  // nothing was run but the question
  }

  // The data root the game was told to use goes to every command, and to the game.
  void namedDataRoot() {
    QTemporaryDir other;
    const QString dest = other.path() + "/elsewhere";
    InstallerWindow* w = Make({}, dest);
    QTRY_COMPARE(w->phase_, PH::Idle);
    QCOMPARE(w->data_root_, QDir::cleanPath(dest));
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(w->last_command_.join(' ').contains("--dest " + dest));
    QVERIFY(QFileInfo::exists(dest + "/.complete"));
    QVERIFY(!QFileInfo::exists(root_));
    QCOMPARE(w->DataArgs(), (QStringList{"--data", dest}));
  }

  // The shader build is ticked by default (BO-93) with a plain text, can be unticked, and never
  // fails the install.
  void shadersAreOnByDefault() {
    shaders_ = true;
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(w->shaders_box_->isChecked());
    QVERIFY(w->shaders_box_->text().startsWith("Prepare graphics for smooth first races"));
    QVERIFY(w->shaders_box_->text().contains("a few seconds"));
    QVERIFY(!w->shaders_box_->text().contains("shader", Qt::CaseInsensitive));
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("shaders")->status, RS::Ok);
    QVERIFY(w->last_command_.contains("--build-shaders"));
    shaders_ = false;
  }

  // The row says the result in a few plain words: the game's own line (counts, bytes, the
  // cache folder) is too long for it and would be cut in the middle. The tooltip keeps it.
  void shaderResultIsOneShortLine() {
    shaders_ = true;
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("shaders")->status, RS::Ok);
    QCOMPARE(w->Row("shaders")->info->FullText(), QString("The graphics are ready."));
    QVERIFY(w->Row("shaders")->info->toolTip().contains("pipelines"));
    shaders_ = false;
  }

  // On a short window the page scrolls; when the disc is accepted its card is brought back into view.
  void acceptedDiscIsInView() {
    InstallerWindow* w = Make();
    w->resize(700, 440);
    QTRY_COMPARE(w->phase_, PH::Idle);
    QScrollBar* bar = w->scroll_->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 0);
    bar->setValue(bar->maximum());
    Choose(w, "good.iso", DS::Valid);
    QTRY_COMPARE(bar->value(), 0);
  }

  void shadersCanBeUnticked() {
    shaders_ = true;
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    w->shaders_box_->setChecked(false);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(!w->Row("shaders"));
    QVERIFY(!w->last_command_.contains("--build-shaders"));
    shaders_ = false;
  }

  void shadersAreOptional() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(!w->shaders_box_->isChecked());
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(!w->Row("shaders"));

    w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    w->shaders_box_->setChecked(true);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("shaders")->status, RS::Ok);
    QVERIFY(w->last_command_.contains("--build-shaders"));
    QVERIFY(!w->note_->isVisible());

    w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    w->shaders_box_->setChecked(true);
    qputenv("TAKEDAWN_FAKE_SHADERS", "fail");
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("disc")->status, RS::Ok);
    QCOMPARE(w->Row("shaders")->status, RS::Warning);
    QVERIFY(w->note_->isVisible());
    QVERIFY(w->play_->isVisible());

    // Stopped while the shaders are built: the install is whole.
    w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    w->shaders_box_->setChecked(true);
    qputenv("TAKEDAWN_FAKE_SHADERS", "hold");
    w->primary_->click();
    QTRY_VERIFY_WITH_TIMEOUT(w->Row("shaders") && w->Row("shaders")->fraction >= 0.5, 20000);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 10000);
    QCOMPARE(w->Row("shaders")->status, RS::Warning);
    QVERIFY(QFileInfo::exists(root_ + "/.complete"));
  }

  // The Steam box is a Linux option, and only once its step exists.
  void platformOptions() {
    w_.reset();
    LaunchConfig config;
    config.program = QCoreApplication::applicationDirPath() + "/fake_installer";
    config.test = true;
    config.platform = "macos";
    InstallerWindow mac(config);
    QVERIFY(!mac.steam_box_);
    config.platform = "linux";
    InstallerWindow lin(config);
    QVERIFY(lin.linux_ui_);
  }

  // The Steam box (Linux): after the install the shortcut is written into the config
  // folder named for the test, never a real one; a second install adds nothing; a
  // file that is not a shortcuts.vdf is left alone and the install still succeeds.
  void steamShortcutAfterTheInstall() {
    w_.reset();
    tmp_ = std::make_unique<QTemporaryDir>();
    root_ = tmp_->path() + "/Takedawn/Game";
    const QString steam_config = tmp_->path() + "/steam/userdata/42/config";
    QDir().mkpath(steam_config);
    qputenv("TAKEDAWN_FAKE_ROOT", root_.toUtf8());
    qputenv("TAKEDAWN_FAKE_PACE", "0");
    qputenv("TAKEDAWN_STEAM_CONFIG", steam_config.toUtf8());
    qunsetenv("APPIMAGE");
    LaunchConfig config;
    config.program = QCoreApplication::applicationDirPath() + "/fake_installer";
    config.test = true;
    config.shaders = false;
    config.platform = "linux";
    auto run = [&](bool tick) {
      QDir(root_).removeRecursively();
      w_ = std::make_unique<InstallerWindow>(config);
      w_->setAttribute(Qt::WA_DontShowOnScreen);
      w_->show();
      QTRY_COMPARE(w_->phase_, PH::Idle);
      QVERIFY(w_->steam_box_);
      QVERIFY(!w_->steam_box_->isChecked());  // never without being asked
      w_->steam_box_->setChecked(tick);
      Choose(w_.get(), "Burnout 3 - Takedown (USA).iso", DS::Valid);
      w_->primary_->click();
      QTRY_COMPARE_WITH_TIMEOUT(w_->phase_, PH::Done, 20000);
      QVERIFY(w_->play_->isVisible());
    };
    const QString vdf = steam_config + "/shortcuts.vdf";
    const QString grid = steam_config + "/grid";
    run(false);
    QVERIFY(!QFileInfo::exists(vdf));
    QVERIFY(!QFileInfo::exists(grid));  // no shortcut, no art
    run(true);
    QVERIFY(QFileInfo::exists(vdf));
    QFile f(vdf);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray first = f.readAll();
    f.close();
    QVERIFY(first.contains("Takedawn"));
    QVERIFY(first.contains(config.program.toUtf8()));
    // The library art, under the id the shortcut has in the file (the fake install has
    // no game pictures: Takedawn's own).
    uint32_t id = 0;
    const std::vector<steam::Node> parsed = steam::Parse(first.toStdString());
    for (const steam::Node& field : parsed.at(0).items.at(0).items)
      if (field.key == "appid") id = uint32_t(field.number);
    QVERIFY(id & 0x80000000u);
    for (int k = 0; k < steamart::KindCount; ++k) {
      const QString art = grid + "/" + steamart::FileName(id, steamart::Kind(k));
      QVERIFY2(QFileInfo::exists(art), qPrintable(art));
      QVERIFY(steamart::IsOurs(art));
    }
    QCOMPARE(QDir(grid).entryList(QDir::Files).size(), int(steamart::KindCount));
    QVERIFY(Plain(w_->note_text_->text()).contains("added to your Steam library"));
    run(true);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), first);
    f.close();
    QVERIFY(Plain(w_->note_text_->text()).contains("already in your Steam library"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("not a vdf");
    f.close();
    QDir(grid).removeRecursively();
    run(true);
    QVERIFY(QDir(grid).entryList(QDir::Files).isEmpty());  // the shortcut was not written: no art either
    QVERIFY(QFileInfo::exists(root_ + "/.complete"));
    QVERIFY(Plain(w_->note_text_->text()).contains("not added to Steam"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("not a vdf"));
    qunsetenv("TAKEDAWN_STEAM_CONFIG");
  }

  // Steam is running (it always is on a Steam Deck): the person is asked; on Yes Steam is
  // closed with `steam -shutdown`, the shortcut is written once it is gone, and Steam is
  // started again; on No nothing is written and Steam is left running. A stand-in script
  // plays Steam (its command line looks like a Steam client's), HOME is a scratch folder.
  // Linux only (it looks at /proc), and not on a machine with a real Steam running.
  void steamIsClosedForTheShortcutAndStartedAgain() {
#ifndef Q_OS_LINUX
    QSKIP("looks at /proc");
#else
    w_.reset();
    if (steam::SteamRunning()) QSKIP("a real Steam is running here; run this in a PID namespace without it");
    tmp_ = std::make_unique<QTemporaryDir>();
    root_ = tmp_->path() + "/Takedawn/Game";
    const QString home = tmp_->path() + "/home";
    const QString bin = tmp_->path() + "/bin";
    const QString config_dir = home + "/.local/share/Steam/userdata/42/config";
    QDir().mkpath(config_dir);
    QDir().mkpath(bin);
    QDir().mkpath(home + "/Applications");
    QFile image(home + "/Applications/Takedawn.AppImage");
    QVERIFY(image.open(QIODevice::WriteOnly));
    image.write("x");
    image.close();
    // The client: runs until told to stop; "-shutdown" tells it; no arguments: started again.
    QFile script(bin + "/steam");
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(QString("#!/bin/sh\nD=\"%1\"\ncase \"$1\" in\n"
                         "  run) echo up >> \"$D/log\"; while [ ! -f \"$D/stop\" ]; do sleep 0.1; done; sleep 0.5 ;;\n"
                         "  -shutdown) echo shutdown >> \"$D/log\"; touch \"$D/stop\" ;;\n"
                         "  *) echo started >> \"$D/log\" ;;\nesac\n").arg(bin).toUtf8());
    script.close();
    script.setPermissions(script.permissions() | QFile::ExeOwner);
    const auto log = [&] {
      QFile f(bin + "/log");
      return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts) : QStringList();
    };
    const auto start_client = [&] {
      QFile::remove(bin + "/stop");
      QFile::remove(bin + "/log");
      QVERIFY(QProcess::startDetached(bin + "/steam", {"run"}));
      QTRY_VERIFY(steam::SteamRunning());
    };
    // Someone else's shortcut stays as it is.
    const QString vdf = config_dir + "/shortcuts.vdf";
    {
      const std::string other = steam::Dump({[] {
        steam::Node shortcuts, entry, name, exe;
        name.key = "AppName"; name.text = "Other"; exe.key = "Exe"; exe.text = "\"/o\"";
        entry.key = "0"; entry.type = steam::Node::Object; entry.items = {name, exe};
        shortcuts.key = "shortcuts"; shortcuts.type = steam::Node::Object; shortcuts.items = {entry};
        return shortcuts;
      }()});
      QFile f(vdf);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(other.data(), qint64(other.size()));
    }
    const QByteArray before = [&] { QFile f(vdf); f.open(QIODevice::ReadOnly); return f.readAll(); }();

    const QByteArray old_home = qgetenv("HOME");
    qputenv("HOME", home.toUtf8());
    qputenv("TAKEDAWN_STEAM_COMMAND", (bin + "/steam").toUtf8());
    qunsetenv("TAKEDAWN_STEAM_CONFIG");
    qunsetenv("APPIMAGE");
    qunsetenv("SteamGameId");
    qunsetenv("SteamAppId");
    qputenv("TAKEDAWN_FAKE_ROOT", root_.toUtf8());
    qputenv("TAKEDAWN_FAKE_PACE", "0");
    LaunchConfig config;
    config.program = QCoreApplication::applicationDirPath() + "/fake_installer";
    config.test = true;
    config.shaders = false;
    config.platform = "linux";
    int asked = 0;
    bool answer = false;
    auto run = [&] {
      QDir(root_).removeRecursively();
      w_ = std::make_unique<InstallerWindow>(config);
      w_->ask_close_steam_ = [&] { ++asked; return answer; };
      w_->setAttribute(Qt::WA_DontShowOnScreen);
      w_->show();
      QTRY_COMPARE(w_->phase_, PH::Idle);
      w_->steam_box_->setChecked(true);
      Choose(w_.get(), "Burnout 3 - Takedown (USA).iso", DS::Valid);
      w_->primary_->click();
      QTRY_COMPARE_WITH_TIMEOUT(w_->phase_, PH::Done, 30000);
    };
    const auto vdf_bytes = [&] { QFile f(vdf); f.open(QIODevice::ReadOnly); return f.readAll(); };

    // No: asked once, nothing written, Steam untouched.
    start_client();
    run();
    QCOMPARE(asked, 1);
    QVERIFY(steam::SteamRunning());
    QCOMPARE(log(), QStringList{"up"});
    QCOMPARE(vdf_bytes(), before);
    QVERIFY(Plain(w_->note_text_->text()).contains("not added to Steam"));
    QVERIFY(Plain(w_->note_text_->text()).contains("Add to Steam"));  // the manual way is said

    // A game started from Steam is running: not asked at all.
    qputenv("SteamGameId", "123");
    run();
    QCOMPARE(asked, 1);
    QCOMPARE(vdf_bytes(), before);
    QVERIFY(Plain(w_->note_text_->text()).contains("already"));
    qunsetenv("SteamGameId");

    // Yes: closed, written, started again.
    answer = true;
    run();
    QCOMPARE(asked, 2);
    QTRY_COMPARE(log(), (QStringList{"up", "shutdown", "started"}));  // (started: detached)
    QVERIFY(!steam::SteamRunning());
    QVERIFY(Plain(w_->note_text_->text()).contains("added to your Steam library"));
    const QByteArray after = vdf_bytes();
    const auto entries = steam::Parse(std::string(after.constData(), size_t(after.size()))).at(0).items;
    QCOMPARE(entries.size(), size_t(2));
    QCOMPARE(entries[0].items[0].text, std::string("Other"));  // untouched
    QCOMPARE(QString::fromStdString([&] {
               for (const auto& f : entries[1].items) if (f.key == "Exe") return f.text;
               return std::string();
             }()),
             "\"" + home + "/Applications/Takedawn.AppImage\"");
    int backups = 0;
    for (const QString& n : QDir(config_dir).entryList()) backups += n.contains("takedawn-backup");
    QCOMPARE(backups, 1);

    // Again, with Steam running again: asked, closed, nothing new written, started again.
    start_client();
    run();
    QCOMPARE(asked, 3);
    QTRY_COMPARE(log(), (QStringList{"up", "shutdown", "started"}));
    QCOMPARE(vdf_bytes(), after);
    QVERIFY(Plain(w_->note_text_->text()).contains("already in your Steam library"));

    // Steam that does not close in time is not killed: a note, the file untouched, Steam started again.
    // (Not run here: the wait is 45 s.)

    qputenv("HOME", old_home);
    qunsetenv("TAKEDAWN_STEAM_COMMAND");
#endif
  }

  // ---- where the data goes (BO-197)

  // A second window over the same roots, as the next start of the game would open it.
  InstallerWindow* Reopen() {
    w_.reset();
    LaunchConfig config;
    config.program = QCoreApplication::applicationDirPath() + "/fake_installer";
    config.test = true;
    config.shaders = shaders_;
    w_ = std::make_unique<InstallerWindow>(config);
    w_->setAttribute(Qt::WA_DontShowOnScreen);
    w_->show();
    return w_.get();
  }

  QString Saved() const {
    QFile f(root_ + ".location");
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
  }

  void changeInstallsThereAndIsRemembered() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(w->change_->isVisible());
    QVERIFY(w->change_->isEnabled());
    QVERIFY(!w->reset_->isVisible());  // at the default already
    const QString other = tmp_->path() + "/Other drive/Games";
    QVERIFY(QDir().mkpath(other));
    QVERIFY(w->SetDest(other));  // empty: used as it is
    QTRY_COMPARE(w->phase_, PH::Idle);
    QCOMPARE(w->data_root_, QDir::cleanPath(other));
    QVERIFY(w->dest_label_->FullText().contains("Other drive"));
    QVERIFY(w->reset_->isVisible());
    QCOMPARE(w->data_place_, QString("default"));  // where the game looks by itself has not changed yet
    Choose(w, "Burnout 3 - Takedown (USA).iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(w->last_command_.contains("--remember"));
    QVERIFY(QFileInfo::exists(other + "/.complete"));
    QVERIFY(!QFileInfo::exists(root_ + "/.complete"));
    QCOMPARE(Saved(), QDir::cleanPath(other));

    // The next start: the game finds it there by itself.
    w = Reopen();
    QTRY_COMPARE(w->phase_, PH::Done);
    QCOMPARE(w->data_place_, QString("saved"));
    QCOMPARE(w->data_root_, QDir::cleanPath(other));
    QVERIFY(w->reset_->isVisible());
  }

  void aFolderWithFilesGetsATakedawnFolder() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    const QString games = tmp_->path() + "/Games";
    QVERIFY(QDir().mkpath(games + "/Something else"));
    QVERIFY(w->SetDest(games));
    QTRY_COMPARE(w->phase_, PH::Idle);
    QCOMPARE(w->data_root_, QDir::cleanPath(games + "/Takedawn"));
    QVERIFY(!QFileInfo::exists(games + "/Takedawn"));  // made by the install, not by choosing
  }

  // Data moved by hand: the folder is checked in place and kept, no disc.
  void aFolderWithTheDataIsCheckedAndKept() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    const QString moved = tmp_->path() + "/Moved";
    OldInstall(moved);
    QFile done(moved + "/.complete");
    QVERIFY(done.open(QIODevice::WriteOnly));
    done.close();
    QVERIFY(w->SetDest(moved + "/Burnout 3 Takedown"));  // the game's folder inside it: its root
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->data_root_, QDir::cleanPath(moved));
    QVERIFY(w->last_command_.contains("--remember"));
    QCOMPARE(Saved(), QDir::cleanPath(moved));
  }

  void defaultTakesTheChoiceBack() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    const QString other = tmp_->path() + "/Elsewhere";
    QVERIFY(w->SetDest(other));
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "Burnout 3 - Takedown (USA).iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(Saved(), QDir::cleanPath(other));
    w = Reopen();
    QTRY_COMPARE(w->phase_, PH::Done);
    QVERIFY(w->reset_->isVisible() && w->reset_->isEnabled());
    w->reset_->click();
    QTRY_COMPARE(w->phase_, PH::Idle);  // nothing at the default
    QCOMPARE(w->data_root_, QDir::cleanPath(root_));
    QVERIFY(!w->reset_->isVisible());
    QVERIFY(!w->play_->isVisible());
    Choose(w, "Burnout 3 - Takedown (USA).iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(QFileInfo::exists(root_ + "/.complete"));
    QVERIFY(!QFileInfo::exists(root_ + ".location"));
  }

  // The saved folder is gone (a drive not connected, the data moved by hand): said, and
  // Change... finds the data where it went.
  void aSavedFolderThatIsGoneIsSaid() {
    QString gone, moved;
    InstallerWindow* w = Make([&](const QString& root) {
      gone = QFileInfo(root).absolutePath() + "/../Unplugged/Takedawn";
      moved = QFileInfo(root).absolutePath() + "/../Moved";
      QDir().mkpath(QFileInfo(root).absolutePath());
      QFile f(root + ".location");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QDir::cleanPath(gone).toUtf8() + "\n");
      OldInstall(moved);
      QFile done(moved + "/.complete");
      QVERIFY(done.open(QIODevice::WriteOnly));
    });
    QTRY_COMPARE(w->phase_, PH::Idle);
    QCOMPARE(w->data_place_, QString("saved"));
    QVERIFY(w->note_->isVisible());
    QVERIFY(Plain(w->note_text_->text()).contains("is not there"));
    QVERIFY(w->SetDest(moved));
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(Saved(), QDir::cleanPath(moved));
  }

  void notInsideTheProgramOrAFile() {
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(!w->SetDest(QCoreApplication::applicationDirPath() + "/data"));
    QVERIFY(w->note_->isVisible());
    QVERIFY(Plain(w->note_text_->text()).contains("program folder"));
    QCOMPARE(w->data_root_, QDir::cleanPath(root_));
    const QString file = tmp_->path() + "/a file";
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QVERIFY(!w->SetDest(file, true));
    QVERIFY(Plain(w->note_text_->text()).contains("is a file"));
    QCOMPARE(w->data_root_, QDir::cleanPath(root_));
  }

  void namedRootHasNoChoice() {
    const QString named = QDir::tempPath() + "/takedawn-named-" + QString::number(QCoreApplication::applicationPid());
    InstallerWindow* w = Make({}, named);
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(!w->change_->isVisible());
    QVERIFY(!w->reset_->isVisible());
    QVERIFY(!w->SetDest(tmp_->path() + "/x"));
  }

  // The palette is the overlay's "game" table (toolkit src/overlay/overlay_ui.cpp).
  void paletteIsTheOverlays() {
    QCOMPARE(theme::kPanel, QColor(0x16, 0x1d, 0x26));
    QCOMPARE(theme::kEdge, QColor(0x5a, 0x99, 0xc0));
    QCOMPARE(theme::kRule, QColor(0x3f, 0x4d, 0x5d));
    QCOMPARE(theme::kCrimson, QColor(0xc4, 0x1c, 0x1c));
    QCOMPARE(theme::kEntry, QColor(0xac, 0xd4, 0xfc));
    QCOMPARE(theme::kDim, QColor(0x82, 0x96, 0xab));
    QCOMPARE(theme::kAccent, theme::kCrimson);
  }

  // BO-311: "Sharper menu pictures". Not shown by a game that does not offer it.
  void upscaleHiddenWithoutOffer() {
    upscale_ = "";
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(!w->upscale_box_->isVisible());
    upscale_ = "no";
    w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(!w->upscale_box_->isVisible());
    upscale_ = "";
  }

  // Offered "on": ticked; its own row runs after the data, with the suggested quality.
  void upscaleTickedRunsAfterTheData() {
    upscale_ = "on";
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(w->upscale_box_->isVisible());
    QVERIFY(w->upscale_box_->isChecked());
    QVERIFY(w->upscale_cost_->text().contains("no internet"));
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("disc")->status, RS::Ok);
    QCOMPARE(w->Row("upscale")->status, RS::Ok);
    QCOMPARE(w->Row("upscale")->info->FullText(), QString("The menu pictures are upscaled."));
    QVERIFY(w->last_command_.contains("--upscale-art"));
    QVERIFY(w->last_command_.contains("best"));
    QVERIFY(!w->upscale_box_->isVisible());
    QVERIFY(QFileInfo::exists(root_ + "/fake-upscale-made"));
    upscale_ = "";
  }

  // With the shader build too: shaders first, then the pictures.
  void upscaleAfterShaders() {
    upscale_ = "on";
    shaders_ = true;
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("shaders")->status, RS::Ok);
    QCOMPARE(w->Row("upscale")->status, RS::Ok);
    QCOMPARE(w->rows_.size(), 3);
    QCOMPARE(w->rows_[1].id, QString("shaders"));
    QCOMPARE(w->rows_[2].id, QString("upscale"));
    shaders_ = false;
    upscale_ = "";
  }

  // Offered "off" (a handheld, built-in graphics): shown, not ticked; installed without it,
  // the offer is turned down so the window does not ask again.
  void upscaleUntickedIsDeclined() {
    upscale_ = "off";
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    QVERIFY(w->upscale_box_->isVisible());
    QVERIFY(!w->upscale_box_->isChecked());
    QVERIFY(w->upscale_cost_->text().contains("Not ticked on this computer"));
    Choose(w, "good.iso", DS::Valid);
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QVERIFY(!w->Row("upscale"));
    QVERIFY(QFileInfo::exists(root_ + "/fake-upscale-declined"));
    upscale_ = "";
  }

  // A failure or a stop is a warning: the game is installed, Play is offered.
  void upscaleFailureIsAWarning() {
    upscale_ = "on";
    InstallerWindow* w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    qputenv("TAKEDAWN_FAKE_UPSCALE_RUN", "fail");
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("disc")->status, RS::Ok);
    QCOMPARE(w->Row("upscale")->status, RS::Warning);
    QVERIFY(w->Row("upscale")->info->FullText().contains("graphics card stopped"));
    QVERIFY(w->note_->isVisible());
    QVERIFY(w->play_->isVisible());

    w = Make();
    QTRY_COMPARE(w->phase_, PH::Idle);
    Choose(w, "good.iso", DS::Valid);
    qputenv("TAKEDAWN_FAKE_UPSCALE_RUN", "hold");
    w->primary_->click();
    QTRY_VERIFY_WITH_TIMEOUT(w->Row("upscale") && w->Row("upscale")->status == RS::Running &&
                             w->Row("upscale")->fraction >= 0.5, 20000);
    QVERIFY(!w->upscale_work_.isEmpty());
    w->Cancel();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QCOMPARE(w->Row("upscale")->status, RS::Warning);
    QVERIFY(w->Row("upscale")->info->FullText().contains("stopped"));
    QVERIFY(w->play_->isVisible());
    upscale_ = "";
  }

  // An update over an install without the pack: offered once, unticked, with its own button.
  void upscaleOfferedOnceOnAnUpdate() {
    upscale_ = "on";
    auto installed = [](const QString& root) {
      OldInstall(root);
      QFile f(root + "/.complete");
      QVERIFY(f.open(QIODevice::WriteOnly));
    };
    InstallerWindow* w = Make(installed);
    QTRY_COMPARE(w->phase_, PH::Done);
    QVERIFY(w->upscale_box_->isVisible());
    QVERIFY(!w->upscale_box_->isChecked());
    QVERIFY(!w->primary_->isVisible());
    QVERIFY(w->play_->isVisible());
    w->upscale_box_->setChecked(true);
    QVERIFY(w->primary_->isVisible());
    QCOMPARE(w->primary_->text(), QString("Upscale menu pictures"));
    w->primary_->click();
    QTRY_COMPARE_WITH_TIMEOUT(w->phase_, PH::Done, 20000);
    QTRY_VERIFY(w->Row("upscale") && w->Row("upscale")->status == RS::Ok);
    QCOMPARE(w->Row("disc")->status, RS::Skipped);
    QVERIFY(!w->upscale_box_->isVisible());
    QVERIFY(!w->primary_->isVisible());
    QVERIFY(w->play_->isVisible());

    // Made: not offered again.
    const QString root = root_;
    w = Make([&](const QString& r) { installed(r); QFile f(r + "/fake-upscale-made"); QVERIFY(f.open(QIODevice::WriteOnly)); });
    QTRY_COMPARE(w->phase_, PH::Done);
    QVERIFY(!w->upscale_box_->isVisible());

    // Seen and Play pressed without it: declined, and the next window does not show it.
    w = Make(installed);
    QTRY_COMPARE(w->phase_, PH::Done);
    QVERIFY(w->upscale_box_->isVisible());
    w->Play();
    QVERIFY(QFileInfo::exists(root_ + "/fake-upscale-declined"));
    const QString declined_root = root_;
    w_.reset();
    {
      // the same data root again
      qputenv("TAKEDAWN_FAKE_ROOT", declined_root.toUtf8());
      LaunchConfig config;
      config.program = QCoreApplication::applicationDirPath() + "/fake_installer";
      config.test = true;
      w_ = std::make_unique<InstallerWindow>(config);
      w_->setAttribute(Qt::WA_DontShowOnScreen);
      w_->show();
      QTRY_COMPARE(w_->phase_, PH::Done);
      QVERIFY(!w_->upscale_box_->isVisible());
    }
    Q_UNUSED(root);
    upscale_ = "";
  }

  void cleanup() { w_.reset(); }
};

QTEST_MAIN(WindowTest)
#include "window_test.moc"
