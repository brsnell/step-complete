// SectionPanel against a fake SectionModel that mimics SectionTool's behavior
// (no viewport, so no display or OpenGL is needed).

#include "app/SectionModel.h"
#include "app/SectionPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>

#include <algorithm>
#include <array>
#include <memory>

using sc::app::SectionModel;
using sc::app::SectionPanel;
using sc::render::SectionPlane;

namespace {

// Same semantics as SectionTool for a box [0,x] x [0,y] x [0,z].
class FakeSectionModel : public SectionModel {
public:
  FakeSectionModel() {
    _planes[0].normal = gp_Dir(1, 0, 0);
    _planes[1].normal = gp_Dir(0, 1, 0);
    _planes[2].normal = gp_Dir(0, 0, 1);
  }
  void setBox(double x, double y, double z) {
    _size = gp_XYZ(x, y, z);
    for (int i = 0; i < 3; ++i) {
      const auto [low, high] = offsetRange(i);
      _planes[static_cast<size_t>(i)].offset = (low + high) / 2.0;
      Q_EMIT planeChanged(i);
    }
  }
  bool isActive() const {
    return std::any_of(_planes.begin(), _planes.end(), [](const SectionPlane& p) { return p.enabled; });
  }
  const SectionPlane& plane(int i) const override { return _planes.at(static_cast<size_t>(i)); }
  void setPlane(int i, const SectionPlane& newPlane) override {
    _planes.at(static_cast<size_t>(i)) = newPlane;
    Q_EMIT planeChanged(i);
  }
  std::pair<double, double> offsetRange(int i) const override {
    const gp_XYZ normal = plane(i).normal.XYZ();
    double low = 0.0, high = 0.0;
    for (const double extent : {normal.X() * _size.X(), normal.Y() * _size.Y(), normal.Z() * _size.Z()}) {
      (extent < 0 ? low : high) += extent;
    }
    return {low, high};
  }
  void flip(int i) override {
    SectionPlane& storedPlane = _planes.at(static_cast<size_t>(i));
    storedPlane.normal.Reverse();
    storedPlane.offset = -storedPlane.offset;
    Q_EMIT planeChanged(i);
  }
  void reset() override {
    for (int i = 0; i < 3; ++i) {
      _planes[static_cast<size_t>(i)].enabled = false;
      Q_EMIT planeChanged(i);
    }
  }
  bool handlesVisible() const override { return _handles; }
  void setHandlesVisible(bool v) override { _handles = v; }
  void pickFace(int i) override {
    _picking = i;
    Q_EMIT pickChanged();
  }
  void cancelPick() override {
    _picking = -1;
    Q_EMIT pickChanged();
  }
  int pickingPlane() const override { return _picking; }
  // What SectionTool does when the pick lands on a flat face.
  void finishPick(const gp_Dir& normal, double offset) {
    const int i = _picking;
    _picking = -1;
    Q_EMIT pickChanged();
    SectionPlane picked = plane(i);
    picked.enabled = true;
    picked.normal = normal;
    picked.offset = offset;
    setPlane(i, picked);
  }

private:
  std::array<SectionPlane, 3> _planes;
  gp_XYZ _size{100, 60, 20};
  bool _handles = true;
  int _picking = -1;
};

} // namespace

class SectionPanelTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<FakeSectionModel> _tool;
  std::unique_ptr<SectionPanel> _panel;
  std::unique_ptr<QSignalSpy> _changed;

  QCheckBox* byText(int row, const QString& text) const {
    const QList<QCheckBox*> all = _panel->findChildren<QCheckBox*>();
    int seen = -1;
    for (QCheckBox* b : all) {
      if (b->text() == text) {
        if (++seen == row) {
          return b;
        }
      }
    }
    return nullptr;
  }
  QCheckBox* enable(int row) const { return byText(0, QStringLiteral("Plane %1").arg(row + 1)); }
  QCheckBox* cap(int row) const { return byText(row, QStringLiteral("Cap")); }
  QCheckBox* hatch(int row) const { return byText(row, QStringLiteral("Hatch")); }
  QComboBox* axis(int row) const { return _panel->findChildren<QComboBox*>().at(row); }
  QSlider* slider(int row) const { return _panel->findChildren<QSlider*>().at(row); }
  QDoubleSpinBox* spin(int row) const {
    return _panel->findChildren<QDoubleSpinBox*>(QStringLiteral("sectionOffset")).at(row);
  }
  QDoubleSpinBox* normal(int row, char axisName) const {
    return _panel->findChildren<QDoubleSpinBox*>(QStringLiteral("sectionNormal%1").arg(QLatin1Char(axisName)))
        .at(row);
  }
  QPushButton* pick(int row) const {
    return _panel->findChildren<QPushButton*>(QStringLiteral("sectionPickFace")).at(row);
  }
  QPushButton* flip(int row) const {
    int seen = -1;
    for (QPushButton* b : _panel->findChildren<QPushButton*>()) {
      if (b->text() == QStringLiteral("Flip") && ++seen == row) {
        return b;
      }
    }
    return nullptr;
  }
  QPushButton* resetButton() const {
    for (QPushButton* b : _panel->findChildren<QPushButton*>()) {
      if (b->text() == QStringLiteral("Reset")) {
        return b;
      }
    }
    return nullptr;
  }

