// OCCT's OpenGL headers must come before any Qt header: Qt pulls in the platform GL
// headers (OpenGL/gl3.h on macOS), whose extension typedefs clash with OCCT's glext.
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>

#include "render/OcctViewport.h"

#include <Graphic3d_Camera.hxx>

#include "core/OccCompat.h"

#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Image_PixMap.hxx>
#include <Message.hxx>
#include <NCollection_Vec2.hxx>
#include <Precision.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Standard_Version.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_ImageDumpOptions.hxx>

#include <QMouseEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace sc::render {

namespace {

#ifdef __APPLE__
// macOS only offers OpenGL > 2.1 through a core profile (4.1 on all supported Macs).
constexpr bool CoreProfile = true;
#else
// Compatibility profile gives the highest version the driver supports and the
// widest support on older GPUs and drivers.
constexpr bool CoreProfile = false;
#endif

// OCCT's hard minimum.
constexpr int MinGlMajor = 2;
constexpr int MinGlMinor = 1;

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

// Aspect_Drawable is an integer (X11 Window) on Linux and a pointer elsewhere.
// (A template, so the branch that does not apply is never instantiated.)
template <class Drawable = Aspect_Drawable>
Drawable toDrawable(WId id) {
  if constexpr (std::is_pointer_v<Drawable>) {
    return reinterpret_cast<Drawable>(id);
  } else {
    return static_cast<Drawable>(id);
  }
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
  if (CoreProfile) {
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
  driver->ChangeOptions().contextCompatible = !CoreProfile;

  _navigationEnd.setSingleShot(true);
  _navigationEnd.setInterval(250);
  connect(&_navigationEnd, &QTimer::timeout, this, [this] { setNavigating(false); });

  _viewer = new V3d_Viewer(driver);
  // Same setup as V3d_Viewer::SetDefaultLights(), but kept so the intensity can be changed.
  _headlight = new V3d_DirectionalLight(V3d_XnegYnegZneg, Quantity_NOC_WHITE, true);
  _ambientLight = new V3d_AmbientLight(Quantity_NOC_WHITE);
  _viewer->AddLight(_headlight);
  _viewer->AddLight(_ambientLight);
  _viewer->SetLightOn();
  applyLighting();

  _context = new AIS_InteractiveContext(_viewer);
  // Selection: saturated blue; hover: light cyan. Both read well on gray and colored parts.
  const Quantity_Color selectedColor(0.20, 0.50, 0.95, Quantity_TOC_sRGB);
  const Quantity_Color hoverColor(0.55, 0.85, 1.00, Quantity_TOC_sRGB);
  setHighlightColor(_context->HighlightStyle(Prs3d_TypeOfHighlight_Selected), selectedColor);
  setHighlightColor(_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected), selectedColor);
  setHighlightColor(_context->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic), hoverColor);
  setHighlightColor(_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic), hoverColor);

  _view = _viewer->CreateView();
  _view->SetImmediateUpdate(false);
  applyBackground(_background);

  // Lightweight defaults for 10-year-old integrated GPUs: no MSAA, no stats overlay.
  Graphic3d_RenderingParams& params = _view->ChangeRenderingParams();
  params.NbMsaaSamples = 0;
  params.ToShowStats = false;

  _viewCube = new AIS_ViewCube();
  _viewCube->SetViewAnimation(ViewAnimation());
  _viewCube->SetFixedAnimationLoop(false);
  _viewCube->SetAutoStartAnimation(true);

  configureNavigation();

  setMouseTracking(true); // hover highlighting
  setFocusPolicy(Qt::StrongFocus);
  setBackgroundRole(QPalette::NoRole);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

  _overlayWidget = new OverlayWidget(this);
  _overlayWidget->setGeometry(rect());
}

OcctViewport::~OcctViewport() {
  // Keep the X11 display connection alive until Qt's context is current again;
  // releasing it first can crash inside QOpenGLWidget's destructor.
  OccHandle<Aspect_DisplayConnection> display = _viewer->Driver()->GetDisplayConnection();

  makeCurrent();
  _context->RemoveAll(false);
  _context.Nullify();
  _view->Remove();
  _view.Nullify();
  _viewer.Nullify();
  doneCurrent();

  display.Nullify();
}

