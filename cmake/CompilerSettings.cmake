# Common warning and sanitizer flags, applied per target via sc_target_defaults().

function(sc_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    # OCCT headers trigger these at /W4.
    target_compile_options(${target} PRIVATE /wd4100 /wd4127 /wd4251 /wd4275 /wd4458)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    if(SC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(SC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(SC_ENABLE_SANITIZERS)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
  # Avoid clashes between Qt's 'signals'/'slots'/'emit' keywords and third-party headers.
  target_compile_definitions(${target} PRIVATE QT_NO_KEYWORDS)
endfunction()
