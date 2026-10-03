cmake_minimum_required(VERSION 3.24)
project(CampusPulse VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# Offline tests write optional local reports; a fresh source checkout has no evidence archive.
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/evidence")
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network Sql Test)
include(FetchContent)
set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lexbor
    URL https://codeload.github.com/lexbor/lexbor/zip/refs/tags/v2.5.0
    URL_HASH SHA256=460c6ee1396568078dc45b96dd55c9e92909deaff27cd6421c25dc8dfc8608b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lexbor)
set_target_properties(lexbor_static PROPERTIES AUTOMOC OFF)
# libical uses older CMake policies; scope compatibility to this pinned dependency.
set(STATIC_ONLY ON CACHE BOOL "" FORCE)
set(ICAL_GLIB OFF CACHE BOOL "" FORCE)
set(WITH_CXX_BINDINGS OFF CACHE BOOL "" FORCE)
set(ICAL_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(LIBICAL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBICAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(WIN32 AND EXISTS "C:/Program Files/Git/usr/bin/perl.exe")
    set(PERL_EXECUTABLE "C:/Program Files/Git/usr/bin/perl.exe" CACHE FILEPATH "" FORCE)
endif()
function(campuspulse_fetch_libical)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_Declare(libical
        URL https://codeload.github.com/libical/libical/zip/refs/tags/v3.0.20
        URL_HASH SHA256=7b48f3f67e241e5efa620cabfe04392eb666917a93e8dfc825f046346f7fa6e7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(libical)
    set(libical_SOURCE_DIR "${libical_SOURCE_DIR}" PARENT_SCOPE)
    set(libical_BINARY_DIR "${libical_BINARY_DIR}" PARENT_SCOPE)
endfunction()
campuspulse_fetch_libical()
set_target_properties(ical PROPERTIES AUTOMOC OFF)
if(MSVC)
    add_compile_options(/utf-8 /W4)
    target_compile_options(lexbor_static PRIVATE /utf-8)
endif()
add_library(campus_domain INTERFACE)
target_include_directories(campus_domain INTERFACE src)
add_library(campus_application src/application/NoticeService.cpp src/application/SourceService.cpp
    src/application/NoticeClassifier.cpp src/application/NoticeMatcher.cpp
    src/application/SubscriptionService.cpp src/application/TaskService.cpp src/application/CalendarService.cpp src/application/ReminderService.cpp src/application/ResourceService.cpp)
target_link_libraries(campus_application PUBLIC campus_domain)
add_library(campus_storage src/storage/SqliteRepository.cpp src/storage/Database.cpp
    src/storage/SqliteMigrations.cpp src/storage/SqliteSourceRepository.cpp
    src/storage/SqliteJson.cpp src/storage/SqliteSubscriptionRepository.cpp
    src/storage/SqliteTaskRepository.cpp src/storage/SqliteReminderRepository.cpp src/storage/SqliteResourceRepository.cpp)
target_link_libraries(campus_storage PUBLIC campus_application Qt6::Sql)
add_library(campus_adapters src/adapters/SchoolPackage.cpp src/adapters/HtmlAdapter.cpp
    src/adapters/RefreshCoordinator.cpp src/adapters/UniversityRegistry.cpp src/adapters/SchoolOnboarding.cpp src/adapters/DeepSeekSearch.cpp src/adapters/IcsExporter.cpp src/adapters/ArtifactWriter.cpp src/adapters/ReminderScheduler.cpp src/adapters/ResourceDiscovery.cpp src/adapters/ResourceClassifier.cpp)
target_link_libraries(campus_adapters PUBLIC campus_application Qt6::Network PRIVATE lexbor_static ical)
target_include_directories(campus_adapters PRIVATE ${libical_BINARY_DIR}/src/libical ${libical_SOURCE_DIR}/src ${libical_SOURCE_DIR}/src/libical)
target_compile_definitions(campus_adapters PRIVATE LIBICAL_ICAL_STATIC_DEFINE)
add_library(campus_presentation src/desktop/NoticeListModel.cpp src/desktop/NoticeFilter.cpp)
target_link_libraries(campus_presentation PUBLIC campus_application Qt6::Gui)
add_library(campus_desktop src/desktop/MainWindow.cpp
    src/desktop/NoticePage.cpp src/desktop/SourcePage.cpp src/desktop/UniversityPage.cpp
    src/desktop/SubscriptionEditorDialog.cpp src/desktop/SubscriptionPage.cpp
    src/desktop/TaskDisplay.cpp src/desktop/TaskEditorDialog.cpp src/desktop/TaskPage.cpp src/desktop/AiSourcesPage.cpp src/desktop/CalendarPage.cpp src/desktop/ResourcePage.cpp src/desktop/BrandTheme.cpp src/desktop/DesktopWorkspace.cpp assets/campuspulse.qrc)
target_link_libraries(campus_desktop PUBLIC campus_adapters campus_presentation Qt6::Widgets)
add_executable(CampusPulse WIN32 src/desktop/main.cpp)
target_link_libraries(CampusPulse PRIVATE campus_desktop campus_storage)
if(WIN32)
    target_sources(CampusPulse PRIVATE assets/campuspulse.rc)
    target_include_directories(CampusPulse PRIVATE ${CMAKE_SOURCE_DIR}/assets)
    target_link_libraries(campus_desktop PRIVATE ole32)
endif()
add_custom_command(TARGET CampusPulse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/configs $<TARGET_FILE_DIR:CampusPulse>/configs)
enable_testing()
add_executable(campus_tests tests/PrototypeTests.cpp)
target_link_libraries(campus_tests PRIVATE campus_adapters campus_storage campus_presentation Qt6::Test)
target_compile_definitions(campus_tests PRIVATE FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/neepu" CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json")
add_test(NAME prototype_contracts COMMAND campus_tests -o -,txt)
add_executable(campus_source_tests tests/SourceTests.cpp)
target_link_libraries(campus_source_tests PRIVATE campus_adapters campus_storage Qt6::Test)
target_compile_definitions(campus_source_tests PRIVATE
    CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json"
    TEMPLATE_FILE="${CMAKE_SOURCE_DIR}/templates/university.example.json"
    REAL_V1_DB="${CMAKE_SOURCE_DIR}/evidence/stage1-v1.sqlite")
add_test(NAME source_contracts COMMAND campus_source_tests -o -,txt)
add_executable(campus_university_tests tests/UniversityRegistryTests.cpp)
target_link_libraries(campus_university_tests PRIVATE campus_adapters Qt6::Test)
target_compile_definitions(campus_university_tests PRIVATE
    SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools"
    CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json")
add_test(NAME university_contracts COMMAND campus_university_tests -o -,txt)
add_executable(campus_desktop_tests tests/DesktopTests.cpp)
target_link_libraries(campus_desktop_tests PRIVATE campus_desktop campus_storage Qt6::Test)
target_compile_definitions(campus_desktop_tests PRIVATE
    CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json"
    SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools"
    REAL_V1_DB="${CMAKE_SOURCE_DIR}/evidence/stage1-v1.sqlite"
    EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME desktop_contracts COMMAND campus_desktop_tests -o -,txt)
add_executable(campus_subscription_tests tests/SubscriptionTests.cpp)
target_link_libraries(campus_subscription_tests PRIVATE campus_storage campus_presentation Qt6::Test)
target_compile_definitions(campus_subscription_tests PRIVATE
    REAL_V2_DB="${CMAKE_SOURCE_DIR}/evidence/stage2-v2.sqlite"
    V2_FIXTURE_FILE="${CMAKE_SOURCE_DIR}/tests/fixtures/database/v2.sql"
    EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME subscription_contracts COMMAND campus_subscription_tests -o -,txt)
add_executable(campus_subscription_desktop_tests tests/SubscriptionDesktopTests.cpp)
target_link_libraries(campus_subscription_desktop_tests PRIVATE campus_desktop campus_storage Qt6::Test)
target_compile_definitions(campus_subscription_desktop_tests PRIVATE
    CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json"
    SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools"
    EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME subscription_desktop_contracts COMMAND campus_subscription_desktop_tests -o -,txt)
add_executable(campus_task_tests tests/TaskTests.cpp)
target_link_libraries(campus_task_tests PRIVATE campus_storage Qt6::Test)
target_compile_definitions(campus_task_tests PRIVATE DATABASE_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures/database"
    REAL_V3_DB="${CMAKE_SOURCE_DIR}/evidence/stage3-v3.sqlite" EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME task_contracts COMMAND campus_task_tests -o -,txt)
add_executable(campus_task_desktop_tests tests/TaskDesktopTests.cpp)
target_link_libraries(campus_task_desktop_tests PRIVATE campus_desktop campus_storage Qt6::Test)
target_compile_definitions(campus_task_desktop_tests PRIVATE
    CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json"
    SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools" EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME task_desktop_contracts COMMAND campus_task_desktop_tests -o -,txt)
add_executable(campus_onboarding_tests tests/OnboardingTests.cpp)
target_link_libraries(campus_onboarding_tests PRIVATE campus_adapters campus_storage Qt6::Test)
target_compile_definitions(campus_onboarding_tests PRIVATE JLU_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures/jlu"
    SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools")
add_test(NAME onboarding_contracts COMMAND campus_onboarding_tests -o -,txt)

add_executable(campus_calendar_tests tests/CalendarTests.cpp)
target_link_libraries(campus_calendar_tests PRIVATE campus_adapters campus_storage Qt6::Test ical)
target_include_directories(campus_calendar_tests PRIVATE ${libical_BINARY_DIR}/src/libical ${libical_SOURCE_DIR}/src ${libical_SOURCE_DIR}/src/libical)
target_compile_definitions(campus_calendar_tests PRIVATE LIBICAL_ICAL_STATIC_DEFINE)
add_test(NAME calendar_contracts COMMAND campus_calendar_tests -o -,txt)
add_executable(campus_calendar_desktop_tests tests/CalendarDesktopTests.cpp)
target_link_libraries(campus_calendar_desktop_tests PRIVATE campus_desktop campus_storage Qt6::Test)
target_compile_definitions(campus_calendar_desktop_tests PRIVATE CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json")
add_test(NAME calendar_desktop_contracts COMMAND campus_calendar_desktop_tests -o -,txt)
add_executable(campus_school_coverage_tests tests/SchoolCoverageTests.cpp)
target_link_libraries(campus_school_coverage_tests PRIVATE campus_adapters Qt6::Test)
target_compile_definitions(campus_school_coverage_tests PRIVATE SCHOOL_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures" SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools" EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME school_coverage_contracts COMMAND campus_school_coverage_tests -o -,txt)

add_executable(campus_theme_coverage_tests tests/ThemeCoverageTests.cpp)
target_link_libraries(campus_theme_coverage_tests PRIVATE campus_presentation Qt6::Test)
add_test(NAME theme_coverage_contracts COMMAND campus_theme_coverage_tests -o -,txt)

add_executable(campus_reminder_tests tests/ReminderTests.cpp)
target_link_libraries(campus_reminder_tests PRIVATE campus_adapters campus_storage Qt6::Test)
add_test(NAME reminder_contracts COMMAND campus_reminder_tests -o -,txt)
target_compile_definitions(campus_calendar_tests PRIVATE EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")

add_executable(campus_calendar_native_probe tests/CalendarNativeProbe.cpp)
target_link_libraries(campus_calendar_native_probe PRIVATE campus_desktop campus_storage)
target_compile_definitions(campus_calendar_native_probe PRIVATE CONFIG_FILE="${CMAKE_SOURCE_DIR}/configs/schools/neepu.example.json" SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools")

add_executable(campus_source_access_native_probe tests/SourceAccessNativeProbe.cpp)
target_link_libraries(campus_source_access_native_probe PRIVATE campus_desktop campus_storage)
target_compile_definitions(campus_source_access_native_probe PRIVATE SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools")

add_executable(campus_resource_tests tests/ResourceTests.cpp)
target_link_libraries(campus_resource_tests PRIVATE campus_storage Qt6::Test)
add_test(NAME resource_contracts COMMAND campus_resource_tests -o -,txt)

add_executable(campus_resource_discovery_tests tests/ResourceDiscoveryTests.cpp)
target_link_libraries(campus_resource_discovery_tests PRIVATE campus_adapters campus_storage Qt6::Test)
target_compile_definitions(campus_resource_discovery_tests PRIVATE SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools" RESOURCE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/resources" EVIDENCE_DIR="${CMAKE_SOURCE_DIR}/evidence")
add_test(NAME resource_discovery_contracts COMMAND campus_resource_discovery_tests -o -,txt)

add_executable(campus_resource_desktop_tests tests/ResourceDesktopTests.cpp)
target_link_libraries(campus_resource_desktop_tests PRIVATE campus_desktop campus_storage Qt6::Test)
target_compile_definitions(campus_resource_desktop_tests PRIVATE SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools")
add_test(NAME resource_desktop_contracts COMMAND campus_resource_desktop_tests -o -,txt)

add_executable(campus_resource_live_probe tests/ResourceLiveProbe.cpp)
target_link_libraries(campus_resource_live_probe PRIVATE campus_adapters campus_storage)

add_executable(campus_resource_native_probe tests/ResourceNativeProbe.cpp)
target_link_libraries(campus_resource_native_probe PRIVATE campus_desktop campus_storage)
target_compile_definitions(campus_resource_native_probe PRIVATE SCHOOL_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs/schools")
