#####################################################################################
# Set source files
#####################################################################################

set(KABOOM_SOURCES
    Options.cpp
    Main.cpp
)

#####################################################################################
# Setup dependencies to external libraries
#####################################################################################

set(LVR2_KABOOM_DEPENDENCIES
	lvr2_static
	lvr2las_static
	lvr2rply_static
	lvr2slam6d_static
	${OPENGL_LIBRARIES}
	${GLUT_LIBRARIES}
	${OpenCV_LIBS}
)

if(PCL_FOUND)
    set(LVR2_KABOOM_DEPENDENCIES ${LVR2_LEICA_DEPENDENCIES} ${PCL_LIBRARIES} )
endif(PCL_FOUND)

#####################################################################################
# Add executable
#####################################################################################

add_executable(lvr2_kaboom ${KABOOM_SOURCES})
target_link_libraries(lvr2_kaboom ${LVR2_KABOOM_DEPENDENCIES})

install(TARGETS lvr2_kaboom
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
