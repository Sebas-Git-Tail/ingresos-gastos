// Categorías: agregar, renombrar, cambiar el color y eliminar (pasando los movimientos a otra).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import IngresosGastos

Page {
    id: pagina
    property int tipo: 1                // 1 = gastos, 0 = ingresos
    property var lista: App.categorias(tipo)
    property var elegida: ({})

    function recargar() { lista = App.categorias(tipo) }
    function mostrar(e) { if (e !== "") ventana.avisar(e); recargar() }
    onTipoChanged: recargar()

    header: ToolBar {
        Material.foreground: "white"
        RowLayout {
            anchors.fill: parent
            ToolButton { text: "←"; font.pixelSize: 22; onClicked: pila.pop(); Accessible.name: "Volver" }
            Label { text: "Categorías"; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TabBar {
            Layout.fillWidth: true
            currentIndex: pagina.tipo === 1 ? 0 : 1
            onCurrentIndexChanged: pagina.tipo = currentIndex === 0 ? 1 : 0
            TabButton { text: "Gastos" }
            TabButton { text: "Ingresos" }
        }
        ListView {
            id: vista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: pagina.lista
            delegate: ItemDelegate {
                required property var modelData
                width: vista.width
                height: 56
                onClicked: { pagina.elegida = modelData; opciones.popup() }
                contentItem: RowLayout {
                    spacing: 12
                    Rectangle { width: 14; height: 14; radius: 3; color: modelData.color }
                    Label { text: modelData.nombre; font.pixelSize: 15; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label {
                        text: (modelData.movimientos === 1 ? "1 movimiento" : modelData.movimientos + " movimientos")
                              + (modelData.presupuesto !== "" ? "\nPresupuesto $ " + modelData.presupuesto : "")
                        font.pixelSize: 12
                        color: ventana.tintaApagada
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }
        Button {
            Layout.fillWidth: true
            Layout.margins: 12
            text: pagina.tipo === 1 ? "+ Agregar categoría de gastos" : "+ Agregar categoría de ingresos"
            highlighted: true
            onClicked: { campoNombre.text = ""; dialogoNombre.renombrando = false; dialogoNombre.open() }
        }
    }

    Menu {
        id: opciones
        MenuItem { text: "Renombrar"; onTriggered: { campoNombre.text = pagina.elegida.nombre; dialogoNombre.renombrando = true; dialogoNombre.open() } }
        MenuItem { text: "Cambiar color"; onTriggered: dialogoColor.open() }
        MenuItem {
            text: "Presupuesto por mes"
            enabled: pagina.tipo === 1
            height: enabled ? implicitHeight : 0
            onTriggered: { campoPresupuesto.text = pagina.elegida.presupuesto; dialogoPresupuesto.open() }
        }
        MenuItem { text: "Eliminar"; enabled: pagina.lista.length > 1; onTriggered: dialogoEliminar.abrir() }
    }

    Dialog {
        id: dialogoNombre
        property bool renombrando: false
        title: renombrando ? "Renombrar «" + pagina.elegida.nombre + "»" : "Nueva categoría"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: campoNombre; width: parent.width; placeholderText: "Nombre" }
        onOpened: campoNombre.forceActiveFocus()
        onAccepted: {
            if (renombrando) pagina.mostrar(App.renombrarCategoria(pagina.elegida.id, campoNombre.text))
            else { const r = App.crearCategoria(campoNombre.text, pagina.tipo); pagina.mostrar(r.ok ? "" : r.error) }
        }
    }

    Dialog {
        id: dialogoColor
        title: "Color de «" + pagina.elegida.nombre + "»"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel
        GridLayout {
            columns: 4
            rowSpacing: 12
            columnSpacing: 12
            Repeater {
                model: App.colores
                Rectangle {
                    required property color modelData
                    required property int index
                    width: 52; height: 52; radius: 8
                    color: modelData
                    border.width: index === pagina.elegida.colorIndice ? 3 : 0
                    border.color: ventana.oscuro ? "white" : "black"
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { pagina.mostrar(App.cambiarColor(pagina.elegida.id, index)); dialogoColor.close() }
                    }
                }
            }
        }
    }

    Dialog {
        id: dialogoPresupuesto
        title: "Presupuesto de «" + pagina.elegida.nombre + "»"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "¿Cuánto pensás gastar por mes? Vacío = sin presupuesto. Si te pasás, la pantalla principal avisa." }
            TextField { id: campoPresupuesto; Layout.fillWidth: true; placeholderText: "150.000"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
        }
        onAccepted: pagina.mostrar(App.fijarPresupuesto(pagina.elegida.id, campoPresupuesto.text))
    }

    Dialog {
        id: dialogoEliminar
        property var otras: []
        function abrir() {
            otras = pagina.lista.filter(c => c.id !== pagina.elegida.id)
            let porDefecto = 0
            for (let i = 0; i < otras.length; ++i) if (otras[i].nombre.indexOf("Varios") >= 0) porDefecto = i
            destino.currentIndex = porDefecto
            open()
        }
        title: "Eliminar «" + pagina.elegida.nombre + "»"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: pagina.elegida.movimientos > 0
                      ? "Tiene " + pagina.elegida.movimientos + " movimiento(s). ¿A qué categoría los paso?"
                      : "¿Eliminar esta categoría?"
            }
            ComboBox {
                id: destino
                Layout.fillWidth: true
                visible: pagina.elegida.movimientos > 0
                model: dialogoEliminar.otras
                textRole: "nombre"
                valueRole: "id"
            }
        }
        onAccepted: pagina.mostrar(App.eliminarCategoria(pagina.elegida.id,
                                   pagina.elegida.movimientos > 0 ? destino.currentValue : 0))
    }
}
