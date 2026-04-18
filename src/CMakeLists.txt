set(POWERD_CORE_SOURCES
    profilemodel.cpp
    profilemanager.cpp
    batterymonitor.cpp
    sensorcontroller.cpp
    sensorfwbackend.cpp
    sysfsbackend.cpp
    radiocontroller.cpp
    systemcontroller.cpp
    automationengine.cpp
)

set(POWERD_CORE_HEADERS
    common.h
    profilemodel.h
    profilemanager.h
    batterymonitor.h
    sensorcontroller.h
    sensorfwbackend.h
    sysfsbackend.h
    radiocontroller.h
    systemcontroller.h
    automationengine.h
)

add_library(powerd-core STATIC ${POWERD_CORE_SOURCES} ${POWERD_CORE_HEADERS})
target_include_directories(powerd-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(powerd-core PUBLIC Qt5::Core Qt5::DBus)

# Daemon executable
add_executable(bolide-powerd
    main.cpp
    dbusinterface.cpp
    dbusinterface.h
)
target_link_libraries(bolide-powerd PRIVATE powerd-core Qt5::Core Qt5::DBus)
install(TARGETS bolide-powerd DESTINATION ${CMAKE_INSTALL_BINDIR})

# D-Bus config
install(FILES ${CMAKE_SOURCE_DIR}/data/org.bolideos.powerd.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d/)

# systemd service
install(FILES ${CMAKE_SOURCE_DIR}/data/bolide-powerd.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)

# D-Bus introspection XML
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/org.bolideos.powerd.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces/)

# Default profiles data
install(FILES ${CMAKE_SOURCE_DIR}/data/default-profiles.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bolide-powerd/)
