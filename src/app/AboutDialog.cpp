#include "app/AboutDialog.h"

#include "core/OcctInfo.h"
#include "core/Version.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QSysInfo>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sc::app {

namespace {

QString readResource(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

} // namespace

QList<AboutDialog::LicenseEntry> AboutDialog::licenseEntries() {
  QList<LicenseEntry> list;
  list.append({QStringLiteral("STEP Complete — MIT"),
               {QStringLiteral(":/licenses/MIT.txt")},
               QStringLiteral("https://github.com/brsnell/step-complete"),
               false});
  list.append(
      {QStringLiteral("Open CASCADE Technology — LGPL 2.1 + OCCT exception"),
       {QStringLiteral(":/licenses/LGPL-2.1.txt"), QStringLiteral(":/licenses/OCCT_LGPL_EXCEPTION.txt")},
       QStringLiteral("https://github.com/Open-Cascade-SAS/OCCT"),
       true});
  list.append({QStringLiteral("Qt 6 — LGPL 3.0"),
               {QStringLiteral(":/licenses/LGPL-3.0.txt"), QStringLiteral(":/licenses/GPL-3.0.txt")},
               QStringLiteral("https://download.qt.io/official_releases/qt/"),
               true});
  list.append({QStringLiteral("FreeType — FTL"),
               {QStringLiteral(":/licenses/FTL.txt")},
               QStringLiteral("https://freetype.org"),
               false});
  list.append({QStringLiteral("Fontconfig (Linux) — MIT-style"),
               {QStringLiteral(":/licenses/fontconfig.txt")},
               QStringLiteral("https://www.freedesktop.org/wiki/Software/fontconfig/"),
               false});
  list.append({QStringLiteral("zlib — zlib license"),
               {QStringLiteral(":/licenses/zlib.txt")},
               QStringLiteral("https://zlib.net"),
               false});
  list.append({QStringLiteral("STIX Two Math font — OFL 1.1"),
               {QStringLiteral(":/licenses/OFL.txt")},
               QStringLiteral("https://github.com/stipub/stixfonts"),
               false});
  return list;
}

QString AboutDialog::licenseText(const LicenseEntry& entry) {
  QString text;
  for (const QString& path : entry.files) {
    const QString part = readResource(path);
    if (part.isEmpty()) {
      return {};
    }
    if (!text.isEmpty()) {
      text += QStringLiteral("\n\n") + QString(72, QLatin1Char('-')) + QStringLiteral("\n\n");
    }
    text += part;
  }
  return text;
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), m_entries(licenseEntries()) {
  setWindowTitle(tr("About %1").arg(QString::fromUtf8(core::kAppName)));
  resize(720, 520);

  auto* tabs = new QTabWidget(this);

  // --- About ---
  auto* about = new QWidget(tabs);
  auto* aboutLayout = new QVBoxLayout(about);
  aboutLayout->addStretch(1);
  const QIcon icon(QStringLiteral(":/icons/stepcomplete.svg"));
  if (!icon.isNull()) {
    auto* iconLabel = new QLabel(about);
    iconLabel->setPixmap(icon.pixmap(96, 96));
    iconLabel->setAlignment(Qt::AlignCenter);
    aboutLayout->addWidget(iconLabel);
  }
  auto* title = new QLabel(QStringLiteral("<h2>%1 %2</h2>")
                               .arg(QString::fromUtf8(core::kAppName).toHtmlEscaped(),
                                    QString::fromUtf8(core::kAppVersion).toHtmlEscaped()),
                           about);
  title->setAlignment(Qt::AlignCenter);
  aboutLayout->addWidget(title);
  auto* description = new QLabel(QString::fromUtf8(core::kAppDescription), about);
  description->setAlignment(Qt::AlignCenter);
  description->setWordWrap(true);
  aboutLayout->addWidget(description);
  auto* license = new QLabel(tr("Licensed under the MIT License."), about);
  license->setAlignment(Qt::AlignCenter);
  aboutLayout->addWidget(license);
  auto* link = new QLabel(QStringLiteral("<a href=\"https://github.com/brsnell/step-complete\">"
                                         "https://github.com/brsnell/step-complete</a>"),
                          about);
  link->setOpenExternalLinks(true);
  link->setTextInteractionFlags(Qt::TextBrowserInteraction);
  link->setAlignment(Qt::AlignCenter);
  aboutLayout->addWidget(link);
  aboutLayout->addStretch(1);
  tabs->addTab(about, tr("About"));

  // --- System info ---
  auto* system = new QWidget(tabs);
  auto* systemLayout = new QVBoxLayout(system);
  m_systemText = new QPlainTextEdit(system);
  m_systemText->setReadOnly(true);
  m_systemText->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_systemText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  systemLayout->addWidget(m_systemText);
  auto* copy = new QPushButton(tr("Copy to clipboard"), system);
  copy->setObjectName(QStringLiteral("copySystemInfoButton"));
  connect(copy, &QPushButton::clicked, this, &AboutDialog::copySystemInfo);
  auto* copyRow = new QHBoxLayout();
  copyRow->addStretch(1);
  copyRow->addWidget(copy);
  systemLayout->addLayout(copyRow);
  tabs->addTab(system, tr("System info"));
  refreshSystemInfo();

  // --- Licenses ---
  auto* licenses = new QWidget(tabs);
  auto* licensesLayout = new QVBoxLayout(licenses);
  auto* splitter = new QSplitter(Qt::Horizontal, licenses);
  m_licenseList = new QListWidget(splitter);
  for (const LicenseEntry& entry : m_entries) {
    m_licenseList->addItem(entry.title);
  }
  m_licenseText = new QPlainTextEdit(splitter);
  m_licenseText->setReadOnly(true);
  m_licenseText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({220, 480});
  licensesLayout->addWidget(splitter, 1);
  m_licenseSource = new QLabel(licenses);
  m_licenseSource->setOpenExternalLinks(true);
  m_licenseSource->setTextInteractionFlags(Qt::TextBrowserInteraction);
  licensesLayout->addWidget(m_licenseSource);
  m_licenseNote = new QLabel(licenses);
  m_licenseNote->setWordWrap(true);
  licensesLayout->addWidget(m_licenseNote);
  tabs->addTab(licenses, tr("Licenses"));
  connect(m_licenseList, &QListWidget::currentRowChanged, this, &AboutDialog::showLicense);
  m_licenseList->setCurrentRow(0);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(tabs);
  layout->addWidget(buttons);
}

void AboutDialog::setGraphicsInfo(const QString& info) {
  m_graphicsInfo = info;
  refreshSystemInfo();
}

QString AboutDialog::systemInfo() const {
  QString text;
  text +=
      QStringLiteral("%1 %2\n").arg(QString::fromUtf8(core::kAppName), QString::fromUtf8(core::kAppVersion));
  text += QStringLiteral("Qt (runtime): %1\n").arg(QString::fromLatin1(qVersion()));
  text += QStringLiteral("Qt (build): %1\n").arg(QStringLiteral(QT_VERSION_STR));
  text += QStringLiteral("Open CASCADE: %1\n").arg(QString::fromStdString(core::occtVersion()));
  text += QStringLiteral("OS: %1\n").arg(QSysInfo::prettyProductName());
  text += QStringLiteral("Kernel: %1 %2\n").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
  text += QStringLiteral("CPU architecture: %1\n").arg(QSysInfo::currentCpuArchitecture());
  text += QStringLiteral("\nGraphics:\n");
  text += m_graphicsInfo.isEmpty() ? QStringLiteral("(not available)") : m_graphicsInfo;
  if (!text.endsWith(QLatin1Char('\n'))) {
    text += QLatin1Char('\n');
  }
  return text;
}

void AboutDialog::refreshSystemInfo() {
  if (m_systemText != nullptr) {
    m_systemText->setPlainText(systemInfo());
  }
}

void AboutDialog::copySystemInfo() {
  if (QClipboard* clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(systemInfo());
  }
}

int AboutDialog::licenseCount() const {
  return static_cast<int>(m_entries.size());
}

void AboutDialog::selectLicense(int index) {
  m_licenseList->setCurrentRow(index);
}

QString AboutDialog::shownLicenseText() const {
  return m_licenseText->toPlainText();
}

void AboutDialog::showLicense(int index) {
  if (index < 0 || index >= m_entries.size()) {
    return;
  }
  const LicenseEntry& entry = m_entries.at(index);
  const QString text = licenseText(entry);
  m_licenseText->setPlainText(text.isEmpty() ? tr("The license text could not be loaded.") : text);
  m_licenseSource->setText(tr("Source: <a href=\"%1\">%1</a>").arg(entry.sourceUrl));
  m_licenseNote->setText(tr("This library is dynamically linked; you may replace it with your own compatible "
                            "build, as the LGPL permits."));
  m_licenseNote->setVisible(entry.replaceable);
}

} // namespace sc::app
