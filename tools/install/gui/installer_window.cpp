#include "installer_window.h"

// The disc this package is for (CMake TAKEDAWN_REGION: NTSC-U or PAL,
// the program's BO3_REGION; docs/pal.md).
#ifndef TAKEDAWN_DISC_SHORT
#define TAKEDAWN_DISC_SHORT "NTSC-U"
#endif
#ifndef TAKEDAWN_DISC_LONG
#define TAKEDAWN_DISC_LONG "North America (NTSC-U)"
#endif

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <vector>
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "app_icon.h"
#include "disc_browser.h"
#include "pad_nav.h"
#include "steam_artwork.h"
#include "steam_shortcut.h"
#include "elided_label.h"
#include "messages.h"
#include "theme.h"
#include "ui_widgets.h"

#ifndef TAKEDAWN_SETUP_VERSION
#define TAKEDAWN_SETUP_VERSION "0.1.0"
#endif

namespace {

const char kGameDir[] = "Burnout 3 Takedown";  // the folder of the disc's files under the data root

// The Steam "add to library" step (Linux, Steam Deck): steam_shortcut.cpp, the
// shortcuts.vdf writer the source installer had in Python. Offered on Linux only,
// as it was there.
bool SteamStepAvailable() { return true; }

// The folder itself if it exists, else the nearest one that does (for the free
// space of a folder that is yet to be made).
QString NearestExisting(const QString& path) {
  QString dir = QDir::cleanPath(QDir(path).absolutePath());
  while (!QFileInfo(dir).isDir()) {
    const QString up = QFileInfo(dir).absolutePath();
    if (up == dir) break;
    dir = up;
  }
  return dir;
}

QString HumanDuration(qint64 millis) {
  const qint64 s = millis / 1000;
  return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

QString Gb(qint64 bytes) {
  const double gb = bytes / 1e9;
  QString text = QString::number(gb, 'f', gb < 10 ? 1 : 0);
  if (text.endsWith(".0")) text.chop(2);  // "3 GB", not "3.0 GB"
  return text;
}

QString Span(const QColor& color, const QString& text, bool bold = false) {
  return QString("<span style=\"color:%1;%2\">%3</span>")
      .arg(theme::Css(color), bold ? "font-weight:600;" : "", text.toHtmlEscaped());
}

void Hint(QLabel* label) { label->setProperty("hint", "true"); }

// The height of a control: a finger wants 44 px and more.
int g_control_height = 34;

QToolButton* MakeToolButton(const QString& text) {
  auto* b = new QToolButton;
  b->setText(text);
  b->setMinimumHeight(g_control_height);
  b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return b;
}

QFrame* MakeSection(QVBoxLayout** inner) {
  auto* frame = new QFrame;
  frame->setObjectName("section");
  frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);  // never taller than its content
  *inner = new QVBoxLayout(frame);
  (*inner)->setContentsMargins(18, 14, 18, 16);
  (*inner)->setSpacing(10);
  return frame;
}

// The page inside the scroll area: as tall as its content is at the width it has.
// (A layout's own minimum counts a wrapped label as one long line, so without this
// the sections are squeezed into each other on a short window before anything scrolls.)
class FlowPage : public QWidget {
 public:
  using QWidget::QWidget;

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    Fit();
  }
  bool event(QEvent* event) override {
    const bool taken = QWidget::event(event);
    if (event->type() == QEvent::LayoutRequest) Fit();
    return taken;
  }

 private:
  void Fit() {
    if (!layout()) return;
    const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width())
                                                : layout()->totalMinimumSize().height();
    if (h != minimumHeight()) setMinimumHeight(h);
  }
};

// What an AppImage sets up for itself is not meant for the game it starts.
QProcessEnvironment ChildEnvironment() {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  for (const char* name : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                           "QT_QPA_PLATFORM", "QT_SCALE_FACTOR"}) {
    env.remove(name);
  }
  if (env.contains("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH")) {
    env.insert("LD_LIBRARY_PATH", env.value("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH"));
  }
#ifdef Q_OS_LINUX
  // The game itself needs what the AppImage carries beside the program (SDL 3
  // is only there, and a stock desktop has none): the image's own usr/lib, ahead
  // of whatever the person had set, as AppRun gives the game when it starts it.
  // That is the folder beside this program's, wherever the image is mounted.
  const QString lib = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../lib");
  if (QFileInfo::exists(lib + "/libSDL3.so.0") || QFileInfo::exists(lib + "/libSDL2-2.0.so.0")) {
    const QString rest = env.value("LD_LIBRARY_PATH");
    env.insert("LD_LIBRARY_PATH", rest.isEmpty() ? lib : lib + ":" + rest);
  }
#endif
  return env;
}

// A program that never got going: the last lines it printed go in the note, where the
// person looks, not only in the details.
QString WithTail(const QString& code, const QString& advice, const QString& detail) {
  if (code != "program-failed" || detail.isEmpty()) return advice;
  const QStringList lines = detail.split('\n');
  return advice + "\n\n" + lines.mid(std::max<qsizetype>(0, lines.size() - 3)).join('\n');
}

}  // namespace

// ---------------------------------------------------------------- the window

