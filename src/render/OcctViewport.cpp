#include "render/OcctViewport.h"

#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Message.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>

#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

#ifdef __APPLE__
// macOS only offers OpenGL > 2.1 through a core profile (4.1 on all supported Macs).
constexpr bool kCoreProfile = true;
#else
// Compatibility profile gives the highest version the driver supports and the
// widest support on older GPUs and drivers.
constexpr bool kCoreProfile = false;
#endif

// OCCT's hard minimum.
constexpr int kMinGlMajor = 2;
constexpr int kMinGlMinor = 1;

// Qt's FBO is linear RGB; tell OCCT not to expect an sRGB framebuffer.
class QtFrameBufferWrapper : public OpenGl_FrameBuffer {
public:
  void BindBuffer(const OccHandle<OpenGl_Context>& glCtx) override {
    OpenGl_FrameBuffer::BindBuffer(glCtx);
    glCtx->SetFrameBufferSRGB(true, false);
  }
  void BindDrawBuffer(const OccHandle<OpenGl_Context>& glCtx) override {
    OpenGl_FrameBuffer::BindDrawBuffer(glCtx);
    glCtx->SetFrameBufferSRGB(true, false);
  }
  void BindReadBuffer(const OccHandle<OpenGl_Context>& glCtx) override {
    OpenGl_FrameBuffer::BindReadBuffer(glCtx);
  }
};

Aspect_VKeyMouse toVKeyMouse(Qt::MouseButtons buttons) {
  Aspect_VKeyMouse result = Aspect_VKeyMouse_NONE;
  if (buttons & Qt::LeftButton) {
    result |= Aspect_VKeyMouse_LeftButton;
  }
  if (buttons & Qt::MiddleButton) {
    result |= Aspect_VKeyMouse_MiddleButton;
  }
  if (buttons & Qt::RightButton) {
    result |= Aspect_VKeyMouse_RightButton;
  }
  return result;
}

Aspect_VKeyFlags toVKeyFlags(Qt::KeyboardModifiers modifiers) {
  Aspect_VKeyFlags result = Aspect_VKeyFlags_NONE;
  if (modifiers & Qt::ShiftModifier) {
    result |= Aspect_VKeyFlags_SHIFT;
  }
  if (modifiers & Qt::ControlModifier) {
    result |= Aspect_VKeyFlags_CTRL;
  }
  if (modifiers & Qt::AltModifier) {
    result |= Aspect_VKeyFlags_ALT;
  }
  return result;
}

// Highlight styles may carry a fill-area aspect that overrides their color on faces.
void setHighlightColor(const OccHandle<Prs3d_Drawer>& style, const Quantity_Color& color) {
  style->SetColor(color);
  const OccHandle<Graphic3d_AspectFillArea3d>& fill = style->BasicFillAreaAspect();
  if (!fill.IsNull()) {
    fill->SetInteriorColor(color);
    Graphic3d_MaterialAspect material = fill->FrontMaterial();
    material.SetColor(color);
    fill->SetFrontMaterial(material);
    fill->SetBackMaterial(material);
  }
}

OccHandle<OpenGl_Context> glContextOf(const OccHandle<V3d_View>& view) {
  OccHandle<OpenGl_View> glView = OccHandle<OpenGl_View>::DownCast(view->View());
  if (glView.IsNull() || glView->GlWindow().IsNull()) {
    return {};
  }
  return glView->GlWindow()->GetGlContext();
}

} // namespace

void OcctViewport::configureDefaultSurfaceFormat() {
  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  if (kCoreProfile) {
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
  } else {
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
  }
  QSurfaceFormat::setDefaultFormat(format);
}

