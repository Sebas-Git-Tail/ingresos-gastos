// Pruebas de la app del celular: el controlador, la sincronización contra una «PC» y las pantallas QML.
// Con IG_CAPTURAS=/carpeta guarda capturas de cada pantalla (para mirarlas).
#include "controlador.h"
#include "dinero.h"
#include "db.h"
#include "graficosquick.h"
#include "ModeloApps.h"
#include "Novedades.h"
#include "sincronizacion.h"
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(IngresosGastosPlugin)
Q_IMPORT_QML_PLUGIN(Comun_UiPlugin)   // comun/ui/qml

namespace {
QVariantMap mov(int tipo, const QString& fecha, qint64 cat, const QString& monto, const QString& desc = {})
{
    return QVariantMap{{"id", 0}, {"tipo", tipo}, {"fechaIso", fecha}, {"categoriaId", cat}, {"monto", monto}, {"descripcion", desc}};
}
qint64 idCat(Controlador& c, int tipo, const QString& nombre)
{
    for (const QVariant& v : c.categorias(tipo)) if (v.toMap().value("nombre").toString() == nombre) return v.toMap().value("id").toLongLong();
    return 0;
}
}

class TstMovil : public QObject
{
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle("Material");
        // Sin pantalla (offscreen): dibujo por software. Con una pantalla (Xvfb) se usa OpenGL, como en el celular
        if (QGuiApplication::platformName() == "offscreen") QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void movimientosYTarjetas()
    {
        Cuentas cuentas(":memory:");
        Controlador c(cuentas);
        c.setVista(0);
        // Fuera del período: salta al mes del movimiento para que se vea
        QCOMPARE(c.guardarMovimiento(mov(0, "2025-03-01", idCat(c, 0, "Sueldo"), "500.000")), QString());
        QCOMPARE(c.periodoTexto(), QString("Marzo 2025"));
        QCOMPARE(c.guardarMovimiento(mov(1, "2025-03-10", idCat(c, 1, "Supermercado"), "123.450,50", "Coto")), QString());
        QCOMPARE(c.guardarMovimiento(mov(1, "2025-03-20", idCat(c, 1, "Transporte"), "15000", "SUBE")), QString());
        QVERIFY(!c.guardarMovimiento(mov(1, "2025-03-20", idCat(c, 1, "Transporte"), "abc")).isEmpty());
        QVERIFY(!c.guardarMovimiento(mov(1, "2025-03-20", idCat(c, 0, "Sueldo"), "10")).isEmpty());   // categoría de otro tipo

        QCOMPARE(c.ingresosTexto(), QString("$ 500.000,00"));
        QCOMPARE(c.gastosTexto(), QString("$ 138.450,50"));
        QCOMPARE(c.resultadoTexto(), QString("+$ 361.549,50"));
        QCOMPARE(c.movimientos()->rowCount(), 3);
        QCOMPARE(c.movimientos()->data(c.movimientos()->index(0), ModeloMovimientos::RolMontoTexto).toString(), QString("-$ 15.000,00"));

        c.setBusqueda("coto");
        QCOMPARE(c.movimientos()->rowCount(), 1);
        QCOMPARE(c.cantidadTexto(), QString("1 de 3 movimientos"));
        c.setBusqueda(QString());
        c.setFiltroTipo(1);
        QCOMPARE(c.movimientos()->rowCount(), 1);
        c.setFiltroTipo(0);

        // Editar, repetir y eliminar
        const qint64 id = c.movimientos()->en(0).id;
        QVariantMap m = c.movimiento(id);
        QCOMPARE(m.value("monto").toString(), QString("15.000,00"));
        m.insert("monto", "16.000");
        QCOMPARE(c.guardarMovimiento(m), QString());
        QCOMPARE(c.gastosTexto(), QString("$ 139.450,50"));
        QCOMPARE(c.repetir(id).value("id").toInt(), 0);
        QCOMPARE(c.repetir(id).value("fechaIso").toString(), QDate::currentDate().toString(Qt::ISODate));
        QCOMPARE(c.eliminarMovimiento(id), QString());
        QCOMPARE(c.movimientos()->rowCount(), 2);

        // Período, gráficos y totales
        c.setVista(1);
        QCOMPARE(c.periodoTexto(), QString("2025"));
        QCOMPARE(c.barras().value("etiquetas").toStringList().size(), 12);
        QCOMPARE(c.barras().value("gastos").toList().at(2).toDouble(), 12345050.0);
        c.irAPeriodo(2);
        QCOMPARE(c.vista(), 0);
        QCOMPARE(c.periodoTexto(), QString("Marzo 2025"));
        QCOMPARE(c.tortaGastos().value("totales").toList().size(), 1);
        QCOMPARE(c.totalesMeses().size(), 13);
        QCOMPARE(c.totalesMeses().at(2).toMap().value("gastos").toString(), QString("$ 123.450,50"));
        QVERIFY(c.totalesMeses().at(2).toMap().value("marcado").toBool());
        QCOMPARE(c.totalesCategorias().first().toMap().value("nombre").toString(), QString("Gastos"));
        c.setVista(2);
        QVERIFY(!c.navegable());
        QCOMPARE(c.barras().value("etiquetas").toStringList(), QStringList{"2025"});
        c.anterior();                                   // en «Todo» no se mueve
        QCOMPARE(c.vista(), 2);
    }