InstallerWindow::InstallerWindow(const LaunchConfig& config, QWidget* parent) : QWidget(parent), config_(config) {
  setWindowTitle(tr("Takedawn Setup"));
  setWindowIcon(AppIcon());
  setAcceptDrops(true);
  setMinimumSize(660, 540);
  ask_close_steam_ = [this] {
    if (config_.test) return true;  // tests: no confirmation boxes
    return QMessageBox::question(this, windowTitle(),
                                 tr("Steam has to close for a moment to add Takedawn. Close Steam and add it?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
  };
  couch_ = config_.couch;
  g_control_height = couch_ ? 48 : 34;
  if (couch_) {
    // The screen is the window: gamescope shows one window at a time, and a
    // title bar is of no use without a pointer.
    setWindowFlag(Qt::FramelessWindowHint);
    const QRect all = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->geometry() : QRect(0, 0, 1024, 640);
    setMinimumSize(std::min(660, all.width()), std::min(540, all.height()));
    setGeometry(all);
  } else {
    // Tall enough to show everything without scrolling, where the screen has the room.
    const QRect avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry()
                                                         : QRect(0, 0, 1280, 800);
    resize(std::clamp(avail.width() - 40, 660, 780), std::clamp(avail.height() - 90, 540, 790));
  }
  // Which system's options the window shows. A test names another.
  QString platform = config_.platform;
  if (platform.isEmpty()) {
#if defined(Q_OS_MACOS)
    platform = "macos";
#elif defined(Q_OS_WIN)
    platform = "windows";
#else
    platform = "linux";
#endif
  }
  linux_ui_ = platform == "linux";
  use_browser_ = config_.browser < 0 ? linux_ui_ || couch_ : config_.browser != 0;

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  header_ = new HeaderBand;
  header_->SetTitle(tr("Takedawn"));
  header_->SetSubtitle(tr("Setup. The game's data comes from your own Burnout 3: Takedown (" TAKEDAWN_DISC_SHORT ") disc."));
  header_->SetIcon(AppIconPixmap(72, devicePixelRatioF()));
  root->addWidget(header_);

  // At large sizes the content stays a comfortable width, centred.
  // The setup itself and the disc browser take turns in the same place.
  auto* host = new QWidget;
  host->setMaximumWidth(940);
  auto* host_layout = new QVBoxLayout(host);
  host_layout->setContentsMargins(16, 14, 16, couch_ ? 8 : 14);
  host_layout->setSpacing(0);
  stack_ = new QStackedWidget;
  host_layout->addWidget(stack_, 1);
  main_page_ = new QWidget;
  auto* outer = new QVBoxLayout(main_page_);
  outer->setSpacing(10);
  outer->setContentsMargins(0, 0, 0, 0);
  stack_->addWidget(main_page_);
  browser_ = new DiscBrowser;
  stack_->addWidget(browser_);
  auto* centre = new QHBoxLayout;
  centre->setContentsMargins(0, 0, 0, 0);
  centre->addStretch(1);
  centre->addWidget(host, 100);
  centre->addStretch(1);
  root->addLayout(centre, 1);

  // On a short screen the two panels scroll; the progress and the buttons stay.
  auto* page = new FlowPage;
  auto* flow = new QVBoxLayout(page);
  flow->setContentsMargins(0, 0, 0, 0);
  flow->setSpacing(10);
  flow->addWidget(BuildDiscSection());
  flow->addWidget(BuildInstallSection());
  flow->addStretch(1);
  auto* scroll = scroll_ = new QScrollArea;
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(page);
  outer->addWidget(scroll, 3);
  outer->addWidget(BuildDetails(), 0);

  progress_ = new HeatProgressBar;
  progress_->setRange(0, 1000);
  progress_->setValue(0);
  outer->addWidget(progress_);

  // The status line is centred; the share done and the time taken sit at its right.
  auto* status_row = new QHBoxLayout;
  status_ = new QLabel;
  status_->setObjectName("status");
  status_->setAlignment(Qt::AlignCenter);
  status_->setWordWrap(true);
  status_->setMinimumHeight(34);
  clock_label_ = new QLabel;
  clock_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  Hint(clock_label_);
  clock_label_->setMinimumWidth(120);
  auto* balance = new QWidget;  // keeps the status text centred
  balance->setMinimumWidth(120);
  status_row->addWidget(balance);
  status_row->addWidget(status_, 1);
  status_row->addWidget(clock_label_);
  outer->addLayout(status_row);

  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(10);
  about_ = new QToolButton;
  about_->setObjectName("fold");
  about_->setText(tr("About and licences"));
  if (couch_) about_->setMinimumHeight(44);
  play_ = new QPushButton(tr("Play"));
  primary_ = new QPushButton(tr("Install"));
  for (QPushButton* b : {primary_, play_}) {
    b->setMinimumHeight(couch_ ? 50 : 46);
    b->setMinimumWidth(220);
    b->setFont(theme::DisplayFont(13));
  }
  play_->setVisible(false);
  primary_->setDefault(true);
  buttons->addWidget(about_);
  buttons->addStretch(1);
  buttons->addWidget(primary_);
  buttons->addWidget(play_);
  outer->addLayout(buttons);

  connect(primary_, &QPushButton::clicked, this, &InstallerWindow::OnPrimaryButton);
  connect(play_, &QPushButton::clicked, this, &InstallerWindow::Play);
  connect(about_, &QToolButton::clicked, this, &InstallerWindow::ShowAbout);
  connect(image_browse_, &QToolButton::clicked, this, &InstallerWindow::BrowseImage);
  connect(folder_browse_, &QToolButton::clicked, this, &InstallerWindow::BrowseFolder);
  if (use_browser_) {
    // One list shows both the images and the folders.
    image_browse_->setText(tr("Choose disc..."));
    image_browse_->setToolTip(tr("The .iso of your disc, or the folder its files were extracted to."));
    folder_browse_->setVisible(false);
  }
  browser_->SetNativeOffered(!couch_);
  connect(browser_, &DiscBrowser::Chosen, this, [this](const QString& path) {
    const bool dest = browse_dest_;
    CloseBrowser();
    if (dest) SetDest(path);
    else SetDisc(path);
  });
  connect(browser_, &DiscBrowser::Cancelled, this, &InstallerWindow::CloseBrowser);
  connect(browser_, &DiscBrowser::NativeRequested, this, [this] {
    if (browse_dest_) NativeDestDialog();
    else NativeImageDialog();
  });
  connect(change_, &QToolButton::clicked, this, &InstallerWindow::ChangeDest);
  connect(reset_, &QToolButton::clicked, this, [this] {
    if (!default_root_.isEmpty()) SetDest(default_root_, true);
  });
  ask_use_anyway_ = [this](const QString& warning) {
    if (config_.test) return true;
    return QMessageBox::question(this, windowTitle(), warning + "\n\n" + tr("Use this folder anyway?")) ==
           QMessageBox::Yes;
  };
  connect(found_use_, &QToolButton::clicked, this, [this] {
    const QString path = found_path_;
    HideFound();
    SetDisc(path);
  });
  connect(found_other_, &QToolButton::clicked, this, [this] {
    HideFound();
    SetStatus(tr("Choose your disc image or folder to begin."));
    if (use_browser_) OpenBrowser();
    else SettleFocus();
  });

  // The controller: what the buttons do is said along the foot of the window
  // while one is there (always, where there is nothing else to use).
  hints_ = new QLabel;
  hints_->setObjectName("padHints");
  hints_->setTextFormat(Qt::RichText);
  hints_->setAlignment(Qt::AlignCenter);
  hints_->setVisible(couch_);
  root->addWidget(hints_);
  connect(qApp, &QApplication::focusChanged, this, [this] { RefreshHints(); });
  nav_ = new PadNavigator(this);
  nav_->Root = [this] { return PadRoot(); };
  nav_->DefaultFocus = [this] { return PreferredFocus(); };
  nav_->Back = [this] {
    if (stack_->currentWidget() == browser_) browser_->GoBack();
  };
  nav_->Start = [this] {
    if (stack_->currentWidget() == browser_) return;
    if (play_->isVisible() && play_->isEnabled()) play_->click();
    else if (primary_->isVisible() && primary_->isEnabled()) primary_->click();
  };
  if (config_.pad && !config_.test && PadSource::Available()) {
    pad_ = new PadSource(this);
    connect(pad_, &PadSource::Pressed, this, [this](PadKey key) {
      // Only the window in front is driven (under gamescope it always is).
      if (!couch_ && !QApplication::activeWindow()) return;
      PadPress(key);
    });
    connect(pad_, &PadSource::ConnectedChanged, this, [this](bool connected, const QString& name) {
      AppendLog(connected ? "controller: " + name : QString("controller: none"));
      if (qEnvironmentVariableIntValue("TAKEDAWN_SETUP_TRACE")) {
        QTextStream(stderr) << "[SETUP] controller: " << (connected ? name : QString("none")) << Qt::endl;
      }
      if (connected) pad_seen_ = true;
      RefreshHints();
      if (connected) SettleFocus();
    });
    pad_->Start();
  }
  tick_.setInterval(200);
  connect(&tick_, &QTimer::timeout, this, [this] {
    for (StepRow& row : rows_) {
      if (row.status == RowStatus::Running) UpdateRow(row);
    }
    clock_label_->setText(ElapsedText());
  });
  // Stop: the polite way first (the file the installer looks for once a second),
  // then the signal.
  cancel_timer_.setSingleShot(true);
  cancel_timer_.setInterval(2500);
  connect(&cancel_timer_, &QTimer::timeout, this, [this] {
    if (driver_) driver_->Cancel();
  });

  shaders_box_->setChecked(config_.shaders);
  BuildRows({"disc"});
  SetDiscState(DiscState::Empty, QString(), QString(), QString());
  SetPhase(Phase::Asking);
  SetStatus(tr("Looking for the game's data..."));
}

InstallerWindow::~InstallerWindow() {
  // (the children go after this: no focus change of theirs may come back here)
  disconnect(qApp, nullptr, this, nullptr);
}

QWidget* InstallerWindow::BuildDiscSection() {
  QVBoxLayout* cur = nullptr;
  QFrame* section = MakeSection(&cur);
  auto* heading = new QLabel(tr("Your game disc"));
  theme::StyleHeading(heading);
  cur->addWidget(heading);

  // The card: what the disc is, and the two ways to choose it. The whole window
  // takes a drop.
  disc_card_ = new QFrame;
  disc_card_->setObjectName("card");
  auto* card = new QHBoxLayout(disc_card_);
  card->setContentsMargins(14, 12, 14, 12);
  card->setSpacing(12);
  disc_glyph_ = new StepGlyph;
  card->addWidget(disc_glyph_, 0, Qt::AlignTop);
  auto* texts = new QVBoxLayout;
  texts->setSpacing(4);
  disc_status_ = new QLabel;
  disc_status_->setWordWrap(true);
  disc_status_->setTextFormat(Qt::RichText);
  disc_status_->setMinimumWidth(300);  // a wrapped label asked for a tiny width reports a huge height
  disc_status_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  disc_file_ = new ElidedLabel;
  disc_file_->setVisible(false);
  texts->addWidget(disc_status_);
  texts->addWidget(disc_file_);
  texts->addStretch(1);
  card->addLayout(texts, 1);
  auto* choose = new QVBoxLayout;
  choose->setSpacing(8);
  image_browse_ = MakeToolButton(tr("Choose image..."));
  folder_browse_ = MakeToolButton(tr("Choose folder..."));
  image_browse_->setToolTip(tr("The .iso of your disc."));
  folder_browse_->setToolTip(tr("The folder your disc's files were extracted to (the one with default.xbe in it)."));
  for (QToolButton* b : {image_browse_, folder_browse_}) b->setMinimumWidth(150);
  choose->addWidget(image_browse_);
  choose->addWidget(folder_browse_);
  choose->addStretch(1);
  card->addLayout(choose);
  cur->addWidget(disc_card_);

  // What the search of the likely places found, offered: nothing is used unasked.
  found_ = new QFrame;
  found_->setObjectName("note");
  auto* found = new QHBoxLayout(found_);
  found->setContentsMargins(12, 10, 12, 10);
  found->setSpacing(10);
  found_text_ = new QLabel;
  found_text_->setWordWrap(true);
  found_text_->setTextFormat(Qt::RichText);
  found_text_->setMinimumWidth(240);
  found_use_ = MakeToolButton(tr("Use it"));
  found_other_ = MakeToolButton(tr("Choose another..."));
  for (QToolButton* b : {found_use_, found_other_}) b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  found_use_->setMinimumWidth(110);
  found->addWidget(found_text_, 1);
  found->addWidget(found_use_, 0, Qt::AlignVCenter);
  found->addWidget(found_other_, 0, Qt::AlignVCenter);
  found_->setVisible(false);
  cur->addWidget(found_);
  return section;
}

QWidget* InstallerWindow::BuildInstallSection() {
  QVBoxLayout* cur = nullptr;
  QFrame* section = MakeSection(&cur);
  auto* heading = new QLabel(tr("Install"));
  theme::StyleHeading(heading);
  cur->addWidget(heading);

  // A note: why an older install is being checked, what failed, what the shader
  // build did not manage.
  note_ = new QFrame;
  note_->setObjectName("note");
  auto* note = new QHBoxLayout(note_);
  note->setContentsMargins(12, 10, 12, 10);
  note->setSpacing(12);
  note_text_ = new QLabel;
  note_text_->setWordWrap(true);
  note_text_->setTextFormat(Qt::RichText);
  note_text_->setMinimumWidth(300);
  note_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  note_copy_ = new QToolButton;
  note_copy_->setText(tr("Copy details"));
  note_copy_->setToolTip(tr("Copies what happened, with the last lines of the log, to paste into a report."));
  note->addWidget(note_text_, 1);
  note->addWidget(note_copy_, 0, Qt::AlignTop);
  note_->setVisible(false);
  cur->addWidget(note_);
  connect(note_copy_, &QToolButton::clicked, this, [this] {
    QApplication::clipboard()->setText(DetailsText());
    SetStatus(tr("The details are copied to the clipboard."));
  });

  rows_host_ = new QWidget;
  auto* grid = new QGridLayout(rows_host_);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(2);
  grid->setColumnStretch(2, 1);
  cur->addWidget(rows_host_);

  // Where it goes: the game's own folder unless the player picks another (BO-197).
  auto* dest_row = new QHBoxLayout;
  dest_row->setSpacing(8);
  auto* dest_caption = new QLabel(tr("Installs to"));
  dest_caption->setObjectName("destCaption");
  Hint(dest_caption);
  dest_label_ = new ElidedLabel;
  change_ = new QToolButton;
  change_->setText(tr("Change..."));
  change_->setToolTip(tr("Install the game's data in another folder or on another drive. The game finds it there "
                         "at every start."));
  reset_ = new QToolButton;
  reset_->setText(tr("Default"));
  for (QToolButton* b : {change_, reset_}) {
    b->setMinimumHeight(g_control_height);
    b->setMinimumWidth(couch_ ? 130 : 96);
  }
  dest_row->addWidget(dest_caption);
  dest_row->addWidget(dest_label_, 1);
  dest_row->addWidget(change_);
  dest_row->addWidget(reset_);
  cur->addLayout(dest_row);
  space_label_ = new QLabel;
  space_label_->setWordWrap(true);
  space_label_->setTextFormat(Qt::RichText);
  space_label_->setMinimumWidth(300);
  Hint(space_label_);
  cur->addWidget(space_label_);

  // The shader pre-build: ticked by default (BO-93), the player can untick it; what it costs said plainly.
  shaders_box_ = new QCheckBox(tr("Prepare graphics for smooth first races (a few seconds, ~2 MB)"));
  shaders_box_->setMinimumHeight(couch_ ? 44 : 26);
  shaders_box_->setToolTip(tr("The game plays a short race and a crash event by itself, with no window and no sound, so "
                              "the graphics are ready before your first race."));
  cur->addWidget(shaders_box_);
  shaders_cost_ = new QLabel(tr("Untick it to skip this. The game works either way, but without it the first races can "
                                "stutter a little while the game prepares the graphics as you play."));
  shaders_cost_->setWordWrap(true);
  shaders_cost_->setMinimumWidth(300);
  shaders_cost_->setContentsMargins(28, 0, 0, 0);
  Hint(shaders_cost_);
  cur->addWidget(shaders_cost_);

  if (linux_ui_ && SteamStepAvailable()) {
    steam_box_ = new QCheckBox(tr("Add Takedawn to my Steam library"));
    steam_box_->setMinimumHeight(couch_ ? 44 : 26);
    steam_box_->setToolTip(tr("Adds a non-Steam shortcut so the game starts from Steam, in Game Mode on a Steam Deck too. "
                              "If Steam is open, you are asked to let it close for a moment."));
    cur->addWidget(steam_box_);
  }
  return section;
}

QWidget* InstallerWindow::BuildDetails() {
  auto* host = new QWidget;
  auto* layout = new QVBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  details_toggle_ = new QToolButton;
  details_toggle_->setObjectName("fold");
  details_toggle_->setText(tr("Show details"));
  details_toggle_->setCheckable(true);
  details_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  details_toggle_->setArrowType(Qt::RightArrow);
  if (couch_) details_toggle_->setMinimumHeight(44);
  layout->addWidget(details_toggle_, 0, Qt::AlignLeft);
  log_ = new QPlainTextEdit;
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(4000);
  log_->setLineWrapMode(QPlainTextEdit::WidgetWidth);  // a long line wraps; it never opens scrolled to the right
  log_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  log_->setMinimumHeight(110);
  log_->setVisible(false);
  layout->addWidget(log_, 1);
  connect(details_toggle_, &QToolButton::toggled, this, [this, host](bool open) {
    log_->setVisible(open);
    if (auto* box = qobject_cast<QVBoxLayout*>(host->parentWidget()->layout())) box->setStretchFactor(host, open ? 2 : 0);
    details_toggle_->setText(open ? tr("Hide details") : tr("Show details"));
    details_toggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
  });
  return host;
}

// Where the licence texts are: in the app's Resources (macOS), beside the program,
// or, in a build inside the source tree, in tools/install/gui/licenses.
QString InstallerWindow::LicenseDir() const {
  const QDir here(QCoreApplication::applicationDirPath());
  QStringList candidates{here.filePath("../Resources/licenses"), here.filePath("licenses"),
                         here.filePath("../share/takedawn/licenses"),
                         here.filePath("../share/licenses/takedawn")};  // (the last: the AppImage's)
  QDir up(here);
  for (int i = 0; i < 7; ++i) {
    candidates << up.filePath("licenses");
    if (!up.cdUp()) break;
  }
  for (const QString& dir : candidates) {
    if (QFileInfo::exists(QDir(dir).filePath("Qt-NOTICE.txt"))) return QDir(dir).absolutePath();
  }
  return QString();
}

void InstallerWindow::ShowAbout() {
  if (!about_dialog_) {
    about_dialog_ = new QDialog(this);
    about_dialog_->setWindowTitle(tr("About Takedawn"));
    about_dialog_->resize(620, 560);
    auto* layout = new QVBoxLayout(about_dialog_);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    auto* text = new QLabel(
        tr("<p><b>%1</b><br>An unofficial port of Burnout 3: Takedown (Xbox, 2004) for today's computers.</p>"
           "<p><b>A fan project.</b> It is not made, endorsed or supported by Electronic Arts or Criterion Games and "
           "has no connection with them. Burnout is a trademark of its owners.</p>"
           "<p><b>Bring your own disc.</b> Takedawn is the program only. The game's data is not part of it and is not "
           "downloaded: this window copies it from your own " TAKEDAWN_DISC_LONG " retail disc, as an image (.iso) or "
           "as extracted files, checks every file, and keeps it on this computer. It needs about 3 GB free.</p>")
            .arg((version_line_.isEmpty() ? tr("Takedawn %1").arg(TAKEDAWN_SETUP_VERSION) : version_line_).toHtmlEscaped()));
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    layout->addWidget(text);
    const QString dir = LicenseDir();
    auto* lic_label = new QLabel(tr("Licences"));
    theme::StyleHeading(lic_label);
    layout->addWidget(lic_label);
    auto* lic_note = new QLabel(
        dir.isEmpty() ? tr("The licence texts are part of the packaged program; this build has none beside it.")
                      : tr("Takedawn and the recompiler toolkit are MIT licensed. The toolkit carries parts under the "
                           "LGPL 2.1. This window is built with Qt %1, linked dynamically, under the LGPL 3: see "
                           "Qt-NOTICE for where its source is and how to swap the libraries.")
                            .arg(QString::fromLatin1(qVersion())));
    lic_note->setWordWrap(true);
    Hint(lic_note);
    layout->addWidget(lic_note);
    if (!dir.isEmpty()) {
      static const struct { const char* file; const char* title; } kTitles[] = {
          {"takedawn-LICENSE.txt", "Takedawn (MIT)"},
          {"xboxrecomp-LICENSE.txt", "The recompiler toolkit (MIT)"},
          {"xboxrecomp-NOTICE.txt", "Toolkit: third-party notices"},
          {"xboxrecomp-LGPL-2.1.txt", "LGPL 2.1 (parts of the toolkit)"},
          {"THIRD_PARTY.txt", "Everything else inside the program"},
          {"Qt-NOTICE.txt", "Qt: how it is used and where its source is"},
          {"Qt-LGPL-3.0.txt", "Qt: GNU LGPL 3"},
          {"Qt-GPL-3.0.txt", "Qt: GNU GPL 3 (the LGPL refers to it)"},
          {"Qt-THIRD-PARTY.txt", "Qt: licences of the other projects' code inside it"},
      };
      auto* pick = new QComboBox;
      pick->setMinimumHeight(34);
      for (const auto& t : kTitles) {
        if (QFileInfo::exists(QDir(dir).filePath(t.file))) {
          pick->addItem(QCoreApplication::translate("Licences", t.title), QDir(dir).filePath(t.file));
        }
      }
      auto* viewer = new QPlainTextEdit;
      viewer->setReadOnly(true);
      viewer->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
      layout->addWidget(pick);
      layout->addWidget(viewer, 1);
      const auto show = [pick, viewer] {
        QFile f(pick->currentData().toString());
        viewer->setPlainText(f.open(QIODevice::ReadOnly)
                                 ? QString::fromUtf8(f.readAll()).replace("@QT_VERSION@", QString::fromLatin1(qVersion()))
                                 : QString());
      };
      connect(pick, &QComboBox::currentIndexChanged, about_dialog_, show);
      show();
    } else {
      layout->addStretch(1);
    }
    auto* close = new QPushButton(tr("Close"));
    close->setMinimumHeight(g_control_height);
    close->setMinimumWidth(140);
    connect(close, &QPushButton::clicked, about_dialog_, &QDialog::accept);
    connect(about_dialog_, &QDialog::finished, this, [this] {
      RefreshHints();
      SettleFocus();
    });
    close->setFocus();
    layout->addWidget(close, 0, Qt::AlignRight);
  }
  if (config_.test) about_dialog_->setAttribute(Qt::WA_DontShowOnScreen);
  if (couch_) {
    // Over the whole window, as everything under gamescope is.
    about_dialog_->setWindowFlag(Qt::FramelessWindowHint);
    about_dialog_->setGeometry(geometry());
  }
  about_dialog_->show();
  RefreshHints();
}

// ------------------------------------------------- the game and its data

// The game's executable: named (the game names itself when it opens this window),
// else beside this program, which is where a release has both
// (Takedawn.app/Contents/MacOS on macOS), else bin/ of the source tree this was
// built in.
bool InstallerWindow::LocateProgram() {
  if (!program_.isEmpty()) return true;
  for (const QString& named : {config_.program, qEnvironmentVariable("TAKEDAWN_PROGRAM")}) {
    if (!named.isEmpty()) {
      if (QFileInfo(named).isFile()) program_ = QFileInfo(named).absoluteFilePath();
      return !program_.isEmpty();
    }
  }
#ifdef Q_OS_WIN
  const QStringList names{"Takedawn.exe", "takedawn.exe", "burnout3.exe"};
#else
  const QStringList names{"Takedawn", "takedawn", "burnout3"};
#endif
  const QDir here(QCoreApplication::applicationDirPath());
  for (const QString& name : names) {
    const QFileInfo info(here.filePath(name));
    if (info.isFile() && info.isExecutable()) {
      program_ = info.absoluteFilePath();
      return true;
    }
  }
  QDir up(here);
  for (int i = 0; i < 6 && up.cdUp(); ++i) {
    for (const QString& name : names) {
      const QFileInfo info(up.filePath("bin/" + name));
      if (info.isFile() && info.isExecutable() && up.exists("src/game/install.c")) {
        program_ = info.absoluteFilePath();
        return true;
      }
    }
  }
  return false;
}

QStringList InstallerWindow::DataArgs() const {
  return config_.dest.isEmpty() ? QStringList() : QStringList{"--data", config_.dest};
}

QStringList InstallerWindow::StatusArgs() const {
  if (!config_.dest.isEmpty() || target_.isEmpty()) return DataArgs();
  return QStringList{"--data", target_};
}

void InstallerWindow::Start() {
  if (started_) return;
  started_ = true;
  if (!LocateProgram()) {
    Break("program-missing", tr("Looked beside %1.").arg(QCoreApplication::applicationFilePath()));
    return;
  }
  QueryStatus();
}

void InstallerWindow::QueryStatus() {
  SetPhase(Phase::Asking);
  status_code_.clear();
  status_text_.clear();
  auto* proc = new DriverProcess(this);
  status_proc_ = proc;
  auto ok = std::make_shared<bool>(false);
  connect(proc, &DriverProcess::EventReceived, this, [this, ok](const Event& e) {
    if (e.kind == Event::Kind::Log && e.step == "data") {
      const QString key = e.text.section(' ', 0, 0), value = e.text.section(' ', 1);
      if (key == "root") data_root_ = QDir::cleanPath(value);
      else if (key == "place" && target_.isEmpty()) data_place_ = value;  // where the game looks by itself
      else if (key == "default") default_root_ = QDir::cleanPath(value);
      else if (key == "disc") disc_name_ = value;
      else if (key == "files") data_files_ = value.toInt();
      else if (key == "bytes") data_bytes_ = value.toLongLong();
      else if (key == "free-needed") free_needed_ = value.toLongLong();
      else if (key == "version") version_line_ = value;
    } else if (e.kind == Event::Kind::Fail && e.step == "data") {
      status_code_ = e.code;
      status_text_ = e.text;
    } else if (e.kind == Event::Kind::Ok && e.step == "data") {
      *ok = true;
      status_text_ = e.text;
    }
  });
  connect(proc, &DriverProcess::Finished, this, [this, proc, ok](const DriverProcess::Result& result) {
    proc->deleteLater();
    if (status_proc_ == proc) status_proc_ = nullptr;
    if (ProgramFailed(result, true)) return;
    if (!*ok && status_code_.isEmpty() && result.started) status_code_ = "not-installed";  // a program without the query
    OnStatus(result);
  });
  proc->Start(program_, QStringList{"--data-status"} + StatusArgs(), ChildEnvironment(), QString());
}

void InstallerWindow::OnStatus(const DriverProcess::Result& result) {
  if (!result.started) {
    Break("program-start", result.start_error);
    return;
  }
  if (data_root_.isEmpty() && !config_.dest.isEmpty()) data_root_ = QDir::cleanPath(config_.dest);
  dest_label_->SetFullText(data_root_.isEmpty() ? tr("the game's own data folder") : QDir::toNativeSeparators(data_root_));
  dest_label_->setToolTip(QDir::toNativeSeparators(data_root_));
  RefreshSpace();
  AppendLog("$ " + program_ + " --data-status " + StatusArgs().join(' '));
  AppendLog(status_code_.isEmpty() ? "data: " + status_text_ : "data: " + status_code_ + ": " + status_text_);
  RefreshDestControls();

  if (status_code_.isEmpty() && !target_.isEmpty() && !DestFixed()) {
    // The chosen folder holds the game's data already (moved there by hand, or an
    // earlier install): checked where it lies and kept as the game's folder, without a disc.
    verify_ = true;
    source_ = data_root_ + "/" + kGameDir;
    SetNote(NoteTone::Info, tr("The game's data is already in this folder."),
            tr("Takedawn checks it where it is and uses it from now on. Nothing is copied and no disc is needed."),
            false);
    SetDiscState(DiscState::Empty, QString(), QString(), QString());
    StartInstall();
    return;
  }
  if (status_code_.isEmpty()) {
    // Whole already: nothing to do but play.
    BuildRows({"disc"});
    if (StepRow* row = Row("disc")) {
      row->status = RowStatus::Skipped;
      row->fraction = 1.0;
      row->detail = tr("%n file(s), checked against the disc's list", nullptr, data_files_);
      UpdateRow(*row);
    }
    SetDiscState(DiscState::Valid, tr("The game's data is installed."), tr("No disc is needed."), QString());
    Succeed();
    return;
  }
  if (status_code_ == "unverified" && !data_root_.isEmpty()) {
    // An install from before this program kept its marker (the source installer's,
    // a copy made by hand): checked where it lies, at once, without a disc.
    verify_ = true;
    source_ = data_root_ + "/" + kGameDir;
    SetNote(NoteTone::Info, tr("Game files from an earlier install were found."),
            tr("Takedawn is checking them where they are, file by file, against the disc's list. Nothing is copied "
               "and no disc is needed; it takes under a minute."), false);
    SetDiscState(DiscState::Empty, QString(), QString(), QString());
    StartInstall();
    return;
  }
  if (status_code_ == "not-installed" && data_place_ == "saved" && target_.isEmpty() && !data_root_.isEmpty() &&
      !QFileInfo(data_root_).isDir()) {
    // The folder chosen before is gone: a drive not connected, or the data moved by hand.
    SetNote(NoteTone::Warning, tr("The folder chosen for the game's data is not there."),
            tr("If it is on a drive that is not connected, connect it and start Takedawn again. If you moved the "
               "data, press Change... and pick its new folder. Or install it again here."),
            false);
  }
  SetPhase(Phase::Idle);
  SetStatus(tr("Choose your disc image or folder to begin."));
  if (!config_.disc.isEmpty() && disc_path_.isEmpty()) SetDisc(config_.disc);
  else if (!disc_path_.isEmpty()) CheckDisc();
  else StartDetect();
}

qint64 InstallerWindow::CopiedBytes() const {
  if (data_root_.isEmpty()) return 0;
  qint64 bytes = 0;
  QDirIterator it(data_root_ + "/" + kGameDir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (!it.fileName().endsWith(".part")) bytes += it.fileInfo().size();
  }
  return bytes;
}

qint64 InstallerWindow::FreeBytes() const {
  const QStorageInfo storage(NearestExisting(data_root_.isEmpty() ? QDir::homePath() : data_root_));
  return storage.isValid() ? storage.bytesAvailable() : -1;
}

qint64 InstallerWindow::StillNeededFree() const {
  const qint64 wanted = free_needed_ > 0 ? free_needed_ : 3000000000ll;
  return std::max<qint64>(0, wanted - CopiedBytes());  // (as the installer counts: what is there is not copied again)
}

void InstallerWindow::RefreshSpace() {
  const qint64 free_bytes = FreeBytes();
  const QString size = data_bytes_ > 0 ? Gb(data_bytes_) : QString("2.5");
  const qint64 wanted = StillNeededFree();
  low_space_ = free_bytes >= 0 && free_bytes < wanted && phase_ != Phase::Done;
  if (free_bytes < 0) {
    space_label_->setText(tr("The game's data takes %1 GB.").arg(size));
  } else if (low_space_) {
    space_label_->setText(Span(theme::kCaution, tr("Takedawn needs %1 GB free (the game uses %2 GB). You have %3 GB. "
                                                    "Free up space on this disk.")
                                                     .arg(Gb(wanted), size, Gb(free_bytes))));
  } else {
    space_label_->setText(tr("%1 GB free on this disk. The game's data takes %2 GB.").arg(Gb(free_bytes), size).toHtmlEscaped());
  }
  // Before the copy starts there is no point in pressing Install; once it has failed, Resume stays.
  if (phase_ == Phase::Idle) primary_->setEnabled(disc_state_ == DiscState::Valid && !low_space_);
}

// ------------------------------------------------- where the data goes

namespace {

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
const Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
const Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

bool SamePath(const QString& a, const QString& b) {
  return QDir::cleanPath(a).compare(QDir::cleanPath(b), kPathCase) == 0;
}

// `path` is `dir` or a folder inside it.
bool IsInside(const QString& path, const QString& dir) {
  const QString p = QDir::cleanPath(path), d = QDir::cleanPath(dir);
  if (d.isEmpty()) return false;
  return p.compare(d, kPathCase) == 0 || p.startsWith(d.endsWith('/') ? d : d + "/", kPathCase);
}

enum class DriveKind { Local, Network, Removable };

// What kind of drive a folder (or the nearest one that exists) is on.
DriveKind KindOfDrive(const QString& folder) {
  const QString nearest = NearestExisting(folder);
#ifdef Q_OS_WIN
  if (nearest.startsWith("//") || nearest.startsWith("\\\\")) return DriveKind::Network;
  const QString root = QStorageInfo(nearest).rootPath();
  const std::wstring w = QDir::toNativeSeparators(root.endsWith('/') ? root : root + "/").toStdWString();
  switch (GetDriveTypeW(w.c_str())) {
    case DRIVE_REMOTE: return DriveKind::Network;
    case DRIVE_REMOVABLE: return DriveKind::Removable;
    default: return DriveKind::Local;
  }
#else
  const QStorageInfo info(nearest);
  const QString fs = QString::fromLatin1(info.fileSystemType()).toLower();
  static const QStringList kNetwork{"nfs", "nfs4", "smbfs", "cifs", "smb3", "afpfs", "webdav", "sshfs",
                                    "fuse.sshfs", "9p", "davfs", "fuse.rclone"};
  if (kNetwork.contains(fs)) return DriveKind::Network;
  const QString root = info.rootPath();
#ifdef Q_OS_MACOS
  if (root.startsWith("/Volumes/")) return DriveKind::Removable;
#else
  if (root.startsWith("/media/") || root.startsWith("/run/media/") || root.startsWith("/mnt/")) return DriveKind::Removable;
#endif
  return DriveKind::Local;
#endif
}

}  // namespace

bool InstallerWindow::DestFixed() const { return !config_.dest.isEmpty() || data_place_ == "checkout"; }

// Change... and Default: there unless the root was named for the game (--data,
// TAKEDAWN_DATA: a choice here would not reach it), usable whenever nothing runs.
void InstallerWindow::RefreshDestControls() {
  if (!change_) return;
  const bool fixed = DestFixed();
  const bool can = !fixed && !program_.isEmpty() &&
                   (phase_ == Phase::Idle || phase_ == Phase::Paused || phase_ == Phase::Failed || phase_ == Phase::Done);
  change_->setVisible(!fixed);
  change_->setEnabled(can);
  reset_->setVisible(!fixed && !default_root_.isEmpty() && !data_root_.isEmpty() && !SamePath(data_root_, default_root_));
  reset_->setEnabled(can);
  reset_->setToolTip(tr("Back to the default folder, %1.").arg(QDir::toNativeSeparators(default_root_)));
  if (!config_.dest.isEmpty()) dest_label_->setToolTip(tr("%1 (set for the game with --data or TAKEDAWN_DATA)")
                                                           .arg(QDir::toNativeSeparators(data_root_)));
}

void InstallerWindow::ChangeDest() {
  if (DestFixed() || phase_ == Phase::Running || phase_ == Phase::Asking || phase_ == Phase::Broken) return;
  if (use_browser_) OpenDestBrowser();
  else NativeDestDialog();
}

// The built-in list (Linux, a controller): the same places as for the disc, the
// cards and drives among them.
void InstallerWindow::OpenDestBrowser() {
  const QString home = config_.home.isEmpty() ? QDir::homePath() : config_.home;
  const QStringList media = config_.home.isEmpty() ? discs::SystemMediaRoots() : config_.media_roots;
  browser_->SetPlaces(config_.test && config_.home.isEmpty() ? QList<discs::Place>() : discs::Places(home, media));
  browse_dest_ = true;
  browser_->SetFolderMode(true);
  stack_->setCurrentWidget(browser_);
  browser_->Open();
  RefreshHints();
}

void InstallerWindow::NativeDestDialog() {
  const QString start = data_root_.isEmpty() ? QDir::homePath() : NearestExisting(data_root_);
  // Window modal: on macOS a sheet on this window, so it opens on the window's display.
  QFileDialog dialog(this, tr("Choose where to install the game's data"), start);
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly);
  dialog.setWindowModality(Qt::WindowModal);
  const QString picked = dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()
                             ? dialog.selectedFiles().first() : QString();
  if (stack_->currentWidget() == browser_) CloseBrowser();
  if (!picked.isEmpty()) SetDest(picked);
}

// The folder the data goes in: the one picked when it is empty, is (or is in) an
// install, or is called Takedawn; else a "Takedawn" folder inside it, so the files
// do not end up loose among others.
QString InstallerWindow::DestFolderFor(const QString& picked_in) const {
  QString picked = QDir::cleanPath(QDir(picked_in).absolutePath());
  const QFileInfo info(picked);
  if (info.fileName() == kGameDir && QFileInfo::exists(info.absolutePath() + "/.complete")) {
    return info.absolutePath();  // the game's own folder inside an install: its root
  }
  if (!info.isDir()) return picked;
  const QDir dir(picked);
  if (dir.exists(kGameDir) || dir.exists(".complete") || dir.exists("install.journal")) return picked;
  if (dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) return picked;
  if (info.fileName().compare("Takedawn", Qt::CaseInsensitive) == 0) return picked;
  return dir.filePath("Takedawn");
}

QString InstallerWindow::CheckDest(const QString& folder, QString* warning) const {
  warning->clear();
  if (folder.isEmpty() || QDir::isRelativePath(folder)) return tr("Choose a whole folder path.");
  const QFileInfo info(folder);
  if (info.exists() && !info.isDir()) return tr("%1 is a file, not a folder.").arg(QDir::toNativeSeparators(folder));
  // The program's own folder is replaced by an update (and a macOS app or an
  // AppImage cannot be written at all).
  QStringList program_dirs{QCoreApplication::applicationDirPath()};
  if (!program_.isEmpty()) program_dirs << QFileInfo(program_).absolutePath();
  const QString appdir = qEnvironmentVariable("APPDIR");  // an AppImage's mounted files
  if (!appdir.isEmpty()) program_dirs << appdir;
  for (const QString& d : QStringList(program_dirs)) {
    const int app = d.indexOf(".app/Contents", 0, Qt::CaseInsensitive);
    if (app > 0) program_dirs << d.left(app + 4);  // the whole bundle
#ifdef Q_OS_WIN
    program_dirs << d + ".prev";  // the setup keeps the version before in <folder>.prev
#endif
  }
  for (const QString& d : program_dirs) {
    if (IsInside(folder, d)) {
      return tr("That folder is inside Takedawn's program folder, which an update replaces. Choose a folder "
                "outside it.");
    }
  }
  // Writable: a folder made and removed where the data's folder would be made.
  {
    QTemporaryDir probe(NearestExisting(folder) + "/.takedawn-check-XXXXXX");
    if (!probe.isValid()) {
      return tr("Takedawn cannot write in %1. Choose a folder you can write to.")
          .arg(QDir::toNativeSeparators(NearestExisting(folder)));
    }
  }
  switch (KindOfDrive(folder)) {
    case DriveKind::Network:
      *warning = tr("This folder is on a network drive. The game reads its data while you play, so a slow or "
                    "lost connection makes it stutter or stop.");
      break;
    case DriveKind::Removable:
      *warning = tr("This folder is on a removable or external drive. It has to be connected every time you "
                    "play.");
      break;
    case DriveKind::Local: break;
  }
  return QString();
}

bool InstallerWindow::SetDest(const QString& picked, bool exact) {
  if (DestFixed() || phase_ == Phase::Running || phase_ == Phase::Asking) return false;
  const QString folder = exact ? QDir::cleanPath(picked) : DestFolderFor(picked);
  QString warning;
  const QString why = CheckDest(folder, &warning);
  AppendLog("destination: " + QDir::toNativeSeparators(folder) + (why.isEmpty() ? QString() : " refused: " + why) +
            (warning.isEmpty() ? QString() : " warning: " + warning));
  if (!why.isEmpty()) {
    SetNote(NoteTone::Warning, tr("The game's data cannot go there."), why, false);
    SetStatus(tr("Choose another folder."));
    return false;
  }
  if (!warning.isEmpty() && !exact && !ask_use_anyway_(warning)) return false;
  target_ = folder;
  // A fresh look at that folder: whatever the run before had found is not about it.
  if (verify_) {
    verify_ = false;
    source_ = disc_state_ == DiscState::Valid ? disc_valid_for_ : QString();
  }
  ClearNote();
  play_->setVisible(false);
  progress_->setValue(0);
  clock_label_->clear();
  BuildRows({"disc"});
  if (disc_path_.isEmpty()) SetDiscState(DiscState::Empty, QString(), QString(), QString());
  QueryStatus();
  return true;
}

// -------------------------------------------------------------- the disc

void InstallerWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls() && image_browse_->isEnabled()) {
    event->acceptProposedAction();
    theme::SetProperty(disc_card_, "tone", "drop");
  }
}

