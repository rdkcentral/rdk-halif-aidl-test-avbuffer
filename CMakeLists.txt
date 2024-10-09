# Minimum required version of CMake
cmake_minimum_required(VERSION 3.8)

# Set the project name and version
project(AvBufferService VERSION 1.0)

# Set C++ standard for the entire project
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Global installation prefix, default to 'output' directory in the root
if (NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/output")
endif()
message(STATUS "Install prefix set to ${CMAKE_INSTALL_PREFIX}")

# Global Linux Binder directory, to be used by all sub-projects
if (NOT DEFINED LINUX_BINDER_OUT_DIR)
    message(STATUS "Setting LINUX_BINDER_OUT_DIR to default value")
    set(LINUX_BINDER_OUT_DIR "${CMAKE_SOURCE_DIR}/../rdk-halif-binder/local")
endif()
message(STATUS "LINUX_BINDER_OUT_DIR set to ${LINUX_BINDER_OUT_DIR}")

# AIDL Example Generation directory
if (NOT DEFINED AIDL_EXAMPLE_GEN_DIR)
    message(STATUS "Setting AIDL_EXAMPLE_GEN_DIR to default value")
    set(AIDL_EXAMPLE_GEN_DIR "${CMAKE_SOURCE_DIR}/hal/avbuffer")
endif()
message(STATUS "AIDL_EXAMPLE_GEN_DIR set to ${AIDL_EXAMPLE_GEN_DIR}")

# avbuffer AIDL Version
if (NOT DEFINED AVBUFFER_AIDL_VERSION)
    message(STATUS "Setting AVBUFFER_AIDL_VERSION to latest version")
    set(AVBUFFER_AIDL_VERSION "${AIDL_EXAMPLE_GEN_DIR}/current")
endif()
message(STATUS "AVBUFFER_AIDL_VERSION set to ${AVBUFFER_AIDL_VERSION}")

# Add subdirectories
# These will invoke the CMakeLists.txt inside MockService and L1Test directories
#add_subdirectory(MockService)
add_subdirectory(L1Test)

## Final installation targets
#install(DIRECTORY ${CMAKE_SOURCE_DIR}/MockService
# DESTINATION ${CMAKE_INSTALL_PREFIX}
# PATTERN "CMakeLists.txt" EXCLUDE)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/L1Test
        DESTINATION ${CMAKE_INSTALL_PREFIX}
        PATTERN "CMakeLists.txt" EXCLUDE)

message(STATUS "Avbuffer project setup is complete.")

