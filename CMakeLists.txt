cmake_minimum_required( VERSION 3.4.0 )
project(hydra_cam_plugin CXX)			

# Установка переменной со списком исходников
set(SOURCE_LIB 
    CamHostRaysDOF.cpp
    ../HydraAPI/hydra_api/HydraRngUtils.cpp
    ../HydraAPI/hydra_api/pugixml.cpp)		

SET (CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fopenmp")

find_package(OpenMP REQUIRED)

# Создание динамической библиотеки с именем example
add_library(hydra_cam_plugin SHARED ${SOURCE_LIB} )	

# does not works ... 

#if (WIN32)
#    set_target_properties(hydra_cam_plugin PROPERTIES
#                          LINK_FLAGS "/WHOLEARCHIVE")
#elseif (APPLE)
#    set_target_properties(hydra_cam_plugin PROPERTIES
#                          LINK_FLAGS "-Wl,-all_load")
#else ()
#    set_target_properties(hydra_cam_plugin PROPERTIES
#                          LINK_FLAGS "-Wl,--whole-archive")
#endif ()