void InstallerWindow::dragLeaveEvent(QDragLeaveEvent*) {
  SetDiscState(disc_state_, property("discHeadline").toString(), property("discAdvice").toString(),
               disc_status_->toolTip());
}

void InstallerWindow::dropEvent(QDropEvent* event) {
  const auto urls = event->mimeData()->urls();
  if (!urls.isEmpty() && image_browse_->isEnabled()) {
    SetDisc(urls.first().toLocalFile());
    event->acceptProposedAction();
  } else {
    dragLeaveEvent(nullptr);
  }
}

void InstallerWindow::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (!started_) QTimer::singleShot(0, this, &InstallerWindow::Start);
}

// Back from freeing some space: look again.
void InstallerWindow::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::ActivationChange && isActiveWindow() && space_label_ && !data_root_.isEmpty()) {
    const bool was_low = low_space_;
    RefreshSpace();
    if (was_low != low_space_ && disc_state_ == DiscState::Valid && phase_ == Phase::Idle) {
      SetStatus(low_space_ ? tr("Not enough free space on this disk.") : tr("Press Install to copy the game's data."));
    }
  }
}

void InstallerWindow::closeEvent(QCloseEvent* event) {
  if (phase_ == Phase::Running) {
    if (!config_.test) {
      const auto answer = QMessageBox::question(
          this, windowTitle(),
          tr("The installation is running. Stop it and quit?\n\nWhat is already copied is kept, and the next start "
             "goes on from there."));
      if (answer != QMessageBox::Yes) {
        event->ignore();
        return;
      }
    }
    if (driver_) {
      // Stop the installer and wait for it, so nothing is left running.
      QEventLoop wait;
      connect(driver_, &DriverProcess::Finished, &wait, &QEventLoop::quit);
      QTimer::singleShot(6000, &wait, &QEventLoop::quit);
      driver_->Cancel();
      wait.exec();
    }
  }
  event->accept();
}

