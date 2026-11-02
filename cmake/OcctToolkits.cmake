# Maps the OCCT toolkits we need to imported targets, hiding naming differences
# between OCCT versions. OCCT 7.8 merged the STEP toolkits (TKSTEP, TKSTEPBase,
# TKSTEPAttr, TKSTEP209, TKXDESTEP) into TKDESTEP.
#
# Defines interface targets:
#   sc::occt_foundation  - kernel, math, geometry, topology
#   sc::occt_visual      - V3d/AIS viewer and OpenGL driver
#   sc::occt_data        - XCAF document and STEP import

if(NOT OpenCASCADE_VERSION)
  set(OpenCASCADE_VERSION "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}")
endif()

if(OpenCASCADE_VERSION VERSION_LESS 7.6)
  message(FATAL_ERROR "OCCT >= 7.6 is required (found ${OpenCASCADE_VERSION}); 8.0.x is recommended")
endif()

set(_sc_occt_foundation TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim
                        TKShHealing TKMesh TKBO)
set(_sc_occt_visual     TKService TKV3d TKOpenGl)
set(_sc_occt_data       TKCDF TKLCAF TKCAF TKXSBase TKXCAF TKBinL TKBin TKBinXCAF)

if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.8)
  list(APPEND _sc_occt_data TKDE TKDESTEP)
else()
  list(APPEND _sc_occt_data TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP TKXDESTEP)
endif()

foreach(_group foundation visual data)
  foreach(_tk IN LISTS _sc_occt_${_group})
    if(NOT TARGET ${_tk})
      message(FATAL_ERROR "OCCT toolkit '${_tk}' not found. Check your OCCT installation.")
    endif()
  endforeach()
  add_library(sc_occt_${_group} INTERFACE)
  add_library(sc::occt_${_group} ALIAS sc_occt_${_group})
  target_link_libraries(sc_occt_${_group} INTERFACE ${_sc_occt_${_group}})
  # Mark OCCT headers as system headers so our warning flags don't apply to them.
  target_include_directories(sc_occt_${_group} SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})
endforeach()

target_link_libraries(sc_occt_visual INTERFACE sc_occt_foundation)
target_link_libraries(sc_occt_data INTERFACE sc_occt_foundation)
