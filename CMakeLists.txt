cmake_minimum_required(VERSION 3.20)
project(ingresos-gastos VERSION 2.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)   # convierte los .ui de Qt Designer en código al compilar

# Qué se construye:
#   PC (Arch):  la versión de escritorio. Con -DIG_MOVIL=ON también la del celular, para probarla en la PC.
#   Android:    solo la del celular (Qt Creator con el kit «Android Qt 6.x»)
if(ANDROID)
    set(IG_ESCRITORIO OFF)
    set(IG_MOVIL ON)
else()
    option(IG_ESCRITORIO "Versión de escritorio (Qt Widgets, pantallas de Qt Designer)" ON)
    option(IG_MOVIL "Versión para celular (Qt Quick); en la PC sirve para probarla" OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Sql Network)

# Código compartido (repo comun-qt, traído con git subtree a comun/): la sincronización cifrada
# PC ↔ celular, la misma que usa gestor-stock-cpp
set(COMUN_SINC ON)
add_subdirectory(comun)

# El núcleo lo comparten las dos versiones: base de datos, CSV, pesos, sincronización y el dibujo de los gráficos
add_library(igcore STATIC
    nucleo/modelos.h
    nucleo/dinero.h nucleo/dinero.cpp
    nucleo/csv.h nucleo/csv.cpp
    nucleo/db.h nucleo/db.cpp
    nucleo/sincronizacion.h
    nucleo/dibujo.h nucleo/dibujo.cpp
)
target_include_directories(igcore PUBLIC nucleo)
target_link_libraries(igcore PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network comun_sinc)
target_compile_definitions(igcore PUBLIC IG_VERSION="${PROJECT_VERSION}")
if(NOT MSVC)
    target_compile_options(igcore PRIVATE -Wall -Wextra)
endif()

if(IG_ESCRITORIO)
    add_subdirectory(escritorio)
endif()
if(IG_MOVIL)
    add_subdirectory(movil)
endif()

if(NOT ANDROID)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