OcctViewport::OcctViewport(QWidget* parent) : QOpenGLWidget(parent) {
  OccHandle<Aspect_DisplayConnection> display = new Aspect_DisplayConnection();
  OccHandle<OpenGl_GraphicDriver> driver = new OpenGl_GraphicDriver(display, false);
  // Qt swaps buffers and owns the framebuffer; OCCT must render into Qt's FBO.
  driver->ChangeOptions().buffersNoSwap = true;
  driver->ChangeOptions().buffersOpaqueAlpha = true;
  driver->ChangeOptions().useSystemBuffer = false;
  driver->ChangeOptions().contextCompatible = !kCoreProfile;

  m_viewer = new V3d_Viewer(driver);
  m_viewer->SetDefaultLights();
  m_viewer->SetLightOn();

  m_context = new AIS_InteractiveContext(m_viewer);
  // Selection: saturated blue; hover: light cyan. Both read well on gray and colored parts.
  const Quantity_Color selectedColor(0.20, 0.50, 0.95, Quantity_TOC_sRGB);
  const Quantity_Color hoverColor(0.55, 0.85, 1.00, Quantity_TOC_sRGB);
  setHighlightColor(m_context->HighlightStyle(Prs3d_TypeOfHighlight_Selected), selectedColor);
  setHighlightColor(m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected), selectedColor);
  setHighlightColor(m_context->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic), hoverColor);
  setHighlightColor(m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic), hoverColor);

  m_view = m_viewer->CreateView();
  m_view->SetImmediateUpdate(false);
  m_view->SetBgGradientColors(Quantity_Color(0.90, 0.92, 0.95, Quantity_TOC_sRGB),
                              Quantity_Color(0.62, 0.66, 0.72, Quantity_TOC_sRGB),
                              Aspect_GradientFillMethod_Vertical, false);

  // Lightweight defaults for 10-year-old integrated GPUs: no MSAA, no stats overlay.
  Graphic3d_RenderingParams& params = m_view->ChangeRenderingParams();
  params.NbMsaaSamples = 0;
  params.ToShowStats = false;

  m_viewCube = new AIS_ViewCube();
  m_viewCube->SetViewAnimation(ViewAnimation());
  m_viewCube->SetFixedAnimationLoop(false);
  m_viewCube->SetAutoStartAnimation(true);

  configureNavigation();

  setMouseTracking(true); // hover highlighting
  setFocusPolicy(Qt::StrongFocus);
  setBackgroundRole(QPalette::NoRole);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

OcctViewport::~OcctViewport() {
  // Keep the X11 display connection alive until Qt's context is current again;
  // releasing it first can crash inside QOpenGLWidget's destructor.
  OccHandle<Aspect_DisplayConnection> display = m_viewer->Driver()->GetDisplayConnection();

  makeCurrent();
  m_context->RemoveAll(false);
  m_context.Nullify();
  m_view->Remove();
  m_view.Nullify();
  m_viewer.Nullify();
  doneCurrent();

  display.Nullify();
}

void OcctViewport::configureNavigation() {
  // Left: orbit. Middle or Shift+Left: pan. Ctrl+Left/Middle: zoom. Wheel: zoom at cursor.
  // Right button is deliberately unbound; it is reserved for context menus.
  AIS_MouseGestureMap& gestures = ChangeMouseGestureMap();
  gestures.Clear();
  gestures.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit);
  gestures.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
  gestures.Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_Pan);
  gestures.Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Zoom);
  gestures.Bind(Aspect_VKeyMouse_MiddleButton | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Zoom);
}

