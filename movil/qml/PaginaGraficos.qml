// Barras de ingresos y gastos por mes (o por año) y tortas por categoría. Tocar una barra la señala.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IngresosGastos

Page {
    Flickable {
        anchors.fill: parent
        contentHeight: columna.implicitHeight + 24
        clip: true
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: columna
            width: parent.width - 24
            x: 12
            y: 12
            spacing: 12

            GraficoBarras {
                id: barras
                Layout.fillWidth: true
                Layout.preferredHeight: 250
                datos: App.barras
                oscuro: ventana.oscuro
                fuente: Qt.font({ pixelSize: 12 })
                Accessible.name: "Ingresos y gastos por " + (App.vista === 2 ? "año" : "mes")
            }
            // Lo que se tocó: los números y un botón para ir a ese mes
            RowLayout {
                Layout.fillWidth: true
                visible: barras.seleccionado >= 0
                Label {
                    Layout.fillWidth: true
                    text: barras.detalleSeleccionado
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }
                Button {
                    text: App.vista === 2 ? "Ver año" : "Ver mes"
                    flat: true
                    onClicked: App.irAPeriodo(barras.seleccionado)
                }
            }
            Label {
                Layout.fillWidth: true
                visible: barras.seleccionado < 0
                text: "Tocá un " + (App.vista === 2 ? "año" : "mes") + " para ver sus números"
                font.pixelSize: 12
                color: ventana.tintaApagada
                horizontalAlignment: Text.AlignHCenter
            }

            GraficoTorta {
                id: tortaGastos
                Layout.fillWidth: true
                Layout.preferredHeight: altoNecesario
                datos: App.tortaGastos
                oscuro: ventana.oscuro
                fuente: Qt.font({ pixelSize: 13 })
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: tortaGastos.detalleSeleccionada
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
            GraficoTorta {
                id: tortaIngresos
                Layout.fillWidth: true
                Layout.preferredHeight: altoNecesario
                datos: App.tortaIngresos
                oscuro: ventana.oscuro
                fuente: Qt.font({ pixelSize: 13 })
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: tortaIngresos.detalleSeleccionada
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
        }
    }
}