void OcctViewport::configureNavigation() {
  // Left: orbit. Middle or Shift+Left: pan. Ctrl+Left/Middle: zoom. Wheel: zoom at cursor.
  // Right button is deliberately unbound; it is reserved for context menus.
  auto& gestures = ChangeMouseGestureMap();
  gestures.Clear();
  gestures.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit);
  gestures.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
  gestures.Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_Pan);
  gestures.Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Zoom);
  gestures.Bind(Aspect_VKeyMouse_MiddleButton | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Zoom);
}

void OcctViewport::setAccumulatingSelection(bool accumulate) {
  auto& schemes = ChangeMouseSelectionSchemes();
  schemes.Bind(Aspect_VKeyMouse_LeftButton,
               accumulate ? AIS_SelectionScheme_XOR : AIS_SelectionScheme_Replace);
}

double OcctViewport::pixelsToModel(double logicalPixels) const {
  if (_view.IsNull() || _view->Window().IsNull()) {
    return 0.0;
  }
  return _view->Convert(static_cast<int>(std::lround(logicalPixels * devicePixelRatioF())));
}

void OcctViewport::initializeGL() {
  if (_glFailed) {
    return;
  }

  OccHandle<OpenGl_Context> glCtx = new OpenGl_Context();
  if (!glCtx->Init(CoreProfile)) {
    failGl(tr("OCCT could not use the OpenGL context created by Qt."));
    return;
  }
  if (glCtx->VersionMajor() < MinGlMajor ||
      (glCtx->VersionMajor() == MinGlMajor && glCtx->VersionMinor() < MinGlMinor)) {
    failGl(tr("OpenGL %1.%2 is too old; at least %3.%4 is required.")
               .arg(glCtx->VersionMajor())
               .arg(glCtx->VersionMinor())
               .arg(MinGlMajor)
               .arg(MinGlMinor));
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
    drawable = toDrawable(window()->winId());
  }
#elif defined(__APPLE__)
  const Aspect_Drawable drawable = toDrawable(winId());
#else
  const Aspect_Drawable drawable = toDrawable(window()->winId());
#endif

  const qreal dpr = devicePixelRatioF();
  const int widthPx = std::max(1, static_cast<int>(std::lround(width() * dpr)));
  const int heightPx = std::max(1, static_cast<int>(std::lround(height() * dpr)));

  // initializeGL runs again if Qt recreates the context (e.g. moving to another screen).
  OccHandle<Aspect_NeutralWindow> window = OccHandle<Aspect_NeutralWindow>::DownCast(_view->Window());
  const bool firstInit = window.IsNull();
  if (firstInit) {
    window = new Aspect_NeutralWindow();
    window->SetVirtual(true);
  }
  window->SetNativeHandle(drawable);
  window->SetSize(widthPx, heightPx);
  _view->SetWindow(window, glCtx->RenderingContext());

  if (firstInit) {
    _viewCube->SetSize(55.0 * dpr, false);
    _viewCube->SetFontHeight(12.0 * dpr);
    _viewCube->SetTransformPersistence(new Graphic3d_TransformPers(
        Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
        NCollection_Vec2<int>(static_cast<int>(85 * dpr), static_cast<int>(85 * dpr))));
    _context->Display(_viewCube, 0, 0, false);
  }

  collectGlInfo(glCtx->VersionMajor(), glCtx->VersionMinor());
  _glReady = true;
  Q_EMIT glInitialized(true, _glSummary);
}

bool OcctViewport::event(QEvent* event) {
  // Sent to every widget of a window that moved to another screen.
  if (event->type() == QEvent::ScreenChangeInternal) {
    checkDevicePixelRatio();
  }
  return QOpenGLWidget::event(event);
}

void OcctViewport::checkDevicePixelRatio() {
  const qreal dpr = devicePixelRatioF();
  if (_lastDpr == 0.0) {
    _lastDpr = dpr;
    return;
  }
  if (!qFuzzyCompare(dpr, _lastDpr)) {
    _lastDpr = dpr;
    QMetaObject::invokeMethod(this, &OcctViewport::devicePixelRatioChanged, Qt::QueuedConnection);
  }
}