    void categoriasYSaldo()
    {
        Cuentas cuentas(":memory:");
        Controlador c(cuentas);
        const QVariantMap r = c.crearCategoria("Mascotas", 1);
        QVERIFY(r.value("ok").toBool());
        QVERIFY(!c.crearCategoria("mascotas", 1).value("ok").toBool());
        QCOMPARE(c.renombrarCategoria(r.value("id").toLongLong(), "Perro"), QString());
        QCOMPARE(c.cambiarColor(r.value("id").toLongLong(), 6), QString());
        QCOMPARE(c.guardarMovimiento(mov(1, QDate::currentDate().toString(Qt::ISODate), r.value("id").toLongLong(), "100")), QString());
        QVERIFY(!c.eliminarCategoria(r.value("id").toLongLong(), 0).isEmpty());       // tiene movimientos
        QCOMPARE(c.eliminarCategoria(r.value("id").toLongLong(), idCat(c, 1, "Gastos Varios")), QString());
        QCOMPARE(idCat(c, 1, "Perro"), qint64(0));
        QCOMPARE(c.colores().size(), 8);

        QVERIFY(c.fijarSaldoInicial("-5.000"));
        QCOMPARE(c.saldoInicialTexto(), QString("-5.000,00"));
        QCOMPARE(c.saldoTexto(), QString("-$ 5.100,00"));   // -5.000 inicial - 100 de gasto
        QVERIFY(!c.fijarSaldoInicial("cinco"));
    }

    void categoriasPorOrdenAlfabetico()
    {
        Cuentas cuentas(":memory:");
        Controlador c(cuentas);
        const auto nombres = [&c](int tipo) {
            QStringList l;
            for (const QVariant& v : c.categorias(tipo)) l << v.toMap().value("nombre").toString();
            return l;
        };
        QCOMPARE(nombres(1), (QStringList{"Gastos Varios", "Internet", "Salud", "Servicios", "Supermercado", "Teléfono",
                                          "Transporte", "Vestimenta"}));
        QCOMPARE(nombres(0), (QStringList{"Ingresos Varios", "Sueldo"}));
        // Una nueva queda en su lugar; renombrada, se muda al que le toca
        const QVariantMap r = c.crearCategoria("Farmacia", 1);
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(nombres(1).indexOf("Farmacia"), 0);
        QCOMPARE(c.renombrarCategoria(r.value("id").toLongLong(), "Zapatería"), QString());
        QCOMPARE(nombres(1).last(), QString("Zapatería"));
        // Un movimiento nuevo arranca en la categoría de siempre, no en la primera de la lista
        QCOMPARE(c.categoriaInicial(1), idCat(c, 1, "Supermercado"));
        QCOMPARE(c.categoriaInicial(0), idCat(c, 0, "Sueldo"));
        QCOMPARE(c.nuevoMovimiento(1).value("categoriaId").toLongLong(), idCat(c, 1, "Supermercado"));
        QCOMPARE(c.nuevoMovimiento(0).value("categoriaId").toLongLong(), idCat(c, 0, "Sueldo"));
    }

