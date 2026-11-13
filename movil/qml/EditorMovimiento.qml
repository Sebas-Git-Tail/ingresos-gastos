// Cargar o editar un ingreso o gasto.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import IngresosGastos

Page {
    id: editor
    property var datos: ({})            // lo que da App.nuevoMovimiento / App.movimiento / App.repetir
    property int tipo: datos.tipo !== undefined ? datos.tipo : 1
    property string fechaIso: datos.fechaIso || App.hoyIso()
    readonly property bool nuevo: !datos.id

    title: nuevo ? (tipo === 0 ? "Nuevo ingreso" : "Nuevo gasto") : "Editar movimiento"

    function cargarCategorias(elegir) {
        const lista = App.categorias(tipo)
        comboCategoria.model = lista
        // Si no se pide una (o ya no está): la inicial y no la primera de la lista, que por orden alfabético sería una de «Varios»
        let i = lista.findIndex(c => c.id === elegir)
        if (i < 0) i = lista.findIndex(c => c.id === App.categoriaInicial(tipo))
        comboCategoria.currentIndex = lista.length > 0 ? Math.max(i, 0) : -1
    }
    Component.onCompleted: {
        campoMonto.text = datos.monto || ""
        campoDescripcion.text = datos.descripcion || ""
        cargarCategorias(datos.categoriaId)
        if (nuevo) campoMonto.forceActiveFocus()
    }
    onTipoChanged: if (comboCategoria.model !== undefined) cargarCategorias(-1)

    header: ToolBar {
        Material.foreground: "white"
        RowLayout {
            anchors.fill: parent
            ToolButton { text: "←"; font.pixelSize: 22; onClicked: pila.pop(); Accessible.name: "Volver" }
            Label { text: editor.title; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            ToolButton {
                text: "Repetir"
                visible: !editor.nuevo
                onClicked: pila.replace(editor, { datos: App.repetir(editor.datos.id) })
            }
            ToolButton {
                text: "🗑"
                font.pixelSize: 20
                visible: !editor.nuevo
                onClicked: confirmar.open()
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

            TabBar {
                Layout.fillWidth: true
                currentIndex: editor.tipo
                onCurrentIndexChanged: editor.tipo = currentIndex
                TabButton { text: "Ingreso" }
                TabButton { text: "Gasto" }
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

            Label { text: "Categoría"; color: ventana.tintaSecundaria }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: comboCategoria
                    Layout.fillWidth: true
                    textRole: "nombre"
                    valueRole: "id"
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: comboCategoria.width
                        highlighted: comboCategoria.highlightedIndex === index
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle { width: 12; height: 12; radius: 3; color: modelData.color }
                            Label { text: modelData.nombre; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }
                Button {
                    text: "+"
                    font.pixelSize: 20
                    onClicked: { nombreNueva.text = ""; dialogoCategoria.open() }
                    Accessible.name: "Nueva categoría"
                }
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
                    const e = App.guardarMovimiento({
                        id: editor.datos.id || 0,
                        tipo: editor.tipo,
                        fechaIso: editor.fechaIso,
                        categoriaId: comboCategoria.currentValue !== undefined ? comboCategoria.currentValue : 0,
                        monto: campoMonto.text,
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

    Dialog {
        id: dialogoCategoria
        title: editor.tipo === 0 ? "Nueva categoría de ingresos" : "Nueva categoría de gastos"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: nombreNueva; width: parent.width; placeholderText: "Nombre" }
        onOpened: nombreNueva.forceActiveFocus()
        onAccepted: {
            const r = App.crearCategoria(nombreNueva.text, editor.tipo)
            if (r.ok) editor.cargarCategorias(r.id)
            else error.text = r.error
        }
    }

    Dialog {
        id: confirmar
        title: "¿Eliminar este movimiento?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            const e = App.eliminarMovimiento(editor.datos.id)
            if (e === "") pila.pop()
            else error.text = e
        }
    }
}