void OcctViewport::paintGL() {
  // Also checked per frame: not every scale change arrives as a screen change.
  checkDevicePixelRatio();
  if (!_glReady || _view->Window().IsNull()) {
    return;
  }

  OccHandle<OpenGl_Context> glCtx = glContextOf(_view);
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

  OccHandle<Aspect_NeutralWindow> window = OccHandle<Aspect_NeutralWindow>::DownCast(_view->Window());
  NCollection_Vec2<int> oldSize;
  window->Size(oldSize.x(), oldSize.y());
  const NCollection_Vec2<int> newSize = fbo->GetVPSize();
  if (newSize != oldSize) {
    window->SetSize(newSize.x(), newSize.y());
    _view->MustBeResized();
    _view->Invalidate();
  }

  _view->InvalidateImmediate();
  FlushViewEvents(_context, _view, true);
  updateOverlayPosition(); // the camera may have moved
}

OverlayWidget::OverlayWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_TranslucentBackground);
}

void OverlayWidget::setMarker(const std::optional<QPointF>& position, const QString& caption) {
  if (position == _position && caption == _caption) {
    return;
  }
  _position = position;
  _caption = caption;
  update();
}

void OverlayWidget::paintEvent(QPaintEvent*) {
  if (!_position) {
    return;
  }
  const QPointF center = *_position;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor accent(242, 115, 13);
  for (const auto& [color, width] : {std::pair<QColor, qreal>(Qt::white, 4.0), {accent, 2.0}}) {
    painter.setPen(QPen(color, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, 6.0, 6.0);
    painter.drawLine(center - QPointF(11, 0), center - QPointF(4, 0));
    painter.drawLine(center + QPointF(4, 0), center + QPointF(11, 0));
    painter.drawLine(center - QPointF(0, 11), center - QPointF(0, 4));
    painter.drawLine(center + QPointF(0, 4), center + QPointF(0, 11));
  }
  if (!_caption.isEmpty()) {
    const QFontMetricsF metrics(painter.font());
    QRectF box = metrics.boundingRect(_caption).adjusted(-4, -2, 4, 2);
    box.moveTopLeft(center + QPointF(12, 8));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 225));
    painter.drawRoundedRect(box, 3, 3);
    painter.setPen(QColor(30, 30, 35));
    painter.drawText(box, Qt::AlignCenter, _caption);
  }
}

void OcctViewport::updateOverlayPosition() {
  if (_overlayWidget == nullptr) {
    return;
  }
  if (!_overlay || _view.IsNull() || _view->Window().IsNull()) {
    _overlayWidget->setMarker(std::nullopt, {});
    return;
  }
  int pixelX = 0;
  int pixelY = 0;
  _view->Convert(_overlay->point.X(), _overlay->point.Y(), _overlay->point.Z(), pixelX, pixelY);
  const qreal dpr = devicePixelRatioF();
  _overlayWidget->setMarker(QPointF(pixelX / dpr, pixelY / dpr), _overlay->caption);
}

void OcctViewport::setOverlayMarker(const std::optional<OverlayMarker>& marker) {
  const bool changed =
      marker.has_value() != _overlay.has_value() ||
      (marker && (!marker->point.IsEqual(_overlay->point, 0.0) || marker->caption != _overlay->caption));
  _overlay = marker;
  if (changed) {
    updateOverlayPosition();
  }
}

void OcctViewport::resizeEvent(QResizeEvent* event) {
  QOpenGLWidget::resizeEvent(event);
  if (_overlayWidget != nullptr) {
    _overlayWidget->setGeometry(rect());
  }
}

void OcctViewport::leaveEvent(QEvent* event) {
  QOpenGLWidget::leaveEvent(event);
  setOverlayMarker(std::nullopt);
}

