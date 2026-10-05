# CedarView — a personal client for one student's own Cedarville records.
#
#   cmake -B build -G Ninja && cmake --build build
#   ctest --test-dir build --output-on-failure
#   ./build/cedarview --demo
#
# The Android build goes through scripts/build-apk, which configures this same
# file with the Android Qt kit. See docs/android.md.

cmake_minimum_required(VERSION 3.21)

# The single version source. The APK's versionName and versionCode, the About
# sheet and `cedarview --version` are all read from here.
project(CedarView VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick)
if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS WebView)
else()
    find_package(Qt6 REQUIRED COMPONENTS Network WebEngineQuick)
endif()
qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/core)

# --- The UI layer, as a library so the tests can link it ---------------------
add_library(cedarview_ui STATIC
    src/platform/backend.h
    src/ui/tasks.h
    src/ui/bridge.h src/ui/bridge.cpp
    src/ui/demotransport.h src/ui/demotransport.cpp
    src/ui/login.h src/ui/login.cpp
    src/ui/modetransport.h src/ui/modetransport.cpp
    src/ui/settings.h src/ui/settings.cpp
    src/ui/sync.h src/ui/sync.cpp
    src/ui/webviewtransport.h src/ui/webviewtransport.cpp
    src/ui/viewmodels/format.h
    src/ui/viewmodels/storage.h
    src/ui/viewmodels/sourcestatus.h src/ui/viewmodels/sourcestatus.cpp
    src/ui/viewmodels/chapel.h src/ui/viewmodels/chapel.cpp
    src/ui/viewmodels/dining.h src/ui/viewmodels/dining.cpp
    src/ui/viewmodels/curfew.h src/ui/viewmodels/curfew.cpp
    src/ui/viewmodels/hours.h src/ui/viewmodels/hours.cpp
    src/ui/viewmodels/campus.h src/ui/viewmodels/campus.cpp
    src/ui/viewmodels/search.h src/ui/viewmodels/search.cpp
    src/ui/viewmodels/today.h src/ui/viewmodels/today.cpp
)
target_include_directories(cedarview_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cedarview_ui PUBLIC cedarview_core Qt6::Core Qt6::Qml)

# --- The app ------------------------------------------------------------------
qt_add_executable(cedarview src/ui/main.cpp)

if(ANDROID)
    target_sources(cedarview PRIVATE src/platform/android.cpp
                                     src/platform/android_sessiontransport.h src/platform/android_sessiontransport.cpp
                                     src/platform/android_urlprobe.h src/platform/android_urlprobe.cpp)
    target_link_libraries(cedarview PRIVATE Qt6::WebView)
else()
    target_sources(cedarview PRIVATE src/platform/desktop.cpp)
    target_link_libraries(cedarview PRIVATE Qt6::WebEngineQuick)
endif()

target_link_libraries(cedarview PRIVATE
    cedarview_ui cedarview_http cedarview_core
    Qt6::Gui Qt6::Qml Qt6::Quick
)
target_compile_definitions(cedarview PRIVATE CEDARVIEW_VERSION="${PROJECT_VERSION}")

