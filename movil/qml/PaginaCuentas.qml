// Cuentas de dinero: efectivo, banco, Mercado Pago… Agregar, renombrar, saldo inicial y eliminar.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import IngresosGastos

Page {
    id: pagina
    property var elegida: ({})
    function mostrar(e) { if (e !== "") ventana.avisar(e) }

    header: ToolBar {
        Material.foreground: "white"
        RowLayout {
            anchors.fill: parent
            ToolButton { text: "←"; font.pixelSize: 22; onClicked: pila.pop(); Accessible.name: "Volver" }
            Label { text: "Cuentas"; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
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
            text: "Dónde está la plata. Cada movimiento va en una cuenta; arriba, en la pantalla principal, se puede mirar una sola o todas."
        }
        ListView {
            id: vista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.cuentas
            delegate: ItemDelegate {
                required property var modelData
                width: vista.width
                onClicked: { pagina.elegida = modelData; opciones.popup() }
                contentItem: RowLayout {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: modelData.nombre + (modelData.principal && modelData.nombre !== "Principal" ? " (principal)" : ""); font.pixelSize: 16; font.bold: true }
                        Label { text: modelData.movimientos + " movimiento(s) · saldo inicial $ " + modelData.saldoInicial; font.pixelSize: 12; color: ventana.tintaApagada }
                    }
                    Label { text: modelData.saldo; font.pixelSize: 18; font.bold: true }
                }
            }
        }
        Button {
            Layout.fillWidth: true
            Layout.margins: 12
            text: "+ Agregar cuenta"
            highlighted: true
            onClicked: { campoNombre.text = ""; campoSaldo.text = ""; dialogoNueva.open() }
        }
    }

    Menu {
        id: opciones
        MenuItem { text: "Renombrar"; onTriggered: { campoRenombrar.text = pagina.elegida.nombre; dialogoRenombrar.open() } }
        MenuItem { text: "Saldo inicial"; onTriggered: { campoSaldoInicial.text = pagina.elegida.saldoInicial; dialogoSaldo.open() } }
        MenuItem { text: "Eliminar"; enabled: !pagina.elegida.principal; onTriggered: dialogoEliminar.abrir() }
    }

    Dialog {
        id: dialogoNueva
        title: "Nueva cuenta"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            TextField { id: campoNombre; Layout.fillWidth: true; placeholderText: "Nombre (Banco, Mercado Pago…)" }
            TextField { id: campoSaldo; Layout.fillWidth: true; placeholderText: "Saldo inicial (opcional)"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
        }
        onAccepted: { const r = App.crearCuenta(campoNombre.text, campoSaldo.text); pagina.mostrar(r.error) }
    }
    Dialog {
        id: dialogoRenombrar
        title: "Renombrar cuenta"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: campoRenombrar; width: parent.width }
        onAccepted: pagina.mostrar(App.renombrarCuenta(pagina.elegida.id, campoRenombrar.text))
    }
    Dialog {
        id: dialogoSaldo
        title: "Saldo inicial de «" + pagina.elegida.nombre + "»"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Plata que había antes del primer movimiento cargado. Puede ser negativo." }
            TextField { id: campoSaldoInicial; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
        }
        onAccepted: pagina.mostrar(App.fijarSaldoInicialCuenta(pagina.elegida.id, campoSaldoInicial.text))
    }
    Dialog {
        id: dialogoEliminar
        property var otras: []
        function abrir() { otras = App.cuentas.filter(c => c.id !== pagina.elegida.id); destino.currentIndex = 0; open() }
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
                text: pagina.elegida.movimientos > 0 ? "Tiene " + pagina.elegida.movimientos + " movimiento(s). ¿A qué cuenta los paso?" : "¿Eliminar esta cuenta?"
            }
            ComboBox { id: destino; Layout.fillWidth: true; visible: pagina.elegida.movimientos > 0; model: dialogoEliminar.otras; textRole: "nombre"; valueRole: "id" }
        }
        onAccepted: pagina.mostrar(App.eliminarCuenta(pagina.elegida.id, pagina.elegida.movimientos > 0 ? destino.currentValue : 0))
    }
}
