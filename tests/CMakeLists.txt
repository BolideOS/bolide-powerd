# Pass source dir to tests so they can find data files
add_definitions(-DCMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(tst_profilemodel tst_profilemodel.cpp)
target_link_libraries(tst_profilemodel Qt5::Test powerd-core)
add_test(NAME tst_profilemodel COMMAND tst_profilemodel)

add_executable(tst_profilemanager tst_profilemanager.cpp)
target_link_libraries(tst_profilemanager Qt5::Test powerd-core)
add_test(NAME tst_profilemanager COMMAND tst_profilemanager)

add_executable(tst_batterymonitor tst_batterymonitor.cpp)
target_link_libraries(tst_batterymonitor Qt5::Test powerd-core)
add_test(NAME tst_batterymonitor COMMAND tst_batterymonitor)

add_executable(tst_controllers tst_controllers.cpp)
target_link_libraries(tst_controllers Qt5::Test powerd-core)
add_test(NAME tst_controllers COMMAND tst_controllers)

add_executable(tst_automationengine tst_automationengine.cpp)
target_link_libraries(tst_automationengine Qt5::Test powerd-core)
add_test(NAME tst_automationengine COMMAND tst_automationengine)

add_executable(tst_dbusinterface tst_dbusinterface.cpp
    ${CMAKE_SOURCE_DIR}/src/dbusinterface.cpp)
target_link_libraries(tst_dbusinterface Qt5::Test Qt5::DBus powerd-core)
target_include_directories(tst_dbusinterface PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_dbusinterface COMMAND tst_dbusinterface)
