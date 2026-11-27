// Ventana principal del celular: período arriba, saldo y tarjetas, y tres pestañas
// (Movimientos, Gráficos, Totales). El menú (☰, tres rayitas) abre lo demás.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Comun.Ui
import QtQuick.Dialogs
import IngresosGastos

ApplicationWindow {
    id: ventana
    width: 412
    height: 880
    visible: true
    title: "Ingresos y Gastos"

    Material.theme: Tema.oscuro ? Material.Dark : Material.Light   // claro u oscuro: lo elige el menú (Tema.alternar)
    Material.accent: "#2a78d6"
    Material.primary: "#2a78d6"

    readonly property bool oscuro: Material.theme === Material.Dark
    readonly property color tintaSecundaria: oscuro ? "#c3c2b7" : "#52514e"
    readonly property color tintaApagada: "#898781"
    readonly property color superficie: oscuro ? "#1a1a19" : "#fcfcfb"
    readonly property color borde: oscuro ? "#2c2c2a" : "#e1e0d9"
    readonly property color azul: oscuro ? "#3987e5" : "#2a78d6"
    readonly property color naranja: oscuro ? "#d95926" : "#eb6834"

    // Montos: verde lo que suma, rojo lo que resta (signo -1, 0, 1; 0 = color normal)
    function colorMonto(signo) { return signo > 0 ? Tema.verde : signo < 0 ? Tema.rojo : Material.foreground }

    onOscuroChanged: App.oscuro = oscuro
    Component.onCompleted: { App.oscuro = oscuro; Tema.acento = "#2a78d6" }   // y el tema común (Comun.Ui) con el color de la app

    // Botón «atrás» de Android: cierra el menú o vuelve a la pantalla anterior antes de salir
    onClosing: (cierre) => {
        if (menu.opened) { menu.close(); cierre.accepted = false }
        else if (pila.depth > 1) { pila.pop(); cierre.accepted = false }
    }

    // Al volver a la app (o abrirla), sincroniza sola si hay PC emparejada
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) { App.cargarFijos(); App.respaldar(); App.sincronizarSiCorresponde() }
        }
    }
    // Movimientos fijos, copia automática del día (quedan las últimas 7) y sincronización
    Timer { interval: 1500; running: true; onTriggered: { App.cargarFijos(); App.respaldar(); App.sincronizarSiCorresponde() } }

    // Mensajito abajo («Guardado», «Sincronizado…»): AvisoFlotante de Comun.Ui
    function avisar(texto) { aviso.mostrar(texto) }
    // Borrar sin preguntar: el aviso ofrece «Deshacer» unos segundos (como en la PC)
    function borrar(id) {
        const e = App.eliminarMovimiento(id)
        if (e !== "") { avisar(e); return }
        aviso.mostrar("Movimiento eliminado", { textoBoton: "Deshacer", milisegundos: 7000,
                                               alBoton: function() { App.restaurarMovimiento(id) } })
    }
    Connections { target: App; function onAviso(texto) { ventana.avisar(texto) } }
    AvisoFlotante { id: aviso; margenAbajo: 90 }

    header: ToolBar {
        Material.foreground: "white"
        visible: pila.depth === 1
        RowLayout {
            anchors.fill: parent
            spacing: 0
            BotonMenu { punto: Apps.versionNueva !== ""; onClicked: menu.open() }   // íconos SVG: algunos celulares (Samsung) no tienen ☰ ▾ ⟳ en la fuente
            ToolButton { text: "‹"; font.pixelSize: 28; enabled: App.navegable; onClicked: App.anterior(); Accessible.name: "Anterior" }
            ToolButton {
                Layout.fillWidth: true
                text: App.periodoTexto
                font.pixelSize: 18
                font.bold: true
                icon.source: "qrc:/IngresosGastos/iconos/desplegar.svg"
                LayoutMirroring.enabled: true           // la flechita a la derecha del texto
                LayoutMirroring.childrenInherit: true
                onClicked: menuVista.open()
                Menu {
                    id: menuVista
                    y: parent.height
                    MenuItem { text: "Ver un mes"; onTriggered: App.vista = 0 }
                    MenuItem { text: "Ver un año"; onTriggered: App.vista = 1 }
                    MenuItem { text: "Ver todo"; onTriggered: App.vista = 2 }
                    MenuItem { text: "Entre fechas…"; onTriggered: dialogoRango.abrir() }
                    MenuSeparator {}
                    MenuItem { text: "Ir a hoy"; enabled: App.navegable; onTriggered: App.hoy() }
                }
            }
            ToolButton { text: "›"; font.pixelSize: 28; enabled: App.navegable; onClicked: App.siguiente(); Accessible.name: "Siguiente" }
            ToolButton {
                icon.source: "qrc:/IngresosGastos/iconos/sincronizar.svg"
                opacity: App.sincronizando ? 0.5 : 1
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
                    { texto: "Apps y actualizaciones", accion: "apps" },
                    { texto: "Acerca de", accion: "acerca" }
                ]
                ItemMenu {
                    text: modelData.texto
                    destacado: modelData.accion === "apps" && Apps.versionNueva !== ""
                    onClicked: { menu.close(); ventana.abrir(modelData.accion) }
                }
            }
            ItemMenu {   // qué cambió e informe de fallas; dejarlo apretado prende el modo Sebastián (escondido)
                text: "Novedades"
                onClicked: { menu.close(); novedades.abrirTodas() }
                onPressAndHold: novedades.alternarDesarrollador()
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; Layout.preferredHeight: 1; color: ventana.borde }
            ItemMenu {   // el menú queda abierto: se ve el cambio enseguida
                text: Tema.textoAlternar
                icono: Tema.iconoAlternar
                onClicked: Tema.alternar()
            }
            Item { Layout.fillHeight: true }
        }
    }

    function abrir(accion) {
        if (accion === "categorias") pila.push(paginaCategorias)
        else if (accion === "saldo") dialogoSaldo.abrir()
        else if (accion === "importar") elegirImportar.open()
        else if (accion === "exportar") dialogoExportar.open()
        else if (accion === "backup") { elegirBackup.selectedFile = elegirBackup.currentFolder + "/" + App.nombreBackup(); elegirBackup.open() }
        else if (accion === "sinc") pila.push(paginaSinc)
        else if (accion === "cuentas") pila.push(paginaCuentas)
        else if (accion === "fijos") pila.push(paginaFijos)
        else if (accion === "apps") pila.push(paginaApps)
        else if (accion === "acerca") acerca.open()
    }

    function editar(datos) {
        if (datos.transferencia) pila.push(editorTransferencia, { datos: App.transferencia(datos.id) })
        else pila.push(editor, { datos: datos })
    }
    function editarTransferencia(datos) { if (datos && datos.desdeCuenta !== undefined) pila.push(editorTransferencia, { datos: datos }) }

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
                                color: ventana.colorMonto(App.saldoSigno)
                                font.pixelSize: 24
                                font.bold: true
                                MouseArea { anchors.fill: parent; onClicked: ventana.avisar(App.saldoAyuda) }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Tarjeta {
                                titulo: "Ingresos"; valor: App.ingresosTexto; signo: App.ingresosSigno; marca: ventana.azul
                                detalle: App.ingresosComparacion; signoDetalle: App.ingresosComparacionSigno
                            }
                            Tarjeta {
                                titulo: "Gastos"; valor: App.gastosTexto; signo: App.gastosSigno; marca: ventana.naranja
                                detalle: App.gastosComparacion; signoDetalle: App.gastosComparacionSigno
                            }
                            Tarjeta {
                                titulo: "Resultado"; valor: App.resultadoTexto; signo: App.resultadoSigno; marca: "transparent"
                                detalle: App.resultadoComparacion
                            }
                        }
                    }
                }

                // Aviso de presupuesto (tocar abre las categorías, donde se cambia)
                ItemDelegate {
                    Layout.fillWidth: true
                    visible: App.avisoPresupuesto !== ""
                    text: App.avisoPresupuesto
                    onClicked: ventana.abrir("categorias")
                    // Pasado: naranja fuerte; solo cerca (80 %): amarillo
                    background: Rectangle {
                        color: App.presupuestoPasado ? (ventana.oscuro ? "#5a2a1a" : "#fde0d0") : (ventana.oscuro ? "#4a3d12" : "#fdf0c8")
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Image { source: "qrc:/IngresosGastos/iconos/aviso.svg"; sourceSize: Qt.size(20, 20); Layout.alignment: Qt.AlignTop }
                        Label { text: App.avisoPresupuesto; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
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
    Component { id: editorTransferencia; EditorTransferencia {} }
    // Lo nuevo de esta versión: se abre solo la primera vez (comun/ui/qml/DialogoNovedades.qml)
    DialogoNovedades { id: novedades }

    Component { id: paginaApps; PaginaApps { conBarra: true; nombreApp: "Ingresos y Gastos" } }

    // Versión nueva de la app (se busca cada vez que se abre: marca el menú; pregunta como mucho una vez por día)
    Timer { interval: 4000; running: true; onTriggered: Apps.buscarActualizacion(false) }
    Connections {
        target: Apps
        function onAvisoVersionNueva() { if (pila.depth === 1) dialogoVersion.open() }   // una vez por día; el menú se marca siempre
    }
    Dialog {
        id: dialogoVersion
        title: "Versión nueva"
        anchors.centerIn: parent
        width: Math.min(ventana.width - 48, 360)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Hay una versión nueva de Ingresos y Gastos: la " + Apps.versionNueva + " (tenés la " + Apps.versionPropia + ").\n\n¿La instalo ahora?"
        }
        onAccepted: { Apps.actualizarEstaApp(); pila.push(paginaApps) }
    }
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
    // Qué exportar: lo que se ve, todo o entre dos fechas
    Dialog {
        id: dialogoExportar
        property string desde: ""
        property string hasta: ""
        title: "Exportar CSV"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            opcionVisto.checked = true
            desde = App.desdeIso || App.hoyIso()
            hasta = App.hastaIso || App.hoyIso()
        }
        ColumnLayout {
            anchors.fill: parent
            RadioButton { id: opcionVisto; text: "Lo que se ve: " + App.periodoTexto; checked: true }
            RadioButton { id: opcionTodo; text: "Todo" }
            RadioButton { id: opcionFechas; text: "Entre estas fechas:" }
            RowLayout {
                Layout.fillWidth: true
                enabled: opcionFechas.checked
                Button {
                    Layout.fillWidth: true
                    text: "Desde " + dialogoExportar.desde.split("-").reverse().join("/")
                    font.capitalization: Font.MixedCase
                    onClicked: { selectorExportar.campo = "desde"; selectorExportar.abrir(dialogoExportar.desde) }
                }
                Button {
                    Layout.fillWidth: true
                    text: "Hasta " + dialogoExportar.hasta.split("-").reverse().join("/")
                    font.capitalization: Font.MixedCase
                    onClicked: { selectorExportar.campo = "hasta"; selectorExportar.abrir(dialogoExportar.hasta) }
                }
            }
        }
        onAccepted: {
            if (opcionTodo.checked) { elegirExportar.desde = ""; elegirExportar.hasta = "" }
            else if (opcionFechas.checked) { elegirExportar.desde = desde; elegirExportar.hasta = hasta }
            else { elegirExportar.desde = App.desdeIso; elegirExportar.hasta = App.hastaIso }
            const nombre = opcionVisto.checked ? App.nombreExportacion() : App.nombreExportacionEntre(elegirExportar.desde, elegirExportar.hasta)
            elegirExportar.selectedFile = elegirExportar.currentFolder + "/" + nombre
            elegirExportar.open()
        }
    }
    SelectorFecha {
        id: selectorExportar
        property string campo: "desde"
        onElegida: (iso) => { if (campo === "desde") dialogoExportar.desde = iso; else dialogoExportar.hasta = iso }
    }
    FileDialog {
        id: elegirExportar
        property string desde: ""
        property string hasta: ""
        title: "Guardar CSV"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV (*.csv)"]
        onAccepted: { const e = App.exportarEntre(selectedFile, desde, hasta); if (e !== "") ventana.avisar("No se pudo guardar: " + e) }
    }

    // «Entre fechas…»: ver (y comparar con los mismos días antes) un rango a elección
    Dialog {
        id: dialogoRango
        property string desde: ""
        property string hasta: ""
        function abrir() { desde = App.desdeIso || App.hoyIso(); hasta = App.hastaIso || App.hoyIso(); open() }
        title: "Ver entre fechas"
        modal: true
        anchors.centerIn: parent
        width: Math.min(ventana.width - 32, 380)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Las tarjetas y los totales se comparan con la misma cantidad de días justo antes."
                color: ventana.tintaSecundaria
            }
            Button {
                Layout.fillWidth: true
                text: "Desde: " + App.fechaTexto(dialogoRango.desde)
                font.capitalization: Font.MixedCase
                onClicked: { selectorRango.campo = "desde"; selectorRango.abrir(dialogoRango.desde) }
            }
            Button {
                Layout.fillWidth: true
                text: "Hasta: " + App.fechaTexto(dialogoRango.hasta)
                font.capitalization: Font.MixedCase
                onClicked: { selectorRango.campo = "hasta"; selectorRango.abrir(dialogoRango.hasta) }
            }
        }
        onAccepted: App.verRango(desde, hasta)
    }
    SelectorFecha {
        id: selectorRango
        property string campo: "desde"
        onElegida: (iso) => { if (campo === "desde") dialogoRango.desde = iso; else dialogoRango.hasta = iso }
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
                  + "Todos los días guarda sola una copia de la base dentro de la app (quedan las últimas 7). "
                  + "Para tener una afuera del celular, usá «Copia de seguridad».\n\n"
                  + "Hecho con C++ y Qt."
        }
    }
}
