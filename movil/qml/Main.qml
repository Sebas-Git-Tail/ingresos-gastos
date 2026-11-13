// Ventana principal del celular: período arriba, saldo y tarjetas, y tres pestañas
// (Movimientos, Gráficos, Totales). El menú ☰ abre lo demás.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import IngresosGastos

ApplicationWindow {
    id: ventana
    width: 412
    height: 880
    visible: true
    title: "Ingresos y Gastos"

    Material.theme: Material.System
    Material.accent: "#2a78d6"
    Material.primary: "#2a78d6"

    readonly property bool oscuro: Material.theme === Material.Dark
    readonly property color tintaSecundaria: oscuro ? "#c3c2b7" : "#52514e"
    readonly property color tintaApagada: "#898781"
    readonly property color superficie: oscuro ? "#1a1a19" : "#fcfcfb"
    readonly property color borde: oscuro ? "#2c2c2a" : "#e1e0d9"
    readonly property color azul: oscuro ? "#3987e5" : "#2a78d6"
    readonly property color naranja: oscuro ? "#d95926" : "#eb6834"

    onOscuroChanged: App.oscuro = oscuro
    Component.onCompleted: App.oscuro = oscuro

    // Botón «atrás» de Android: cierra el menú o vuelve a la pantalla anterior antes de salir
    onClosing: (cierre) => {
        if (menu.opened) { menu.close(); cierre.accepted = false }
        else if (pila.depth > 1) { pila.pop(); cierre.accepted = false }
    }

    // Al volver a la app (o abrirla), sincroniza sola si hay PC emparejada
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) { App.cargarFijos(); App.sincronizarSiCorresponde() }
        }
    }
    Timer { interval: 1500; running: true; onTriggered: { App.cargarFijos(); App.sincronizarSiCorresponde() } }

    // Mensajito abajo («Gasto guardado», «Sincronizado…»)
    function avisar(texto) { aviso.texto = texto; aviso.open() }
    Connections { target: App; function onAviso(texto) { ventana.avisar(texto) } }
    Popup {
        id: aviso
        property string texto
        x: (ventana.width - width) / 2
        y: ventana.height - height - 90
        width: Math.min(ventana.width - 32, etiquetaAviso.implicitWidth + 32)
        padding: 12
        modal: false
        closePolicy: Popup.CloseOnPressOutside
        background: Rectangle { radius: 8; color: ventana.oscuro ? "#f0efec" : "#2c2c2a" }
        contentItem: Label {
            id: etiquetaAviso
            text: aviso.texto
            wrapMode: Text.WordWrap
            color: ventana.oscuro ? "#0b0b0b" : "#f0efec"
        }
        onOpened: cerrarAviso.restart()
        Timer { id: cerrarAviso; interval: 3500; onTriggered: aviso.close() }
    }

    header: ToolBar {
        Material.foreground: "white"
        visible: pila.depth === 1
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ToolButton { text: "☰"; font.pixelSize: 22; onClicked: menu.open(); Accessible.name: "Menú" }
            ToolButton { text: "‹"; font.pixelSize: 28; enabled: App.navegable; onClicked: App.anterior(); Accessible.name: "Anterior" }
            ToolButton {
                Layout.fillWidth: true
                text: App.periodoTexto + "  ▾"
                font.pixelSize: 18
                font.bold: true
                onClicked: menuVista.open()
                Menu {
                    id: menuVista
                    y: parent.height
                    MenuItem { text: "Ver un mes"; onTriggered: App.vista = 0 }
                    MenuItem { text: "Ver un año"; onTriggered: App.vista = 1 }
                    MenuItem { text: "Ver todo"; onTriggered: App.vista = 2 }
                    MenuSeparator {}
                    MenuItem { text: "Ir a hoy"; enabled: App.navegable; onTriggered: App.hoy() }
                }
            }
            ToolButton { text: "›"; font.pixelSize: 28; enabled: App.navegable; onClicked: App.siguiente(); Accessible.name: "Siguiente" }
            ToolButton {
                text: App.sincronizando ? "⟳…" : "⟳"
                font.pixelSize: 20
                visible: App.pcEmparejada
                enabled: !App.sincronizando
                onClicked: App.sincronizar()
                Accessible.name: "Sincronizar con la PC"
            }
        }
    }

    Drawer {
        id: menu
        width: Math.min(ventana.width * 0.8, 320)
        height: ventana.height
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Label {
                text: "Ingresos y Gastos"
                font.pixelSize: 20
                font.bold: true
                Layout.margins: 20
            }
            Repeater {
                model: [
                    { texto: "Categorías y presupuestos", accion: "categorias" },
                    { texto: "Cuentas", accion: "cuentas" },
                    { texto: "Movimientos fijos", accion: "fijos" },
                    { texto: "Saldo inicial", accion: "saldo" },
                    { texto: "Importar CSV (también de ExpenLess)", accion: "importar" },
                    { texto: "Exportar CSV", accion: "exportar" },
                    { texto: "Copia de seguridad", accion: "backup" },
                    { texto: "Sincronizar con la PC", accion: "sinc" },
                    { texto: "Acerca de", accion: "acerca" }
                ]
                ItemDelegate {
                    Layout.fillWidth: true
                    text: modelData.texto
                    onClicked: { menu.close(); ventana.abrir(modelData.accion) }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    function abrir(accion) {
        if (accion === "categorias") pila.push(paginaCategorias)
        else if (accion === "saldo") dialogoSaldo.abrir()
        else if (accion === "importar") elegirImportar.open()
        else if (accion === "exportar") { elegirExportar.selectedFile = elegirExportar.currentFolder + "/" + App.nombreExportacion(); elegirExportar.open() }
        else if (accion === "backup") { elegirBackup.selectedFile = elegirBackup.currentFolder + "/" + App.nombreBackup(); elegirBackup.open() }
        else if (accion === "sinc") pila.push(paginaSinc)
        else if (accion === "cuentas") pila.push(paginaCuentas)
        else if (accion === "fijos") pila.push(paginaFijos)
        else if (accion === "acerca") acerca.open()
    }

    function editar(datos) { pila.push(editor, { datos: datos }) }

    StackView {
        id: pila
        anchors.fill: parent
        initialItem: principal
    }

    Component {
        id: principal
        Page {
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Saldo de la cuenta y tarjetas del período
                Pane {
                    Layout.fillWidth: true
                    padding: 12
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: App.cuentas.length > 1 ? "Saldo de" : "Saldo de la cuenta"; color: ventana.tintaSecundaria }
                            // Con más de una cuenta: cuál mirar (o todas)
                            ComboBox {
                                id: comboCuentaVista
                                visible: App.cuentas.length > 1
                                Layout.fillWidth: true
                                model: [{ id: 0, nombre: "Todas" }].concat(App.cuentas)
                                textRole: "nombre"
                                valueRole: "id"
                                currentIndex: Math.max(0, model.findIndex(c => c.id === App.cuentaVista))
                                onActivated: (i) => App.cuentaVista = model[i].id
                            }
                            Item { Layout.fillWidth: true; visible: App.cuentas.length <= 1 }
                            Label {
                                text: App.saldoTexto
                                font.pixelSize: 24
                                font.bold: true
                                MouseArea { anchors.fill: parent; onClicked: ventana.avisar(App.saldoAyuda) }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Tarjeta { titulo: "Ingresos"; valor: App.ingresosTexto; marca: ventana.azul }
                            Tarjeta { titulo: "Gastos"; valor: App.gastosTexto; marca: ventana.naranja }
                            Tarjeta { titulo: "Resultado"; valor: App.resultadoTexto; marca: "transparent" }
                        }
                    }
                }

                // Aviso de presupuesto (tocar abre las categorías, donde se cambia)
                ItemDelegate {
                    Layout.fillWidth: true
                    visible: App.avisoPresupuesto !== ""
                    text: "⚠ " + App.avisoPresupuesto
                    onClicked: ventana.abrir("categorias")
                    background: Rectangle { color: ventana.oscuro ? "#5a2a1a" : "#fde0d0" }
                    contentItem: Label { text: parent.text; wrapMode: Text.WordWrap }
                }

                TabBar {
                    id: pestanias
                    Layout.fillWidth: true
                    currentIndex: paginas.currentIndex
                    TabButton { text: "Movimientos" }
                    TabButton { text: "Gráficos" }
                    TabButton { text: "Totales" }
                }
                SwipeView {
                    id: paginas
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: pestanias.currentIndex
                    clip: true
                    PaginaMovimientos {}
                    PaginaGraficos {}
                    PaginaTotales {}
                }
            }
        }
    }

    Component { id: editor; EditorMovimiento {} }
    Component { id: paginaCategorias; PaginaCategorias {} }
    Component { id: paginaSinc; PaginaSincronizacion {} }
    Component { id: paginaCuentas; PaginaCuentas {} }
    Component { id: paginaFijos; PaginaFijos {} }
    Component { id: editorFijo; EditorFijo {} }
    function editarFijo(datos) { if (datos && datos.categoriaId !== undefined) pila.push(editorFijo, { datos: datos }) }

    // ---------------------------------------------------------------- saldo inicial
    Dialog {
        id: dialogoSaldo
        title: "Saldo inicial"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        function abrir() { campoSaldo.text = App.saldoInicialTexto(); errorSaldo.text = ""; open() }
        ColumnLayout {
            anchors.fill: parent
            Label {
                text: "Plata que había en la cuenta antes del primer movimiento cargado. Puede ser negativo (por ejemplo -5.000)."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: campoSaldo
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                horizontalAlignment: TextInput.AlignRight
            }
            Label { id: errorSaldo; color: "#e34948"; visible: text !== "" }
        }
        onAccepted: {
            if (!App.fijarSaldoInicial(campoSaldo.text)) {
                errorSaldo.text = "No es un monto válido. Ejemplo: 150.000,00"
                open()
            }
        }
    }

    // ---------------------------------------------------------------- archivos
    FileDialog {
        id: elegirImportar
        title: "Elegí el CSV para importar"
        fileMode: FileDialog.OpenFile
        nameFilters: ["CSV (*.csv *.txt)", "Todos los archivos (*)"]
        onAccepted: {
            const p = App.previsualizarImportacion(selectedFile)
            if (!p.ok) { ventana.avisar("No se pudo leer: " + p.error); return }
            confirmarImportar.archivo = selectedFile
            confirmarImportar.datos = p
            confirmarImportar.open()
        }
    }
    Dialog {
        id: confirmarImportar
        property url archivo
        property var datos: ({})
        title: "Importar"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 400)
        standardButtons: datos.nuevos > 0 ? (Dialog.Ok | Dialog.Cancel) : Dialog.Close
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: {
                const d = confirmarImportar.datos
                let t = d.nuevos > 0 ? ("Se van a agregar " + d.nuevos + " movimiento(s).") : "No hay movimientos nuevos para agregar."
                if (d.repetidos > 0) t += "\n" + d.repetidos + " ya estaba(n) cargado(s) y no se duplica(n)."
                if (d.categorias) t += "\n\nCategorías nuevas: " + d.categorias
                if (d.avisos > 0) t += "\n\n" + d.avisos + " renglón(es) no se pudieron leer:\n" + d.detalleAvisos
                return t
            }
        }
        onAccepted: if (datos.nuevos > 0) App.importar(archivo)
    }
    FileDialog {
        id: elegirExportar
        title: "Guardar CSV"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV (*.csv)"]
        onAccepted: { const e = App.exportar(selectedFile); if (e !== "") ventana.avisar("No se pudo guardar: " + e) }
    }
    FileDialog {
        id: elegirBackup
        title: "Guardar copia de seguridad"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "db"
        nameFilters: ["Base de datos (*.db)"]
        onAccepted: { const e = App.backup(selectedFile); if (e !== "") ventana.avisar("No se pudo guardar: " + e) }
    }

    Dialog {
        id: acerca
        title: "Ingresos y Gastos " + App.version
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 400)
        standardButtons: Dialog.Close
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Ingresos, gastos y saldo de la cuenta, en pesos argentinos.\n\n"
                  + "Se sincroniza con Ingresos y Gastos de la PC (Linux).\n\n"
                  + "Hecho con C++ y Qt."
        }
    }
}
