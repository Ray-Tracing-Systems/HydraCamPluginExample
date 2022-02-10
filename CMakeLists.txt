cmake_minimum_required( VERSION 3.4.0 )
project(hydra_cam_plugin CXX)			

# Установка переменной со списком исходников
set(SOURCE_LIB CamHostRaysDOF.cpp)		

# Создание динамической библиотеки с именем example
add_library(hydra_cam_plugin SHARED ${SOURCE_LIB} )	
