#pragma once

#include "core/OccHandle.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QOpenGLWidget>
#include <QString>

class AIS_ViewCube;

namespace sc::render {

// 3D viewport: OCCT's V3d viewer rendering inside a QOpenGLWidget.
//
// Qt owns the OpenGL context and the framebuffer object. OCCT wraps both
// (OpenGl_Context::Init on the current context, OpenGl_FrameBuffer::InitWrapper
// on Qt's FBO) and draws into them. This lets the viewport live inside normal
// widget layouts and docks, and handle HiDPI correctly, which the older
// "native window handle" integration cannot do.
//
// Input goes through AIS_ViewController, so OCCT's navigation, selection,
// highlighting and view animation all work as in OCCT's own viewers.
// Redraws happen only on demand (input, animation, scene changes).
class OcctViewport : public QOpenGLWidget, public AIS_ViewController {
  Q_OBJECT

public:
  explicit OcctViewport(QWidget* parent = nullptr);
  ~OcctViewport() override;

  OcctViewport(const OcctViewport&) = delete;
  OcctViewport& operator=(const OcctViewport&) = delete;

  // Must be called before the QApplication is constructed.
  static void configureDefaultSurfaceFormat();

  const OccHandle<V3d_Viewer>& viewer() const { return m_viewer; }
  const OccHandle<V3d_View>& view() const { return m_view; }
  const OccHandle<AIS_InteractiveContext>& context() const { return m_context; }

  // True once OCCT has successfully wrapped Qt's OpenGL context.
  bool isGlReady() const { return m_glReady; }
  // Multi-line driver/renderer/version report, for the Graphics Information dialog.
  QString glInfo() const { return m_glInfo; }
  // One-line summary, e.g. "OpenGL 4.6 · Mesa Intel(R) HD Graphics 520".
  QString glSummary() const { return m_glSummary; }

  // Schedules a full redraw. Call after changing displayed objects (styles,
  // visibility, new shapes). Plain update() only redraws OCCT's immediate layer
  // (hover highlight), which is what keeps hovering cheap on large models.
  void invalidateScene();

  void fitAll();
  void setStandardView(V3d_TypeOfOrientation orientation);
  void setPerspective(bool enabled);
  bool isPerspective() const;
  void setMsaaSamples(int samples);
  int msaaSamples() const;
  void setShowFrameRate(bool enabled);

  QSize minimumSizeHint() const override { return {200, 150}; }

Q_SIGNALS:
  void glInitialized(bool ok, const QString& message);
  void selectionChanged();

protected:
  void initializeGL() override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  // AIS_ViewController
  void handleViewRedraw(const OccHandle<AIS_InteractiveContext>& context,
                        const OccHandle<V3d_View>& view) override;
  void OnSelectionChanged(const OccHandle<AIS_InteractiveContext>& context,
                          const OccHandle<V3d_View>& view) override;

private:
  Graphic3d_Vec2i toViewPixels(const QPointF& logicalPos) const;
  void configureNavigation();
  void collectGlInfo(int glMajor, int glMinor);
  void failGl(const QString& message);

  OccHandle<V3d_Viewer> m_viewer;
  OccHandle<V3d_View> m_view;
  OccHandle<AIS_InteractiveContext> m_context;
  OccHandle<AIS_ViewCube> m_viewCube;

  QString m_glInfo;
  QString m_glSummary;
  bool m_glReady = false;
  bool m_glFailed = false;
};

} // namespace sc::render