// The built-in list of folders and discs, in the window itself.
void InstallerWindow::OpenBrowser() {
  if (phase_ == Phase::Running) return;
  const QString home = config_.home.isEmpty() ? QDir::homePath() : config_.home;
  const QStringList media = config_.home.isEmpty() ? discs::SystemMediaRoots() : config_.media_roots;
  browser_->SetPlaces(config_.test && config_.home.isEmpty() ? QList<discs::Place>() : discs::Places(home, media));
  browse_dest_ = false;
  browser_->SetFolderMode(false);
  stack_->setCurrentWidget(browser_);
  browser_->Open(disc_path_.isEmpty() ? QString() : QFileInfo(disc_path_).absolutePath());
  RefreshHints();
}

void InstallerWindow::CloseBrowser() {
  browse_dest_ = false;
  browser_->SetFolderMode(false);
  stack_->setCurrentWidget(main_page_);
  RefreshHints();
  SettleFocus();
}

void InstallerWindow::NativeImageDialog() {
  const QString start = browser_->CurrentFolder().isEmpty() ? QDir::homePath() : browser_->CurrentFolder();
  const QString path = QFileDialog::getOpenFileName(this, tr("Choose your Burnout 3: Takedown (" TAKEDAWN_DISC_SHORT ") disc image"), start,
                                                    tr("Disc images (*.iso *.xiso);;All files (*)"));
  if (path.isEmpty()) return;
  CloseBrowser();
  SetDisc(path);
}