void OcctViewport::handleMoveTo(const OccHandle<AIS_InteractiveContext>& context,
                                const OccHandle<V3d_View>& view) {
  AIS_ViewController::handleMoveTo(context, view);
  Q_EMIT hoverUpdated();
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

NCollection_Vec2<int> OcctViewport::toViewPixels(const QPointF& logicalPos) const {
  const qreal dpr = devicePixelRatioF();
  return NCollection_Vec2<int>(static_cast<int>(std::lround(logicalPos.x() * dpr)),
                               static_cast<int>(std::lround(logicalPos.y() * dpr)));
}

void OcctViewport::mousePressEvent(QMouseEvent* event) {
  QOpenGLWidget::mousePressEvent(event);
  if (_glReady && _dragHandler != nullptr && event->buttons() == Qt::LeftButton &&
      event->modifiers() == Qt::NoModifier && _context->HasDetected()) {
    const NCollection_Vec2<int> pixel = toViewPixels(event->position());
    if (_dragHandler->beginDrag(_context->DetectedInteractive(), pixel)) {
      _dragging = true;
      _dragStart = pixel;
      return;
    }
  }
  setNavigating(true);
  if (_glReady && UpdateMouseButtons(toViewPixels(event->position()), toVKeyMouse(event->buttons()),
                                     toVKeyFlags(event->modifiers()), false)) {
    update();
  }
}

void OcctViewport::mouseReleaseEvent(QMouseEvent* event) {
  QOpenGLWidget::mouseReleaseEvent(event);
  if (_dragging) {
    _dragging = false;
    if (_dragHandler != nullptr) {
      _dragHandler->endDrag();
    }
    invalidateScene();
    return;
  }
  if (_glReady && UpdateMouseButtons(toViewPixels(event->position()), toVKeyMouse(event->buttons()),
                                     toVKeyFlags(event->modifiers()), false)) {
    update();
  }
  if (event->buttons() == Qt::NoButton) {
    setNavigating(false);
  }
}

void OcctViewport::mouseMoveEvent(QMouseEvent* event) {
  QOpenGLWidget::mouseMoveEvent(event);
  if (_dragging) {
    if (_dragHandler != nullptr) {
      _dragHandler->drag(_dragStart, toViewPixels(event->position()));
    }
    invalidateScene();
    return;
  }
  if (_glReady && UpdateMousePosition(toViewPixels(event->position()), toVKeyMouse(event->buttons()),
                                      toVKeyFlags(event->modifiers()), false)) {
    update();
  }
}

void OcctViewport::wheelEvent(QWheelEvent* event) {
  QOpenGLWidget::wheelEvent(event);
  if (!_glReady) {
    return;
  }
  // angleDelta is in eighths of a degree; one notch = 15 degrees.
  const double delta = event->angleDelta().y() / 8.0;
  if (delta != 0.0 && UpdateZoom(Aspect_ScrollDelta(toViewPixels(event->position()), delta))) {
    setNavigating(true);
    _navigationEnd.start(); // wheel events have no "release": end after a pause
    update();
  }
}

void OcctViewport::setNavigating(bool navigating) {
  if (navigating == _navigating || _viewer.IsNull()) {
    return;
  }
  _navigating = navigating;
  // Bounding-box diagonal in pixels below which an object is skipped (needs
  // frustum culling, which is on by default).
  constexpr double CullingPixels = 4.0;
  Graphic3d_ZLayerSettings settings = _viewer->ZLayerSettings(Graphic3d_ZLayerId_Default);
  settings.SetCullingSize(navigating ? CullingPixels * devicePixelRatioF() : Precision::Infinite());
  _viewer->SetZLayerSettings(Graphic3d_ZLayerId_Default, settings);
  if (!navigating) {
    invalidateScene(); // redraw with everything
  }
}

void OcctViewport::invalidateScene() {
  _view->Invalidate();
  update();
}

void OcctViewport::fitAll() {
  _view->FitAll(0.01, false);
  update();
}

void OcctViewport::setStandardView(V3d_TypeOfOrientation orientation) {
  _view->SetProj(orientation, false);
  _view->FitAll(0.01, false);
  update();
}

void OcctViewport::setPerspective(bool enabled) {
  _view->Camera()->SetProjectionType(enabled ? Graphic3d_Camera::Projection_Perspective
                                             : Graphic3d_Camera::Projection_Orthographic);
  update();
}

bool OcctViewport::isPerspective() const {
  return _view->Camera()->ProjectionType() == Graphic3d_Camera::Projection_Perspective;
}

CameraState OcctViewport::cameraState() const {
  CameraState state;
  if (_view.IsNull()) {
    return state;
  }
  const OccHandle<Graphic3d_Camera>& camera = _view->Camera();
  state.eye = camera->Eye();
  state.center = camera->Center();
  state.up = camera->Up();
  state.perspective = camera->ProjectionType() == Graphic3d_Camera::Projection_Perspective;
  state.scale = camera->Scale();
  state.fieldOfView = camera->FOVy();
  return state;
}

void OcctViewport::setCameraState(const CameraState& state) {
  if (_view.IsNull() || state.eye.Distance(state.center) < 1e-9) {
    return;
  }
  const OccHandle<Graphic3d_Camera>& camera = _view->Camera();
  camera->SetProjectionType(state.perspective ? Graphic3d_Camera::Projection_Perspective
                                              : Graphic3d_Camera::Projection_Orthographic);
  camera->SetEyeAndCenter(state.eye, state.center);
  camera->SetUp(state.up);
  // In perspective, the scale is the eye's distance (already set); setting it would move the eye.
  if (state.perspective) {
    camera->SetFOVy(state.fieldOfView);
  } else if (state.scale > 0.0) {
    camera->SetScale(state.scale);
  }
  _view->AutoZFit();
  invalidateScene();
}

void OcctViewport::setMsaaSamples(int samples) {
  _view->ChangeRenderingParams().NbMsaaSamples = std::max(0, samples);
  update();
}

int OcctViewport::msaaSamples() const {
  return _view->RenderingParams().NbMsaaSamples;
}

void OcctViewport::setShowFrameRate(bool enabled) {
  _view->ChangeRenderingParams().ToShowStats = enabled;
  update();
}

void OcctViewport::applyBackground(const BackgroundStyle& style) {
  auto toOcct = [](const QColor& color) {
    return Quantity_Color(color.redF(), color.greenF(), color.blueF(), Quantity_TOC_sRGB);
  };
  switch (style.kind) {
  case BackgroundStyle::Kind::Gradient:
    _view->SetBgGradientColors(toOcct(style.top), toOcct(style.bottom), Aspect_GradientFillMethod_Vertical,
                               false);
    break;
  case BackgroundStyle::Kind::Solid:
  case BackgroundStyle::Kind::White: {
    const Quantity_Color color = style.kind == BackgroundStyle::Kind::White
                                     ? Quantity_Color(1.0, 1.0, 1.0, Quantity_TOC_sRGB)
                                     : toOcct(style.solid);
    _view->SetBackgroundColor(color);
    _view->SetBgGradientColors(color, color, Aspect_GradientFillMethod_Vertical, false);
    break;
  }
  }
  _view->Invalidate();
}

void OcctViewport::setBackground(const BackgroundStyle& style) {
  _background = style;
  applyBackground(_background);
  update();
}

void OcctViewport::applyLighting() {
  // Headlight / ambient intensity. Cheap on purpose: no shadows, no extra lights.
  double headlight = 1.0;
  double ambient = 1.0;
  switch (_lighting) {
  case LightingPreset::Default:
    break;
  case LightingPreset::Brighter:
    headlight = 1.0;
    ambient = 2.5;
    break;
  case LightingPreset::Softer:
    headlight = 0.3;
    ambient = 1.0;
    break;
  }
  // The headlight is dimmed through its color: OCCT does not apply SetIntensity to it (checked
  // with Mesa llvmpipe), while the ambient light honors intensities above 1.
  _headlight->SetColor(Quantity_Color(headlight, headlight, headlight, Quantity_TOC_RGB));
  _ambientLight->SetIntensity(static_cast<float>(ambient));
  _viewer->UpdateLights();
  if (!_view.IsNull()) {
    _view->Invalidate();
  }
}

void OcctViewport::setLighting(LightingPreset preset) {
  _lighting = preset;
  applyLighting();
  update();
}

QSize OcctViewport::framebufferSize() const {
  const qreal dpr = devicePixelRatioF();
  return QSize(std::max(1, static_cast<int>(std::lround(width() * dpr))),
               std::max(1, static_cast<int>(std::lround(height() * dpr))));
}

// Dumps the view into an RGBA image of the given size using OCCT's offscreen render
// buffer. Qt's context must be current. ToPixMap renders into its own FBO (tiled when
// larger than the GPU allows) and restores the camera, so the on-screen state is intact.
QImage OcctViewport::dumpView(int width, int height) {
  Image_PixMap pixmap;
  V3d_ImageDumpOptions options;
  options.Width = width;
  options.Height = height;
  options.BufferType = Graphic3d_BT_RGBA;
  options.ToAdjustAspect = true; // match the camera aspect to the image, restored afterwards
#if OCC_VERSION_HEX >= 0x070700
  options.TargetZLayerId = Graphic3d_ZLayerId_Topmost; // include the view cube (OSD layers)
#endif
  if (!_view->ToPixMap(pixmap, options) || pixmap.IsEmpty() || pixmap.Width() != static_cast<size_t>(width) ||
      pixmap.Height() != static_cast<size_t>(height)) {
    return {};
  }

  const bool bgra = pixmap.Format() == Image_Format_BGRA;
  if (!bgra && pixmap.Format() != Image_Format_RGBA) {
    return {};
  }
  QImage image(width, height, QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y) {
    const Standard_Byte* src =
        pixmap.Data() + static_cast<size_t>(pixmap.IsTopDown() ? y : height - 1 - y) * pixmap.SizeRowBytes();
    QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < width; ++x, src += 4) {
      dst[x] = bgra ? qRgba(src[2], src[1], src[0], src[3]) : qRgba(src[0], src[1], src[2], src[3]);
    }
  }
  return image;
}

