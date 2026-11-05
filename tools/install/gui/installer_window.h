#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

#include "driver_process.h"
#include "pad_input.h"
#include "protocol.h"

class DiscBrowser;
class ElidedLabel;
class HeaderBand;
class HeatProgressBar;
class QCheckBox;
class QDialog;
class QFrame;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class PadNavigator;
class QToolButton;
class StepGlyph;

// What the command line (or a test) sets before the window opens.
struct LaunchConfig {
  QString program;   // the game's executable (default: found beside this one, see LocateProgram)
  QString dest;      // the data root the game was told to use (--data / TAKEDAWN_DATA); empty: the game's own
  QString disc;      // a disc image or folder to start with
  QString platform;  // tests: show another system's options ("macos", "linux", "windows")
  bool shaders = true;   // the "Prepare graphics" box starts ticked (BO-93); the player can untick it
  // "Sharper menu pictures" (BO-311): -1 as the game offers it for this computer
  // (--data-status: ticked, unticked or not shown), 0 unticked, 1 ticked.
  int upscale = -1;
  bool test = false;     // tests and screenshots: no confirmation boxes, Play starts nothing
  // No mouse or keyboard to count on (gamescope, a Steam Deck in Game Mode): the
  // window fills the screen without a frame, its controls are finger sized, and
  // the controller hints are always shown.
  bool couch = false;
  // The built-in disc browser in place of the system's file dialog: 1 on, 0 off,
  // -1 by the system (on where the options are Linux's).
  int browser = -1;
  // Where the browser and the search for the disc start: the home folder and
  // the folders drives are mounted under. Empty: this system's. A test names a
  // tree of its own (and without one a test looks nowhere).
  QString home;
  QStringList media_roots;
  bool pad = true;       // read the controllers (tests press the keys themselves)
};

// Takedawn's setup window: the front end of the installer inside the game's own
// executable. It asks the game where its data is and whether it is whole
// (`--data-status`), takes the player's disc image or folder, has the game check it
// (`--install <disc> --check`) and copy it (`--install <disc>`), follows the
// STEP / PROGRESS / RESULT lines, and starts the game when it is done. The window
// decides nothing about the disc or the files: every answer is the installer's.
class InstallerWindow : public QWidget {
  Q_OBJECT

 public:
  explicit InstallerWindow(const LaunchConfig& config, QWidget* parent = nullptr);
  ~InstallerWindow() override;

  // Finds the game and asks it for the state of its data. Called when the window is
  // first shown; a test calls it itself.
  void Start();
  // The disc image or folder to install from: checked at once.
  void SetDisc(const QString& path);
  // A controller's press (the controllers' own come through here too).
  void PadPress(PadKey key);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  friend class ShotRunner;  // shots.cpp: drives the window and grabs it
  friend class WindowTest;  // tests/window_test.cpp
  friend class PadTest;     // tests/pad_test.cpp
  friend class PadScript;   // shots.cpp: the real program driven by a made-up controller

  // Asking: `--data-status` is out. Idle: waiting for a disc (or for Install).
  // Running: a step is. Paused: stopped by the player. Failed: a step failed.
  // Done: the data is whole. Broken: there is no game to run.
  enum class Phase { Asking, Idle, Running, Paused, Failed, Done, Broken };
  enum class DiscState { Empty, Checking, Valid, Invalid };
  enum class RowStatus { Pending, Running, Ok, Skipped, Failed, Warning };
  enum class NoteTone { Info, Warning, Error };

  struct StepRow {
    QString id;        // "disc", "shaders", "upscale"
    int weight = 1;    // its share of the progress bar
    RowStatus status = RowStatus::Pending;
    double fraction = 0.0;
    QString detail;
    QElapsedTimer clock;
    qint64 millis = 0;
    StepGlyph* glyph = nullptr;
    QLabel* title = nullptr;
    ElidedLabel* info = nullptr;
    QLabel* time = nullptr;
  };

  struct Failure {
    QString step;
    QString code;
    QString message;  // the installer's own words
    QStringList stderr_tail;
  };

  // Building the window.
  QWidget* BuildDiscSection();
  QWidget* BuildInstallSection();
  QWidget* BuildDetails();
  void ShowAbout();
  QString LicenseDir() const;

  // The game's program and the state of its data.
  bool LocateProgram();
  void QueryStatus();
  void OnStatus(const DriverProcess::Result& result);
  QStringList DataArgs() const;  // "--data <dest>" when the root was named
  QStringList StatusArgs() const;  // DataArgs, else "--data <target>" for a folder the player chose