// At the start, when nothing is installed and no disc was named: a look into
// the likely places (the browser's own starting points and a few levels under
// them, never the whole disk) for what could be the disc. The game checks each
// find, as it checks a chosen one, and the first it accepts is offered.
void InstallerWindow::StartDetect() {
  if (!use_browser_ || (config_.test && config_.home.isEmpty())) return;
  if (qEnvironmentVariableIntValue("TAKEDAWN_SETUP_NO_SEARCH")) return;  // the player chooses, nothing is looked for
  const QString home = config_.home.isEmpty() ? QDir::homePath() : config_.home;
  const QStringList media = config_.home.isEmpty() ? discs::SystemMediaRoots() : config_.media_roots;
  detect_queue_ = discs::Candidates(discs::Places(home, media)).mid(0, 4);
  AppendLog(QString("search: %1 possible disc(s) in the usual places").arg(detect_queue_.size()));
  if (qEnvironmentVariableIntValue("TAKEDAWN_SETUP_TRACE")) {
    QTextStream(stderr) << "[SETUP] search: " << detect_queue_.join(" | ") << Qt::endl;
  }
  if (detect_queue_.isEmpty()) return;
  SetStatus(tr("Looking for your disc..."));
  DetectNext();
}

void InstallerWindow::DetectNext() {
  if (!disc_path_.isEmpty() || phase_ != Phase::Idle) return;  // the player chose meanwhile
  if (detect_queue_.isEmpty()) {
    SetStatus(tr("Choose your disc image or folder to begin."));
    return;
  }
  const QString path = detect_queue_.takeFirst();
  auto* proc = new DriverProcess(this);
  detect_proc_ = proc;
  auto ok = std::make_shared<bool>(false);
  connect(proc, &DriverProcess::EventReceived, this, [ok](const Event& e) {
    if (e.kind == Event::Kind::Ok && e.step == "disc") *ok = true;
  });
  connect(proc, &DriverProcess::Finished, this, [this, proc, ok, path](const DriverProcess::Result& result) {
    proc->deleteLater();
    if (detect_proc_ != proc) return;
    detect_proc_ = nullptr;
    // The program itself failing is not "not the disc": say so, once, and stop looking.
    if (!*ok && ProgramFailed(result, false)) {
      detect_queue_.clear();
      return;
    }
    AppendLog(QString("search: %1: %2").arg(path, *ok ? "the disc" : "not the disc"));
    if (!disc_path_.isEmpty() || phase_ != Phase::Idle) return;
    if (*ok) ShowFound(path);
    else DetectNext();
  });
  proc->Start(program_, QStringList{"--install", QFileInfo(path).absoluteFilePath(), "--check"}, ChildEnvironment(),
              QString());
}

void InstallerWindow::ShowFound(const QString& path) {
  found_path_ = path;
  found_text_->setText(Span(theme::kLit, tr("Found Burnout 3: Takedown"), true) + "<br>" +
                       Span(theme::kEntry, QDir::toNativeSeparators(path)) + "<br>" +
                       Span(theme::kDim, tr("Use it?")));
  found_->setVisible(true);
  SetStatus(tr("Your disc was found. Use it, or choose another."));
  SettleFocus();
}

void InstallerWindow::HideFound() {
  found_path_.clear();
  found_->setVisible(false);
}

void InstallerWindow::BrowseImage() {
  if (use_browser_) {
    OpenBrowser();
    return;
  }
  const QString start = disc_path_.isEmpty() ? QDir::homePath() : QFileInfo(disc_path_).absolutePath();
  const QString path = QFileDialog::getOpenFileName(this, tr("Choose your Burnout 3: Takedown (" TAKEDAWN_DISC_SHORT ") disc image"), start,
                                                    tr("Disc images (*.iso *.xiso);;All files (*)"));
  if (!path.isEmpty()) SetDisc(path);
}

void InstallerWindow::BrowseFolder() {
  if (use_browser_) {
    OpenBrowser();
    return;
  }
  const QString start = disc_path_.isEmpty() ? QDir::homePath() : QFileInfo(disc_path_).absolutePath();
  const QString path =
      QFileDialog::getExistingDirectory(this, tr("Choose the folder your disc's files were extracted to"), start);
  if (!path.isEmpty()) SetDisc(path);
}

void InstallerWindow::SetDisc(const QString& path) {
  if (phase_ == Phase::Running) return;
  disc_path_ = path.trimmed();
  HideFound();
  detect_queue_.clear();
  if (phase_ == Phase::Asking || phase_ == Phase::Broken) return;  // checked once the game has answered
  CheckDisc();
}

void InstallerWindow::CheckDisc() {
  ++disc_serial_;  // an answer about an older choice is no longer wanted
  if (disc_proc_) {
    disc_proc_->disconnect(this);
    disc_proc_->Cancel();
    disc_proc_->deleteLater();
    disc_proc_ = nullptr;
  }
  if (disc_path_.isEmpty()) {
    SetDiscState(DiscState::Empty, QString(), QString(), QString());
    return;
  }
  // Another disc: what an earlier run said is about the old one.
  if (phase_ == Phase::Failed || phase_ == Phase::Paused || phase_ == Phase::Done) {
    ClearNote();
    BuildRows({"disc"});
    progress_->setValue(0);
    run_millis_ = 0;
    clock_label_->clear();
  }
  disc_valid_for_.clear();
  SetDiscState(DiscState::Checking, tr("Checking the disc..."), QString(), QString());
  SetPhase(Phase::Idle);
  SetStatus(tr("Checking the disc..."));
  const int serial = disc_serial_;
  const QString path = disc_path_;
  auto* proc = new DriverProcess(this);
  disc_proc_ = proc;
  struct Answer { QString ok, code, text; };
  auto answer = std::make_shared<Answer>();
  connect(proc, &DriverProcess::EventReceived, this, [answer](const Event& e) {
    if (e.kind == Event::Kind::Ok && e.step == "disc") {
      answer->ok = e.text;
    } else if (e.kind == Event::Kind::Fail && e.step == "disc") {
      answer->code = e.code;
      answer->text = e.text;
    } else if (e.kind == Event::Kind::ResultFail && answer->code.isEmpty()) {
      answer->code = e.code;
    }
  });
  connect(proc, &DriverProcess::Finished, this, [this, proc, serial, answer, path](const DriverProcess::Result& result) {
    proc->deleteLater();
    if (disc_proc_ == proc) disc_proc_ = nullptr;
    if (serial != disc_serial_) return;
    AppendLog("$ " + program_ + " --install " + path + " --check");
    if (!result.started) {
      const auto why = messages::ExplainProgram("program-start");
      SetDiscState(DiscState::Invalid, why.headline, why.advice, result.start_error);
      AppendLog("check: " + result.start_error, true);
    } else if (!answer->ok.isEmpty()) {
      disc_valid_for_ = path;
      verify_ = false;
      source_ = path;
      SetDiscState(DiscState::Valid, tr("Burnout 3: Takedown found."),
                   tr("Looks like the right disc. Every file is checked while copying."), answer->ok);
      AppendLog("check: " + answer->ok);
      RefreshSpace();
      SetStatus(low_space_ ? tr("Not enough free space on this disk.") : tr("Press Install to copy the game's data."));
    } else if (!answer->code.isEmpty()) {
      messages::InstallFacts facts;
      facts.source = path;
      const auto why = messages::ExplainInstall(answer->code, facts);
      SetDiscState(DiscState::Invalid, why.headline, why.advice, answer->text);
      AppendLog("check: " + answer->code + ": " + answer->text, true);
      SetStatus(tr("This disc cannot be used. Choose another."));
    } else if (ProgramFailed(result, false)) {
      SetDiscState(DiscState::Empty, QString(), QString(), QString());
      return;
    } else {
      const auto why = messages::ExplainProgram(result.crashed ? "program-crashed" : "program-no-result");
      SetDiscState(DiscState::Invalid, why.headline, why.advice, result.stderr_tail.join('\n'));
      AppendLog("check: no answer (status " + QString::number(result.exit_code) + ")", true);
    }
    SetPhase(Phase::Idle);
    SettleFocus();
  });
  proc->Start(program_, QStringList{"--install", QFileInfo(path).absoluteFilePath(), "--check"}, ChildEnvironment(),
              QString());
}

void InstallerWindow::SetDiscState(DiscState state, const QString& headline, const QString& advice,
                                   const QString& detail) {
  disc_state_ = state;
  setProperty("discHeadline", headline);
  setProperty("discAdvice", advice);
  QString html;
  const auto with_advice = [&](const QColor& color) {
    return Span(color, headline, true) + (advice.isEmpty() ? QString() : "<br>" + advice.toHtmlEscaped());
  };
  switch (state) {
    case DiscState::Empty:
      if (verify_) {
        html = Span(theme::kLit, tr("No disc is needed right now."), true) + "<br>" +
               Span(theme::kDim, tr("If the check finds a damaged file, your disc image or folder repairs it."));
      } else {
        html = Span(theme::kLit, couch_ ? tr("Choose your Burnout 3: Takedown (" TAKEDAWN_DISC_SHORT ") disc")
                                        : tr("Drop your Burnout 3: Takedown (" TAKEDAWN_DISC_SHORT ") disc image here"), true) + "<br>" +
               Span(theme::kDim, tr("The .iso of your own Xbox disc, or the folder its files were extracted to. "
                                    "It is only read."));
      }
      break;
    case DiscState::Checking: html = Span(theme::kEntry, headline, true); break;
    case DiscState::Valid: html = with_advice(theme::kLit); break;
    case DiscState::Invalid: html = with_advice(theme::kWarnText); break;
  }
  using S = StepGlyph::State;
  disc_glyph_->SetState(state == DiscState::Valid ? S::Done : state == DiscState::Invalid ? S::Failed
                        : state == DiscState::Checking ? S::Running : S::Pending);
  theme::SetProperty(disc_card_, "tone", state == DiscState::Valid ? "valid" : state == DiscState::Invalid ? "invalid"
                                         : state == DiscState::Checking ? "checking" : "empty");
  disc_status_->setText(html);
  disc_status_->setToolTip(detail);
  if (scroll_ && (state == DiscState::Valid || state == DiscState::Invalid)) {
    // On a short window the page scrolls (a controller moving to the options, the
    // focus going elsewhere): the answer about the disc is at its top and must be seen.
    QTimer::singleShot(0, this, [this] { scroll_->verticalScrollBar()->setValue(0); });
  }
  const bool show_file = !disc_path_.isEmpty() && state != DiscState::Empty;
  disc_file_->setVisible(show_file);
  if (show_file) {
    disc_file_->SetFullText(QDir::toNativeSeparators(disc_path_));
    disc_file_->setToolTip(QDir::toNativeSeparators(disc_path_));
  }
}

// ------------------------------------------------------------ the install

void InstallerWindow::OnPrimaryButton() {
  if (phase_ == Phase::Running) {
    Cancel();
  } else if (phase_ == Phase::Broken) {
    close();
  } else {
    StartInstall();
  }
}

void InstallerWindow::StartInstall() {
  if (phase_ == Phase::Running || source_.isEmpty()) return;
  // A fresh run.
  saw_failure_ = false;
  cancel_requested_ = false;
  shader_warning_.clear();
  gpu_warning_.clear();
  shader_cache_.clear();
  failure_ = {};
  if (!verify_) ClearNote();
  play_->setVisible(false);
  progress_->setValue(0);
  QStringList steps{"disc"};
  if (shaders_box_->isChecked()) steps << "shaders";
  BuildRows(steps);
  queue_ = steps.mid(1);
  run_millis_ = 0;
  run_clock_.start();
  tick_.start();
  RunInstall();
}

// `--install <source>`: from the disc into the game's data folder, or, for an older
// install, from that folder into itself, which checks it where it lies.
void InstallerWindow::RunInstall() {
  QStringList args{"--install", QFileInfo(source_).absoluteFilePath()};
  // A folder the player chose, or the one chosen before (data-folder.txt): named, and
  // kept for the next start (--remember; the default folder removes the choice).
  const bool remember = !DestFixed() && !data_root_.isEmpty() && (!target_.isEmpty() || data_place_ == "saved");
  if (verify_ || remember) args << "--dest" << data_root_;
  else if (!config_.dest.isEmpty()) args << "--dest" << config_.dest;
  if (remember) args << "--remember";
  StartStep("disc", args);
}

