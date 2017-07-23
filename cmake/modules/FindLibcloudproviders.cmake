# FindLibcloudproviders.cmake

# LIBCLOUDPROVIDERS_FOUND - System has LIBCLOUDPROVIDERS
# LIBCLOUDPROVIDERS_INCLUDES - The LIBCLOUDPROVIDERS include directories
# LIBCLOUDPROVIDERS_LIBRARIES - The libraries needed to use LIBCLOUDPROVIDERS
# LIBCLOUDPROVIDERS_DEFINITIONS - Compiler switches required for using LIBCLOUDPROVIDERS

include(FindPackageHandleStandardArgs)
find_package(PkgConfig)

find_path(LIBCLOUDPROVIDERS_INCLUDE_DIR
    NAMES cloudprovider.h cloudproviderproxy.h
    PATH_SUFFIXES cloudproviders
    PATHS
    /usr/lib
    /usr/lib/${CMAKE_ARCH_TRIPLET}
    /usr/local/lib
    /opt/local/lib
    ${CMAKE_LIBRARY_PATH}
    ${CMAKE_INSTALL_PREFIX}/lib
    /home/jus/jhbuild/install/include
)
find_library(LIBCLOUDPROVIDERS_LIBRARY
    NAMES libcloudproviders.so
    PATHS /home/jus/jhbuild/install/lib
)
message ( ${LIBCLOUDPROVIDERS_LIBRARY} )
message ( ${LIBCLOUDPROVIDERS_INCLUDE_DIR} )
set(LIBCLOUDPROVIDERS_LIBRARIES ${LIBCLOUDPROVIDERS_LIBRARY})
set(LIBCLOUDPROVIDERS_INCLUDE_DIRS ${LIBCLOUDPROVIDERS_INCLUDE_DIR})

