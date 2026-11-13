// Alta y edición de un movimiento fijo.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import IngresosGastos

Page {
    id: editor
    property var datos: ({})                 // lo que da App.nuevoFijo / App.fijo / App.fijoDesdeMovimiento
    property int tipo: datos.tipo !== undefined ? datos.tipo : 1
    property string desdeIso: datos.desdeIso || App.hoyIso()
    property string hastaIso: datos.hastaIso || ""
    readonly property bool nuevo: !datos.id

    function cargarCategorias(elegir) {
        const lista = App.categorias(tipo)
        comboCategoria.model = lista
        let i = lista.findIndex(c => c.id === elegir)
        if (i < 0) i = lista.findIndex(c => c.id === App.categoriaInicial(tipo))
        comboCategoria.currentIndex = lista.length > 0 ? Math.max(i, 0) : -1
    }
    Component.onCompleted: {
        campoMonto.text = datos.monto || ""
        campoDescripcion.text = datos.descripcion || ""
        campoDia.value = datos.dia || 1
        checkActivo.checked = datos.activo !== false
        cargarCategorias(datos.categoriaId)
        comboCuenta.currentIndex = Math.max(0, App.cuentas.findIndex(c => c.id === datos.cuentaId))
    }
    onTipoChanged: if (comboCategoria.model !== undefined) cargarCategorias(-1)

    header: ToolBar {
        Material.foreground: "white"
        RowLayout {
            anchors.fill: parent
            ToolButton { text: "←"; font.pixelSize: 22; onClicked: pila.pop(); Accessible.name: "Volver" }
            Label { text: editor.nuevo ? "Nuevo movimiento fijo" : "Movimiento fijo"; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
            ToolButton {
                text: "Eliminar"
                visible: !editor.nuevo
                onClicked: { const e = App.eliminarFijo(editor.datos.id); if (e === "") pila.pop(); else error.text = e }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: columna.implicitHeight + 32
        clip: true
        ColumnLayout {
            id: columna
            x: 16; y: 16
            width: parent.width - 32
            spacing: 6
            RowLayout {
                RadioButton { text: "Ingreso"; checked: editor.tipo === 0; onClicked: editor.tipo = 0 }
                RadioButton { text: "Gasto"; checked: editor.tipo === 1; onClicked: editor.tipo = 1 }
            }
            Label { text: "Monto"; color: ventana.tintaSecundaria }
            TextField { id: campoMonto; Layout.fillWidth: true; placeholderText: "15.430,50"; inputMethodHints: Qt.ImhFormattedNumbersOnly; onTextEdited: error.text = "" }
            Label { text: "Categoría"; color: ventana.tintaSecundaria }
            ComboBox { id: comboCategoria; Layout.fillWidth: true; textRole: "nombre"; valueRole: "id" }
            Label { text: "Cuenta"; color: ventana.tintaSecundaria; visible: App.cuentas.length > 1 }
            ComboBox { id: comboCuenta; Layout.fillWidth: true; visible: App.cuentas.length > 1; model: App.cuentas; textRole: "nombre"; valueRole: "id" }
            Label { text: "Descripción"; color: ventana.tintaSecundaria }
            TextField { id: campoDescripcion; Layout.fillWidth: true; placeholderText: "Alquiler, sueldo, internet…" }
            Label { text: "Día del mes (si el mes es más corto, el último día)"; color: ventana.tintaSecundaria; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            SpinBox { id: campoDia; from: 1; to: 31; editable: true }
            Label { text: "Desde"; color: ventana.tintaSecundaria }
            Button { Layout.fillWidth: true; text: App.fechaTexto(editor.desdeIso); font.capitalization: Font.MixedCase; onClicked: selectorDesde.abrir(editor.desdeIso) }
            CheckBox { id: checkHasta; text: "Hasta una fecha"; checked: editor.hastaIso !== ""; onToggled: editor.hastaIso = checked ? App.hoyIso() : "" }
            Button { Layout.fillWidth: true; visible: editor.hastaIso !== ""; text: App.fechaTexto(editor.hastaIso); font.capitalization: Font.MixedCase; onClicked: selectorHasta.abrir(editor.hastaIso) }
            CheckBox { id: checkActivo; text: "Activo (si se destilda, deja de cargarse)" }
            Label { id: error; Layout.fillWidth: true; color: "#e34948"; wrapMode: Text.WordWrap; visible: text !== "" }
            Button {
                Layout.fillWidth: true
                Layout.topMargin: 8
                text: "Guardar"
                highlighted: true
                onClicked: {
                    const e = App.guardarFijo({
                        id: editor.datos.id || 0,
                        categoriaId: comboCategoria.currentValue !== undefined ? comboCategoria.currentValue : 0,
                        cuentaId: comboCuenta.currentValue !== undefined ? comboCuenta.currentValue : 0,
                        monto: campoMonto.text,
                        descripcion: campoDescripcion.text,
                        dia: campoDia.value,
                        desdeIso: editor.desdeIso,
                        hastaIso: editor.hastaIso,
                        activo: checkActivo.checked
                    })
                    if (e === "") { ventana.avisar("Movimiento fijo guardado"); pila.pop() }
                    else error.text = e
                }
            }
        }
    }

    SelectorFecha { id: selectorDesde; onElegida: (iso) => editor.desdeIso = iso }
    SelectorFecha { id: selectorHasta; onElegida: (iso) => editor.hastaIso = iso }
}