void OcctViewport::initializeGL() {
  if (m_glFailed) {
    return;
  }

  OccHandle<OpenGl_Context> glCtx = new OpenGl_Context();
  if (!glCtx->Init(kCoreProfile)) {
    failGl(tr("OCCT could not use the OpenGL context created by Qt."));
    return;
  }
  if (glCtx->VersionMajor() < kMinGlMajor ||
      (glCtx->VersionMajor() == kMinGlMajor && glCtx->VersionMinor() < kMinGlMinor)) {
    failGl(tr("OpenGL %1.%2 is too old; at least %3.%4 is required.")
               .arg(glCtx->VersionMajor())
               .arg(glCtx->VersionMinor())
               .arg(kMinGlMajor)
               .arg(kMinGlMinor));
    return;
  }

  // The drawable OCCT binds when making the context current.
  // - Windows: the window of the DC Qt made current.
  // - X11: must be a real X Window (OCCT queries its visual; Qt's current drawable
  //   may be a pbuffer). Use the top-level window: calling winId() on this widget
  //   would turn it into a native child window, which breaks Qt's compositing of
  //   every other widget in the window (black menu and status bars).
  // - macOS: the widget's NSView, as in OCCT's reference Qt sample.
  //   TODO(M0): verify on macOS hardware whether window()->winId() is preferable.
#if defined(_WIN32)
  Aspect_Drawable drawable = glCtx->Window();
  if (drawable == 0) {
    drawable = static_cast<Aspect_Drawable>(window()->winId());
  }
#elif defined(__APPLE__)
  const Aspect_Drawable drawable = static_cast<Aspect_Drawable>(winId());
#else
  const Aspect_Drawable drawable = static_cast<Aspect_Drawable>(window()->winId());
#endif

  const qreal dpr = devicePixelRatioF();
  const int widthPx = std::max(1, static_cast<int>(std::lround(width() * dpr)));
  const int heightPx = std::max(1, static_cast<int>(std::lround(height() * dpr)));

  // initializeGL runs again if Qt recreates the context (e.g. moving to another screen).
  OccHandle<Aspect_NeutralWindow> window = OccHandle<Aspect_NeutralWindow>::DownCast(m_view->Window());
  const bool firstInit = window.IsNull();
  if (firstInit) {
    window = new Aspect_NeutralWindow();
    window->SetVirtual(true);
  }
  window->SetNativeHandle(drawable);
  window->SetSize(widthPx, heightPx);
  m_view->SetWindow(window, glCtx->RenderingContext());

  if (firstInit) {
    m_viewCube->SetSize(55.0 * dpr, false);
    m_viewCube->SetFontHeight(12.0 * dpr);
    m_viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
                                    Graphic3d_Vec2i(static_cast<int>(85 * dpr), static_cast<int>(85 * dpr))));
    m_context->Display(m_viewCube, 0, 0, false);
  }

  collectGlInfo(glCtx->VersionMajor(), glCtx->VersionMinor());
  m_glReady = true;
  Q_EMIT glInitialized(true, m_glSummary);
}

void OcctViewport::paintGL() {
  if (!m_glReady || m_view->Window().IsNull()) {
    return;
  }

  OccHandle<OpenGl_Context> glCtx = glContextOf(m_view);
  if (glCtx.IsNull()) {
    return;
  }

  // Wrap the FBO Qt bound for this frame as OCCT's default framebuffer.
  OccHandle<OpenGl_FrameBuffer> fbo = glCtx->DefaultFrameBuffer();
  if (fbo.IsNull()) {
    fbo = new QtFrameBufferWrapper();
    glCtx->SetDefaultFrameBuffer(fbo);
  }
  if (!fbo->InitWrapper(glCtx)) {
    failGl(tr("OCCT could not use Qt's framebuffer."));
    return;
  }

  OccHandle<Aspect_NeutralWindow> window = OccHandle<Aspect_NeutralWindow>::DownCast(m_view->Window());
  Graphic3d_Vec2i oldSize;
  window->Size(oldSize.x(), oldSize.y());
  const Graphic3d_Vec2i newSize = fbo->GetVPSize();
  if (newSize != oldSize) {
    window->SetSize(newSize.x(), newSize.y());
    m_view->MustBeResized();
    m_view->Invalidate();
  }

  m_view->InvalidateImmediate();
  FlushViewEvents(m_context, m_view, true);
}

void OcctViewport::handleViewRedraw(const OccHandle<AIS_InteractiveContext>& context,
                                    const OccHandle<V3d_View>& view) {
  AIS_ViewController::handleViewRedraw(context, view);
  if (toAskNextFrame()) {
    update(); // keep animating (view cube transitions, inertia)
  }
}

void OcctViewport::OnSelectionChanged(const OccHandle<AIS_InteractiveContext>&, const OccHandle<V3d_View>&) {
  Q_EMIT selectionChanged();
}

Graphic3d_Vec2i OcctViewport::toViewPixels(const QPointF& logicalPos) const {
  const qreal dpr = devicePixelRatioF();
  return Graphic3d_Vec2i(static_cast<int>(std::lround(logicalPos.x() * dpr)),
                         static_cast<int>(std::lround(logicalPos.y() * dpr)));
}