// `--build-shaders`: the game's own pre-build, the same protocol with the one step
// "shaders". Options of the game go before it; the rest of the line is its own.
void InstallerWindow::RunShaders() {
  StartStep("shaders", DataArgs() + QStringList{"--no-setup", "--build-shaders"});
}

void InstallerWindow::StartStep(const QString& step, const QStringList& arguments) {
  current_step_ = step;
  saw_failure_ = false;
  SetPhase(Phase::Running);
  driver_ = new DriverProcess(this);
  connect(driver_, &DriverProcess::EventReceived, this, &InstallerWindow::OnEvent);
  connect(driver_, &DriverProcess::Finished, this, &InstallerWindow::OnFinished);
  last_command_ = QStringList{program_} + arguments;
  AppendLog("$ " + last_command_.join(' '));
  SetStatus(RunningVerb() + "...");
  driver_->Start(program_, arguments, ChildEnvironment(), QString());
}

QString InstallerWindow::RunningVerb() const {
  if (current_step_ == "shaders") return tr("Preparing graphics");
  return verify_ ? tr("Checking the installed data") : tr("Copying the game's data");
}

void InstallerWindow::OnEvent(const Event& e) {
  using K = Event::Kind;
  StepRow* row = Row(current_step_);
  switch (e.kind) {
    case K::Begin:
      if (row) {
        row->status = RowStatus::Running;
        row->fraction = 0;
        row->detail.clear();
        row->clock.start();
        UpdateRow(*row);
      }
      AppendLog("== " + e.text);
      UpdateProgress();
      break;
    case K::Progress:
      if (row) {
        row->fraction = e.fraction;
        if (!e.text.isEmpty()) row->detail = e.text;
        UpdateRow(*row);
      }
      SetStatus(QString("%1...  %2%").arg(RunningVerb()).arg(qRound(e.fraction * 100)));
      UpdateProgress();
      RefreshLook();
      break;
    case K::Log:
      if (e.step == "shaders" && e.text.startsWith("cache ")) shader_cache_ = e.text.mid(6).trimmed();
      AppendLog("[" + e.step + "] " + e.text);
      break;
    case K::Warn:
      AppendLog("[" + e.step + "] " + e.text, true);
      if (e.step == "shaders" && e.text.startsWith("no-gpu")) {
        // Not a stop: the game runs on the CPU, and the person should know why it is slow.
        gpu_warning_ = e.text.mid(6).trimmed();
        if (gpu_warning_.isEmpty()) gpu_warning_ = tr("software renderer");
        const auto why = messages::ExplainGpu();
        SetNote(NoteTone::Warning, why.headline, why.advice, false);
      }
      break;
    case K::Skip:
    case K::Ok:
      if (row) {
        row->status = e.kind == K::Skip ? RowStatus::Skipped : RowStatus::Ok;
        row->fraction = 1.0;
        row->detail = e.text;
        row->millis = row->clock.isValid() ? row->clock.elapsed() : 0;
        UpdateRow(*row);
      }
      AppendLog("[" + e.step + "] " + (e.kind == K::Skip ? "nothing to do: " : "done: ") + e.text);
      UpdateProgress();
      break;
    case K::Fail:
      saw_failure_ = true;
      failure_ = {current_step_, e.code, e.text, {}};
      AppendLog("[" + e.step + "] FAILED " + e.code + ": " + e.text, true);
      break;
    case K::ResultFail:
      if (!saw_failure_) {
        saw_failure_ = true;
        failure_ = {current_step_, e.code, QString(), {}};
      }
      break;
    case K::Unknown:
      AppendLog(e.text);
      break;
    case K::ResultOk:
    case K::Protocol:
    case K::Plan:
    case K::Missing:
      break;
  }
}

void InstallerWindow::OnFinished(const DriverProcess::Result& result) {
  cancel_timer_.stop();
  driver_->deleteLater();
  driver_ = nullptr;
  const bool shaders = current_step_ == "shaders";
  Failure failure = failure_;
  failure.step = current_step_;
  failure.stderr_tail = result.stderr_tail;
  bool failed = true;
  if (cancel_requested_ || result.cancelled || (saw_failure_ && failure.code == "cancelled")) {
    failure.code = "cancelled";
  } else if (!result.started) {
    failure.code = "program-start";
    failure.message = result.start_error;
  } else if (saw_failure_) {
    // the installer said what
  } else if (!result.saw_protocol && (result.crashed || result.exit_code != 0)) {
    // not one line of the protocol: the program never got going
    failure.code = "program-failed";
    failure.message = result.stderr_tail.isEmpty()
                          ? tr("The program ended with status %1 without printing anything.").arg(result.exit_code)
                          : result.stderr_tail.mid(std::max<qsizetype>(0, result.stderr_tail.size() - 3)).join('\n');
  } else if (result.crashed) {
    failure.code = "program-killed";
    failure.message = tr("The program was ended by a signal before it finished.");
  } else if (!result.saw_result) {
    failure.code = result.exit_code == 0 ? "program-no-result" : "program-crashed";
    failure.message = tr("The program ended with status %1 without a result.").arg(result.exit_code);
  } else if (result.exit_code != 0) {
    failure.code = "program-crashed";
    failure.message = tr("The program ended with status %1.").arg(result.exit_code);
  } else {
    failed = false;
  }

  if (shaders) {
    // The optional step: whatever happened to it, the install is whole.
    if (failed) {
      shader_warning_ = failure.code;
      if (StepRow* row = Row("shaders")) {
        row->status = RowStatus::Warning;
        row->detail = messages::ExplainShaders(failure.code).headline;
        row->millis = row->clock.isValid() ? row->clock.elapsed() : 0;
        UpdateRow(*row);
      }
      AppendLog("[shaders] did not finish (" + failure.code + ")", true);
    }
    Succeed();
    return;
  }
  if (failed && failure.code == "cancelled") {
    Pause();
    return;
  }
  if (failed) {
    Fail(failure);
    return;
  }
  status_code_.clear();
  if (!queue_.isEmpty() && queue_.takeFirst() == "shaders") {
    RunShaders();
    return;
  }
  Succeed();
}

void InstallerWindow::Succeed() {
  run_millis_ = run_clock_.isValid() ? run_clock_.elapsed() : 0;
  tick_.stop();
  if (verify_ && phase_ == Phase::Running) {
    if (target_.isEmpty()) {
      SetNote(NoteTone::Info, tr("The earlier install is complete."),
              tr("Every file matches the disc's list. Nothing had to be copied."), false);
    } else {
      SetNote(NoteTone::Info, tr("The game's data in this folder is complete."),
              tr("Every file matches the disc's list. Nothing had to be copied. Takedawn uses this folder from now on."),
              false);
    }
  }
  if (phase_ == Phase::Running) {
    SetDiscState(DiscState::Valid, tr("The game's data is installed."),
                 verify_ ? tr("No disc is needed.") : tr("The disc is not needed any more."), QString());
  }
  if (!shader_warning_.isEmpty()) {
    const auto why = messages::ExplainShaders(shader_warning_);
    SetNote(NoteTone::Warning, why.headline, tr("The game is installed and ready. ") + why.advice, false);
  }
  if (!gpu_warning_.isEmpty()) {
    // The CPU warning outlasts the install: it is what the person needs to act on.
    const auto why = messages::ExplainGpu();
    SetNote(NoteTone::Warning, why.headline, tr("The game is installed and ready. ") + why.advice, false);
  }
  if (steam_box_ && steam_box_->isChecked() && phase_ == Phase::Running) AddToSteam();
  status_code_.clear();
  progress_->setValue(progress_->maximum());
  SetPhase(Phase::Done);
  play_->setVisible(true);
  play_->setFocus();
  RefreshSpace();
  SetStatus(tr("Ready. Press Play to start the game."));
  clock_label_->setText(run_millis_ > 0 ? ElapsedText() : QString());
  RefreshLook();
  RefreshHints();
  SettleFocus();
}

// The optional Steam shortcut, after the install: whatever happens to it the game is
// installed, so a refusal is a note, never a failure. The rules are steam_shortcut.h's:
// no guess between several users, a backup first, existing entries untouched.
//
// Steam rewrites shortcuts.vdf when it exits, so the file may only be written while it
// is closed, and on a Steam Deck (also in Desktop Mode) Steam is always running. So the
// person is asked; on Yes, `steam -shutdown` closes it cleanly, the shortcut is written
// once its processes are gone, and Steam is started again. Not done (a note with the
// manual steps instead) when a game started from Steam is running, when this setup was
// itself started from Steam (Game Mode: closing Steam would end the session, and Takedawn
// is in the library already), when Steam does not close in time, or on No.
// What is started from Steam is ~/Applications/Takedawn.AppImage (the stable name; it
// survives updates) when that file is there, else the AppImage running now (APPIMAGE),
// else the game's program. No launch options: under gamescope any window is fullscreen.
// TAKEDAWN_STEAM_CONFIG names a userdata/<id>/config folder outright (tests, and a
// person who knows which user): the running check is theirs then.
// TAKEDAWN_STEAM_COMMAND replaces the `steam` program (tests: a stand-in).
void InstallerWindow::AddToSteam() {
  const QString manual = tr("In Desktop Mode on a Steam Deck, right-click Takedawn.AppImage in the file manager and "
                            "choose \"Add to Steam\"; elsewhere use Steam's Games menu, \"Add a Non-Steam Game to My "
                            "Library\", and choose Takedawn.");
  const auto note = [this](const QString& headline, const QString& text) {
    AppendLog("[steam] " + headline + " " + text, true);
    SetNote(NoteTone::Warning, headline, tr("The game is installed and ready. ") + text, false);
  };
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString config = env.value("TAKEDAWN_STEAM_CONFIG");
  std::vector<std::string> steam_command{"steam"};
  bool closed_steam = false;  // we closed it: it is started again whatever happens next
  struct Reopen {
    InstallerWindow* w;
    const bool* closed;
    const std::vector<std::string>* command;
    ~Reopen() {
      if (*closed) w->StartSteam(*command);
    }
  } reopen{this, &closed_steam, &steam_command};

  if (config.isEmpty()) {
    steam::User user;
    std::string why;
    if (!steam::PickUser(steam::UserdataConfigs(QDir::homePath().toStdString()), &user, &why)) {
      note(tr("Takedawn was not added to Steam."),
           tr("No Steam user could be chosen (%1). ").arg(QString::fromStdString(why)) + manual);
      return;
    }
    AppendLog(QString("[steam] using Steam user %1: %2").arg(QString::fromStdString(user.id), QString::fromStdString(why)));
    config = QString::fromStdString(user.config);
    steam_command = steam::SteamCommand(user.root);
    if (env.contains("TAKEDAWN_STEAM_COMMAND")) steam_command = {env.value("TAKEDAWN_STEAM_COMMAND").toStdString()};

    if (steam::SteamRunning()) {
      if (env.contains("SteamGameId") || env.contains("SteamAppId")) {
        SetNote(NoteTone::Info, tr("Takedawn is started from Steam here."),
                tr("So it is in your Steam library already; nothing was added."), false);
        AppendLog("[steam] this setup was started from Steam (SteamGameId): nothing to add, Steam stays open");
        return;
      }
      if (steam::SteamGameRunning()) {
        note(tr("Takedawn was not added to Steam: a game started from Steam is running."),
             tr("Steam is left open so that game is not closed. Quit it and run this setup again, or add Takedawn by "
                "hand. ") + manual);
        return;
      }
      if (!ask_close_steam_()) {
        note(tr("Takedawn was not added to Steam."),
             tr("Steam stayed open, and it keeps its list of shortcuts while it runs. ") + manual);
        return;
      }
      if (!CloseSteam(steam_command, &closed_steam)) {
        note(tr("Takedawn was not added to Steam: Steam did not close."),
             tr("It was asked to and did not finish in time, so its list of shortcuts was left alone. ") + manual);
        return;
      }
    }
  }
  const QString home = QDir::homePath();
  QString exe = QString::fromStdString(
      steam::ShortcutExe(home.toStdString(), env.value("APPIMAGE").toStdString(),
                         QFileInfo(program_).absoluteFilePath().toStdString()));
  const QString name = "Takedawn";
  const quint32 id = steam::AppId(("\"" + exe + "\"").toStdString(), name.toStdString());
  // The icon Steam shows: the project's own, beside the other art of this shortcut
  // (written with it below, once the shortcut is there).
  const QString grid = QDir(config).filePath("grid");
  const QString icon = QDir(grid).filePath(steamart::FileName(id, steamart::Icon));
  try {
    const steam::Added added =
        steam::AddShortcut(QDir(config).filePath("shortcuts.vdf").toStdString(), name.toStdString(), exe.toStdString(),
                           QFileInfo(exe).absolutePath().toStdString(), icon.toStdString());
    // Library art for our own shortcut only: an entry for this program that someone
    // else made (another id) keeps whatever art it has.
    if (added.added || added.app_id == id) WriteSteamArt(grid, id);
    AppendLog(added.added ? QString("[steam] added \"%1\" (%2) to %3%4")
                                .arg(name, exe, config,
                                     added.backup.empty() ? QString()
                                                          : ", backup " + QString::fromStdString(added.backup))
                          : QString("[steam] \"%1\" (%2) is already in %3").arg(name, exe, config));
    SetNote(NoteTone::Info, added.added ? tr("Takedawn was added to your Steam library.")
                                        : tr("Takedawn is already in your Steam library."),
            closed_steam ? tr("Steam is starting again; Takedawn is among the non-Steam games.")
                         : tr("Start Steam to see it among the non-Steam games."),
            false);
  } catch (const std::exception& e) {
    note(tr("Takedawn was not added to Steam."),
         tr("Steam's list of shortcuts could not be changed (%1); it was left as it was. ")
                 .arg(QString::fromUtf8(e.what())) + manual);
  }
}