# Sample-data preview's fixtures, built in (about 120 KB) so the preview works
# on a phone as well as with --demo: the real captures in tests/fixtures/,
# served from ":/fixtures" by a FixtureTransport like any other directory.
# Only the top level — samples/ holds test-only oddities.
file(GLOB CEDARVIEW_FIXTURES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/*.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/*.html)
qt_add_resources(cedarview "fixtures"
    PREFIX "/fixtures"
    BASE tests/fixtures
    FILES ${CEDARVIEW_FIXTURES}
)

# The QML, compiled ahead of time by qmlcachegen and embedded in the binary.
#
# Each platform gets only its own web surface. That is not tidiness: the QML
# import scanner that decides what goes into the APK reads these files, and
# WebSurfaceDesktop.qml imports QtWebEngine, which does not exist on Android.
# Main.qml's Loader names the surface file relative to itself, so both
# resolve inside the module on either platform.
set(CEDARVIEW_QML
    qml/Main.qml
    qml/Theme.qml
    qml/Banner.qml
    qml/BottomSheet.qml
    qml/CampusView.qml
    qml/Card.qml
    qml/ChapelSheet.qml
    qml/ChapelView.qml
    qml/Chip.qml
    qml/ClockText.qml
    qml/CurfewHero.qml
    qml/DiningSections.qml
    qml/EmptyState.qml
    qml/Glyph.qml
    qml/HeroCard.qml
    qml/HoursView.qml
    qml/IconButton.qml
    qml/LiveDot.qml
    qml/MealPlanView.qml
    qml/MenuView.qml
    qml/MoreSheet.qml
    qml/NavButton.qml
    qml/ScrollPage.qml
    qml/PipMeter.qml
    qml/PlaceRow.qml
    qml/PrimaryButton.qml
    qml/SearchBox.qml
    qml/SearchView.qml
    qml/SectionCaption.qml
    qml/SegmentedControl.qml
    qml/SignInView.qml
    qml/Skeleton.qml
    qml/StatusStamp.qml
    qml/TimelineRow.qml
    qml/TodayView.qml
    qml/ToggleSwitch.qml
    qml/TopProgress.qml
    qml/WelcomeView.qml
    qml/WebSurfaceStub.qml
)
if(ANDROID)
    list(APPEND CEDARVIEW_QML qml/WebSurfaceAndroid.qml)
else()
    list(APPEND CEDARVIEW_QML qml/WebSurfaceDesktop.qml)
endif()

# The two typefaces (Bricolage Grotesque and Instrument Sans, OFL), as static
# Latin instances. main.cpp registers every .ttf under fonts/ before QML loads.
set(CEDARVIEW_FONTS
    qml/fonts/BricolageGrotesque-SemiBold.ttf
    qml/fonts/BricolageGrotesque-Bold.ttf
    qml/fonts/BricolageGrotesque-ExtraBold.ttf
    qml/fonts/InstrumentSans-Regular.ttf
    qml/fonts/InstrumentSans-Medium.ttf
    qml/fonts/InstrumentSans-SemiBold.ttf
    qml/fonts/InstrumentSans-Bold.ttf
)

# The files live in qml/, but the module's resource paths must not carry that
# prefix: Main.qml refers to "icon.png" and "WebSurface*.qml" by bare name.
foreach(_file IN LISTS CEDARVIEW_QML ITEMS qml/icon.png)
    get_filename_component(_name ${_file} NAME)
    set_source_files_properties(${_file} PROPERTIES QT_RESOURCE_ALIAS ${_name})
endforeach()
foreach(_file IN LISTS CEDARVIEW_FONTS)
    get_filename_component(_name ${_file} NAME)
    set_source_files_properties(${_file} PROPERTIES QT_RESOURCE_ALIAS fonts/${_name})
endforeach()

qt_add_qml_module(cedarview
    URI CedarView
    VERSION 1.0
    QML_FILES ${CEDARVIEW_QML}
    RESOURCES qml/icon.png ${CEDARVIEW_FONTS}
)

# --- Android packaging --------------------------------------------------------
if(ANDROID)
    # python-for-android's versionCode formula, kept so that every release
    # still sorts above the Python-built APKs already installed from GitHub:
    # "10" + minSdk + the version folded two digits per part
    # (p4a build.py: `version_code *= 100; version_code += int(part)`).
    # 0.1.0 -> "10" "28" "100" -> 1028100, which is what the last Python
    # build of 0.1.0 carried.
    set(CEDARVIEW_MIN_SDK 28)
    math(EXPR _folded "(${PROJECT_VERSION_MAJOR} * 100 + ${PROJECT_VERSION_MINOR}) * 100 + ${PROJECT_VERSION_PATCH}")
    set(CEDARVIEW_VERSION_CODE "10${CEDARVIEW_MIN_SDK}${_folded}")

    # The Android package sources: android/ as written, plus the launcher icon
    # PNGs copied in from assets/, which stays their single source (it is what
    # scripts/make-icon-layers writes). Assembled in the build tree so no
    # second copy of the icons lives in git.
    set(CEDARVIEW_ANDROID_PACKAGE ${CMAKE_BINARY_DIR}/android-package)
    file(GLOB_RECURSE _android_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/android
         ${CMAKE_CURRENT_SOURCE_DIR}/android/*)
    foreach(_file IN LISTS _android_files)
        configure_file(android/${_file} ${CEDARVIEW_ANDROID_PACKAGE}/${_file} COPYONLY)
    endforeach()
    configure_file(assets/appicon.png ${CEDARVIEW_ANDROID_PACKAGE}/res/mipmap/icon.png COPYONLY)
    configure_file(assets/appicon_foreground.png
                   ${CEDARVIEW_ANDROID_PACKAGE}/res/mipmap/icon_foreground.png COPYONLY)
    configure_file(assets/appicon_background.png
                   ${CEDARVIEW_ANDROID_PACKAGE}/res/mipmap/icon_background.png COPYONLY)

    set_target_properties(cedarview PROPERTIES
        QT_ANDROID_PACKAGE_NAME com.kromakobra.cedarview
        QT_ANDROID_APP_NAME CedarView
        # Google Play has required 36 for new apps and updates since
        # 2026-08-31. Raise it each year when Play does.
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_COMPILE_SDK_VERSION 36
        # Qt 6.11's floor: Android 9 (API 28) to 16 (API 36).
        QT_ANDROID_MIN_SDK_VERSION ${CEDARVIEW_MIN_SDK}
        QT_ANDROID_ABIS arm64-v8a
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CEDARVIEW_ANDROID_PACKAGE}
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE ${CEDARVIEW_VERSION_CODE}
    )

    # 16 KB pages. Play refuses uploads whose native libraries are 4 KB
    # aligned, and a 16 KB-page phone refuses to load them. Qt's own libraries
    # are built that way already; this is for ours. scripts/build-apk checks
    # every .so in the result rather than trusting either.
    target_link_options(cedarview PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")

    # The NDK compiles with -g in every configuration and leaves stripping to
    # Gradle, which cannot find the NDK's strip when the NDK lives outside the
    # SDK (it says "Unable to strip the following libraries" and ships them
    # whole). Qt's own libraries arrive stripped; strip ours at link time.
    target_link_options(cedarview PRIVATE $<$<NOT:$<CONFIG:Debug>>:-Wl,--strip-all>)
endif()

# --- Tests --------------------------------------------------------------------
if(NOT ANDROID)
    include(CTest)
    if(BUILD_TESTING)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_subdirectory(tests)
    endif()
endif()
