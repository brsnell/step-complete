#pragma once

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>

namespace sc::core {

// A CAMERA_MODEL_D3 (AP242 saved view camera) read straight from the STEP
// entities. OCCT's XCAF view object loses the camera location and keeps the
// view window in file units, so the importer collects these separately.
//
// All lengths are in millimeters. Points and directions of the view reference
// system are in world coordinates; the view window is given in the camera's
// own frame (X = refDirection, Z = axis, Y = Z cross X), where the view plane
// lies at z = viewPlaneDistance.
struct StepCamera {
  std::string name;

  // VIEW_REFERENCE_SYSTEM (AXIS2_PLACEMENT_3D): the camera frame.
  gp_Pnt location;
  gp_Dir axis{0.0, 0.0, 1.0};         // viewing direction (towards the model)
  gp_Dir refDirection{1.0, 0.0, 0.0}; // camera X

  // VIEW_VOLUME.
  bool perspective = false;
  gp_Pnt projectionPoint;         // in the camera frame, usually (0, 0, 0)
  double viewPlaneDistance = 0.0; // along the axis, from the camera frame origin
  std::optional<double> frontPlaneDistance;
  std::optional<double> backPlaneDistance;
  bool frontPlaneClipping = false;
  bool backPlaneClipping = false;
  bool sidesClipping = false;

  // PLANAR_BOX (view window); zero if absent.
  double windowWidth = 0.0;
  double windowHeight = 0.0;
  // Window centre in the camera frame (x, y; its z is the view plane).
  double windowCenterX = 0.0;
  double windowCenterY = 0.0;
};

} // namespace sc::core