// The library art (steam_artwork.h): composed from the installed game's own pictures,
// else Takedawn's icon and name. A failure is a line in the log; Steam then shows its
// grey tile, as without this.
void InstallerWindow::WriteSteamArt(const QString& grid, quint32 id) {
  QString note;
  const steamart::Art art = steamart::ForShortcut(
      data_root_.isEmpty() ? QString() : QDir(data_root_).filePath(kGameDir), QImage(":/takedawn/icon-512.png"), &note);
  AppendLog("[steam] " + note);
  const steamart::Written w = steamart::WriteGridArt(grid, id, art);
  for (const QString& f : w.files) AppendLog("[steam] wrote " + f);
  for (const QString& b : w.backups) AppendLog("[steam] kept the earlier file as " + b);
  for (const QString& e : w.errors) AppendLog("[steam] library art: " + e);
}

// Steam's own environment: what the AppImage sets up for itself is not meant for it.
static QProcessEnvironment SteamEnvironment() {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  for (const char* name : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                           "QT_QPA_PLATFORM", "QT_SCALE_FACTOR", "APPIMAGE", "APPDIR", "ARGV0", "OWD"}) {
    env.remove(name);
  }
  if (env.contains("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH")) {
    env.insert("LD_LIBRARY_PATH", env.value("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH"));
    env.remove("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH");
  }
  return env;
}

// `steam -shutdown`, then wait (the window stays alive, no clicks) until no Steam
// process is left, at most 45 s. `closed` says Steam was running and was asked to close:
// whatever the outcome, the caller starts it again.
bool InstallerWindow::CloseSteam(const std::vector<std::string>& command, bool* closed) {
  QStringList args;
  for (size_t i = 1; i < command.size(); ++i) args << QString::fromStdString(command[i]);
  args << "-shutdown";
  QProcess quit;
  quit.setProcessEnvironment(SteamEnvironment());
  quit.setProcessChannelMode(QProcess::MergedChannels);
  quit.start(QString::fromStdString(command[0]), args);
  if (!quit.waitForStarted(5000)) {
    AppendLog("[steam] cannot run " + QString::fromStdString(command[0]) + ": " + quit.errorString());
    return false;
  }
  *closed = true;
  SetStatus(tr("Closing Steam for a moment..."));
  AppendLog("[steam] asked Steam to close (-shutdown)");
  quit.waitForFinished(15000);
  QEventLoop wait;
  QElapsedTimer clock;
  clock.start();
  QTimer poll;
  connect(&poll, &QTimer::timeout, &wait, [&] {
    if (!steam::SteamRunning() || clock.elapsed() > 45000) wait.quit();
  });
  poll.start(250);
  wait.exec(QEventLoop::ExcludeUserInputEvents);
  const bool gone = !steam::SteamRunning();
  AppendLog(gone ? QString("[steam] Steam closed after %1 ms").arg(clock.elapsed())
                 : QString("[steam] Steam was still running after 45 s"));
  return gone;
}

// Steam again, detached, as the person's own desktop would start it.
void InstallerWindow::StartSteam(const std::vector<std::string>& command) {
  QProcess steam_process;
  QStringList args;
  for (size_t i = 1; i < command.size(); ++i) args << QString::fromStdString(command[i]);
  steam_process.setProgram(QString::fromStdString(command[0]));
  steam_process.setArguments(args);
  steam_process.setProcessEnvironment(SteamEnvironment());
  steam_process.setWorkingDirectory(QDir::homePath());
  steam_process.setStandardInputFile(QProcess::nullDevice());
  steam_process.setStandardOutputFile(QProcess::nullDevice());
  steam_process.setStandardErrorFile(QProcess::nullDevice());
  qint64 pid = 0;
  AppendLog(steam_process.startDetached(&pid) ? QString("[steam] started Steam again (pid %1)").arg(pid)
                                              : QString("[steam] could not start Steam again: ") + steam_process.errorString());
}

void InstallerWindow::Fail(const Failure& failure) {
  run_millis_ = run_clock_.isValid() ? run_clock_.elapsed() : 0;
  tick_.stop();
  failure_ = failure;
  if (StepRow* row = Row(failure.step)) {
    row->status = RowStatus::Failed;
    row->detail = failure.message;
    row->millis = row->clock.isValid() ? row->clock.elapsed() : 0;
    UpdateRow(*row);
  }
  messages::InstallFacts facts;
  facts.in_place = verify_;
  facts.source = source_;
  facts.kept = CopiedBytes() > 0;
  facts.needed = StillNeededFree();
  facts.data = data_bytes_;
  facts.free = FreeBytes();
  const auto why = failure.code.startsWith("program-") ? messages::ExplainProgram(failure.code)
                                                       : messages::ExplainInstall(failure.code, facts);
  if (!verify_ && messages::IsDiscProblem(failure.code)) {
    // What is wrong with the disc is said on the disc card, once; the note says where it stopped and what to do.
    const QString file = messages::DamagedFile(failure.message);
    SetDiscState(DiscState::Invalid, why.headline, tr("Choose another image or folder."), failure.message);
    SetNote(NoteTone::Error, file.isEmpty() ? tr("The copy stopped.") : tr("Stopped at %1.").arg(file), why.advice, true);
  } else {
    SetNote(NoteTone::Error, why.headline, WithTail(failure.code, why.advice, failure.message), true);
  }
  SetPhase(Phase::Failed);
  SetStatus(why.headline);
  clock_label_->setText(ElapsedText());
}

void InstallerWindow::Pause() {
  run_millis_ = run_clock_.isValid() ? run_clock_.elapsed() : 0;
  tick_.stop();
  for (StepRow& row : rows_) {
    if (row.status == RowStatus::Running) {
      row.status = RowStatus::Pending;
      row.detail = tr("stopped at %1%").arg(qRound(row.fraction * 100));
      UpdateRow(row);
    }
  }
  AppendLog("Stopped by the player.");
  SetPhase(Phase::Paused);
  SetStatus(tr("Stopped. What was copied is kept: Resume goes on from there."));
  clock_label_->setText(ElapsedText());
}

void InstallerWindow::Cancel() {
  if (!config_.test) {
    const auto answer = QMessageBox::question(
        this, windowTitle(), tr("Stop the installation? What is already copied is kept, and Resume goes on from there."));
    if (answer != QMessageBox::Yes) return;
  }
  if (!driver_) return;
  cancel_requested_ = true;
  SetStatus(tr("Stopping..."));
  primary_->setEnabled(false);
  // The file the program looks for once a second; it ends by itself and says so.
  QString file;
  if (current_step_ == "shaders") {
    if (!shader_cache_.isEmpty()) file = shader_cache_ + "/prebuild/cancel";
  } else if (!data_root_.isEmpty()) {
    file = (config_.dest.isEmpty() || verify_ ? data_root_ : QDir::cleanPath(config_.dest)) + "/install.cancel";
  }
  bool asked = false;
  if (!file.isEmpty() && QDir().mkpath(QFileInfo(file).absolutePath())) {
    QFile f(file);
    asked = f.open(QIODevice::WriteOnly);
  }
  if (asked) cancel_timer_.start();
  else driver_->Cancel();
}

bool InstallerWindow::ProgramFailed(const DriverProcess::Result& result, bool usage_exit_is_old_program) {
  if (!result.started || result.cancelled || result.saw_protocol) return false;
  if (!result.crashed && (result.exit_code == 0 || (usage_exit_is_old_program && result.exit_code == 2))) return false;
  QString detail = result.stderr_tail.isEmpty() ? tr("The program ended with status %1 without printing anything.").arg(result.exit_code)
                                                : result.stderr_tail.join('\n');
  AppendLog("$ " + program_ + ": ended with status " + QString::number(result.exit_code) +
                (result.crashed ? " (crashed)" : "") + " without a protocol line", true);
  Break("program-failed", detail);
  return true;
}

// There is no game to run: nothing here can work.
void InstallerWindow::Break(const QString& code, const QString& detail) {
  const auto why = messages::ExplainProgram(code);
  failure_ = {"start", code, detail, {}};
  AppendLog(code + ": " + detail, true);
  SetNote(NoteTone::Error, why.headline, WithTail(code, why.advice, detail), true);
  SetPhase(Phase::Broken);
  SetStatus(why.headline);
}

// Starts the game and leaves: the window's work is done.
void InstallerWindow::Play() {
  ++play_count_;
  if (config_.test) return;
  const QStringList args = DataArgs();
#ifdef Q_OS_LINUX
  // This process becomes the game, as the game became this window: whoever
  // started it (Steam, an AppImage's runtime, which takes its mounted files
  // away when its one process ends) goes on watching the same process. A game
  // started beside the window would lose its launcher the moment the window
  // closed.
  {
    QList<QByteArray> store;
    store << QFile::encodeName(program_);
    for (const QString& a : args) store << a.toLocal8Bit();
    const int argc = int(store.size());
    for (const QString& e : ChildEnvironment().toStringList()) store << e.toLocal8Bit();
    std::vector<char*> argv, envp;
    for (int i = 0; i < int(store.size()); ++i) (i < argc ? argv : envp).push_back(store[i].data());
    argv.push_back(nullptr);
    envp.push_back(nullptr);
    hide();
    theme::RemoveTemporaryFiles();  // exec runs no destructors: the style sheet's pictures go now
    if (pad_) pad_->Stop();  // the controllers are the game's from here
    QCoreApplication::processEvents();
    fflush(stdout);
    fflush(stderr);
    execve(argv[0], argv.data(), envp.data());
    const QString why = QString::fromLocal8Bit(strerror(errno));
    show();
    if (pad_) pad_->Start();
    Break("program-start", program_ + ": " + why);
    return;
  }
#else
  if (!QProcess::startDetached(program_, args, QString())) {
    Break("program-start", program_);
    return;
  }
  close();
#endif
}

// -------------------------------------------------------------- showing it

void InstallerWindow::BuildRows(const QStringList& steps) {
  auto* grid = static_cast<QGridLayout*>(rows_host_->layout());
  for (StepRow& row : rows_) {
    for (QWidget* w : {static_cast<QWidget*>(row.glyph), static_cast<QWidget*>(row.title),
                       static_cast<QWidget*>(row.info), static_cast<QWidget*>(row.time)}) {
      grid->removeWidget(w);
      delete w;
    }
  }
  rows_.clear();
  int r = 0;
  for (const QString& id : steps) {
    StepRow row;
    row.id = id;
    row.weight = id == "shaders" ? 60 : 100;
    row.glyph = new StepGlyph;
    row.title = new QLabel(messages::StepTitle(id == "disc" && verify_ ? QString("verify") : id));
    row.title->setMinimumHeight(30);
    row.title->setFont(theme::DisplayFont(11.5));
    row.info = new ElidedLabel;
    row.info->setMinimumHeight(26);
    row.time = new QLabel;
    row.time->setMinimumWidth(44);
    row.time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(row.glyph, r, 0);
    grid->addWidget(row.title, r, 1);
    grid->addWidget(row.info, r, 2);
    grid->addWidget(row.time, r, 3);
    ++r;
    rows_.append(row);
  }
  grid->setColumnMinimumWidth(1, 150);
  for (StepRow& row : rows_) UpdateRow(row);
}

