#include "app/MainWindow.h"

#include "core/OcctInfo.h"
#include "core/Version.h"
#include "render/OcctViewport.h"
#include "render/SceneStyle.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

namespace sc::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QString::fromUtf8(core::kAppName));
  resize(1100, 720);

  m_viewport = new render::OcctViewport(this);
  setCentralWidget(m_viewport);
  connect(m_viewport, &render::OcctViewport::glInitialized, this, &MainWindow::onGlInitialized);

  m_glStatus = new QLabel(this);
  statusBar()->addPermanentWidget(m_glStatus);

  createActions();
}

void MainWindow::createActions() {
  // --- File -----------------------------------------------------------------
  QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
  QAction* quit = fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit);
  quit->setShortcut(QKeySequence::Quit);

  // --- View -----------------------------------------------------------------
  QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

  QAction* fit = viewMenu->addAction(tr("&Fit All"), m_viewport, &render::OcctViewport::fitAll);
  fit->setShortcut(Qt::Key_F);

  QMenu* standardViews = viewMenu->addMenu(tr("&Standard Views"));
  struct NamedView {
    const char* label;
    V3d_TypeOfOrientation orientation;
    Qt::Key key;
  };
  const NamedView views[] = {
      {QT_TR_NOOP("&Front"), V3d_Yneg, Qt::Key_1},
      {QT_TR_NOOP("&Back"), V3d_Ypos, Qt::Key_2},
      {QT_TR_NOOP("&Top"), V3d_Zpos, Qt::Key_3},
      {QT_TR_NOOP("B&ottom"), V3d_Zneg, Qt::Key_4},
      {QT_TR_NOOP("&Left"), V3d_Xneg, Qt::Key_5},
      {QT_TR_NOOP("&Right"), V3d_Xpos, Qt::Key_6},
      {QT_TR_NOOP("&Isometric"), V3d_XposYnegZpos, Qt::Key_0},
  };
  for (const NamedView& v : views) {
    QAction* action = standardViews->addAction(tr(v.label));
    action->setShortcut(v.key);
    const V3d_TypeOfOrientation orientation = v.orientation;
    connect(action, &QAction::triggered, this,
            [this, orientation] { m_viewport->setStandardView(orientation); });
  }

  QAction* perspective = viewMenu->addAction(tr("&Perspective Projection"));
  perspective->setCheckable(true);
  perspective->setShortcut(Qt::Key_P);
  connect(perspective, &QAction::toggled, m_viewport, &render::OcctViewport::setPerspective);

  viewMenu->addSeparator();
  m_styleGroup = new QActionGroup(this);
  struct NamedStyle {
    const char* label;
    render::DisplayStyle style;
    Qt::Key key;
  };
  const NamedStyle styles[] = {
      {QT_TR_NOOP("S&haded"), render::DisplayStyle::Shaded, Qt::Key_S},
      {QT_TR_NOOP("Shaded with &Edges"), render::DisplayStyle::ShadedWithEdges, Qt::Key_E},
      {QT_TR_NOOP("&Wireframe"), render::DisplayStyle::Wireframe, Qt::Key_W},
  };
  for (const NamedStyle& s : styles) {
    QAction* action = viewMenu->addAction(tr(s.label));
    action->setCheckable(true);
    action->setShortcut(s.key);
    action->setChecked(s.style == render::DisplayStyle::ShadedWithEdges);
    m_styleGroup->addAction(action);
    const render::DisplayStyle style = s.style;
    connect(action, &QAction::triggered, this, [this, style] {
      render::applyDisplayStyle(m_viewport->context(), style);
      m_viewport->invalidateScene();
    });
  }

  viewMenu->addSeparator();
  QAction* antialiasing = viewMenu->addAction(tr("&Antialiasing (4x MSAA)"));
  antialiasing->setCheckable(true);
  connect(antialiasing, &QAction::toggled, this, [this](bool on) { m_viewport->setMsaaSamples(on ? 4 : 0); });

  QAction* frameRate = viewMenu->addAction(tr("Show Frame &Rate"));
  frameRate->setCheckable(true);
  connect(frameRate, &QAction::toggled, m_viewport, &render::OcctViewport::setShowFrameRate);

  // --- Help -----------------------------------------------------------------
  QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(tr("&Graphics Information..."), this, &MainWindow::showGraphicsInfo);
  helpMenu->addSeparator();
  helpMenu->addAction(tr("&About %1").arg(QString::fromUtf8(core::kAppName)), this, &MainWindow::showAbout);
  helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::onGlInitialized(bool ok, const QString& message) {
  if (!ok) {
    m_glStatus->setText(tr("3D view unavailable"));
    QString details = message;
#ifdef __linux__
    details += tr("\n\nTry starting with --software-gl to use software rendering.");
#endif
    QMessageBox::critical(this, tr("Graphics Error"), details);
    return;
  }

  m_glStatus->setText(message);
  if (!m_demoShown) {
    // Placeholder geometry until STEP import lands in M1.
    m_demoShown = true;
    m_viewport->context()->Display(render::makeDemoPart(), false);
    m_viewport->fitAll();
    statusBar()->showMessage(tr("Showing demo part. STEP import arrives in milestone M1."), 8000);
  }
}

void MainWindow::showGraphicsInfo() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Graphics Information"));
  dialog.resize(560, 420);

  auto* text = new QPlainTextEdit(&dialog);
  text->setReadOnly(true);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  const QString info = m_viewport->isGlReady()
                           ? m_viewport->glInfo()
                           : tr("OpenGL was not initialized; the 3D view is unavailable.");
  text->setPlainText(info);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
  connect(copy, &QPushButton::clicked, &dialog, [info] { QGuiApplication::clipboard()->setText(info); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(text);
  layout->addWidget(buttons);
  dialog.exec();
}

void MainWindow::showAbout() {
  const QString text = tr("<h3>%1 %2</h3>"
                          "<p>%3</p>"
                          "<p>Licensed under the MIT License.</p>"
                          "<p>Built with:<br>"
                          "Open CASCADE Technology %4 (LGPL 2.1 with exception)<br>"
                          "Qt %5 (LGPL 3)</p>"
                          "<p>Qt and Open CASCADE are dynamically linked and can be replaced. "
                          "See THIRD_PARTY_NOTICES.md for full license texts and sources.</p>")
                           .arg(QString::fromUtf8(core::kAppName), QString::fromUtf8(core::kAppVersion),
                                QString::fromUtf8(core::kAppDescription),
                                QString::fromStdString(core::occtVersion()), QString::fromLatin1(qVersion()));
  QMessageBox::about(this, tr("About %1").arg(QString::fromUtf8(core::kAppName)), text);
}

} // namespace sc::app
