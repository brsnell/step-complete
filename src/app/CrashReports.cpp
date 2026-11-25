#include "app/CrashReports.h"

#include "app/CrashHandler.h"
#include "core/Version.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace sc::app {

namespace {

const QString LastSeenFile = QStringLiteral("last-seen");

QString nameFilter() {
  return QString::fromLatin1(CrashReportPrefix) + QStringLiteral("*.txt");
}

} // namespace

CrashReports::CrashReports(QString folder) : _folder(std::move(folder)) {}

QString CrashReports::defaultFolder() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/crashes");
}

QStringList CrashReports::reports() const {
  const QDir dir(_folder);
  QStringList names = dir.entryList({nameFilter()}, QDir::Files, QDir::Name);
  std::reverse(names.begin(), names.end());
  QStringList paths;
  for (const QString& name : names) {
    paths.append(dir.filePath(name));
  }
  return paths;
}

QString CrashReports::lastSeen() const {
  QFile file(QDir(_folder).filePath(LastSeenFile));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll()).trimmed();
}

QStringList CrashReports::newReports() const {
  const QString seen = lastSeen();
  QStringList fresh;
  for (const QString& path : reports()) {
    if (QFileInfo(path).fileName() > seen) {
      fresh.append(path);
    }
  }
  return fresh;
}

void CrashReports::markSeen(const QString& report) {
  const QString name = QFileInfo(report).fileName();
  if (name <= lastSeen()) {
    return;
  }
  QDir().mkpath(_folder);
  QSaveFile file(QDir(_folder).filePath(LastSeenFile));
  if (file.open(QIODevice::WriteOnly)) {
    file.write(name.toUtf8());
    file.commit();
  }
}

void CrashReports::prune(int keep) {
  const QStringList all = reports();
  for (int i = std::max(0, keep); i < all.size(); ++i) {
    const QString dump = dumpFor(all[i]);
    if (!dump.isEmpty()) {
      QFile::remove(dump);
    }
    QFile::remove(all[i]);
  }
}

QString CrashReports::dumpFor(const QString& report) {
  const QFileInfo info(report);
  const QString dump = info.dir().filePath(info.completeBaseName() + QStringLiteral(".dmp"));
  return QFileInfo::exists(dump) ? dump : QString();
}

QUrl CrashReports::issueUrl(const QString& reportText, bool hasDump) {
  QString cause;
  for (const QString& line : reportText.split(QLatin1Char('\n'))) {
    if (line.startsWith(QStringLiteral("Cause: "))) {
      cause = line.mid(7).section(QStringLiteral(" at "), 0, 0).trimmed();
      break;
    }
  }
  const QString title = QStringLiteral("Crash in %1 %2%3")
                            .arg(QString::fromUtf8(core::AppName), QString::fromUtf8(core::AppVersion),
                                 cause.isEmpty() ? QString() : QStringLiteral(": ") + cause);

  QString intro = QStringLiteral("**What were you doing when it crashed?**\n\n\n\n");
  if (hasDump) {
    intro +=
        QStringLiteral("**Please attach the minidump** (the `.dmp` file with the same name as the report, "
                       "in Help > Crash Reports).\n\n");
  }
  const QString open = QStringLiteral("Crash report:\n\n```\n");
  const QString close = QStringLiteral("```\n");
  const QString cutMark =
      QStringLiteral("\n[... cut to fit the link; the full report is in the crash folder]\n");
  QString report = reportText;
  const int room = MaxIssueBody - static_cast<int>(intro.size() + open.size() + close.size());
  if (report.size() > room) {
    report = report.left(std::max(0, room - static_cast<int>(cutMark.size()))) + cutMark;
  }
  if (!report.endsWith(QLatin1Char('\n'))) {
    report += QLatin1Char('\n');
  }

  const QByteArray query = "title=" + QUrl::toPercentEncoding(title) +
                           "&body=" + QUrl::toPercentEncoding(intro + open + report + close);
  return QUrl::fromEncoded(QByteArray(core::ProjectUrl) + "/issues/new?" + query, QUrl::StrictMode);
}

CrashReportDialog::CrashReportDialog(const QString& reportPath, int earlier, QWidget* parent)
    : QDialog(parent), _path(reportPath) {
  setWindowTitle(tr("%1 Crash Report").arg(QString::fromUtf8(core::AppName)));
  QFile file(reportPath);
  if (file.open(QIODevice::ReadOnly)) {
    _text = QString::fromUtf8(file.readAll());
  }

  QString intro = tr("%1 closed unexpectedly last time. The report below was saved on this computer; "
                     "nothing has been sent.")
                      .arg(QString::fromUtf8(core::AppName));
  if (earlier > 0) {
    intro +=
        QLatin1Char(' ') + tr("There are %n earlier report(s) in the crash folder too.", nullptr, earlier);
  }
  intro +=
      QStringLiteral("<br><br>") +
      tr("To help fix it, choose <b>Report on GitHub</b>: it opens a new issue with this report, which you "
         "can read and change before you submit it.");
  auto* label = new QLabel(intro, this);
  label->setWordWrap(true);
  label->setTextFormat(Qt::RichText);

  auto* text = new QPlainTextEdit(_text, this);
  text->setObjectName(QStringLiteral("crashReportText"));
  text->setReadOnly(true);
  text->setLineWrapMode(QPlainTextEdit::NoWrap);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  text->setMinimumSize(640, 320);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QPushButton* report = buttons->addButton(tr("Report on GitHub..."), QDialogButtonBox::ActionRole);
  report->setObjectName(QStringLiteral("crashReportGitHub"));
  QPushButton* copy = buttons->addButton(tr("Copy Report"), QDialogButtonBox::ActionRole);
  copy->setObjectName(QStringLiteral("crashReportCopy"));
  QPushButton* folder = buttons->addButton(tr("Open Folder"), QDialogButtonBox::ActionRole);
  folder->setObjectName(QStringLiteral("crashReportFolder"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(report, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(issueUrl()); });
  connect(copy, &QPushButton::clicked, this, [this] {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
      clipboard->setText(_text);
    }
  });
  connect(folder, &QPushButton::clicked, this,
          [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(_path).absolutePath())); });

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(label);
  layout->addWidget(text, 1);
  layout->addWidget(buttons);
  report->setDefault(true);
}

QUrl CrashReportDialog::issueUrl() const {
  return CrashReports::issueUrl(_text, !CrashReports::dumpFor(_path).isEmpty());
}

} // namespace sc::app
