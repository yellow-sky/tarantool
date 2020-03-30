# A macro to build the bundled libujit
macro(pmdk_build)
    set(LIBPMDK_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/pmdk)
    set(LIBPMDK_BINARY_DIR ${PROJECT_BINARY_DIR}/build/pmdk/work)
    set(LIBPMDK_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/pmdk/dest)

    set (pmdk_cflags ${CMAKE_C_FLAGS})
    set (pmdk_ldflags ${CMAKE_EXE_LINKER_FLAGS})
    separate_arguments(pmdk_cflags)
    separate_arguments(pmdk_ldflags)

    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (pmdk_ccopt -O0)
        set (pmdk_src_dir debug)
    else ()
        set (pmdk_ccopt -O2)
        set (pmdk_src_dir nondebug)
    endif()

    set(pmdk_buildoptions
        BUILDMODE=static
        HOST_CC="${pmdk_hostcc}"
        TARGET_CC="${pmdk_cc}"
        TARGET_CFLAGS="${pmdk_cflags}"
        TARGET_LD="${pmdk_ld}"
        TARGET_LDFLAGS="${pmdk_ldflags}"
        TARGET_AR="${pmdk_ar}"
        TARGET_STRIP="${pmdk_strip}"
        TARGET_SYS="${CMAKE_SYSTEM_NAME}"
        CCOPT="${pmdk_ccopt}"
        CCDEBUG="${pmdk_ccdebug}"
        XCFLAGS="${pmdk_xcflags}"
    )
    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/pmdk/src/${pmdk_src_dir}/libpmemlog.so
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/pmdk
        COMMAND ${ECHO} 'pandoc is required to build doc' > .skip-doc
        COMMAND $(MAKE) ${pmdk_buildoptions} clean
        COMMAND $(MAKE) ${pmdk_buildoptions}
        DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
    )
    add_custom_target(libpmdk
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/pmdk/src/${pmdk_src_dir}/libpmemlog.so
    )
    add_dependencies(build_bundled_libs libpmdk)
    unset (pmdk_buildoptions)


    set(inc "${PROJECT_SOURCE_DIR}/third_party/pmdk/src/include/")
    install(FILES ${inc}/libpmemlog.h
        DESTINATION ${MODULE_INCLUDEDIR})
    unset(LIBPMDK_INSTALL_DIR)
    unset(LIBPMDK_BINARY_DIR)
    set(PMDK_LIB "${PROJECT_BINARY_DIR}/third_party/pmdk/src/${pmdk_src_dir}/libpmemlog.so")
    set(PMDK_INCLUDE "${PROJECT_SOURCE_DIR}/third_party/pmdk/src/include/")
    set(PMDK_INCLUDE_DIRS ${PMDK_INCLUDE})
    set(PMDK_LIBRARIES ${PMDK_LIB})
    find_package_handle_standard_args(PMDK
        REQUIRED_VARS PMDK_INCLUDE PMDK_LIB)
endmacro(pmdk_build)

# vim: et sw=4 ts=4 sts=4:
