// Cargar o editar una transferencia entre cuentas: plata que pasa de una cuenta a otra
// (no es ingreso ni gasto; solo cambia el saldo de cada cuenta).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import IngresosGastos

Page {
    id: editor
    property var datos: ({})            // lo que da App.nuevaTransferencia / App.transferencia / App.repetirTransferencia
    property string fechaIso: datos.fechaIso || App.hoyIso()
    readonly property bool nueva: !datos.id
    // Las cuentas de cuando se abrió: si la lista cambia mientras tanto (llegó una sincronización), los combos
    // no vuelven a la primera
    property var cuentas: []

    title: nueva ? "Nueva transferencia" : "Editar transferencia"

    Component.onCompleted: {
        cuentas = App.cuentas
        campoMonto.text = datos.monto || ""
        campoDescripcion.text = datos.descripcion || ""
        comboDesde.currentIndex = Math.max(0, cuentas.findIndex(c => c.id === datos.desdeCuenta))
        comboHasta.currentIndex = Math.max(0, cuentas.findIndex(c => c.id === datos.hastaCuenta))
        if (nueva) campoMonto.forceActiveFocus()
    }

    header: ToolBar {
        Material.foreground: "white"
        RowLayout {
            anchors.fill: parent
            ToolButton { text: "←"; font.pixelSize: 22; onClicked: pila.pop(); Accessible.name: "Volver" }
            Label { text: editor.title; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            ToolButton {
                text: "Repetir"
                visible: !editor.nueva
                onClicked: pila.replace(editor, { datos: App.repetirTransferencia(editor.datos.id) })
            }
            ToolButton {
                icon.source: "qrc:/IngresosGastos/iconos/eliminar.svg"
                visible: !editor.nueva
                onClicked: { const id = editor.datos.id; pila.pop(); ventana.borrar(id) }   // sin preguntar: el aviso ofrece «Deshacer»
                Accessible.name: "Eliminar"
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: formulario.implicitHeight + 32
        clip: true

        ColumnLayout {
            id: formulario
            width: parent.width - 32
            x: 16
            y: 16
            spacing: 14

            Label {
                Layout.fillWidth: true
                text: "No cuenta como ingreso ni como gasto: solo pasa la plata de una cuenta a la otra."
                wrapMode: Text.WordWrap
                color: ventana.tintaSecundaria
            }

            Label { text: "Sale de"; color: ventana.tintaSecundaria }
            ComboBox {
                id: comboDesde
                Layout.fillWidth: true
                model: editor.cuentas
                textRole: "nombre"
                valueRole: "id"
            }

            Label { text: "Entra en"; color: ventana.tintaSecundaria }
            ComboBox {
                id: comboHasta
                Layout.fillWidth: true
                model: editor.cuentas
                textRole: "nombre"
                valueRole: "id"
            }

            Label { text: "Monto ($)"; color: ventana.tintaSecundaria }
            TextField {
                id: campoMonto
                Layout.fillWidth: true
                placeholderText: "Ej.: 15.430,50"
                font.pixelSize: 24
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: error.text = ""
            }

            Label { text: "Fecha"; color: ventana.tintaSecundaria }
            Button {
                Layout.fillWidth: true
                text: App.fechaTexto(editor.fechaIso)
                font.capitalization: Font.MixedCase
                onClicked: selector.abrir(editor.fechaIso)
            }

            Label { text: "Descripción"; color: ventana.tintaSecundaria }
            TextField {
                id: campoDescripcion
                Layout.fillWidth: true
                placeholderText: "Opcional"
            }

            Label {
                id: error
                Layout.fillWidth: true
                color: "#e34948"
                wrapMode: Text.WordWrap
                visible: text !== ""
            }

            Button {
                Layout.fillWidth: true
                Layout.topMargin: 8
                text: "Guardar"
                highlighted: true
                onClicked: {
                    const e = App.guardarTransferencia({
                        id: editor.datos.id || 0,
                        desdeCuenta: comboDesde.currentValue !== undefined ? comboDesde.currentValue : 0,
                        hastaCuenta: comboHasta.currentValue !== undefined ? comboHasta.currentValue : 0,
                        monto: campoMonto.text,
                        fechaIso: editor.fechaIso,
                        descripcion: campoDescripcion.text
                    })
                    if (e === "") pila.pop()
                    else error.text = e
                }
            }
        }
    }

    SelectorFecha {
        id: selector
        onElegida: (iso) => editor.fechaIso = iso
    }
}