    void csvYCopia()
    {
        Cuentas cuentas(":memory:");
        Controlador c(cuentas);
        const QString csv = dir_.filePath("in.csv");
        {
            QFile f(csv);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Fecha;Tipo;Categoría;Monto;Descripción\n01/02/2025;Gasto;Kiosco;1500,50;Alfajor\n03/02/2025;Ingreso;Sueldo;900000;\n");
        }
        const QVariantMap p = c.previsualizarImportacion(QUrl::fromLocalFile(csv));
        QVERIFY(p.value("ok").toBool());
        QCOMPARE(p.value("nuevos").toInt(), 2);
        QCOMPARE(p.value("categorias").toString(), QString("Gasto: Kiosco"));
        c.setVista(0);
        c.importar(QUrl::fromLocalFile(csv));
        QCOMPARE(c.periodoTexto(), QString("Febrero 2025"));     // el mes que se veía quedó vacío: va a lo importado
        QCOMPARE(c.importar(QUrl::fromLocalFile(csv)).value("repetidos").toInt(), 2);

        const QString salida = dir_.filePath("out.csv");
        QCOMPARE(c.exportar(QUrl::fromLocalFile(salida)), QString());
        Cuentas otra(":memory:");
        QCOMPARE(otra.importar(leerCsv(decodificarArchivo([&] { QFile f(salida); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }())).filas).nuevos, 2);

        const QString copia = dir_.filePath("copia.db");
        QCOMPARE(c.backup(QUrl::fromLocalFile(copia)), QString());
        Cuentas restaurada(copia);
        QCOMPARE(restaurada.movimientos().size(), 2);
        QVERIFY(!c.previsualizarImportacion(QUrl::fromLocalFile(dir_.filePath("no-existe.csv"))).value("ok").toBool());
    }

    void sincronizaConLaPc()
    {
        Cuentas pc(":memory:"), cel(":memory:");
        pc.setNombreDispositivo("PC de prueba");
        ServidorSinc servidor(pc);
        QVERIFY(servidor.iniciar(0, false));
        Controlador c(cel);
        QVERIFY(!c.pcEmparejada());
        Movimiento m;
        m.fecha = QDate(2026, 10, 1);
        m.tipo = Tipo::Ingreso;
        m.categoriaId = pc.buscarCategoria("Sueldo", Tipo::Ingreso)->id;
        m.monto = 80000000;
        QVERIFY(pc.crearMovimiento(m).ok);

        QSignalSpy fin(&c, &Controlador::sincTerminada);
        c.emparejar("127.0.0.1", servidor.puerto(), servidor.nuevoCodigo());
        QVERIFY(c.sincronizando());
        QVERIFY(fin.wait(10000));
        QVERIFY2(fin.first().at(0).toBool(), qPrintable(fin.first().at(1).toString()));
        QVERIFY(c.pcEmparejada());
        QCOMPARE(c.nombrePc(), QString("PC de prueba"));
        QCOMPARE(cel.movimientos().size(), 1);         // llegó lo de la PC (y se refrescó la pantalla)
        c.setVista(2);
        QCOMPARE(c.ingresosTexto(), QString("$ 800.000,00"));

        // Lo que se carga en el celular llega a la PC
        QCOMPARE(c.guardarMovimiento(mov(1, "2026-10-02", idCat(c, 1, "Supermercado"), "15.430,50", "Coto")), QString());
        fin.clear();
        c.sincronizar();
        QVERIFY(fin.wait(10000));
        QVERIFY(fin.first().at(0).toBool());
        QCOMPARE(pc.movimientos().size(), 2);
        QVERIFY(c.ultimaSincTexto().startsWith("Última vez"));

        // Al olvidar la PC, no se sincroniza más (y los datos quedan)
        c.olvidarPc();
        QVERIFY(!c.pcEmparejada());
        QCOMPARE(cel.movimientos().size(), 2);
    }

    void graficoBarrasTocar()
    {
        GraficoBarrasQuick g;
        g.setSize(QSizeF(380, 240));
        g.setDatos(QVariantMap{{"titulo", "2026"}, {"etiquetas", QStringList{"E", "F", "M"}}, {"detalles", QStringList{"Enero 2026", "Febrero 2026", "Marzo 2026"}},
                               {"ingresos", QVariantList{100000.0, 0.0, 50000.0}}, {"gastos", QVariantList{20000.0, 0.0, 70000.0}}, {"marcado", 0}});
        const int g2 = g.grupoEn(330, 150);
        QCOMPARE(g2, 2);
        g.setSeleccionado(2);
        QVERIFY(g.detalleSeleccionado().startsWith("Marzo 2026 · Ingresos $ 500,00 · Gastos $ 700,00"));
        GraficoTortaQuick t;
        t.setSize(QSizeF(380, 100));
        t.setDatos(QVariantMap{{"titulo", "Gastos"}, {"totales", QVariantList{QVariantMap{{"nombre", "A"}, {"color", 0}, {"total", 300.0}},
                                                                             QVariantMap{{"nombre", "B"}, {"color", 1}, {"total", 100.0}}}}});
        QCOMPARE(t.cantidadPorciones(), 2);
        QVERIFY(t.altoNecesario() > 250);              // angosto: torta arriba y leyenda abajo
    }

