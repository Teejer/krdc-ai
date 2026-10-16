if(NOT NDEBUG)
    set(BUILD_TEST true)
endif()

if(NOT BUILD_TEST)
    return()
endif()

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)

# The dark mode filter's recolor rules are all tuned against real remote
# desktop captures, which makes them worth pinning down: the test needs no
# connection, no display and no mock server, just the filter and some
# hand-built QImages mirroring those captures.
add_executable(smartinvert_test smartinvert_test.cpp)
set_target_properties(smartinvert_test PROPERTIES AUTOMOC TRUE)

target_include_directories(smartinvert_test PRIVATE ${CMAKE_SOURCE_DIR}/core)

target_link_libraries(smartinvert_test
    Qt::Core
    Qt::Gui
    Qt::Test
    krdccore
)

add_test(NAME smartinvert_test COMMAND smartinvert_test)

add_library(krdc_testplugin MODULE)

target_sources(krdc_testplugin PRIVATE
    testviewfactory.cpp
    testview.cpp
)

target_link_libraries(krdc_testplugin
    Qt::Core
    KF6::ConfigCore
    KF6::I18n
    KF6::CoreAddons
    Qt::Gui
    krdccore
)

install(TARGETS krdc_testplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/krdc)
