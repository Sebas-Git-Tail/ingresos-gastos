// Totales por mes (del año), por categoría (del período) y por año.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IngresosGastos

Page {
    // Renglón de mes o año: nombre y saldo arriba, ingresos y gastos abajo
    component FilaPeriodo: ColumnLayout {
        property var fila
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Label { text: fila.nombre; font.bold: fila.marcado || fila.total; font.pixelSize: 15; Layout.fillWidth: true }
            Label { text: fila.saldo; font.bold: fila.marcado || fila.total; font.pixelSize: 15 }
        }
        Label {
            text: "Ingresos " + fila.ingresos + "   ·   Gastos " + fila.gastos
            font.pixelSize: 12
            color: ventana.tintaSecundaria
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: ventana.borde }
    }
    component Titulo: Label {
        font.pixelSize: 17
        font.bold: true
        Layout.topMargin: 12
    }

    Flickable {
        anchors.fill: parent
        contentHeight: columna.implicitHeight + 24
        clip: true
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: columna
            width: parent.width - 32
            x: 16
            spacing: 8

            Titulo { text: "Por categoría — " + App.tituloCategorias }
            Repeater {
                model: App.totalesCategorias
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.topMargin: modelData.clase === "titulo" ? 8 : 0
                    spacing: 10
                    Rectangle {
                        width: 12; height: 12; radius: 3
                        color: modelData.color
                        visible: modelData.clase === "cat"
                    }
                    Label {
                        text: modelData.nombre
                        font.bold: modelData.clase === "titulo"
                        font.pixelSize: 15
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label { text: modelData.total; font.bold: modelData.clase === "titulo"; font.pixelSize: 15 }
                    Label {
                        text: modelData.pct
                        font.pixelSize: 13
                        color: ventana.tintaApagada
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 52
                    }
                }
            }

            Titulo { text: App.tituloMeses }
            Repeater {
                model: App.totalesMeses
                FilaPeriodo { required property var modelData; fila: modelData }
            }

            Titulo { text: "Por año" }
            Repeater {
                model: App.totalesAnios
                FilaPeriodo { required property var modelData; fila: modelData }
            }
        }
    }
}
