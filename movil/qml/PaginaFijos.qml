// Movimientos fijos: se cargan solos todos los meses (sueldo, alquiler, internet…).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import IngresosGastos

Page {
    id: pagina
    header: ToolBar {
        Material.foreground: "white"
        RowLayout {
            anchors.fill: parent
            ToolButton { text: "←"; font.pixelSize: 22; onClicked: pila.pop(); Accessible.name: "Volver" }
            Label { text: "Movimientos fijos"; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            Layout.fillWidth: true
            Layout.margins: 12
            wrapMode: Text.WordWrap
            color: ventana.tintaSecundaria
            text: "Se cargan solos el día indicado de cada mes (al abrir la app). Si borrás uno de los que se cargaron, no vuelve. También se crean desde un movimiento: mantenerlo apretado → «Repetir todos los meses»."
        }
        ListView {
            id: vista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.fijos
            delegate: ItemDelegate {
                required property var modelData
                width: vista.width
                onClicked: ventana.editarFijo(App.fijo(modelData.id))
                contentItem: RowLayout {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: modelData.descripcion !== "" ? modelData.descripcion : modelData.categoria; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            text: "Día " + modelData.dia + " · " + modelData.categoria + (modelData.activo ? "" : " · pausado")
                                  + (App.cuentas.length > 1 ? " · " + modelData.cuenta : "")
                            font.pixelSize: 12
                            color: ventana.tintaApagada
                        }
                    }
                    Label { text: modelData.montoTexto; font.pixelSize: 16; font.bold: true }
                }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: vista.count === 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: ventana.tintaApagada
                text: "Todavía no hay movimientos fijos."
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Button { text: "+ Ingreso fijo"; Layout.fillWidth: true; onClicked: ventana.editarFijo(App.nuevoFijo(0)) }
            Button { text: "+ Gasto fijo"; Layout.fillWidth: true; highlighted: true; onClicked: ventana.editarFijo(App.nuevoFijo(1)) }
        }
    }
}
