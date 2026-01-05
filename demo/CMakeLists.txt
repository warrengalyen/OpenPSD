# PSD Viewer GUI Application

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK gtk+-3.0)
pkg_check_modules(CAIRO cairo)

if(GTK_FOUND AND CAIRO_FOUND)
    # Create executable
    add_executable(psd_viewer psd_viewer.c)
    
    # Link libraries
    target_link_libraries(psd_viewer 
        openpsd
        ${GTK_LIBRARIES}
        ${CAIRO_LIBRARIES}
    )
    
    # Include directories
    target_include_directories(psd_viewer PRIVATE
        ${GTK_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include
    )
    
    # Compiler flags
    target_compile_options(psd_viewer PRIVATE
        ${GTK_CFLAGS_OTHER}
        ${CAIRO_CFLAGS_OTHER}
    )
    
    message(STATUS "PSD Viewer: GTK and Cairo found - building GUI app")
else()
    message(STATUS "PSD Viewer: GTK and/or Cairo not found - skipping GUI app")
    if(NOT GTK_FOUND)
        message(STATUS "  Missing: gtk+-3.0")
    endif()
    if(NOT CAIRO_FOUND)
        message(STATUS "  Missing: cairo")
    endif()
endif()
