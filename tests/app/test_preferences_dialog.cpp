// PreferencesDialog and ExportImageDialog: settings round trip and option handling.
// Runs on the offscreen platform (no display or OpenGL needed).

#include "app/ExportImageDialog.h"
#include "app/PreferencesDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

Q_DECLARE_METATYPE(sc::app::ViewPreferences)

using sc::app::ExportImageDialog;
using sc::app::PreferencesDialog;
using sc::app::ViewPreferences;
using sc::render::BackgroundStyle;
using sc::render::ExportBackground;
using sc::render::LightingPreset;

class PreferencesDialogTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir m_settingsDir;

private Q_SLOTS:
  void initTestCase() {
    // Keep the developer's real settings out of the test.
    QCoreApplication::setOrganizationName(QStringLiteral("StepCompleteTest"));
    QCoreApplication::setApplicationName(QStringLiteral("preferences-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
  }

  void init() { QSettings().clear(); }

  void defaultsMatchTheBuiltInLook() {
    const ViewPreferences prefs = ViewPreferences::load();
    QCOMPARE(prefs.background.kind, BackgroundStyle::Kind::Gradient);
    QCOMPARE(prefs.background.top, QColor(230, 235, 242));
    QCOMPARE(prefs.background.bottom, QColor(158, 168, 184));
    QCOMPARE(prefs.lighting, LightingPreset::Default);
    QVERIFY(!prefs.antialiasing);
  }

  void settingsRoundTrip() {
    ViewPreferences prefs;
    prefs.background.kind = BackgroundStyle::Kind::Solid;
    prefs.background.top = QColor(1, 2, 3);
    prefs.background.bottom = QColor(4, 5, 6);
    prefs.background.solid = QColor(7, 8, 9);
    prefs.lighting = LightingPreset::Softer;
    prefs.antialiasing = true;
    prefs.save();
    QCOMPARE(ViewPreferences::load(), prefs);

    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("view/lighting")).toInt(), int(LightingPreset::Softer));
    QVERIFY(settings.value(QStringLiteral("view/antialiasing")).toBool());
  }

  void invalidStoredValuesFallBackToDefaults() {
    QSettings settings;
    settings.setValue(QStringLiteral("view/backgroundStyle"), 99);
    settings.setValue(QStringLiteral("view/backgroundTop"), QStringLiteral("not a color"));
    settings.setValue(QStringLiteral("view/lighting"), -4);
    QCOMPARE(ViewPreferences::load(), ViewPreferences());
  }

  void dialogEditsAreReportedAndRoundTrip() {
    ViewPreferences initial;
    initial.background.kind = BackgroundStyle::Kind::Solid;
    initial.background.solid = QColor(10, 20, 30);

    PreferencesDialog dialog(initial);
    QSignalSpy spy(&dialog, &PreferencesDialog::preferencesChanged);
    QCOMPARE(dialog.preferences(), initial);
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("backgroundStyle"))->currentIndex(), 1);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("solidColor"))->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("gradientTop"))->isEnabled());

    dialog.findChild<QComboBox*>(QStringLiteral("backgroundStyle"))->setCurrentIndex(2); // White
    dialog.findChild<QComboBox*>(QStringLiteral("lighting"))->setCurrentIndex(1);        // Brighter
    dialog.findChild<QCheckBox*>(QStringLiteral("antialiasing"))->setChecked(true);
    QCOMPARE(spy.count(), 3);
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("solidColor"))->isEnabled());

    dialog.accept();
    dialog.preferences().save();
    const ViewPreferences loaded = ViewPreferences::load();
    QCOMPARE(loaded.background.kind, BackgroundStyle::Kind::White);
    QCOMPARE(loaded.background.solid, QColor(10, 20, 30)); // kept while another style is active
    QCOMPARE(loaded.lighting, LightingPreset::Brighter);
    QVERIFY(loaded.antialiasing);

    // Re-opening shows what was saved.
    PreferencesDialog reopened(loaded);
    QCOMPARE(reopened.findChild<QComboBox*>(QStringLiteral("backgroundStyle"))->currentIndex(), 2);
    QCOMPARE(reopened.findChild<QComboBox*>(QStringLiteral("lighting"))->currentIndex(), 1);
    QVERIFY(reopened.findChild<QCheckBox*>(QStringLiteral("antialiasing"))->isChecked());
  }

  void cancelRestoresTheOriginalValues() {
    const ViewPreferences initial;
    PreferencesDialog dialog(initial);
    QSignalSpy spy(&dialog, &PreferencesDialog::preferencesChanged);
    dialog.findChild<QCheckBox*>(QStringLiteral("antialiasing"))->setChecked(true);
    QCOMPARE(spy.count(), 1);
    dialog.reject();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).value<ViewPreferences>(), initial);
    QCOMPARE(dialog.preferences(), initial);
  }

  void exportDialogSizeAndBackground() {
    ExportImageDialog dialog(QSize(800, 400), 8192, true);
    QCOMPARE(dialog.imageSize(), QSize(800, 400));
    QCOMPARE(dialog.background(), ExportBackground::AsShown);

    auto spins = dialog.findChildren<QSpinBox*>();
    QCOMPARE(spins.size(), 2);
    QVERIFY(!spins[0]->isEnabled()); // custom size is off
    dialog.findChildren<QRadioButton*>().at(1)->setChecked(true);
    spins[0]->setValue(1600);
    QCOMPARE(spins[1]->value(), 800); // aspect ratio kept
    QCOMPARE(dialog.imageSize(), QSize(1600, 800));
    spins[0]->setMaximum(8192);
    spins[0]->setValue(100000);
    QCOMPARE(dialog.imageSize().width(), 8192);

    auto* background = dialog.findChild<QComboBox*>();
    QCOMPARE(background->count(), 3);
    background->setCurrentIndex(2);
    QCOMPARE(dialog.background(), ExportBackground::Transparent);

    ExportImageDialog jpeg(QSize(800, 400), 8192, false);
    QCOMPARE(jpeg.findChild<QComboBox*>()->count(), 2); // no transparency for JPEG
  }
};

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  PreferencesDialogTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_preferences_dialog.moc"
