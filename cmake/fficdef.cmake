# A helper function for unified Lua ffi.cdef builder
function(rebuild_module_fficdef)
    set (dstfile "${CMAKE_CURRENT_BINARY_DIR}/lua/load_ffi_defs.lua")
    set (tmpfile "${dstfile}.new")
    set (headers)
    # Get absolute path for header files (required of out-of-source build)
    foreach (header ${ARGN})
        if (IS_ABSOLUTE ${header})
            list(APPEND headers ${header})
        else()
            list(APPEND headers ${CMAKE_CURRENT_SOURCE_DIR}/${header})
        endif()
    endforeach()

    set (cflags ${CMAKE_C_FLAGS})
    separate_arguments(cflags)
    # Pass sysroot settings on OSX
    if (NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
        set (cflags ${cflags} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT})
    endif()
    add_custom_command(OUTPUT ${dstfile}
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/lua/load_ffi_defs.header.lua > ${tmpfile}
        COMMAND cat ${headers} | ${CMAKE_SOURCE_DIR}/extra/fficdefgen >> ${tmpfile}
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/lua/load_ffi_defs.footer.lua >> ${tmpfile}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmpfile} ${dstfile}
        COMMAND ${CMAKE_COMMAND} -E remove ${errcodefile} ${tmpfile}
        DEPENDS ${CMAKE_SOURCE_DIR}/extra/fficdefgen
                ${CMAKE_CURRENT_SOURCE_DIR}/lua/load_ffi_defs.header.lua
                ${CMAKE_CURRENT_SOURCE_DIR}/lua/load_ffi_defs.footer.lua
                ${CMAKE_SOURCE_DIR}/src/box/errcode.h
                ${headers}
        )

    add_custom_target(fficdef ALL DEPENDS ${dstfile})
    install(FILES ${dstfile} DESTINATION ${MODULE_INCLUDEDIR})
endfunction()
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/lua/load_ffi_cdef.lua" PROPERTIES GENERATED HEADER_FILE_ONLY)