  // Where the data goes (BO-197): Change... and Default. The folder the player picks
  // gets "Takedawn" added unless it is empty or already holds the game's data.
  void ChangeDest();
  void OpenDestBrowser();
  void NativeDestDialog();
  QString DestFolderFor(const QString& picked) const;
  // Checks a folder and, when it is fit, makes it the target and asks the game about
  // it. False with the reason in a note when it is not (or the player said no to a
  // warning). `exact`: the folder as it is (Default), no "Takedawn" added.
  bool SetDest(const QString& picked, bool exact = false);
  // Empty when `folder` can hold the data, else why not. `warning` gets a sentence
  // for a folder that works but may not be a good place (a network or removable drive).
  QString CheckDest(const QString& folder, QString* warning) const;
  void RefreshDestControls();
  bool DestFixed() const;  // --data / TAKEDAWN_DATA named the root, or a checkout: no choice
  void RefreshSpace();
  void AdoptDiscProgram(const QString& build, const QString& root, const QString& place, const QString& def,
                        qint64 bytes, qint64 free_needed);
  qint64 CopiedBytes() const;  // the game's files already in the data folder
  qint64 FreeBytes() const;    // free on the disk the data folder is on, -1 when unknown
  qint64 StillNeededFree() const;  // what the installer wants free now (the 3 GB less what is already there)

  // The disc.
  void OpenBrowser();
  void CloseBrowser();
  void NativeImageDialog();
  void StartDetect();
  void DetectNext();
  void ShowFound(const QString& path);
  void HideFound();
  void BrowseImage();
  void BrowseFolder();
  void CheckDisc();
  void SetDiscState(DiscState state, const QString& headline, const QString& advice, const QString& detail);

  // The install.
  void OnPrimaryButton();
  void StartInstall();
  void RunInstall();
  void RunShaders();
  void RunUpscale();
  // The optional steps after the copy, in order; false when none is left.
  bool RunNextOptional();
  void StartStep(const QString& step, const QStringList& arguments);
  void OnEvent(const Event& event);
  void OnFinished(const DriverProcess::Result& result);
  void Succeed();
  void AddToSteam();
  void WriteSteamArt(const QString& grid, quint32 id);
  bool CloseSteam(const std::vector<std::string>& command, bool* closed);
  void StartSteam(const std::vector<std::string>& command);
  void Fail(const Failure& failure);
  void Pause();
  void Cancel();
  void Break(const QString& code, const QString& detail);
  // The game's program ran and ended without speaking the protocol at all (a library
  // it needs is missing, it crashed): nothing can work, and the window says so, with
  // what the program printed. False when the program answered or was stopped on purpose.
  bool ProgramFailed(const DriverProcess::Result& result, bool usage_exit_is_old_program);
  void Play();

  // The controller.
  QWidget* PadRoot();
  QWidget* PreferredFocus();
  void SettleFocus();
  void RefreshHints();
  void MarkPadFocus();

  // Showing it.
  void BuildRows(const QStringList& steps);
  StepRow* Row(const QString& id);
  void UpdateRow(StepRow& row);
  void UpdateProgress();
  void SetStatus(const QString& text);
  void SetNote(NoteTone tone, const QString& headline, const QString& text, bool with_copy);
  void ClearNote();
  void SetPhase(Phase phase);
  void RefreshLook();
  void AppendLog(const QString& line, bool warning = false);
  QString ElapsedText() const;
  QString DetailsText() const;
  QString RunningVerb() const;

  LaunchConfig config_;
  Phase phase_ = Phase::Asking;
  bool started_ = false;
  bool linux_ui_ = false;
  bool couch_ = false;
  bool use_browser_ = false;
  bool pad_seen_ = false;   // a controller is there, or one was used
  QPointer<QWidget> pad_marked_;     // the control that carries the controller's focus mark
  QWidget* focus_wanted_ = nullptr;  // where SettleFocus last put the focus

  // What the game said (`--data-status`).
  QString program_;
  QString data_root_;
  QString version_line_;
  QString disc_name_;
  qint64 data_bytes_ = 0;
  qint64 free_needed_ = 0;
  int data_files_ = 0;
  QString data_build_;    // the build of the program that answered (--data-status, then the disc's --check)
  QString data_place_;    // where the game looks by itself: "named", "saved", "default", "checkout"
  QString default_root_;  // the per-user default folder
  QString target_;        // the folder the player chose with Change... (empty: where the game looks)
  QString status_code_;  // "" installed, "not-installed", "unverified"
  QString status_text_;
  DriverProcess* status_proc_ = nullptr;

