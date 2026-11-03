# Can be built standalone:  cmake -B build/tests -S tests
# Or included from root with: -DBUILD_TESTS=ON
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(WallpaperEngineKdeTests CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
    set(WEKDE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
else()
    set(WEKDE_SRC_DIR "${CMAKE_SOURCE_DIR}/src")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Test Concurrent DBus Gui Quick Qml)
find_package(KF6GlobalAccel REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(Plasma REQUIRED)
set(CMAKE_AUTOMOC ON)

# ── FileHelper unit tests ──────────────────────────────────────────────────────
add_executable(tst_filehelper
    tst_filehelper.cpp
    ${WEKDE_SRC_DIR}/FileHelper.cpp
    ${WEKDE_SRC_DIR}/SteamPaths.cpp
    ${WEKDE_SRC_DIR}/SteamKeyValues.cpp
    ${WEKDE_SRC_DIR}/SteamAccounts.cpp
    ${WEKDE_SRC_DIR}/SteamAccountsWatcher.cpp
)
target_include_directories(tst_filehelper PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_filehelper PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME tst_filehelper COMMAND tst_filehelper -v2)

# ── Steam path detection unit tests ────────────────────────────────────────────
add_executable(tst_steampaths
    tst_steampaths.cpp
    ${WEKDE_SRC_DIR}/SteamPaths.cpp
    ${WEKDE_SRC_DIR}/SteamKeyValues.cpp
)
target_include_directories(tst_steampaths PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_steampaths PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_steampaths COMMAND tst_steampaths -v2)

find_package(Qt6 6.7 QUIET COMPONENTS QuickTest)
if(Qt6_FOUND AND TARGET Qt6::qmltestrunner)
    add_test(NAME qml_tests COMMAND Qt6::qmltestrunner
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml)
    set_tests_properties(qml_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_test(NAME qml_runtime_reload_controller COMMAND Qt6::qmltestrunner
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_runtime_reload_controller.qml)
    set_tests_properties(qml_runtime_reload_controller PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 10)
    add_test(NAME qml_runtime_next_controller COMMAND Qt6::qmltestrunner
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_runtime_next_controller.qml)
    set_tests_properties(qml_runtime_next_controller PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 10)
    add_test(NAME qml_wallpaper_list_refresh COMMAND Qt6::qmltestrunner
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_wallpaper_list_refresh.qml)
    set_tests_properties(qml_wallpaper_list_refresh PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 20)
    add_test(NAME qml_qtmultimedia_reload_property COMMAND Qt6::qmltestrunner
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_qtmultimedia_reload_property.qml)
    set_tests_properties(qml_qtmultimedia_reload_property PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 10)
endif()

add_executable(tst_wallpaperenginefollower
    tst_wallpaperenginefollower.cpp
    ${WEKDE_SRC_DIR}/WallpaperEngineFollower.cpp
    ${WEKDE_SRC_DIR}/SteamPaths.cpp
    ${WEKDE_SRC_DIR}/SteamKeyValues.cpp)
target_include_directories(tst_wallpaperenginefollower PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_wallpaperenginefollower PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME tst_wallpaperenginefollower COMMAND tst_wallpaperenginefollower)

add_executable(tst_projectdirectorywatcher
    tst_projectdirectorywatcher.cpp
    ${WEKDE_SRC_DIR}/ProjectDirectoryWatcher.cpp)
target_include_directories(tst_projectdirectorywatcher PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_projectdirectorywatcher PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_projectdirectorywatcher COMMAND tst_projectdirectorywatcher)

add_executable(tst_wallpaperplaybackpolicy
    tst_wallpaperplaybackpolicy.cpp
    ${WEKDE_SRC_DIR}/WallpaperPlaybackPolicy.cpp)
target_include_directories(tst_wallpaperplaybackpolicy PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_wallpaperplaybackpolicy PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_wallpaperplaybackpolicy COMMAND tst_wallpaperplaybackpolicy)

add_executable(tst_wallpaperruntime
    tst_wallpaperruntime.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntime.cpp
    ${WEKDE_SRC_DIR}/WallpaperPlaybackPolicy.cpp)
target_include_directories(tst_wallpaperruntime PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_wallpaperruntime PRIVATE Qt6::Core Qt6::Test Qt6::DBus)
add_test(NAME tst_wallpaperruntime
    COMMAND dbus-run-session -- $<TARGET_FILE:tst_wallpaperruntime>)
set_tests_properties(tst_wallpaperruntime PROPERTIES TIMEOUT 30)

add_executable(tst_wallpaperruntimeqmlclient
    tst_wallpaperruntimeqmlclient.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntimeQmlClient.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntime.cpp
    ${WEKDE_SRC_DIR}/WallpaperPlaybackPolicy.cpp)
target_include_directories(tst_wallpaperruntimeqmlclient PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_wallpaperruntimeqmlclient PRIVATE
    Qt6::Core Qt6::Test Qt6::DBus Qt6::Qml)
add_dependencies(tst_wallpaperruntimeqmlclient tst_wallpaperruntime)
target_compile_definitions(tst_wallpaperruntimeqmlclient PRIVATE
    RUNTIME_SERVICE_WORKER_EXECUTABLE="$<TARGET_FILE:tst_wallpaperruntime>")
add_test(NAME tst_wallpaperruntimeqmlclient
    COMMAND dbus-run-session -- $<TARGET_FILE:tst_wallpaperruntimeqmlclient>)
set_tests_properties(tst_wallpaperruntimeqmlclient PROPERTIES TIMEOUT 30)

add_executable(tst_wallpaperruntimehost
    tst_wallpaperruntimehost.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntimeHost.cpp
    ${WEKDE_SRC_DIR}/PlasmaConfigurationInvoker.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntimeRegistration.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntime.cpp
    ${WEKDE_SRC_DIR}/WallpaperPlaybackPolicy.cpp
    ${WEKDE_SRC_DIR}/GlobalConfigStore.cpp)
target_include_directories(tst_wallpaperruntimehost PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_wallpaperruntimehost PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test Qt6::DBus)
target_compile_definitions(tst_wallpaperruntimehost PRIVATE
    RUNTIME_FOLLOW_STATE_BINDING_QML="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/runtime_follow_state_binding.qml")
add_test(NAME tst_wallpaperruntimehost
    COMMAND dbus-run-session -- $<TARGET_FILE:tst_wallpaperruntimehost>)
set_tests_properties(tst_wallpaperruntimehost PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 30)

add_executable(tst_plasmaconfigurationinvoker
    tst_plasmaconfigurationinvoker.cpp
    ${WEKDE_SRC_DIR}/PlasmaConfigurationInvoker.cpp)
target_include_directories(tst_plasmaconfigurationinvoker PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_plasmaconfigurationinvoker PRIVATE Qt6::Core Qt6::Test Qt6::DBus Qt6::Qml)
add_test(NAME tst_plasmaconfigurationinvoker COMMAND tst_plasmaconfigurationinvoker)

add_executable(tst_desktopsettingsinvoker
    tst_desktopsettingsinvoker.cpp
    ${WEKDE_SRC_DIR}/DesktopSettingsInvoker.cpp
    ${WEKDE_SRC_DIR}/PlasmaConfigurationInvoker.cpp)
target_include_directories(tst_desktopsettingsinvoker PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_desktopsettingsinvoker PRIVATE Qt6::Core Qt6::Test Qt6::DBus)
add_test(NAME tst_desktopsettingsinvoker COMMAND tst_desktopsettingsinvoker)

add_executable(tst_globalplaybackshortcut
    tst_globalplaybackshortcut.cpp
    ${WEKDE_SRC_DIR}/GlobalPlaybackShortcut.cpp)
target_include_directories(tst_globalplaybackshortcut PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_globalplaybackshortcut PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test KF6::GlobalAccel KF6::XmlGui)
add_test(NAME tst_globalplaybackshortcut COMMAND tst_globalplaybackshortcut)

add_executable(tst_runtimefollowassignment tst_runtimefollowassignment.cpp)
target_link_libraries(tst_runtimefollowassignment PRIVATE Qt6::Core Qt6::Test Qt6::Qml)
target_compile_definitions(tst_runtimefollowassignment PRIVATE
    RUNTIME_FOLLOW_ASSIGNMENT_QML="${CMAKE_CURRENT_SOURCE_DIR}/../plugin/contents/ui/components/RuntimeFollowAssignment.qml")
add_test(NAME tst_runtimefollowassignment COMMAND tst_runtimefollowassignment)
set_tests_properties(tst_runtimefollowassignment PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 15)

add_executable(tst_followchoicebuilderqml tst_followchoicebuilderqml.cpp)
target_link_libraries(tst_followchoicebuilderqml PRIVATE Qt6::Core Qt6::Test Qt6::Qml)
add_test(NAME tst_followchoicebuilderqml COMMAND tst_followchoicebuilderqml)
set_tests_properties(tst_followchoicebuilderqml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FOLLOW_CHOICE_BUILDER_PATH=${CMAKE_CURRENT_SOURCE_DIR}/../plugin/contents/ui/js/follow-choice-builder.mjs"
    TIMEOUT 15)

add_executable(tst_wallpaperruntimeregistration
    tst_wallpaperruntimeregistration.cpp
    ${WEKDE_SRC_DIR}/WallpaperRuntimeRegistration.cpp)
target_include_directories(tst_wallpaperruntimeregistration PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_wallpaperruntimeregistration PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
add_test(NAME tst_wallpaperruntimeregistration
    COMMAND tst_wallpaperruntimeregistration)
set_tests_properties(tst_wallpaperruntimeregistration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 15)

add_executable(tst_globalconfigstore
    tst_globalconfigstore.cpp
    ${WEKDE_SRC_DIR}/GlobalConfigStore.cpp)
target_include_directories(tst_globalconfigstore PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_globalconfigstore PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_globalconfigstore COMMAND tst_globalconfigstore)

add_executable(tst_desktopcoverage
    tst_desktopcoverage.cpp
    ${WEKDE_SRC_DIR}/DesktopCoverage.cpp)
target_include_directories(tst_desktopcoverage PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_desktopcoverage PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME tst_desktopcoverage COMMAND tst_desktopcoverage)

add_executable(tst_desktopcoveragecalculator
    tst_desktopcoveragecalculator.cpp
    ${WEKDE_SRC_DIR}/DesktopCoverageCalculator.cpp
    ${WEKDE_SRC_DIR}/DesktopCoverage.cpp)
target_include_directories(tst_desktopcoveragecalculator PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_desktopcoveragecalculator PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME tst_desktopcoveragecalculator COMMAND tst_desktopcoveragecalculator)

add_executable(tst_desktopcoveragecalculatorqml
    tst_desktopcoveragecalculatorqml.cpp
    ${WEKDE_SRC_DIR}/DesktopCoverageCalculator.cpp
    ${WEKDE_SRC_DIR}/DesktopCoverage.cpp)
target_include_directories(tst_desktopcoveragecalculatorqml PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_desktopcoveragecalculatorqml PRIVATE Qt6::Core Qt6::Gui Qt6::Test Qt6::Qml)
target_compile_definitions(tst_desktopcoveragecalculatorqml PRIVATE
    COVERAGE_QML_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/desktop_coverage_calculator_bridge.qml")
add_test(NAME tst_desktopcoveragecalculatorqml COMMAND tst_desktopcoveragecalculatorqml)

add_executable(tst_desktopvisibilitymonitor
    tst_desktopvisibilitymonitor.cpp
    ${WEKDE_SRC_DIR}/DesktopVisibilityMonitor.cpp)
target_include_directories(tst_desktopvisibilitymonitor PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_desktopvisibilitymonitor PRIVATE Qt6::Core Qt6::Test Qt6::DBus)
add_test(NAME tst_desktopvisibilitymonitor
    COMMAND dbus-run-session -- $<TARGET_FILE:tst_desktopvisibilitymonitor>)
set_tests_properties(tst_desktopvisibilitymonitor PROPERTIES TIMEOUT 20)

add_executable(tst_primaryscreenmonitor
    tst_primaryscreenmonitor.cpp
    ${WEKDE_SRC_DIR}/PrimaryScreenMonitor.cpp)
target_include_directories(tst_primaryscreenmonitor PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_primaryscreenmonitor PRIVATE Qt6::Core Qt6::Gui Qt6::Test Plasma::Plasma)
add_test(NAME tst_primaryscreenmonitor COMMAND tst_primaryscreenmonitor)
set_tests_properties(tst_primaryscreenmonitor PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 15)

add_executable(tst_steamkeyvalues
    tst_steamkeyvalues.cpp
    ${WEKDE_SRC_DIR}/SteamKeyValues.cpp)
target_include_directories(tst_steamkeyvalues PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_steamkeyvalues PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_steamkeyvalues COMMAND tst_steamkeyvalues)

add_executable(tst_steamaccounts
    tst_steamaccounts.cpp
    ${WEKDE_SRC_DIR}/SteamAccounts.cpp
    ${WEKDE_SRC_DIR}/SteamKeyValues.cpp)
target_include_directories(tst_steamaccounts PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_steamaccounts PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_steamaccounts COMMAND tst_steamaccounts)

add_executable(tst_steamaccountswatcher
    tst_steamaccountswatcher.cpp
    ${WEKDE_SRC_DIR}/SteamAccountsWatcher.cpp
    ${WEKDE_SRC_DIR}/SteamAccounts.cpp
    ${WEKDE_SRC_DIR}/SteamKeyValues.cpp
    ${WEKDE_SRC_DIR}/SteamPaths.cpp)
target_include_directories(tst_steamaccountswatcher PRIVATE ${WEKDE_SRC_DIR})
target_link_libraries(tst_steamaccountswatcher PRIVATE
    Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME tst_steamaccountswatcher COMMAND tst_steamaccountswatcher)

add_executable(tst_panelappletqml tst_panelappletqml.cpp)
target_link_libraries(tst_panelappletqml PRIVATE Qt6::Core Qt6::Test Qt6::Gui Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_panelappletqml PRIVATE
    PANEL_APPLET_QML="${CMAKE_CURRENT_SOURCE_DIR}/../panel/contents/ui/main.qml")
add_test(NAME tst_panelappletqml COMMAND tst_panelappletqml)
set_tests_properties(tst_panelappletqml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 15)