void OcctViewport::mousePressEvent(QMouseEvent* event) {
  QOpenGLWidget::mousePressEvent(event);
  if (m_glReady && UpdateMouseButtons(toViewPixels(event->position()), toVKeyMouse(event->buttons()),
                                      toVKeyFlags(event->modifiers()), false)) {
    update();
  }
}

void OcctViewport::mouseReleaseEvent(QMouseEvent* event) {
  QOpenGLWidget::mouseReleaseEvent(event);
  if (m_glReady && UpdateMouseButtons(toViewPixels(event->position()), toVKeyMouse(event->buttons()),
                                      toVKeyFlags(event->modifiers()), false)) {
    update();
  }
}

void OcctViewport::mouseMoveEvent(QMouseEvent* event) {
  QOpenGLWidget::mouseMoveEvent(event);
  if (m_glReady && UpdateMousePosition(toViewPixels(event->position()), toVKeyMouse(event->buttons()),
                                       toVKeyFlags(event->modifiers()), false)) {
    update();
  }
}

void OcctViewport::wheelEvent(QWheelEvent* event) {
  QOpenGLWidget::wheelEvent(event);
  if (!m_glReady) {
    return;
  }
  // angleDelta is in eighths of a degree; one notch = 15 degrees.
  const double delta = event->angleDelta().y() / 8.0;
  if (delta != 0.0 && UpdateZoom(Aspect_ScrollDelta(toViewPixels(event->position()), delta))) {
    update();
  }
}

void OcctViewport::invalidateScene() {
  m_view->Invalidate();
  update();
}

void OcctViewport::fitAll() {
  m_view->FitAll(0.01, false);
  update();
}

void OcctViewport::setStandardView(V3d_TypeOfOrientation orientation) {
  m_view->SetProj(orientation, false);
  m_view->FitAll(0.01, false);
  update();
}

void OcctViewport::setPerspective(bool enabled) {
  m_view->Camera()->SetProjectionType(enabled ? Graphic3d_Camera::Projection_Perspective
                                              : Graphic3d_Camera::Projection_Orthographic);
  update();
}

bool OcctViewport::isPerspective() const {
  return m_view->Camera()->ProjectionType() == Graphic3d_Camera::Projection_Perspective;
}

void OcctViewport::setMsaaSamples(int samples) {
  m_view->ChangeRenderingParams().NbMsaaSamples = std::max(0, samples);
  update();
}

int OcctViewport::msaaSamples() const {
  return m_view->RenderingParams().NbMsaaSamples;
}

void OcctViewport::setShowFrameRate(bool enabled) {
  m_view->ChangeRenderingParams().ToShowStats = enabled;
  update();
}

void OcctViewport::collectGlInfo(int glMajor, int glMinor) {
  TColStd_IndexedDataMapOfStringString info;
  m_view->DiagnosticInformation(info, Graphic3d_DiagnosticInfo_Basic);

  QString text;
  QString renderer;
  for (TColStd_IndexedDataMapOfStringString::Iterator it(info); it.More(); it.Next()) {
    const QString key = QString::fromUtf8(it.Key().ToCString());
    const QString value = QString::fromUtf8(it.Value().ToCString());
    text += key + QStringLiteral(": ") + value + QLatin1Char('\n');
    if (key == QLatin1String("GLdevice") || key == QLatin1String("GLrenderer")) {
      renderer = value;
    }
  }
  m_glInfo = text;
  m_glSummary = QStringLiteral("OpenGL %1.%2").arg(glMajor).arg(glMinor);
  if (!renderer.isEmpty()) {
    m_glSummary += QStringLiteral(" · ") + renderer;
  }
  Message::SendInfo() << "OpenGL context wrapped by OCCT: " << m_glSummary.toStdString().c_str();
}

void OcctViewport::failGl(const QString& message) {
  m_glReady = false;
  m_glFailed = true;
  Message::SendFail() << message.toStdString().c_str();
  Q_EMIT glInitialized(false, message);
}

} // namespace sc::render
