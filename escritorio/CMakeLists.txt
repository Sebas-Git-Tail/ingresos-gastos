find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Todo menos main(): así las pruebas usan exactamente el mismo código que el programa
add_library(igescritorio STATIC
    graficos.h graficos.cpp
    dialogomovimiento.h dialogomovimiento.cpp dialogo_movimiento.ui
    dialogocategorias.h dialogocategorias.cpp dialogo_categorias.ui
    dialogosincronizacion.h dialogosincronizacion.cpp dialogo_sincronizacion.ui
    ventanaprincipal.h ventanaprincipal.cpp main.ui
)
target_include_directories(igescritorio PUBLIC .)
target_link_libraries(igescritorio PUBLIC igcore Qt6::Widgets)
target_compile_options(igescritorio PRIVATE -Wall -Wextra)

add_executable(ingresos-gastos main.cpp)
target_link_libraries(ingresos-gastos PRIVATE igescritorio)
target_compile_options(ingresos-gastos PRIVATE -Wall -Wextra)

install(TARGETS ingresos-gastos RUNTIME DESTINATION bin)
install(FILES ${PROJECT_SOURCE_DIR}/data/ingresos-gastos.desktop DESTINATION share/applications)
install(FILES ${PROJECT_SOURCE_DIR}/data/ingresos-gastos.svg DESTINATION share/icons/hicolor/scalable/apps)
