# Qt Test suites, run by ctest. Every one of them runs offscreen and with the
# network blocked; see testsupport.h.
#
#   ctest --test-dir build --output-on-failure

function(cedarview_test name)
    cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
    qt_add_executable(${name} ${name}.cpp testsupport.h)
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Network ${ARG_LIBS})
    target_compile_definitions(${name} PRIVATE CEDARVIEW_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                                                CEDARVIEW_BINARY_DIR="${PROJECT_BINARY_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# The core: parsing and persistence, no GUI.
cedarview_test(tst_transport LIBS cedarview_core cedarview_ui)
cedarview_test(tst_session LIBS cedarview_core)
cedarview_test(tst_calendar LIBS cedarview_core)
cedarview_test(tst_chapel_provider LIBS cedarview_core)
cedarview_test(tst_chapel_schedule_provider LIBS cedarview_core)
cedarview_test(tst_dining_provider LIBS cedarview_core)
cedarview_test(tst_meals_provider LIBS cedarview_core)
cedarview_test(tst_core_is_gui_free LIBS cedarview_core)

# The Qt layer, headless.
cedarview_test(tst_tasks LIBS cedarview_ui)
cedarview_test(tst_login_flow LIBS cedarview_ui)
cedarview_test(tst_viewmodels LIBS cedarview_ui)
cedarview_test(tst_qml_contract LIBS cedarview_ui)
cedarview_test(tst_surfaces LIBS cedarview_ui)

# The C++ transport against a real browser: offscreen QtWebEngine fetching
# from a loopback server. The one test that exercises the in-page fetch end
# to end without Cedarville in the loop.
cedarview_test(tst_webview_transport LIBS cedarview_ui Qt6::Gui Qt6::Quick Qt6::WebEngineQuick)
set_tests_properties(tst_webview_transport PROPERTIES
    TIMEOUT 120
    # Chromium's own sandbox needs user namespaces that build sandboxes and
    # some CI runners do not grant; the page it loads is our own loopback one.
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_DISABLE_SANDBOX=1;QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu")
