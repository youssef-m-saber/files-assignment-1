cmake_minimum_required(VERSION 4.0)
project(files_assignment_1)

set(CMAKE_CXX_STANDARD 14)

include_directories(.vs/src)

add_executable(files_assignment_1
        .vs/src/Appointments.cpp
        .vs/src/Appointments.h
        .vs/src/Binary_file.cpp
        .vs/src/Binary_file.h
        .vs/src/Doctors.cpp
        .vs/src/Doctors.h
        .vs/src/main.cpp
        .vs/src/QueryEngine.cpp
        .vs/src/QueryEngine.h)
