// Tarjeta chica del período: título, monto y una rayita del color de la serie
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: tarjeta
    property string titulo
    property string valor
    property color marca: "transparent"

    Layout.fillWidth: true
    Layout.preferredWidth: 1
    implicitHeight: columna.implicitHeight + 16
    radius: 6
    color: ventana.superficie
    border.color: ventana.borde

    Rectangle {                                     // rayita de color a la izquierda (como en la PC)
        width: 4
        radius: 2
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
        color: tarjeta.marca
    }
    ColumnLayout {
        id: columna
        anchors { fill: parent; leftMargin: 10; rightMargin: 6; topMargin: 8; bottomMargin: 8 }
        spacing: 2
        Label { text: tarjeta.titulo; font.pixelSize: 12; color: ventana.tintaSecundaria }
        Label {
            Layout.fillWidth: true
            text: tarjeta.valor
            font.pixelSize: 15
            font.bold: true
            fontSizeMode: Text.HorizontalFit             // los montos grandes se achican, nunca se cortan
            minimumPixelSize: 9
        }
    }
}
