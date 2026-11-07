cmake_minimum_required(VERSION 3.5)
project(ru.mai.lab2tasks CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# QtWebSockets is NOT used; HTTPS REST is implemented in C++ via Qt Network.
find_package(Qt5 COMPONENTS Core Network Qml Gui Quick Sql REQUIRED)
include(FindPkgConfig)
pkg_search_module(AURORA auroraapp REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/TaskBackend.h
    src/TaskBackend.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE ${AURORA_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Quick Qt5::Network Qt5::Sql Qt5::Qml ${AURORA_LDFLAGS})

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY qml DESTINATION share/${PROJECT_NAME})
install(FILES ${PROJECT_NAME}.desktop DESTINATION share/applications)
foreach(_size 86x86 108x108 128x128 172x172)
    install(FILES icons/${_size}/${PROJECT_NAME}.png DESTINATION share/icons/hicolor/${_size}/apps)
endforeach()

# Keep non-compiled files visible in Qt Creator.
add_custom_target(distfiles SOURCES
    qml/lab2tasks.qml
    qml/pages/MainPage.qml
    qml/cover/CoverPage.qml
    ${PROJECT_NAME}.desktop
    rpm/${PROJECT_NAME}.spec
)
file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}\n${CMAKE_BINARY_DIR}/${PROJECT_NAME}:bin\n")
