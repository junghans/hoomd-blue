# Maintainer: joaander

# generic CPack settings
set(CPACK_DESCRIPTION_SUMMARY "Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition")
set(CPACK_PACKAGE_VENDOR "University of Michigan")
set(CPACK_PACKAGE_VERSION_MAJOR ${HOOMD_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${HOOMD_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${HOOMD_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "hoomd")

# the package name is upper case now, force it lower here to be pretty
set(CPACK_PACKAGE_NAME "hoomd")
set(CPACK_PACKAGE_VERSION "${HOOMD_VERSION_LONG}")

#set(CPACK_RESOURCE_FILE_LICENSE "${HOOMD_SOURCE_DIR}/LICENSE")

if (APPLE)
add_subdirectory(mac)
endif (APPLE)