    void pantallasQml()
    {
        Cuentas cuentas(":memory:");
        Controlador c(cuentas);
        Controlador::setInstancia(&c);
        // Algunos datos para que las pantallas tengan qué mostrar
        const QString hoy = QDate::currentDate().toString(Qt::ISODate);
        const QString mesPasado = QDate::currentDate().addMonths(-1).toString(Qt::ISODate);
        c.guardarMovimiento(mov(0, mesPasado, idCat(c, 0, "Sueldo"), "950.000", "Sueldo"));
        c.guardarMovimiento(mov(1, mesPasado, idCat(c, 1, "Supermercado"), "180.500", "Compra del mes"));
        c.guardarMovimiento(mov(0, hoy, idCat(c, 0, "Sueldo"), "1.150.000", "Sueldo"));
        c.guardarMovimiento(mov(1, hoy, idCat(c, 1, "Supermercado"), "235.321,14", "Compra mensual"));
        c.guardarMovimiento(mov(1, hoy, idCat(c, 1, "Servicios"), "79.493,11", "Luz, gas y agua"));
        c.guardarMovimiento(mov(1, hoy, idCat(c, 1, "Transporte"), "45.000", "SUBE"));
        c.guardarMovimiento(mov(1, hoy, idCat(c, 1, "Internet"), "28.900", "Fibra 300 MB"));
        c.guardarMovimiento(mov(1, hoy, idCat(c, 1, "Teléfono"), "16.500", "Abono"));
        c.setVista(0);
        c.hoy();

        if (qEnvironmentVariableIsSet("IG_OSCURO")) {      // tema oscuro del «sistema» (para las capturas)
            QPalette p;
            p.setColor(QPalette::Window, QColor(30, 30, 30));
            p.setColor(QPalette::WindowText, Qt::white);
            p.setColor(QPalette::Base, QColor(20, 20, 20));
            p.setColor(QPalette::Text, Qt::white);
            QGuiApplication::setPalette(p);
        }
        QStringList advertencias;
        QQmlApplicationEngine engine;
        // «Apps y actualizaciones» como en main.cpp, pero sin salir a internet
        qputenv("COMUN_APPS_SIN_RED", "1");
        apps::ModeloApps modeloApps(QStringLiteral("ingresos-gastos"));
        engine.rootContext()->setContextProperty(QStringLiteral("Apps"), &modeloApps);
        soporte::Novedades novedades;   // «Novedades» (comun/soporte): el diálogo de lo nuevo y el informe de fallas
        engine.rootContext()->setContextProperty(QStringLiteral("Novedades"), &novedades);
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& l) { for (const auto& e : l) advertencias << e.toString(); });
        engine.addImportPath("qrc:/");
        engine.load(QUrl("qrc:/IngresosGastos/qml/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(advertencias.join('\n')));
        auto* ventana = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(ventana);
        QVERIFY(QTest::qWaitForWindowExposed(ventana));
        if (qEnvironmentVariableIsSet("IG_OSCURO")) {      // como si el celular estuviera en modo oscuro
            QQmlProperty tema(ventana, "Material.theme", qmlContext(ventana));
            QVERIFY(tema.write(1));                         // Material.Dark
        }
        QTest::qWait(200);

        const QString capturas = qEnvironmentVariable("IG_CAPTURAS");
        auto capturar = [&](const QString& nombre) {
            QTest::qWait(300);
            if (!capturas.isEmpty()) ventana->grabWindow().save(capturas + "/" + nombre + ".png");
        };
        std::function<QQuickItem*(QQuickItem*, const char*)> buscar = [&](QQuickItem* i, const char* clase) -> QQuickItem* {
            if (QByteArray(i->metaObject()->className()).contains(clase)) return i;
            for (QQuickItem* h : i->childItems()) if (QQuickItem* r = buscar(h, clase)) return r;
            return nullptr;
        };
        auto pestania = [&](int i) {
            QQuickItem* swipe = buscar(ventana->contentItem(), "SwipeView");
            QVERIFY(swipe);
            swipe->setProperty("currentIndex", i);
        };
        capturar("1-movimientos");
        auto* barras = qobject_cast<GraficoBarrasQuick*>(buscar(ventana->contentItem(), "GraficoBarrasQuick"));
        QVERIFY(barras);
        QCOMPARE(barras->datos().value("etiquetas").toStringList().size(), 12);
        pestania(1);
        capturar("2-graficos");
        pestania(2);
        capturar("3-totales");
        pestania(0);

        QMetaObject::invokeMethod(ventana, "editar", Q_ARG(QVariant, QVariant(c.nuevoMovimiento(1))));
        capturar("4-nuevo-gasto");
        // El editor ofrece las categorías en orden alfabético pero arranca en la de siempre («Supermercado» / «Sueldo»),
        // no en la primera de la lista («Gastos Varios» / «Ingresos Varios»), también al cambiar de tipo
        QQuickItem* editor = buscar(ventana->contentItem(), "EditorMovimiento");
        QVERIFY(editor);
        QQuickItem* combo = buscar(editor, "ComboBox");
        QVERIFY(combo);
        QCOMPARE(combo->property("count").toInt(), 8);
        QCOMPARE(combo->property("currentText").toString(), QString("Supermercado"));
        QVERIFY(editor->setProperty("tipo", 0));
        QCOMPARE(combo->property("currentText").toString(), QString("Sueldo"));
        QVERIFY(editor->setProperty("tipo", 1));
        QCOMPARE(combo->property("currentText").toString(), QString("Supermercado"));
        QMetaObject::invokeMethod(ventana, "abrir", Q_ARG(QVariant, QVariant("categorias")));
        capturar("5-categorias");
        QMetaObject::invokeMethod(ventana, "abrir", Q_ARG(QVariant, QVariant("sinc")));
        capturar("6-sincronizar");

        // 2.1.0: con dos cuentas aparecen los selectores; presupuesto pasado y movimientos fijos
        QVERIFY(c.crearCuenta("Banco", "1.000").value("ok").toBool());
        QCOMPARE(c.fijarPresupuesto(c.categoriaInicial(1), "1"), QString());
        QVariantMap gasto = c.nuevoMovimiento(1);
        gasto.insert("monto", "50");
        QCOMPARE(c.guardarMovimiento(gasto), QString());
        QVERIFY(!c.avisoPresupuesto().isEmpty());
        QMetaObject::invokeMethod(ventana, "abrir", Q_ARG(QVariant, QVariant("cuentas")));
        capturar("7-cuentas");
        QMetaObject::invokeMethod(ventana, "abrir", Q_ARG(QVariant, QVariant("fijos")));
        capturar("8-fijos");
        QMetaObject::invokeMethod(ventana, "editarFijo", Q_ARG(QVariant, QVariant(c.nuevoFijo(1))));
        capturar("9-nuevo-fijo");
        QMetaObject::invokeMethod(ventana, "editar", Q_ARG(QVariant, QVariant(c.nuevoMovimiento(1))));
        capturar("10-gasto-con-cuenta");
        QMetaObject::invokeMethod(ventana, "abrir", Q_ARG(QVariant, QVariant("apps")));
        capturar("11-apps");

        // 2.8.0: transferencia, borrar con «Deshacer», entre fechas con comparación, exportar y etiquetas
        QMetaObject::invokeMethod(ventana, "editarTransferencia", Q_ARG(QVariant, QVariant(c.nuevaTransferencia())));
        capturar("12-transferencia");
        QVERIFY(buscar(ventana->contentItem(), "EditorTransferencia"));
        QVariantMap t = c.nuevaTransferencia();
        t.insert("monto", "1.000");
        QCOMPARE(c.guardarTransferencia(t), QString());
        qint64 idTransf = 0;
        for (int i = 0; i < c.movimientos()->rowCount(); ++i)
            if (c.movimientos()->en(i).transferencia) idTransf = c.movimientos()->en(i).id;
        QVERIFY(idTransf);
        QMetaObject::invokeMethod(ventana, "editar", Q_ARG(QVariant, QVariant(c.movimiento(idTransf))));   // abre el de transferencias
        capturar("13-editar-transferencia");
        const int antes = cuentas.movimientos().size();
        QMetaObject::invokeMethod(ventana, "borrar", Q_ARG(QVariant, QVariant(idTransf)));
        QCOMPARE(cuentas.movimientos().size(), antes - 2);
        capturar("14-borrado-con-deshacer");
        QCOMPARE(c.guardarMovimiento(mov(1, hoy, idCat(c, 1, "Transporte"), "3.000", "Micro #vacaciones")), QString());
        c.verRango(QDate::currentDate().addMonths(-1).toString(Qt::ISODate), hoy);
        QQuickItem* pila = buscar(ventana->contentItem(), "StackView");
        QVERIFY(pila);
        QVERIFY(QMetaObject::invokeMethod(pila, "popToIndex", Q_ARG(int, 0)));   // volver a la pantalla principal
        QTest::qWait(400);
        capturar("15-movimientos-entre-fechas");
        pestania(2);
        capturar("16-totales-entre-fechas");
        pestania(0);
        QMetaObject::invokeMethod(ventana, "abrir", Q_ARG(QVariant, QVariant("exportar")));
        QTest::qWait(300);
        capturar("17-exportar");
        // Las advertencias de QML son errores de verdad (propiedad mal escrita, tipo que no existe...)
        QVERIFY2(advertencias.isEmpty(), qPrintable(advertencias.join('\n')));
        Controlador::setInstancia(nullptr);
    }

    void cuentasYFijosDesdeElCelular()
    {
        Cuentas db(":memory:");
        Controlador c(db);
        QCOMPARE(c.cuentas().size(), 1);
        const QVariantMap banco = c.crearCuenta("Banco", "500");
        QVERIFY(banco.value("ok").toBool());
        QCOMPARE(c.cuentas().size(), 2);
        c.setCuentaVista(banco.value("id").toLongLong());
        QVariantMap m = c.nuevoMovimiento(0);
        QCOMPARE(m.value("cuentaId").toLongLong(), banco.value("id").toLongLong());   // arranca en la que se mira
        m.insert("monto", "100");
        QCOMPARE(c.guardarMovimiento(m), QString());
        QCOMPARE(c.saldoTexto(), formatearPesos(50000 + 10000));
        c.setCuentaVista(0);

        QVariantMap f = c.nuevoFijo(1);
        f.insert("monto", "2.000");
        f.insert("dia", 1);
        f.insert("desdeIso", QDate::currentDate().addMonths(-1).toString(Qt::ISODate));
        QCOMPARE(c.guardarFijo(f), QString());
        QCOMPARE(c.fijos().size(), 1);
        QVERIFY(c.cargarFijos() == 0);                              // guardarFijo ya los cargó
        QCOMPARE(c.eliminarFijo(c.fijos().first().toMap().value("id").toLongLong()), QString());
        QVERIFY(c.fijos().isEmpty());
        QCOMPARE(c.eliminarCuenta(banco.value("id").toLongLong(), db.cuentaPrincipal()), QString());
        QCOMPARE(c.cuentas().size(), 1);
    }

    // ---- 2.8.0
    void deshacerAlBorrar()
    {
        Cuentas db(":memory:");
        Controlador c(db);
        c.setVista(2);
        QCOMPARE(c.guardarMovimiento(mov(1, "2026-11-01", idCat(c, 1, "Supermercado"), "100")), QString());
        const qint64 id = c.movimientos()->en(0).id;
        QCOMPARE(c.eliminarMovimiento(id), QString());
        QCOMPARE(c.movimientos()->rowCount(), 0);
        QCOMPARE(c.restaurarMovimiento(id), QString());
        QCOMPARE(c.movimientos()->rowCount(), 1);
        QCOMPARE(db.movimiento(id)->monto, Centavos(10000));
        QVERIFY(!c.restaurarMovimiento(id).isEmpty());            // ya estaba
    }

    void transferenciasDesdeElCelular()
    {
        Cuentas db(":memory:");
        Controlador c(db);
        c.setVista(2);
        const qint64 banco = c.crearCuenta("Banco", "").value("id").toLongLong();
        QCOMPARE(c.guardarMovimiento(mov(0, "2026-11-01", idCat(c, 0, "Sueldo"), "10.000")), QString());
        QVariantMap t = c.nuevaTransferencia();
        QCOMPARE(t.value("desdeCuenta").toLongLong(), db.cuentaPrincipal());
        QCOMPARE(t.value("hastaCuenta").toLongLong(), banco);
        t.insert("monto", "cero");
        QVERIFY(!c.guardarTransferencia(t).isEmpty());
        t.insert("monto", "4.000");
        t.insert("fechaIso", "2026-11-02");
        t.insert("descripcion", "Al banco");
        QCOMPARE(c.guardarTransferencia(t), QString());
        QCOMPARE(c.ingresosTexto(), QString("$ 10.000,00"));        // no es ingreso ni gasto
        QCOMPARE(c.gastosTexto(), QString("$ 0,00"));
        QCOMPARE(c.movimientos()->rowCount(), 3);
        const auto modelo = c.movimientos();
        QStringList nombres;
        for (int i = 0; i < modelo->rowCount(); ++i) nombres << modelo->data(modelo->index(i), ModeloMovimientos::RolCategoria).toString();
        QVERIFY2(nombres.contains("Transferencia a Banco") && nombres.contains("Transferencia desde Principal"), qPrintable(nombres.join(", ")));
        c.setFiltroTipo(3);
        QCOMPARE(c.movimientos()->rowCount(), 2);
        c.setFiltroTipo(1);
        QCOMPARE(c.movimientos()->rowCount(), 1);                   // el sueldo y no la entrada de la transferencia
        c.setFiltroTipo(0);
        qint64 id = 0;
        for (int i = 0; i < modelo->rowCount(); ++i) if (modelo->en(i).transferencia) id = modelo->en(i).id;
        QVERIFY(c.movimiento(id).value("transferencia").toBool());
        QVariantMap leida = c.transferencia(id);
        QCOMPARE(leida.value("monto").toString(), QString("4.000,00"));
        QCOMPARE(leida.value("hastaCuenta").toLongLong(), banco);
        QVERIFY(c.transferencia(c.movimientos()->en(c.movimientos()->rowCount() - 1).id).isEmpty());   // el sueldo
        QCOMPARE(c.repetirTransferencia(id).value("id").toInt(), 0);
        QVERIFY(c.fijoDesdeMovimiento(id).isEmpty());               // no se repite todos los meses
        c.setCuentaVista(banco);
        QCOMPARE(c.saldoTexto(), QString("$ 4.000,00"));
        c.setCuentaVista(0);
        QCOMPARE(c.eliminarMovimiento(id), QString());               // las dos mitades
        QCOMPARE(c.movimientos()->rowCount(), 1);
        QCOMPARE(c.restaurarMovimiento(id), QString());
        QCOMPARE(c.movimientos()->rowCount(), 3);
    }

    void avisoCercaDelPresupuesto()
    {
        Cuentas db(":memory:");
        Controlador c(db);
        c.setVista(0);
        c.hoy();
        const qint64 super = idCat(c, 1, "Supermercado");
        QCOMPARE(c.fijarPresupuesto(super, "100"), QString());
        QVERIFY(c.avisoPresupuesto().isEmpty());
        const QString hoy = QDate::currentDate().toString(Qt::ISODate);
        QCOMPARE(c.guardarMovimiento(mov(1, hoy, super, "85")), QString());
        QCOMPARE(c.avisoPresupuesto(), QString("Cerca del presupuesto: Supermercado (85 %)"));
        QVERIFY(!c.presupuestoPasado());
        QCOMPARE(c.guardarMovimiento(mov(1, hoy, super, "20")), QString());
        QCOMPARE(c.avisoPresupuesto(), QString("Te pasaste del presupuesto: Supermercado (105 %)"));
        QVERIFY(c.presupuestoPasado());
    }

    void entreFechasComparacionYBusqueda()
    {
        Cuentas db(":memory:");
        Controlador c(db);
        QCOMPARE(c.guardarMovimiento(mov(1, "2025-02-10", idCat(c, 1, "Supermercado"), "1.000")), QString());
        QCOMPARE(c.guardarMovimiento(mov(1, "2025-03-10", idCat(c, 1, "Supermercado"), "1.200", "Coto")), QString());
        QCOMPARE(c.guardarMovimiento(mov(0, "2025-03-12", idCat(c, 0, "Sueldo"), "5.000")), QString());
        QCOMPARE(c.guardarMovimiento(mov(1, "2025-04-02", idCat(c, 1, "Transporte"), "300", "Viaje #vacaciones")), QString());
        c.setVista(0);
        c.irAPeriodo(-1);
        QCOMPARE(c.periodoTexto(), QString("Abril 2025"));          // guardarMovimiento saltó al último
        c.anterior();
        QCOMPARE(c.gastosComparacion(), QString("+20 % que en febrero 2025"));
        QCOMPARE(c.gastosComparacionSigno(), -1);                   // más gastos: rojo
        QCOMPARE(c.ingresosComparacion(), QString("Sin datos en febrero 2025"));
        QCOMPARE(c.resultadoComparacion(), QString("febrero 2025: -$ 1.000,00"));
        QCOMPARE(c.totalesCategorias().at(1).toMap().value("antes").toString(), QString("+20 %"));
        QCOMPARE(c.totalesCategorias().at(1).toMap().value("signoAntes").toInt(), -1);
        c.setVista(2);
        QVERIFY(c.gastosComparacion().isEmpty());

        c.verRango("2025-04-15", "2025-03-01");                      // al revés también
        QCOMPARE(c.vista(), 3);
        QCOMPARE(c.periodoTexto(), QString("1/3/2025 al 15/4/2025"));
        QCOMPARE(c.desdeIso(), QString("2025-03-01"));
        QCOMPARE(c.gastosTexto(), QString("$ 1.500,00"));
        QCOMPARE(c.gastosComparacion(), QString("+50 % que en los 46 días anteriores"));
        QCOMPARE(c.movimientos()->rowCount(), 3);
        QCOMPARE(c.barras().value("etiquetas").toStringList().size(), 2);   // marzo y abril
        QCOMPARE(c.totalesEtiquetas().size(), 1);
        QCOMPARE(c.totalesEtiquetas().first().toMap().value("nombre").toString(), QString("#vacaciones"));
        QVERIFY(c.navegable());
        c.anterior();
        QCOMPARE(c.desdeIso(), QString("2025-01-14"));
        QCOMPARE(c.hastaIso(), QString("2025-02-28"));
        c.siguiente();
        c.irAPeriodo(1);                                             // tocar la barra de abril
        QCOMPARE(c.vista(), 0);
        QCOMPARE(c.periodoTexto(), QString("Abril 2025"));

        // Buscador por monto y etiqueta
        c.setVista(2);
        c.setBusqueda(">1000");
        QCOMPARE(c.movimientos()->rowCount(), 2);
        c.setBusqueda("super 1100-1300");
        QCOMPARE(c.movimientos()->rowCount(), 1);
        c.setBusqueda("#vacaciones");
        QCOMPARE(c.movimientos()->rowCount(), 1);
        c.setBusqueda(QString());
    }

    void exportarTodoOEntreFechasYCopiaDiaria()
    {
        QTemporaryDir dir;
        Cuentas db(dir.filePath("cuentas.db"));
        Controlador c(db);
        QCOMPARE(c.guardarMovimiento(mov(0, "2024-12-31", idCat(c, 0, "Sueldo"), "10")), QString());
        QCOMPARE(c.guardarMovimiento(mov(0, "2025-02-01", idCat(c, 0, "Sueldo"), "20")), QString());
        QCOMPARE(c.guardarMovimiento(mov(0, "2025-03-15", idCat(c, 0, "Sueldo"), "30")), QString());
        const auto filas = [&](const QString& nombre) {
            QFile f(dir.filePath(nombre));
            return f.open(QIODevice::ReadOnly) ? leerCsv(decodificarArchivo(f.readAll())).filas.size() : -1;
        };
        QCOMPARE(c.exportarEntre(QUrl::fromLocalFile(dir.filePath("todo.csv")), "", ""), QString());
        QCOMPARE(filas("todo.csv"), 3);
        QCOMPARE(c.exportarEntre(QUrl::fromLocalFile(dir.filePath("rango.csv")), "2024-12-01", "2025-02-01"), QString());
        QCOMPARE(filas("rango.csv"), 2);
        QVERIFY(!c.exportarEntre(QUrl::fromLocalFile(dir.filePath("x.csv")), "2025-03-01", "2025-01-01").isEmpty());
        QCOMPARE(c.nombreExportacionEntre("", ""), QString("movimientos-todos.csv"));
        QCOMPARE(c.nombreExportacionEntre("2025-01-01", "2025-02-01"), QString("movimientos-2025-01-01-al-2025-02-01.csv"));
        c.verRango("2025-01-01", "2025-01-31");
        QCOMPARE(c.nombreExportacion(), QString("movimientos-2025-01-01-al-2025-01-31.csv"));

        QVERIFY(c.respaldar());                                      // la copia de hoy
        QVERIFY(!c.respaldar());                                     // una por día
        QVERIFY(QFile::exists(c.carpetaRespaldos() + "/cuentas-" + QDate::currentDate().toString(Qt::ISODate) + ".db"));
    }
};

QTEST_MAIN(TstMovil)
#include "tst_movil.moc"
