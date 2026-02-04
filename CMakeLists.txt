cmake_minimum_required(VERSION 3.10)
project(GattServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(sdbus-c++ REQUIRED)
find_package(sdbus-c++-tools REQUIRED)

# XML Generation setup
set(DBUS_XML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dbus")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(DBUS_XML_FILES
    "org.bluez.GattService1.xml"
    "org.bluez.GattCharacteristic1.xml"
    "org.bluez.LEAdvertisement1.xml"
    "org.bluez.MediaEndpoint1.xml"
)

set(GENERATED_SOURCES "")

foreach(XML_FILE ${DBUS_XML_FILES})
    set(INPUT_XML "${DBUS_XML_DIR}/${XML_FILE}")
    
    # Logic: org.bluez.Name.xml -> Name_adaptor.h
    string(REPLACE "org.bluez." "" BASE_NAME ${XML_FILE})
    string(REPLACE ".xml" "" BASE_NAME ${BASE_NAME})
    set(HEADER_NAME "${BASE_NAME}_adaptor.h")
    
    set(OUTPUT_HEADER "${GENERATED_DIR}/${HEADER_NAME}")
    
    add_custom_command(
        OUTPUT ${OUTPUT_HEADER}
        COMMAND SDBusCpp::sdbus-c++-xml2cpp ${INPUT_XML} --adaptor=${OUTPUT_HEADER}
        DEPENDS ${INPUT_XML}
        COMMENT "Generating adaptor header for ${XML_FILE}"
    )
    list(APPEND GENERATED_SOURCES ${OUTPUT_HEADER})
endforeach()

add_executable(GattServer
    src/GattServer.cpp
    src/main.cpp
    ${GENERATED_SOURCES}
)

target_include_directories(GattServer PRIVATE src ${GENERATED_DIR})

target_link_libraries(GattServer PRIVATE SDBusCpp::sdbus-c++)
