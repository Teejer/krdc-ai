# Standalone test for the AI assistant's conversation engine: the risk
# classification that decides what needs approval is the part worth covering.
# Needs a mock endpoint, so it stays off by default; see test/run_ai_test.sh.
option(BUILD_AI_ENGINE_TEST "Build the AI conversation engine test (needs a mock server)" OFF)

if(BUILD_AI_ENGINE_TEST AND WITH_AI)
    add_executable(aiengine_test)

    target_sources(aiengine_test PRIVATE
        aiengine_test.cpp
        ${CMAKE_SOURCE_DIR}/ai/aiconfig.cpp
        ${CMAKE_SOURCE_DIR}/ai/aitools.cpp
        ${CMAKE_SOURCE_DIR}/ai/aikeys.cpp
        ${CMAKE_SOURCE_DIR}/ai/aihttpclient.cpp
        ${CMAKE_SOURCE_DIR}/ai/aiconversation.cpp
        ${CMAKE_SOURCE_DIR}/ai/aiconversationpage.cpp
        ${CMAKE_SOURCE_DIR}/ai/aisettingsdialog.cpp
    )

    target_include_directories(aiengine_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/ai
        ${CMAKE_SOURCE_DIR}/core
    )

    target_link_libraries(aiengine_test
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Network
        KF6::ConfigCore
        KF6::I18n
        krdccore
    )

    ecm_qt_declare_logging_category(aiengine_test
        HEADER krdc_debug.h
        IDENTIFIER KRDC
        CATEGORY_NAME KRDC
    )
endif()

if(NOT NDEBUG)
    set(BUILD_TEST true)
endif()

if(NOT BUILD_TEST)
    return()
endif()

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