  // The disc.
  DiscState disc_state_ = DiscState::Empty;
  QString disc_path_;
  QString disc_valid_for_;
  DriverProcess* disc_proc_ = nullptr;
  int disc_serial_ = 0;
  QStringList detect_queue_;   // what the search found, still to be checked by the game
  DriverProcess* detect_proc_ = nullptr;
  QString found_path_;         // the one the game accepted, offered to the player

  // The run.
  bool verify_ = false;     // checking an older install in place, not copying from a disc
  bool low_space_ = false;  // not enough room for the copy: Install waits
  QString source_;          // what the running (or failed) install reads
  QStringList queue_;       // the steps still to run after the current one
  QString current_step_;
  QString shader_cache_;    // from "LOG shaders cache <dir>", for its cancel file
  QString shader_warning_;
  QString upscale_offer_ = "no";   // from "LOG data upscale <on|off|no> <quality> <why>"
  QString upscale_quality_ = "best";
  QString upscale_work_;     // from "LOG upscale work <dir>", for its cancel file
  QString upscale_warning_;
  QString upscale_pack_ = "none";  // from "LOG data upscale-pack made|none|declined"
  // The data was whole already (an update): the box is offered once, unticked, with
  // its own button; nothing else runs. Seen and not used: `--upscale-art --dismiss`.
  bool late_upscale_ = false;
  bool upscale_shown_ = false;     // the box was on screen at some point
  bool upscale_answered_ = false;  // used, or the dismissal sent
  void StartLateUpscale();
  void DismissUpscaleOffer();
  QString gpu_warning_;  // the graphics preparation ran on the CPU: the renderer's name
  DriverProcess* driver_ = nullptr;
  QStringList last_command_;
  bool cancel_requested_ = false;
  bool saw_failure_ = false;
  Failure failure_;
  QElapsedTimer run_clock_;
  qint64 run_millis_ = 0;
  QTimer tick_;
  QTimer cancel_timer_;
  QStringList log_lines_;
  int play_count_ = 0;      // tests: how often Play was pressed

  // Widgets.
  HeaderBand* header_ = nullptr;
  QScrollArea* scroll_ = nullptr;  // the disc and install sections, which scroll on a short window
  QFrame* disc_card_ = nullptr;
  StepGlyph* disc_glyph_ = nullptr;
  QLabel* disc_status_ = nullptr;
  ElidedLabel* disc_file_ = nullptr;
  QToolButton* image_browse_ = nullptr;
  QToolButton* folder_browse_ = nullptr;
  QFrame* note_ = nullptr;
  QLabel* note_text_ = nullptr;
  QToolButton* note_copy_ = nullptr;
  QWidget* rows_host_ = nullptr;
  QList<StepRow> rows_;
  ElidedLabel* dest_label_ = nullptr;
  QLabel* space_label_ = nullptr;
  QCheckBox* shaders_box_ = nullptr;
  QLabel* shaders_cost_ = nullptr;
  QCheckBox* upscale_box_ = nullptr;
  QLabel* upscale_cost_ = nullptr;
  void RefreshUpscaleOffer();
  QCheckBox* steam_box_ = nullptr;  // Linux only: the Steam shortcut (steam_shortcut.h)
  // Asked when Steam is running and has to close for the shortcut; tests replace it.
  std::function<bool()> ask_close_steam_;
  // Asked about a network or removable drive; tests replace it.
  std::function<bool(const QString&)> ask_use_anyway_;
  bool browse_dest_ = false;  // the built-in browser is choosing where the data goes, not the disc
  QToolButton* change_ = nullptr;
  QToolButton* reset_ = nullptr;
  QToolButton* details_toggle_ = nullptr;
  QPlainTextEdit* log_ = nullptr;
  HeatProgressBar* progress_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* clock_label_ = nullptr;
  QToolButton* about_ = nullptr;
  QPushButton* play_ = nullptr;
  QPushButton* primary_ = nullptr;
  QDialog* about_dialog_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QWidget* main_page_ = nullptr;
  DiscBrowser* browser_ = nullptr;
  QFrame* found_ = nullptr;
  QLabel* found_text_ = nullptr;
  QToolButton* found_use_ = nullptr;
  QToolButton* found_other_ = nullptr;
  QLabel* hints_ = nullptr;
  PadSource* pad_ = nullptr;
  PadNavigator* nav_ = nullptr;
};
