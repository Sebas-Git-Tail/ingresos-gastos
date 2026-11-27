// Lista de movimientos del período, agrupados por día. Tocar uno lo edita; mantener apretado, más opciones.
// Borrar no pregunta: se borra y el aviso de abajo ofrece «Deshacer» (como en la PC).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Comun.Ui
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
                placeholderText: "Buscar (también #etiqueta, >20000)…"
                text: App.busqueda
                onTextChanged: App.busqueda = text
                inputMethodHints: Qt.ImhNoPredictiveText
            }
            ComboBox {
                model: ["Todos", "Ingresos", "Gastos", "Transferencias"]
                currentIndex: App.filtroTipo
                onActivated: (i) => App.filtroTipo = i
                implicitWidth: 150
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
                required property bool transferencia
                width: lista.width
                height: 60
                onClicked: ventana.editar(App.movimiento(idMov))
                onPressAndHold: { menuFila.idMov = idMov; menuFila.transferencia = transferencia; menuFila.popup() }

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
                    // Una transferencia no es ingreso ni gasto: el monto en el color normal
                    Label {
                        text: fila.montoTexto
                        font.pixelSize: 15
                        font.bold: fila.ingreso && !fila.transferencia
                        color: ventana.colorMonto(fila.transferencia ? 0 : fila.ingreso ? 1 : -1)
                    }
                }
            }

            EstadoVacio {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: lista.count === 0
                readonly property bool filtrando: App.busqueda !== "" || App.filtroTipo !== 0
                icono: filtrando ? "buscar" : "dinero"
                titulo: filtrando ? "Nada coincide con la búsqueda" : "No hay movimientos en " + App.periodoTexto
                texto: filtrando ? "Probá con otra palabra, una categoría o un monto." : "Cargá el primero con el botón de abajo."
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
            Button {   // con una sola cuenta no hay a dónde transferir
                Layout.fillWidth: true
                visible: App.cuentas.length > 1
                text: "Transferir"
                onClicked: ventana.editarTransferencia(App.nuevaTransferencia())
            }
        }
    }

    Menu {
        id: menuFila
        property var idMov
        property bool transferencia: false
        MenuItem { text: "Editar"; onTriggered: ventana.editar(App.movimiento(menuFila.idMov)) }
        MenuItem {
            text: "Repetir con fecha de hoy"
            onTriggered: menuFila.transferencia ? ventana.editarTransferencia(App.repetirTransferencia(menuFila.idMov))
                                                : ventana.editar(App.repetir(menuFila.idMov))
        }
        MenuItem {   // los fijos son ingresos o gastos
            text: "Repetir todos los meses"
            enabled: !menuFila.transferencia
            onTriggered: ventana.editarFijo(App.fijoDesdeMovimiento(menuFila.idMov))
        }
        MenuItem { text: "Eliminar"; onTriggered: ventana.borrar(menuFila.idMov) }
    }
}