QImage OcctViewport::renderImage(int width, int height, ExportBackground background) {
  if (!_glReady || _view.IsNull() || _view->Window().IsNull()) {
    return {};
  }
  width = std::clamp(width, 1, MaxExportSize);
  height = std::clamp(height, 1, MaxExportSize);

  // Exactly what is on screen: take Qt's own framebuffer.
  if (background == ExportBackground::AsShown && QSize(width, height) == framebufferSize()) {
    QImage grabbed = grabFramebuffer();
    if (!grabbed.isNull()) {
      return grabbed.convertToFormat(QImage::Format_RGB32);
    }
  }

  makeCurrent();
  const BackgroundStyle shown = _background;
  QImage result;
  if (background == ExportBackground::AsShown) {
    result = dumpView(width, height);
  } else {
    BackgroundStyle plain;
    plain.kind = BackgroundStyle::Kind::Solid;
    plain.solid = Qt::white;
    applyBackground(plain);
    result = dumpView(width, height);
    if (background == ExportBackground::Transparent && !result.isNull()) {
      // OCCT has no alpha background. Render over white and over black, then solve
      // for alpha: over a background b, pixel = c * a + b * (1 - a), so
      // white - black = 255 * (1 - a). Handles anti-aliased edges and translucent parts.
      plain.solid = Qt::black;
      applyBackground(plain);
      const QImage onBlack = dumpView(width, height);
      if (onBlack.size() != result.size()) {
        result = QImage();
      } else {
        for (int y = 0; y < height; ++y) {
          QRgb* white = reinterpret_cast<QRgb*>(result.scanLine(y));
          const QRgb* black = reinterpret_cast<const QRgb*>(onBlack.constScanLine(y));
          for (int x = 0; x < width; ++x) {
            const int diff = (qRed(white[x]) - qRed(black[x]) + qGreen(white[x]) - qGreen(black[x]) +
                              qBlue(white[x]) - qBlue(black[x])) /
                             3;
            const int alpha = std::clamp(255 - diff, 0, 255);
            if (alpha == 0) {
              white[x] = qRgba(0, 0, 0, 0);
            } else {
              // Straight (non-premultiplied) color: black pixel = color * alpha.
              const auto unpremultiply = [alpha](int v) { return std::min(255, v * 255 / alpha); };
              white[x] = qRgba(unpremultiply(qRed(black[x])), unpremultiply(qGreen(black[x])),
                               unpremultiply(qBlue(black[x])), alpha);
            }
          }
        }
      }
    }
    applyBackground(shown);
  }
  doneCurrent();
  _view->Invalidate();
  update();

  if (result.isNull()) {
    return {};
  }
  return background == ExportBackground::Transparent ? result : result.convertToFormat(QImage::Format_RGB32);
}

void OcctViewport::collectGlInfo(int glMajor, int glMinor) {
  occ::StringStringMap info;
  _view->DiagnosticInformation(info, Graphic3d_DiagnosticInfo_Basic);

  QString text;
  QString renderer;
  for (occ::StringStringMap::Iterator it(info); it.More(); it.Next()) {
    const QString key = QString::fromUtf8(it.Key().ToCString());
    const QString value = QString::fromUtf8(it.Value().ToCString());
    text += key + QStringLiteral(": ") + value + QLatin1Char('\n');
    if (key == QLatin1String("GLdevice") || key == QLatin1String("GLrenderer")) {
      renderer = value;
    }
  }
  _glInfo = text;
  _glSummary = QStringLiteral("OpenGL %1.%2").arg(glMajor).arg(glMinor);
  if (!renderer.isEmpty()) {
    _glSummary += QStringLiteral(" · ") + renderer;
  }
  Message::SendInfo() << "OpenGL context wrapped by OCCT: " << _glSummary.toStdString().c_str();
}

void OcctViewport::failGl(const QString& message) {
  _glReady = false;
  _glFailed = true;
  Message::SendFail() << message.toStdString().c_str();
  Q_EMIT glInitialized(false, message);
}

} // namespace sc::render
