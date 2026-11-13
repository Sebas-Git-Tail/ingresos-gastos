// Lista de movimientos del período, agrupados por día. Tocar uno lo edita; mantener apretado, más opciones.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IngresosGastos

Page {
    id: pagina

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 8
            TextField {
                Layout.fillWidth: true
                placeholderText: "Buscar…"
                text: App.busqueda
                onTextChanged: App.busqueda = text
                inputMethodHints: Qt.ImhNoPredictiveText
            }
            ComboBox {
                model: ["Todos", "Ingresos", "Gastos"]
                currentIndex: App.filtroTipo
                onActivated: (i) => App.filtroTipo = i
                implicitWidth: 120
            }
        }

        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.movimientos
            section.property: "dia"
            section.delegate: Label {
                required property string section
                width: lista.width
                text: App.fechaTexto(section)
                leftPadding: 16
                topPadding: 12
                bottomPadding: 4
                font.pixelSize: 13
                font.bold: true
                color: ventana.tintaSecundaria
            }
            ScrollIndicator.vertical: ScrollIndicator {}

            delegate: ItemDelegate {
                id: fila
                required property var idMov
                required property string categoria
                required property color colorCategoria
                required property string descripcion
                required property string montoTexto
                required property bool ingreso
                width: lista.width
                height: 60
                onClicked: ventana.editar(App.movimiento(idMov))
                onPressAndHold: { menuFila.idMov = idMov; menuFila.popup() }

                contentItem: RowLayout {
                    spacing: 12
                    Rectangle { width: 12; height: 12; radius: 3; color: fila.colorCategoria }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label { text: fila.categoria; font.pixelSize: 15; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            text: fila.descripcion
                            visible: text !== ""
                            font.pixelSize: 13
                            color: ventana.tintaSecundaria
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Label { text: fila.montoTexto; font.pixelSize: 15; font.bold: fila.ingreso }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: lista.count === 0
                text: App.busqueda !== "" || App.filtroTipo !== 0 ? "Nada coincide con la búsqueda"
                                                                  : "No hay movimientos en " + App.periodoTexto
                color: ventana.tintaApagada
            }
        }

        Label {
            Layout.fillWidth: true
            text: App.cantidadTexto
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 12
            color: ventana.tintaApagada
            bottomPadding: 4
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            spacing: 12
            Button {
                Layout.fillWidth: true
                text: "+ Ingreso"
                highlighted: true
                onClicked: ventana.editar(App.nuevoMovimiento(0))
            }
            Button {
                Layout.fillWidth: true
                text: "− Gasto"
                highlighted: true
                onClicked: ventana.editar(App.nuevoMovimiento(1))
            }
        }
    }

    Menu {
        id: menuFila
        property var idMov
        MenuItem { text: "Editar"; onTriggered: ventana.editar(App.movimiento(menuFila.idMov)) }
        MenuItem { text: "Repetir con fecha de hoy"; onTriggered: ventana.editar(App.repetir(menuFila.idMov)) }
        MenuItem { text: "Eliminar"; onTriggered: confirmarBorrar.abrir(menuFila.idMov) }
    }

    Dialog {
        id: confirmarBorrar
        property var idMov
        function abrir(id) { idMov = id; open() }
        title: "¿Eliminar este movimiento?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: App.eliminarMovimiento(idMov)
    }
}
