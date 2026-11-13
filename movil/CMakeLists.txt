find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2)

# Todo menos main(): así las pruebas usan el mismo código que la app
qt_add_library(igmovil STATIC)
qt_add_qml_module(igmovil
    URI IngresosGastos
    VERSION 1.0
    RESOURCE_PREFIX /
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/IngresosGastos
    QML_FILES
        qml/Main.qml
        qml/Tarjeta.qml
        qml/PaginaMovimientos.qml
        qml/PaginaGraficos.qml
        qml/PaginaTotales.qml
        qml/EditorMovimiento.qml
        qml/SelectorFecha.qml
        qml/PaginaCategorias.qml
        qml/PaginaSincronizacion.qml
    SOURCES
        controlador.h controlador.cpp
        modelomovimientos.h modelomovimientos.cpp
        graficosquick.h graficosquick.cpp
)
target_include_directories(igmovil PUBLIC .)
target_link_libraries(igmovil PUBLIC igcore Qt6::Quick Qt6::QuickControls2)

qt_add_executable(ingresos-gastos-movil main.cpp)
target_link_libraries(ingresos-gastos-movil PRIVATE igmovil igmovilplugin)

# Android (Qt 6.9 o más nuevo): nombre, ícono y paquete de la app. En la PC se ignoran.
set_target_properties(ingresos-gastos-movil PROPERTIES
    QT_ANDROID_PACKAGE_NAME "ar.ingresosgastos.app"
    QT_ANDROID_APP_NAME "Ingresos y Gastos"
    QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
    QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    QT_ANDROID_VERSION_CODE 3
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
)