InstallerWindow::StepRow* InstallerWindow::Row(const QString& id) {
  for (StepRow& row : rows_) {
    if (row.id == id) return &row;
  }
  return nullptr;
}

void InstallerWindow::UpdateRow(StepRow& row) {
  using S = StepGlyph::State;
  row.glyph->SetState(row.status == RowStatus::Pending ? S::Pending : row.status == RowStatus::Running ? S::Running
                      : row.status == RowStatus::Failed ? S::Failed : row.status == RowStatus::Warning ? S::Warning : S::Done);
  const QString state = row.status == RowStatus::Pending ? "pending" : row.status == RowStatus::Running ? "running"
                        : row.status == RowStatus::Failed ? "failed" : row.status == RowStatus::Warning ? "warning" : "done";
  theme::SetProperty(row.title, "state", state);
  theme::SetProperty(row.time, "state", "pending");
  QString info = row.detail;
  if (row.status == RowStatus::Failed) {
    // The installer's own sentence is long (a file, a path, two hashes): it is the tooltip and the details.
    info = tr("stopped at %1%").arg(qRound(row.fraction * 100));
  } else if (row.status == RowStatus::Running && row.fraction > 0) {
    info = QString("%1%  %2").arg(qRound(row.fraction * 100)).arg(row.detail);
  } else if (row.id == "disc" && data_files_ > 0 && (row.status == RowStatus::Ok || row.status == RowStatus::Skipped)) {
    // (the installer's own count of what it copied, kept and checked is the tooltip)
    info = tr("%1 files, %2 GB: every file matches the disc's list").arg(data_files_).arg(Gb(data_bytes_));
  } else if (row.id == "shaders" && (row.status == RowStatus::Ok || row.status == RowStatus::Skipped)) {
    // The game's own line (counts, bytes, the cache folder) is too long for the row; it stays in the tooltip and the log.
    info = row.status == RowStatus::Ok ? tr("The graphics are ready.") : tr("The graphics were already prepared.");
  }
  row.info->SetFullText(info);
  row.info->setToolTip(row.detail);
  qint64 ms = row.millis;
  if (row.status == RowStatus::Running && row.clock.isValid()) ms = row.clock.elapsed();
  row.time->setText(row.status == RowStatus::Pending || row.status == RowStatus::Skipped || ms <= 0 ? QString()
                                                                                                    : HumanDuration(ms));
}

void InstallerWindow::UpdateProgress() {
  double total = 0, done = 0;
  for (const StepRow& row : rows_) {
    total += row.weight;
    switch (row.status) {
      case RowStatus::Ok:
      case RowStatus::Warning:
      case RowStatus::Skipped: done += row.weight; break;
      case RowStatus::Running: done += row.weight * std::clamp(row.fraction, 0.0, 1.0); break;
      default: break;
    }
  }
  progress_->setValue(total > 0 ? int(progress_->maximum() * done / total) : 0);
}

void InstallerWindow::SetStatus(const QString& text) { status_->setText(text); }

void InstallerWindow::SetNote(NoteTone tone, const QString& headline, const QString& text, bool with_copy) {
  const QColor head = tone == NoteTone::Error ? theme::kWarnText : tone == NoteTone::Warning ? theme::kCaution : theme::kLit;
  note_text_->setText(Span(head, headline, true) + (text.isEmpty() ? QString() : "<br>" + text.toHtmlEscaped().replace('\n', "<br>")));
  note_copy_->setVisible(with_copy);
  theme::SetProperty(note_, "tone", tone == NoteTone::Error ? "error" : tone == NoteTone::Warning ? "warning" : "info");
  note_->setVisible(true);
}

void InstallerWindow::ClearNote() { note_->setVisible(false); }

void InstallerWindow::SetPhase(Phase phase) {
  phase_ = phase;
  const bool running = phase == Phase::Running;
  const bool can_choose = phase == Phase::Idle || phase == Phase::Paused || phase == Phase::Failed;
  image_browse_->setEnabled(can_choose);
  folder_browse_->setEnabled(can_choose);
  shaders_box_->setEnabled(can_choose);
  shaders_cost_->setEnabled(can_choose);
  if (steam_box_) steam_box_->setEnabled(can_choose);
  const bool offer = phase != Phase::Done && phase != Phase::Broken;  // once installed, "Build shaders now" in the game's settings
  shaders_box_->setVisible(offer);
  shaders_cost_->setVisible(offer);
  primary_->setVisible(phase != Phase::Done);
  if (auto* caption = findChild<QLabel*>("destCaption")) {
    caption->setText(phase == Phase::Done ? tr("Installed in") : tr("Installs to"));
  }
  primary_->setEnabled(true);
  switch (phase) {
    case Phase::Asking:
      primary_->setText(tr("Install"));
      primary_->setEnabled(false);
      break;
    case Phase::Idle:
      primary_->setText(tr("Install"));
      primary_->setEnabled(disc_state_ == DiscState::Valid && !low_space_);
      break;
    case Phase::Running: primary_->setText(tr("Stop")); break;
    case Phase::Paused:
    case Phase::Failed: primary_->setText(tr("Resume")); break;
    case Phase::Done: break;
    case Phase::Broken: primary_->setText(tr("Quit")); break;
  }
  Q_UNUSED(running);
  RefreshDestControls();
  RefreshLook();
  RefreshHints();
  SettleFocus();
}

// --------------------------------------------------------- the controller

void InstallerWindow::PadPress(PadKey key) {
  if (qEnvironmentVariableIntValue("TAKEDAWN_SETUP_TRACE")) {
    static const char* const names[] = {"up", "down", "left", "right", "accept", "back", "start", "page-up", "page-down"};
    QTextStream(stderr) << "[SETUP] press " << names[int(key)] << Qt::endl;
  }
  if (!pad_seen_) {
    pad_seen_ = true;
    RefreshHints();
  }
  nav_->Press(key);
  RefreshHints();
}

// What the presses drive: the box on top (a question, About), else the window.
QWidget* InstallerWindow::PadRoot() {
  if (QWidget* modal = QApplication::activeModalWidget()) return modal;
  if (about_dialog_ && about_dialog_->isVisible()) return about_dialog_;
  return this;
}

// Where the focus belongs when it is nowhere: on the next thing to do.
QWidget* InstallerWindow::PreferredFocus() {
  if (stack_->currentWidget() == browser_) return browser_->List();
  if (found_->isVisible()) return found_use_;
  if (play_->isVisible()) return play_;
  if (primary_->isVisible() && primary_->isEnabled()) return primary_;
  if (image_browse_->isEnabled()) return image_browse_;
  return about_;
}

// With a controller the focus is the pointer: it is never left on a control
// that went away or cannot be pressed.
void InstallerWindow::SettleFocus() {
  if (!couch_ && !pad_seen_) return;
  if (PadRoot() == this) {
    QWidget* want = PreferredFocus();
    QWidget* focus = focusWidget();
    // Kept where the player put it, unless the next thing to do has just changed.
    if (!focus || want != focus_wanted_ || !PadNavigator::Targets(this).contains(focus)) {
      focus_wanted_ = want;
      if (want) want->setFocus(Qt::OtherFocusReason);
    }
  }
  MarkPadFocus();
}

void InstallerWindow::RefreshHints() {
  if (!hints_) return;
  hints_->setVisible(couch_ || pad_seen_);
  const auto key = [](const QString& name, const QString& what) {
    return QString("<span style=\"color:%1;font-weight:600;\">%2</span>&nbsp;&nbsp;%3")
        .arg(theme::Css(theme::kLit), name.toHtmlEscaped(), what.toHtmlEscaped());
  };
  const QString gap = "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";
  QStringList parts;
  QWidget* top = PadRoot();
  if (top != this) {
    parts << key("A", tr("Select")) << key("B", tr("Close"));
    if (top == about_dialog_) parts << key(tr("Shoulder buttons"), tr("Scroll"));
  } else if (stack_->currentWidget() == browser_) {
    parts << key("A", tr("Open")) << key("B", tr("Back")) << key(tr("Shoulder buttons"), tr("Page"));
  } else {
    parts << key("A", tr("Select"));
    if (play_->isVisible()) parts << key("Start", play_->text());
    else if (primary_->isVisible() && primary_->isEnabled()) parts << key("Start", primary_->text());
  }
  hints_->setText(parts.join(gap));
  MarkPadFocus();
}

// The controller's focus is drawn by a mark of its own (the style sheet's
// padFocus), not left to the window system's idea of which window is active:
// it is the only pointer there is.
void InstallerWindow::MarkPadFocus() {
  QWidget* now = nullptr;
  if (couch_ || pad_seen_) {
    QWidget* top = PadRoot();
    QWidget* focus = top->window()->focusWidget();
    if (focus && PadNavigator::Targets(top).contains(focus)) now = focus;
  }
  if (now == pad_marked_) return;
  if (pad_marked_) {
    theme::SetProperty(pad_marked_, "padFocus", "false");
    if (auto* list = qobject_cast<QListWidget*>(pad_marked_.data())) list->viewport()->update();
  }
  pad_marked_ = now;
  if (now) {
    theme::SetProperty(now, "padFocus", "true");
    if (auto* list = qobject_cast<QListWidget*>(now)) list->viewport()->update();
  }
}

// The colours and the roles of the buttons follow the state.
void InstallerWindow::RefreshLook() {
  const bool running = phase_ == Phase::Running;
  const bool failed = phase_ == Phase::Failed || phase_ == Phase::Broken;
  const Tone tone = failed ? Tone::Error : phase_ == Phase::Done ? Tone::Ready : Tone::Normal;
  header_->SetTone(tone);
  progress_->SetTone(tone);
  progress_->SetAnimated(running);
  theme::SetProperty(status_, "tone", failed ? "error" : phase_ == Phase::Done ? "success" : "");
  theme::SetProperty(primary_, "role", running ? "stop" : "primary");
  theme::SetProperty(play_, "role", "primary");
  QString big, small = version_line_;
  if (small.startsWith("Takedawn ")) small = tr("version %1").arg(small.mid(9));
  const int percent = progress_->value() * 100 / std::max(1, progress_->maximum());
  switch (phase_) {
    case Phase::Asking:
    case Phase::Idle: big = tr("SETUP"); break;
    case Phase::Running:
      big = QString("%1%").arg(percent);
      small = tr("Overall progress");  // the rows and the status line count their own step
      break;
    case Phase::Paused:
      big = tr("PAUSED");
      small = tr("Resume goes on from here");
      break;
    case Phase::Failed:
    case Phase::Broken:
      big = tr("STOPPED");
      small = failure_.code;
      break;
    case Phase::Done: big = tr("READY TO PLAY"); break;
  }
  header_->SetReadout(big, small);
}

void InstallerWindow::AppendLog(const QString& line, bool warning) {
  log_lines_.append(line);
  while (log_lines_.size() > 4000) log_lines_.removeFirst();
  if (warning) {
    log_->appendHtml(Span(theme::kWarnText, line).replace("  ", "&nbsp; "));
  } else {
    log_->appendPlainText(line);
  }
}

QString InstallerWindow::ElapsedText() const {
  const qint64 ms = phase_ == Phase::Running ? run_clock_.elapsed() : run_millis_;
  return ms > 0 ? tr("elapsed %1").arg(HumanDuration(ms)) : QString();
}

QString InstallerWindow::DetailsText() const {
  QString out;
  QTextStream t(&out);
  t << (version_line_.isEmpty() ? QString("Takedawn") : version_line_) << ", setup window " << TAKEDAWN_SETUP_VERSION
    << " on " << QSysInfo::prettyProductName() << " (" << QSysInfo::currentCpuArchitecture() << ")\n";
  t << "Step: " << failure_.step << "\n";
  t << "Code: " << failure_.code << "\n";
  t << "Message: " << failure_.message << "\n";
  t << "Program: " << (program_.isEmpty() ? QString("not found") : program_) << "\n";
  if (!last_command_.isEmpty()) t << "Command: " << last_command_.join(' ') << "\n";
  t << "Data folder: " << data_root_ << "\n";
  t << "Last lines of the log:\n";
  for (const QString& line : log_lines_.mid(std::max<qsizetype>(0, log_lines_.size() - 40))) t << "  " << line << "\n";
  if (!failure_.stderr_tail.isEmpty()) {
    t << "Error output of the program:\n";
    for (const QString& line : failure_.stderr_tail) t << "  " << line << "\n";
  }
  return out;
}
