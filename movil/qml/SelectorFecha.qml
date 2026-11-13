// Calendario para elegir una fecha (con meses en castellano y la semana empezando el lunes).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: selector
    signal elegida(string iso)
    property int anio: 2026
    property int mes: 0                 // 0 = enero (como en JavaScript)
    property string elegidaIso: ""
    readonly property var nombresMeses: ["Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio", "Agosto",
                                         "Septiembre", "Octubre", "Noviembre", "Diciembre"]

    function aIso(d) {
        const m = d.getMonth() + 1, dia = d.getDate()
        return d.getFullYear() + "-" + (m < 10 ? "0" : "") + m + "-" + (dia < 10 ? "0" : "") + dia
    }
    function abrir(iso) {
        const p = iso.split("-")
        anio = parseInt(p[0]); mes = parseInt(p[1]) - 1
        elegidaIso = iso
        open()
    }
    function mover(pasos) {
        let m = mes + pasos, a = anio
        while (m < 0) { m += 12; a -= 1 }
        while (m > 11) { m -= 12; a += 1 }
        mes = m; anio = a
    }

    modal: true
    anchors.centerIn: parent
    width: Math.min(ventana.width - 24, 380)
    padding: 12
    standardButtons: Dialog.Cancel

    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Layout.fillWidth: true
            ToolButton { text: "‹"; font.pixelSize: 26; onClicked: selector.mover(-1) }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: selector.nombresMeses[selector.mes] + " " + selector.anio
                font.pixelSize: 17
                font.bold: true
            }
            ToolButton { text: "›"; font.pixelSize: 26; onClicked: selector.mover(1) }
        }
        DayOfWeekRow {
            Layout.fillWidth: true
            locale: Qt.locale("es_AR")
            delegate: Label {
                required property string shortName
                text: shortName.substring(0, 2)
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 12
                color: ventana.tintaApagada
            }
        }
        MonthGrid {
            id: grilla
            Layout.fillWidth: true
            month: selector.mes
            year: selector.anio
            locale: Qt.locale("es_AR")
            delegate: Label {
                required property var model
                readonly property string iso: selector.aIso(model.date)
                text: model.day
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: model.month === grilla.month ? 1 : 0.35
                font.bold: model.today
                padding: 8
                color: iso === selector.elegidaIso ? "white" : (ventana.oscuro ? "#f0efec" : "#0b0b0b")
                background: Rectangle {
                    radius: height / 2
                    color: iso === selector.elegidaIso ? ventana.azul : "transparent"
                    border.color: model.today ? ventana.azul : "transparent"
                }
            }
            onClicked: (fecha) => { selector.elegida(selector.aIso(fecha)); selector.close() }
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Hoy"
            flat: true
            onClicked: { selector.elegida(App.hoyIso()); selector.close() }
        }
    }
}
