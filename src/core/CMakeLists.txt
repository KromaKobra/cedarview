# The core: parsing, models, session metadata, the transport seam.
#
# The rule that matters: cedarview_core links Qt Core and NOTHING else — no
# Gui, no Quick, no Qml, no WebEngine/WebView, not even Network. That is what
# lets every provider run under ctest with no display and no network, and it
# is enforced twice: by the check just below, at configure time, and by
# tests/tst_core_is_gui_free.cpp, which also greps these sources for
# module-qualified includes that would sneak past the include paths.

add_library(cedarview_core STATIC
    errors.h
    log.h log.cpp
    pyjson.h pyjson.cpp
    transport.h transport.cpp
    session.h session.cpp
    calendar.h calendar.cpp
    models.h models.cpp
    htmlattrs.h htmlattrs.cpp
    providers/chapel.h providers/chapel.cpp
    providers/chapel_schedule.h providers/chapel_schedule.cpp
    providers/dining.h providers/dining.cpp
    providers/meals.h providers/meals.cpp
)
target_include_directories(cedarview_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cedarview_core PUBLIC Qt6::Core)

get_target_property(_core_libs cedarview_core LINK_LIBRARIES)
if(NOT _core_libs STREQUAL "Qt6::Core")
    message(FATAL_ERROR
        "cedarview_core must link Qt6::Core only, but links: ${_core_libs}. "
        "Anything that needs more belongs in src/ui (or, for networking, in cedarview_http).")
endif()
# Recorded for tst_core_is_gui_free, so the rule is visible in the test run too.
file(WRITE ${CMAKE_BINARY_DIR}/core_link_libraries.txt "${_core_libs}")

# The one piece of the core that needs a network stack. Desktop uses Qt
# Network; Android calls the platform's HttpsURLConnection over JNI, which is
# in Qt Core, so the APK needs no Qt Network (and no bundled OpenSSL) for it.
add_library(cedarview_http STATIC httptransport.h httptransport.cpp)
target_link_libraries(cedarview_http PUBLIC cedarview_core)
if(NOT ANDROID)
    target_link_libraries(cedarview_http PUBLIC Qt6::Network)
endif()