private Q_SLOTS:
  void init() {
    _tool = std::make_unique<FakeSectionModel>();
    _tool->setBox(100.0, 60.0, 20.0); // offsets span 0..100 (X), 0..60 (Y), 0..20 (Z)
    _panel = std::make_unique<SectionPanel>(static_cast<SectionModel*>(_tool.get()));
    _changed = std::make_unique<QSignalSpy>(_panel.get(), &SectionPanel::sectionChanged);
  }

  void cleanup() {
    _changed.reset();
    _panel.reset();
    _tool.reset();
  }

  void initialStateMirrorsTool() {
    for (int i = 0; i < 3; ++i) {
      QVERIFY(!enable(i)->isChecked());
      QVERIFY(!cap(i)->isHidden());
      QCOMPARE(axis(i)->currentIndex(), i);
      QCOMPARE(axis(i)->count(), 4);                       // X, Y, Z and Custom
      QVERIFY(!normal(i, 'X')->isVisibleTo(_panel.get())); // the custom fields show only for Custom
    }
    QCOMPARE(spin(0)->value(), 50.0); // setBounds centers the planes
    QCOMPARE(slider(0)->value(), SectionPanel::SliderSteps / 2);
    QCOMPARE(_changed->count(), 0);
  }

  void controlsDisabledWhenPlaneOff() {
    QVERIFY(!axis(0)->isEnabled());
    QVERIFY(!slider(0)->isEnabled());
    QVERIFY(!spin(0)->isEnabled());
    QVERIFY(!flip(0)->isEnabled());
    QVERIFY(!cap(0)->isEnabled());
    QVERIFY(!hatch(0)->isEnabled());
    QVERIFY(enable(0)->isEnabled());

    enable(0)->setChecked(true);
    QVERIFY(axis(0)->isEnabled());
    QVERIFY(slider(0)->isEnabled());
    QVERIFY(spin(0)->isEnabled());
    QVERIFY(flip(0)->isEnabled());
    QVERIFY(cap(0)->isEnabled());
    QVERIFY(hatch(0)->isEnabled());
    // Other rows stay off.
    QVERIFY(!slider(1)->isEnabled());

    cap(0)->setChecked(false); // hatching needs caps
    QVERIFY(!hatch(0)->isEnabled());
  }

  void enableCheckboxEnablesPlane() {
    QVERIFY(!_tool->plane(1).enabled);
    enable(1)->setChecked(true);
    QVERIFY(_tool->plane(1).enabled);
    QVERIFY(_tool->isActive());
    QCOMPARE(_changed->count(), 1);
    enable(1)->setChecked(false);
    QVERIFY(!_tool->plane(1).enabled);
    QCOMPARE(_changed->count(), 2);
  }

  void sliderSetsOffsetAndSyncsSpin() {
    enable(0)->setChecked(true);
    _changed->clear();
    slider(0)->setValue(250); // 25 % of 0..100
    QVERIFY(qFuzzyCompare(_tool->plane(0).offset, 25.0));
    QCOMPARE(spin(0)->value(), 25.0);
    QCOMPARE(_changed->count(), 1);
  }

  void spinSetsOffsetAndSyncsSlider() {
    enable(0)->setChecked(true);
    _changed->clear();
    spin(0)->setValue(80.0);
    QVERIFY(qFuzzyCompare(_tool->plane(0).offset, 80.0));
    QCOMPARE(slider(0)->value(), 800);
    QCOMPARE(_changed->count(), 1);
  }

  void axisComboChangesNormal() {
    enable(0)->setChecked(true);
    _changed->clear();
    axis(0)->setCurrentIndex(2);
    axis(0)->activated(2);
    const gp_Dir normal = _tool->plane(0).normal;
    QVERIFY(normal.IsEqual(gp_Dir(0, 0, 1), 1e-9));
    QCOMPARE(_changed->count() >= 1, true);
    // Offset 50 does not cross the 20 mm tall model along Z: pulled into range.
    QVERIFY(_tool->plane(0).offset <= 20.0 + 1e-9);
    QCOMPARE(spin(0)->maximum(), 20.0);
    QCOMPARE(axis(0)->currentIndex(), 2);
  }

  void flipReversesNormalKeepingPlaneInPlace() {
    enable(0)->setChecked(true);
    spin(0)->setValue(30.0);
    const gp_Pnt before = _tool->plane(0).plane().Location();
    _changed->clear();
    flip(0)->click();
    const SectionPlane plane = _tool->plane(0);
    QVERIFY(plane.normal.IsEqual(gp_Dir(-1, 0, 0), 1e-9));
    QVERIFY(plane.plane().Location().Distance(before) < 1e-9);
    QCOMPARE(spin(0)->value(), -30.0);
    QCOMPARE(axis(0)->currentIndex(), 0); // still the X axis
    QCOMPARE(_changed->count(), 1);
  }

  void capAndHatchToggles() {
    enable(0)->setChecked(true);
    QVERIFY(_tool->plane(0).capping);
    QVERIFY(_tool->plane(0).hatch);
    hatch(0)->setChecked(false);
    QVERIFY(!_tool->plane(0).hatch);
    QVERIFY(_tool->plane(0).capping);
    cap(0)->setChecked(false);
    QVERIFY(!_tool->plane(0).capping);
    cap(0)->setChecked(true);
    hatch(0)->setChecked(true);
    QVERIFY(_tool->plane(0).capping);
    QVERIFY(_tool->plane(0).hatch);
  }

  void toolDrivenChangeUpdatesWidgetsSilently() {
    SectionPlane plane = _tool->plane(2);
    plane.enabled = true;
    plane.offset = 5.0;
    plane.capping = false;
    plane.hatch = false;
    _tool->setPlane(2, plane);
    QVERIFY(enable(2)->isChecked());
    QVERIFY(slider(2)->isEnabled());
    QCOMPARE(spin(2)->value(), 5.0);
    QCOMPARE(slider(2)->value(), 250);
    QVERIFY(!cap(2)->isChecked());
    QVERIFY(!hatch(2)->isChecked());
    QCOMPARE(_changed->count(), 0);

    // A non-axis normal shows "Custom" with its fields; going back to an axis shows the axis.
    plane.normal = gp_Dir(1, 1, 0);
    _tool->setPlane(2, plane);
    QCOMPARE(axis(2)->currentText(), QStringLiteral("Custom"));
    QVERIFY(normal(2, 'X')->isVisibleTo(_panel.get()));
    QCOMPARE(normal(2, 'X')->value(), 0.707);
    plane.normal = gp_Dir(0, 1, 0);
    _tool->setPlane(2, plane);
    QCOMPARE(axis(2)->currentIndex(), 1);
    QVERIFY(!normal(2, 'X')->isVisibleTo(_panel.get()));
    QCOMPARE(_changed->count(), 0);
  }

  void showHandlesCheckbox() {
    QVERIFY(_tool->handlesVisible());
    QCheckBox* box = byText(0, QStringLiteral("Show handles"));
    QVERIFY(box != nullptr);
    QVERIFY(box->isChecked());
    box->setChecked(false);
    QVERIFY(!_tool->handlesVisible());
    QCOMPARE(_changed->count(), 1);
  }

  void resetClearsEverything() {
    enable(0)->setChecked(true);
    enable(2)->setChecked(true);
    QVERIFY(_tool->isActive());
    _changed->clear();
    resetButton()->click();
    QVERIFY(!_tool->isActive());
    for (int i = 0; i < 3; ++i) {
      QVERIFY(!enable(i)->isChecked());
      QVERIFY(!slider(i)->isEnabled());
    }
    QCOMPARE(_changed->count(), 1);
  }

  void refreshRereadsRanges() {
    _tool->setBox(10.0, 10.0, 10.0); // emits planeChanged: panel follows on its own
    QCOMPARE(spin(0)->maximum(), 10.0);
    QCOMPARE(spin(0)->value(), 5.0);
    _panel->refresh();
    QCOMPARE(spin(0)->value(), 5.0);
    QCOMPARE(_changed->count(), 0);
  }

  // --- Section planes at any angle (docs/designs/angled-section-planes.md) ---------------

  void customShowsTheNormalFieldsAndSetsANormalizedDirection() {
    enable(0)->setChecked(true);
    axis(0)->setCurrentIndex(3);
    axis(0)->activated(3);
    QVERIFY(normal(0, 'X')->isVisibleTo(_panel.get()));
    QVERIFY(_tool->plane(0).normal.IsEqual(gp_Dir(1, 0, 0), 1e-9)); // unchanged until edited
    QCOMPARE(axis(0)->currentText(), QStringLiteral("Custom"));
    _changed->clear();
    normal(0, 'Y')->setValue(1.0); // (1, 1, 0)
    QVERIFY(_tool->plane(0).normal.IsEqual(gp_Dir(1, 1, 0), 1e-9));
    QCOMPARE(_changed->count() >= 1, true);
    QCOMPARE(axis(0)->currentText(), QStringLiteral("Custom"));
    // The offset stays inside the range along the new direction.
    const auto [low, high] = _tool->offsetRange(0);
    QVERIFY(_tool->plane(0).offset >= low - 1e-9 && _tool->plane(0).offset <= high + 1e-9);

    // Choosing an axis again hides the fields.
    axis(0)->setCurrentIndex(2);
    axis(0)->activated(2);
    QVERIFY(_tool->plane(0).normal.IsEqual(gp_Dir(0, 0, 1), 1e-9));
    QVERIFY(!normal(0, 'X')->isVisibleTo(_panel.get()));
  }

  void aZeroNormalIsRefused() {
    enable(1)->setChecked(true);
    axis(1)->setCurrentIndex(3);
    axis(1)->activated(3);
    _changed->clear();
    normal(1, 'Y')->setValue(0.0); // (0, 0, 0): no direction
    QVERIFY(_tool->plane(1).normal.IsEqual(gp_Dir(0, 1, 0), 1e-9));
    QCOMPARE(normal(1, 'Y')->value(), 1.0); // the field is put back
    QCOMPARE(_changed->count(), 0);
  }

  void pickFaceAsksTheModelAndShowsThePickedPlane() {
    QVERIFY(!enable(2)->isChecked());
    QVERIFY(pick(2)->isEnabled()); // usable while the plane is off
    pick(2)->click();
    QCOMPARE(_tool->pickingPlane(), 2);
    QVERIFY(pick(2)->isChecked());
    QVERIFY(!pick(0)->isChecked());
    pick(2)->click(); // again: cancel
    QCOMPARE(_tool->pickingPlane(), -1);
    QVERIFY(!pick(2)->isChecked());

    pick(2)->click();
    _tool->finishPick(gp_Dir(0, 1, 1), 10.0);
    QVERIFY(!pick(2)->isChecked());
    QVERIFY(enable(2)->isChecked());
    QCOMPARE(axis(2)->currentText(), QStringLiteral("Custom"));
    QCOMPARE(normal(2, 'Y')->value(), 0.707);
    QCOMPARE(normal(2, 'Z')->value(), 0.707);
    QCOMPARE(spin(2)->value(), 10.0);
  }

  void grabScreenshot() {
    const QString dir = qEnvironmentVariable("SC_PANEL_DUMP_DIR");
    if (dir.isEmpty()) {
      return;
    }
    enable(0)->setChecked(true);
    spin(0)->setValue(30.0);
    enable(1)->setChecked(true);
    flip(1)->click();
    SectionPlane plane = _tool->plane(2);
    plane.enabled = true;
    plane.normal = gp_Dir(1, 1, 0);
    _tool->setPlane(2, plane);
    _panel->resize(320, _panel->sizeHint().height());
    _panel->show();
    QVERIFY(QTest::qWaitForWindowExposed(_panel.get()));
    QDir().mkpath(dir);
    QVERIFY(_panel->grab().save(QDir(dir).filePath(QStringLiteral("section_panel.png"))));
  }
};

int runSectionPanelTests(int argc, char** argv) {
  SectionPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_section_panel.moc"
