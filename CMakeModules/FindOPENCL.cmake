########################################################################
# Find OpenCL Wrapper 
# This is a find_package(catkin) wrapper for packages
# that are able to be build with and without ros
# The syntax is the same as find_package(catkin)
# Compatible with latest CUDA versions
# Additional Variables:
# OPENCL_FOUND - True if Ros code can be build
#
find_package(OpenCL)
if(NOT OpenCL_FOUND)
    find_package(CUDA)
    if(CUDA_FOUND)
    if(CUDA_OpenCL_LIBRARY)
        set(OpenCL_INCLUDE_DIRS "${CUDA_INCLUDE_DIRS}")
        set(OpenCL_LIBRARY "${CUDA_OpenCL_LIBRARY}")
        set(OpenCL_LIBRARIES "${CUDA_OpenCL_LIBRARY}" )
        set(OpenCL_FOUND 1)
        set(OPENCL_FOUND 1)
    endif()
    endif()
endif()
