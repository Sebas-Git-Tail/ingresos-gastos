// Pruebas de las pantallas: ventana principal, diálogo de movimiento y diálogo de categorías.
#include "db.h"
#include "dinero.h"
#include "dialogocategorias.h"
#include "dialogocuentas.h"
#include "dialogofijos.h"
#include "dialogofechas.h"
#include "dialogomovimiento.h"
#include "dialogosincronizacion.h"
#include "dialogotransferencia.h"
#include "graficos.h"
#include "csv.h"
#include "sincronizacion.h"
#include "ventanaprincipal.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTabWidget>
#include <QtTest>

namespace {

qint64 idCategoria(Cuentas& c, const QString& nombre, Tipo t)
{
    const auto cat = c.buscarCategoria(nombre, t);
    return cat ? cat->id : 0;
}

Movimiento mov(Cuentas& c, const QDate& f, const QString& cat, Tipo t, Centavos monto, const QString& desc = {})
{
    Movimiento m;
    m.fecha = f;
    m.tipo = t;
    m.categoriaId = idCategoria(c, cat, t);
    m.monto = monto;
    m.descripcion = desc;
    return m;
}

// Responde el próximo diálogo modal que se abra (sin trabarse esperando)
template <typename D, typename F>
void alAbrir(F accion)
{
    QTimer::singleShot(0, [accion] {
        for (int i = 0; i < 100; ++i) {
            if (auto* d = qobject_cast<D*>(QApplication::activeModalWidget())) { accion(d); return; }
            QTest::qWait(20);
        }
        QFAIL("no se abrió el diálogo");
    });
}

} // namespace

class TstVentana : public QObject
{
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setOrganizationName("ig-pruebas");
        QApplication::setApplicationName("ig-pruebas");
        QSettings().clear();
    }

    void cargaYTarjetas()
    {
        Cuentas c(dir_.filePath("a.db"));
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 5}, "Sueldo", Tipo::Ingreso, 50000000, "Marzo")).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 10}, "Supermercado", Tipo::Gasto, 12345050, "Coto")).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 20}, "Transporte", Tipo::Gasto, 1500000, "SUBE")).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 4, 2}, "Supermercado", Tipo::Gasto, 2000000)).ok);
        c.fijarSaldoInicial(10000000);

        VentanaPrincipal v(c);
        v.setVista(VentanaPrincipal::Vista::Mes);
        v.setPeriodo({2025, 3, 15});
        QCOMPARE(v.periodo(), QDate(2025, 3, 1));
        QCOMPARE(v.findChild<QLabel*>("labelPeriodo")->text(), QString("Marzo 2025"));
        auto* tabla = v.findChild<QTableView*>("tablaMovimientos");
        QCOMPARE(tabla->model()->rowCount(), 3);
        QCOMPARE(v.findChild<QLabel*>("labelIngresos")->text(), QString("$ 500.000,00"));
        QCOMPARE(v.findChild<QLabel*>("labelGastos")->text(), QString("$ 138.450,50"));
        QCOMPARE(v.findChild<QLabel*>("labelResultado")->text(), QString("+$ 361.549,50"));
        // Saldo: 100.000 + 500.000 - 123.450,50 - 15.000 - 20.000
        QCOMPARE(v.findChild<QLabel*>("labelSaldo")->text(), QString("$ 441.549,50"));
        // El más nuevo arriba, con el monto con signo
        QCOMPARE(tabla->model()->index(0, 4).data().toString(), QString("-$ 15.000,00"));

        v.moverPeriodo(+1);
        QCOMPARE(tabla->model()->rowCount(), 1);
        v.setVista(VentanaPrincipal::Vista::Anio);
        QCOMPARE(v.findChild<QLabel*>("labelPeriodo")->text(), QString("2025"));
        QCOMPARE(tabla->model()->rowCount(), 4);
        v.setVista(VentanaPrincipal::Vista::Todo);
        QVERIFY(!v.findChild<QToolButton*>("btnAnterior")->isEnabled());
        QCOMPARE(tabla->model()->rowCount(), 4);

        // Buscador: sin tildes ni mayúsculas, también por monto
        v.setVista(VentanaPrincipal::Vista::Anio);
        auto* buscar = v.findChild<QLineEdit*>("inputBuscar");
        buscar->setText("coto");
        QCOMPARE(tabla->model()->rowCount(), 1);
        buscar->setText("SUPERMERCADO");
        QCOMPARE(tabla->model()->rowCount(), 2);
        buscar->setText("15.000");
        QCOMPARE(tabla->model()->rowCount(), 1);
        buscar->clear();
        v.findChild<QComboBox*>("comboFiltroTipo")->setCurrentIndex(1);   // solo ingresos
        QCOMPARE(tabla->model()->rowCount(), 1);
        v.findChild<QComboBox*>("comboFiltroTipo")->setCurrentIndex(0);

        // Totales
        auto* meses = v.findChild<QTableWidget*>("tablaMeses");
        QCOMPARE(meses->rowCount(), 13);
        QCOMPARE(meses->item(2, 2)->text(), QString("$ 138.450,50"));
        QCOMPARE(meses->item(12, 2)->text(), QString("$ 158.450,50"));
        auto* cats = v.findChild<QTableWidget*>("tablaCategorias");
        QCOMPARE(cats->item(0, 0)->text(), QString("Gastos"));
        QCOMPARE(cats->item(1, 0)->text(), QString("Supermercado"));
        QCOMPARE(cats->item(1, 1)->text(), QString("$ 143.450,50"));
        auto* anios = v.findChild<QTableWidget*>("tablaAnios");
        QCOMPARE(anios->rowCount(), 2);
        QCOMPARE(anios->item(0, 0)->text(), QString("2025"));

        // Gráficos con los datos del período
        auto* torta = v.findChild<GraficoTorta*>("tortaGastos");
        QCOMPARE(torta->porciones().size(), 2);
        QCOMPARE(torta->porciones().first().nombre, QString("Supermercado"));
    }

    void clicEnBarraVaAlMes()
    {
        Cuentas c(":memory:");
        QVERIFY(c.crearMovimiento(mov(c, {2025, 7, 1}, "Sueldo", Tipo::Ingreso, 100000)).ok);
        VentanaPrincipal v(c);
        v.resize(1100, 700);
        v.setVista(VentanaPrincipal::Vista::Anio);
        v.setPeriodo({2025, 1, 1});
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.findChild<QTabWidget*>("tabWidget")->setCurrentIndex(1);
        auto* barras = v.findChild<GraficoBarras*>("graficoBarras");
        QTest::qWait(50);
        QTest::mouseClick(barras, Qt::LeftButton, {}, barras->rectBarra(6, true).center().toPoint());
        QCOMPARE(v.vista(), VentanaPrincipal::Vista::Mes);
        QCOMPARE(v.periodo(), QDate(2025, 7, 1));

        // En «Todo», un clic en una barra va a ese año
        v.setVista(VentanaPrincipal::Vista::Todo);
        QTest::qWait(20);
        QTest::mouseClick(barras, Qt::LeftButton, {}, barras->rectBarra(0, false).center().toPoint() - QPoint(0, 5));
        QCOMPARE(v.vista(), VentanaPrincipal::Vista::Anio);
        QCOMPARE(v.periodo().year(), 2025);
    }

    void dialogoNuevoGasto()
    {
        Cuentas c(":memory:");
        VentanaPrincipal v(c);
        v.show();
        alAbrir<DialogoMovimiento>([](DialogoMovimiento* d) {
            QVERIFY(d->findChild<QRadioButton*>("radioGasto")->isChecked());
            auto* combo = d->findChild<QComboBox*>("comboCategoria");
            combo->setCurrentIndex(combo->findText("Transporte"));
            auto* monto = d->findChild<QLineEdit*>("inputMonto");
            auto* guardar = d->findChild<QDialogButtonBox*>("botones")->button(QDialogButtonBox::Save);
            monto->setText("abc");
            guardar->click();                                     // monto inválido: no se cierra
            QVERIFY(d->isVisible());
            QVERIFY(!d->findChild<QLabel*>("labelError")->text().isEmpty());
            monto->setText("1.234,56");
            d->findChild<QLineEdit*>("inputDescripcion")->setText("Nafta");
            guardar->click();
        });
        v.nuevoGasto();
        const auto lista = c.movimientos();
        QCOMPARE(lista.size(), 1);
        QCOMPARE(lista.first().monto, Centavos(123456));
        QCOMPARE(lista.first().categoria, QString("Transporte"));
        QCOMPARE(lista.first().descripcion, QString("Nafta"));
        QCOMPARE(lista.first().fecha, QDate::currentDate());
        QCOMPARE(v.idsSeleccionados(), QList<qint64>{lista.first().id});   // queda elegido el nuevo
    }

    void dialogoEditarYCambiarTipo()
    {
        Cuentas c(":memory:");
        const auto r = c.crearMovimiento(mov(c, {2025, 5, 1}, "Internet", Tipo::Gasto, 2500000, "Fibra"));
        DialogoMovimiento d(c, *c.movimiento(r.id));
        QCOMPARE(d.findChild<QLineEdit*>("inputMonto")->text(), QString("25.000,00"));
        QCOMPARE(d.findChild<QComboBox*>("comboCategoria")->currentText(), QString("Internet"));
        QCOMPARE(d.findChild<QDateEdit*>("inputFecha")->date(), QDate(2025, 5, 1));
        // Pasarlo a ingreso: el combo ofrece las categorías de ingresos
        d.findChild<QRadioButton*>("radioIngreso")->setChecked(true);
        auto* combo = d.findChild<QComboBox*>("comboCategoria");
        QVERIFY(combo->findText("Sueldo") >= 0);
        QCOMPARE(combo->findText("Internet"), -1);
        combo->setCurrentIndex(combo->findText("Ingresos Varios"));
        d.accept();
        QCOMPARE(d.result(), int(QDialog::Accepted));
        const auto m = c.movimiento(r.id);
        QCOMPARE(m->tipo, Tipo::Ingreso);
        QCOMPARE(m->categoria, QString("Ingresos Varios"));
        QCOMPARE(c.movimientos().size(), 1);                      // editó, no duplicó
    }

    void dialogoMovimientoCategoriasEnOrdenAlfabetico()
    {
        Cuentas c(":memory:");
        QVERIFY(c.crearCategoria("Farmacia", Tipo::Gasto).ok);
        Movimiento nuevo;
        nuevo.tipo = Tipo::Gasto;
        DialogoMovimiento d(c, nuevo);
        auto* combo = d.findChild<QComboBox*>("comboCategoria");
        const auto items = [combo] { QStringList l; for (int i = 0; i < combo->count(); ++i) l << combo->itemText(i); return l; };
        QCOMPARE(items(), (QStringList{"Farmacia", "Gastos Varios", "Internet", "Salud", "Servicios", "Supermercado",
                                       "Teléfono", "Transporte", "Vestimenta"}));
        QCOMPARE(combo->currentText(), QString("Supermercado"));   // la de siempre, no la primera de la lista

        // Pasarlo a ingreso: sus categorías, también en orden, y arranca en «Sueldo» y no en «Ingresos Varios»
        d.findChild<QRadioButton*>("radioIngreso")->setChecked(true);
        QCOMPARE(items(), (QStringList{"Ingresos Varios", "Sueldo"}));
        QCOMPARE(combo->currentText(), QString("Sueldo"));
        d.findChild<QRadioButton*>("radioGasto")->setChecked(true);
        QCOMPARE(combo->currentText(), QString("Supermercado"));

        // Una categoría creada con «+» queda elegida y en el lugar que le toca
        alAbrir<QInputDialog>([](QInputDialog* q) { q->setTextValue("Gimnasio"); q->accept(); });
        d.findChild<QToolButton*>("btnNuevaCategoria")->click();
        QCOMPARE(combo->currentText(), QString("Gimnasio"));
        QCOMPARE(items().indexOf("Gimnasio"), 2);                  // entre «Gastos Varios» e «Internet»
    }

    void dialogoCategoriasEnOrdenAlfabetico()
    {
        Cuentas c(":memory:");
        DialogoCategorias d(c, Tipo::Gasto);
        auto* lista = d.findChild<QListWidget*>("listaGastos");
        const auto nombres = [lista] {
            QStringList l;
            for (int i = 0; i < lista->count(); ++i) l << lista->item(i)->data(Qt::UserRole + 1).toString();
            return l;
        };
        QCOMPARE(nombres(), (QStringList{"Gastos Varios", "Internet", "Salud", "Servicios", "Supermercado", "Teléfono",
                                         "Transporte", "Vestimenta"}));
        QVERIFY(d.agregar("Gimnasio").ok);
        QCOMPARE(nombres().indexOf("Gimnasio"), 1);                // entre «Gastos Varios» e «Internet»
        QCOMPARE(lista->currentRow(), 1);                          // y queda elegida
        QCOMPARE(d.seleccionada(), idCategoria(c, "Gimnasio", Tipo::Gasto));
        QVERIFY(d.renombrar("Zapatería").ok);                      // renombrada, se muda al final y sigue elegida
        QCOMPARE(nombres().last(), QString("Zapatería"));
        QCOMPARE(lista->currentItem()->data(Qt::UserRole + 1).toString(), QString("Zapatería"));
        QCOMPARE(d.seleccionada(), idCategoria(c, "Zapatería", Tipo::Gasto));
    }

    void eliminarYSaldoInicial()
    {
        Cuentas c(":memory:");
        const auto a = c.crearMovimiento(mov(c, {2025, 1, 1}, "Sueldo", Tipo::Ingreso, 100000));
        const auto b = c.crearMovimiento(mov(c, {2025, 1, 2}, "Supermercado", Tipo::Gasto, 30000));
        VentanaPrincipal v(c);
        QCOMPARE(v.eliminarMovimientos({b.id}), 1);
        QCOMPARE(c.movimientos().size(), 1);
        QVERIFY(c.movimiento(a.id).has_value());
        QVERIFY(v.fijarSaldoInicialTexto("-5.000"));
        QCOMPARE(c.saldoInicial(), Centavos(-500000));
        QCOMPARE(v.findChild<QLabel*>("labelSaldo")->text(), QString("-$ 4.000,00"));
        QVERIFY(!v.fijarSaldoInicialTexto("cinco mil"));
        QCOMPARE(c.saldoInicial(), Centavos(-500000));
    }

    void exportarEImportar()
    {
        Cuentas origen(":memory:");
        QVERIFY(origen.crearCategoria("Mascotas", Tipo::Gasto).ok);
        QVERIFY(origen.crearMovimiento(mov(origen, {2025, 2, 1}, "Mascotas", Tipo::Gasto, 450000, "Alimento; perro")).ok);
        QVERIFY(origen.crearMovimiento(mov(origen, {2025, 2, 3}, "Sueldo", Tipo::Ingreso, 80000000)).ok);
        QVERIFY(origen.crearMovimiento(mov(origen, {2025, 3, 3}, "Sueldo", Tipo::Ingreso, 80000000)).ok);
        VentanaPrincipal vo(origen);
        vo.setVista(VentanaPrincipal::Vista::Mes);
        vo.setPeriodo({2025, 2, 1});
        const QString ruta = dir_.filePath("feb.csv");
        QVERIFY(vo.exportarArchivo(ruta));                        // exporta solo el período que se ve

        Cuentas destino(":memory:");
        VentanaPrincipal vd(destino);
        vd.setVista(VentanaPrincipal::Vista::Mes);
        vd.setPeriodo({2026, 11, 1});                             // un mes vacío: al importar salta a febrero 2025
        QString error;
        ResultadoImportacion r = vd.importarArchivo(ruta, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(r.nuevos, 2);
        QCOMPARE(r.categoriasNuevas, QStringList{"Gasto: Mascotas"});
        QCOMPARE(destino.movimientos().size(), 2);
        QCOMPARE(vd.periodo(), QDate(2025, 2, 1));
        QCOMPARE(destino.totales({2025, 2, 1}, {2025, 2, 28}).saldo(), Centavos(80000000 - 450000));
        r = vd.importarArchivo(ruta);                             // otra vez: no duplica
        QCOMPARE(r.nuevos, 0);
        QCOMPARE(r.repetidos, 2);

        QVERIFY(vd.importarArchivo(dir_.filePath("no-existe.csv"), &error).nuevos == 0);
        QVERIFY(!error.isEmpty());
    }

    void categorias()
    {
        Cuentas c(":memory:");
        QVERIFY(c.crearMovimiento(mov(c, {2025, 1, 1}, "Vestimenta", Tipo::Gasto, 1000)).ok);
        DialogoCategorias d(c, Tipo::Gasto);
        QCOMPARE(d.tipoActual(), Tipo::Gasto);
        QVERIFY(d.agregar("Gimnasio").ok);
        QVERIFY(c.buscarCategoria("Gimnasio", Tipo::Gasto).has_value());
        QCOMPARE(d.seleccionada(), idCategoria(c, "Gimnasio", Tipo::Gasto));
        QVERIFY(!d.agregar("gimnasio").ok);                       // repetida (sin importar mayúsculas)
        QVERIFY(d.renombrar("Gym").ok);
        QVERIFY(d.ponerColor(5).ok);
        QCOMPARE(c.buscarCategoria("Gym", Tipo::Gasto)->color, 5);
        QVERIFY(d.eliminar().ok);                                 // sin movimientos: se borra
        QVERIFY(!c.buscarCategoria("Gym", Tipo::Gasto).has_value());

        // Con movimientos: hay que pasarlos a otra
        d.seleccionar(idCategoria(c, "Vestimenta", Tipo::Gasto));
        QVERIFY(!d.eliminar().ok);
        const qint64 varios = idCategoria(c, "Gastos Varios", Tipo::Gasto);
        QVERIFY(d.eliminar(varios).ok);
        QCOMPARE(c.movimientos().first().categoria, QString("Gastos Varios"));
        QVERIFY(d.huboCambios());

        // Pestaña de ingresos
        d.seleccionar(idCategoria(c, "Sueldo", Tipo::Ingreso));
        QCOMPARE(d.tipoActual(), Tipo::Ingreso);
        QVERIFY(d.agregar("Aguinaldo").ok);
        QVERIFY(c.buscarCategoria("Aguinaldo", Tipo::Ingreso).has_value());
    }

    // ---- 2.1.0: cuentas, presupuestos y movimientos fijos
    void cuentasEnLaVentana()
    {
        Cuentas c(":memory:");
        VentanaPrincipal w(c);
        w.show();
        auto* combo = w.findChild<QComboBox*>("comboCuenta");
        QVERIFY(combo && combo->isHidden());                        // con una sola cuenta no se muestra
        {
            DialogoCuentas d(c);
            QVERIFY(d.agregar("Banco", "1.000").ok);
            QVERIFY(!d.agregar("Otra", "mucho").ok);
            QCOMPARE(d.filas(), 2);
        }
        w.refrescar();
        QVERIFY(!combo->isHidden());
        QCOMPARE(combo->count(), 3);                                // Todas, Principal, Banco
        Movimiento m = mov(c, QDate::currentDate(), "Sueldo", Tipo::Ingreso, 50000);
        m.cuentaId = combo->itemData(2).toLongLong();
        QVERIFY(c.crearMovimiento(m).ok);
        QVERIFY(c.crearMovimiento(mov(c, QDate::currentDate(), "Supermercado", Tipo::Gasto, 3000)).ok);
        combo->setCurrentIndex(2);
        emit combo->activated(2);
        QCOMPARE(c.cuentaVista(), combo->itemData(2).toLongLong());
        QCOMPARE(w.findChild<QLabel*>("labelSaldo")->text(), formatearPesos(100000 + 50000));
        combo->setCurrentIndex(0);
        emit combo->activated(0);
        QCOMPARE(w.findChild<QLabel*>("labelSaldo")->text(), formatearPesos(100000 + 50000 - 3000));
    }

    void avisoDePresupuesto()
    {
        Cuentas c(":memory:");
        const qint64 super = idCategoria(c, "Supermercado", Tipo::Gasto);
        QVERIFY(c.fijarPresupuesto(super, 10000).ok);
        VentanaPrincipal w(c);
        w.show();
        auto* aviso = w.findChild<QLabel*>("labelAvisos");
        QVERIFY(aviso->isHidden());
        QVERIFY(c.crearMovimiento(mov(c, QDate::currentDate(), "Supermercado", Tipo::Gasto, 12000)).ok);
        w.refrescar();
        QVERIFY(!aviso->isHidden());
        QVERIFY2(aviso->text().contains("Te pasaste"), qPrintable(aviso->text()));
        QVERIFY(aviso->text().contains("120 %"));
    }

    void movimientoFijoDesdeElDialogo()
    {
        Cuentas c(":memory:");
        Recurrente r;
        r.tipo = Tipo::Gasto;
        r.categoriaId = idCategoria(c, "Internet", Tipo::Gasto);
        r.dia = 1;
        const QDate hoy = QDate::currentDate();
        r.desde = QDate(hoy.year(), hoy.month(), 1).addMonths(-1);  // el 1 del mes pasado
        DialogoFijo d(c, r);
        d.setMonto("15.000");
        d.accept();
        QCOMPARE(d.result(), int(QDialog::Accepted));
        QCOMPARE(c.recurrentes().size(), 1);
        QCOMPARE(c.recurrentes().first().monto, 1500000);
        QCOMPARE(c.generarRecurrentes(), 2);                        // el mes pasado y este
        DialogoFijos lista(c);
        QCOMPARE(lista.filas(), 1);
    }

    // ---- 2.8.0
    void buscadorPorMonto()
    {
        Cuentas c(":memory:");
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 5}, "Sueldo", Tipo::Ingreso, 50000000, "Marzo")).ok);        // 500.000
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 10}, "Supermercado", Tipo::Gasto, 12345050, "Coto #mensual")).ok);   // 123.450,50
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 20}, "Transporte", Tipo::Gasto, 1500000, "SUBE")).ok);       // 15.000
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 21}, "Transporte", Tipo::Gasto, 250000, "Colectivo")).ok);   // 2.500
        VentanaPrincipal v(c);
        v.setVista(VentanaPrincipal::Vista::Mes);
        v.setPeriodo({2025, 3, 1});
        auto* tabla = v.findChild<QTableView*>("tablaMovimientos");
        auto* buscar = v.findChild<QLineEdit*>("inputBuscar");
        buscar->setText("1000-20000");
        QCOMPARE(tabla->model()->rowCount(), 2);                 // 15.000 y 2.500
        buscar->setText(">100000");
        QCOMPARE(tabla->model()->rowCount(), 2);                 // sueldo y súper
        buscar->setText("<=2.500");
        QCOMPARE(tabla->model()->rowCount(), 1);
        buscar->setText("transporte >10000");                    // palabra + monto
        QCOMPARE(tabla->model()->rowCount(), 1);
        QCOMPARE(tabla->model()->index(0, 3).data().toString(), QString("SUBE"));
        buscar->setText("#mensual");                             // etiqueta
        QCOMPARE(tabla->model()->rowCount(), 1);
        buscar->setText("123.450,50");                           // un número suelto sigue buscando como texto
        QCOMPARE(tabla->model()->rowCount(), 1);
        buscar->setText(">1000000");                             // nada: el estado vacío
        QCOMPARE(tabla->model()->rowCount(), 0);
        buscar->clear();
        QCOMPARE(tabla->model()->rowCount(), 4);
    }

    void ventanaDeSincronizacion()
    {
        QSettings().setValue("sinc/puerto", 0);                  // cualquier puerto libre (no choca con el programa real)
        Cuentas pc(":memory:");
        VentanaPrincipal w(pc);
        {
            DialogoSincronizacion d(pc, w.servidor());
            d.show();
            QVERIFY(!d.findChild<QCheckBox*>("chkActivar")->isChecked());
            QVERIFY(d.findChild<QLabel*>("labelCodigo")->text().isEmpty());
            d.findChild<QPushButton*>("btnEmparejar")->click();  // activa y muestra el código
            QVERIFY(w.servidor().activo());
            QVERIFY(d.findChild<QCheckBox*>("chkActivar")->isChecked());
            QCOMPARE(d.findChild<QLabel*>("labelCodigo")->text().remove(' ').size(), 6);
            QVERIFY(d.findChild<QLabel*>("labelEstado")->text().startsWith("Activada"));
            d.findChild<QPushButton*>("btnEmparejar")->click();  // cancelar el código
            QVERIFY(d.findChild<QLabel*>("labelCodigo")->text().isEmpty());
            QCOMPARE(d.findChild<QListWidget*>("listaPares")->count(), 1);   // «Ninguno todavía»
            QVERIFY(!d.findChild<QPushButton*>("btnQuitar")->isEnabled());
        }
        QVERIFY(QSettings().value("sinc/activa").toBool());     // se recuerda

        // Un celular se empareja y sincroniza: la ventana se actualiza sola y el celular aparece en la lista
        Cuentas cel(":memory:");
        cel.setNombreDispositivo("Celu de prueba");
        Movimiento m = mov(cel, {2026, 10, 1}, "Supermercado", Tipo::Gasto, 154350, "Cargado en el celular");
        QVERIFY(cel.crearMovimiento(m).ok);
        w.setVista(VentanaPrincipal::Vista::Todo);
        auto* tabla = w.findChild<QTableView*>("tablaMovimientos");
        QCOMPARE(tabla->model()->rowCount(), 0);
        ClienteSinc cliente(cel);
        QSignalSpy fin(&cliente, &ClienteSinc::terminado);
        cliente.emparejar("127.0.0.1", w.servidor().puerto(), w.servidor().nuevoCodigo());
        QVERIFY(fin.wait(15000));
        QVERIFY2(fin.first().at(0).toBool(), qPrintable(fin.first().at(1).toString()));
        QTRY_COMPARE(tabla->model()->rowCount(), 1);
        {
            DialogoSincronizacion d(pc, w.servidor());
            auto* lista = d.findChild<QListWidget*>("listaPares");
            QCOMPARE(lista->count(), 1);
            QVERIFY(lista->item(0)->text().startsWith("Celu de prueba"));
            lista->setCurrentRow(0);
            QVERIFY(d.findChild<QPushButton*>("btnQuitar")->isEnabled());
            d.findChild<QCheckBox*>("chkActivar")->setChecked(false);   // apagarla también se recuerda
            QVERIFY(!w.servidor().activo());
        }
        QVERIFY(!QSettings().value("sinc/activa").toBool());
    }

    void transferenciaDesdeLaVentana()
    {
        Cuentas c(":memory:");
        VentanaPrincipal w(c);
        w.show();
        auto* boton = w.findChild<QPushButton*>("btnNuevaTransferencia");
        QVERIFY(boton->isHidden());                              // con una sola cuenta no hay a dónde transferir
        const qint64 banco = c.crearCuentaDinero("Banco", 0).id;
        QVERIFY(c.crearMovimiento(mov(c, QDate::currentDate(), "Sueldo", Tipo::Ingreso, 1000000)).ok);
        w.setVista(VentanaPrincipal::Vista::Mes);
        w.setPeriodo(QDate::currentDate());
        QVERIFY(!boton->isHidden());

        alAbrir<DialogoTransferencia>([banco](DialogoTransferencia* d) {
            auto* monto = d->findChild<QLineEdit*>("inputMonto");
            auto* guardar = d->findChild<QDialogButtonBox*>("botones")->button(QDialogButtonBox::Save);
            auto* hasta = d->findChild<QComboBox*>("comboHasta");
            QCOMPARE(hasta->currentData().toLongLong(), banco);   // de entrada, a la otra cuenta
            hasta->setCurrentIndex(d->findChild<QComboBox*>("comboDesde")->currentIndex());
            monto->setText("4.000");
            guardar->click();                                     // misma cuenta: no se cierra
            QVERIFY(d->isVisible());
            QVERIFY(!d->findChild<QLabel*>("labelError")->text().isEmpty());
            hasta->setCurrentIndex(hasta->findData(banco));
            d->findChild<QLineEdit*>("inputDescripcion")->setText("Al banco");
            guardar->click();
        });
        boton->click();
        QCOMPARE(c.movimientos().size(), 3);
        // Las tarjetas no cambian: no es ingreso ni gasto
        QCOMPARE(w.findChild<QLabel*>("labelIngresos")->text(), QString("$ 10.000,00"));
        QCOMPARE(w.findChild<QLabel*>("labelGastos")->text(), QString("$ 0,00"));
        auto* tabla = w.findChild<QTableView*>("tablaMovimientos");
        QStringList categorias;
        for (int r = 0; r < tabla->model()->rowCount(); ++r) categorias << tabla->model()->index(r, 2).data().toString();
        QVERIFY2(categorias.contains("Transferencia a Banco"), qPrintable(categorias.join(", ")));
        QVERIFY(categorias.contains("Transferencia desde Principal"));
        auto* filtro = w.findChild<QComboBox*>("comboFiltroTipo");
        filtro->setCurrentIndex(3);                               // solo transferencias
        QCOMPARE(tabla->model()->rowCount(), 2);
        filtro->setCurrentIndex(2);                               // solo gastos: la transferencia no es un gasto
        QCOMPARE(tabla->model()->rowCount(), 0);
        filtro->setCurrentIndex(0);
        // Mirando el banco, el saldo es lo que entró
        auto* combo = w.findChild<QComboBox*>("comboCuenta");
        combo->setCurrentIndex(combo->findData(banco));
        emit combo->activated(combo->currentIndex());
        QCOMPARE(w.findChild<QLabel*>("labelSaldo")->text(), QString("$ 4.000,00"));
        combo->setCurrentIndex(0);
        emit combo->activated(0);
        QCOMPARE(w.findChild<QLabel*>("labelSaldo")->text(), QString("$ 10.000,00"));
        // Borrar una mitad borra las dos (con «Deshacer» como siempre)
        qint64 idTransf = 0;
        for (const Movimiento& m : c.movimientos()) if (m.transferencia) idTransf = m.id;
        QCOMPARE(w.eliminarMovimientos({idTransf}), 1);
        QCOMPARE(c.movimientos().size(), 1);
    }

    void verEntreFechasYComparar()
    {
        Cuentas c(":memory:");
        QVERIFY(c.crearMovimiento(mov(c, {2025, 2, 10}, "Supermercado", Tipo::Gasto, 100000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 10}, "Supermercado", Tipo::Gasto, 120000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 12}, "Sueldo", Tipo::Ingreso, 500000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 4, 2}, "Transporte", Tipo::Gasto, 30000, "Viaje #vacaciones")).ok);
        VentanaPrincipal w(c);
        w.show();
        w.setVista(VentanaPrincipal::Vista::Mes);
        w.setPeriodo({2025, 3, 1});
        // Marzo contra febrero: 20 % más de gastos (en rojo), ingresos sin nada antes
        QCOMPARE(w.findChild<QLabel*>("labelGastosComparacion")->text(), QString("+20 % que en febrero 2025"));
        QCOMPARE(w.findChild<QLabel*>("labelIngresosComparacion")->text(), QString("Sin datos en febrero 2025"));
        QVERIFY(w.findChild<QLabel*>("labelResultadoComparacion")->text().startsWith("febrero 2025: -$ 1.000,00"));
        auto* cats = w.findChild<QTableWidget*>("tablaCategorias");
        QCOMPARE(cats->item(1, 0)->text(), QString("Supermercado"));
        QCOMPARE(cats->item(1, 3)->text(), QString("+20 %"));
        w.setVista(VentanaPrincipal::Vista::Todo);
        QVERIFY(w.findChild<QLabel*>("labelGastosComparacion")->isHidden());   // en «Todo» no hay con qué comparar

        // Entre fechas: del 1/3 al 15/4 (las tarjetas, la tabla y los gráficos de esos días)
        w.verRango({2025, 3, 1}, {2025, 4, 15});
        QCOMPARE(w.vista(), VentanaPrincipal::Vista::Rango);
        QCOMPARE(w.findChild<QComboBox*>("comboVista")->currentIndex(), 3);
        QCOMPARE(w.findChild<QLabel*>("labelPeriodo")->text(), QString("1/3/2025 al 15/4/2025"));
        QCOMPARE(w.findChild<QTableView*>("tablaMovimientos")->model()->rowCount(), 3);
        QCOMPARE(w.findChild<QLabel*>("labelGastos")->text(), QString("$ 1.500,00"));
        QCOMPARE(w.findChild<QLabel*>("labelGastosComparacion")->text(), QString("+50 % que en los 46 días anteriores"));
        auto* barras = w.findChild<GraficoBarras*>("graficoBarras");
        QVERIFY(barras);
        // Etiquetas en los totales
        bool hayEtiqueta = false;
        for (int r = 0; r < cats->rowCount(); ++r) if (cats->item(r, 0) && cats->item(r, 0)->text() == "#vacaciones") hayEtiqueta = true;
        QVERIFY(hayEtiqueta);
        // ‹ › mueven los mismos días
        w.moverPeriodo(-1);
        QCOMPARE(w.rango(), (QPair<QDate, QDate>{{2025, 1, 14}, {2025, 2, 28}}));
        QCOMPARE(w.findChild<QTableView*>("tablaMovimientos")->model()->rowCount(), 1);

        // Elegir «Entre fechas…» en el combo pide las fechas; si se cancela, vuelve a lo de antes
        w.setVista(VentanaPrincipal::Vista::Mes);
        alAbrir<DialogoFechas>([](DialogoFechas* d) { d->reject(); });
        w.findChild<QComboBox*>("comboVista")->setCurrentIndex(3);
        QCOMPARE(w.vista(), VentanaPrincipal::Vista::Mes);
        QCOMPARE(w.findChild<QComboBox*>("comboVista")->currentIndex(), 0);
        alAbrir<DialogoFechas>([](DialogoFechas* d) { d->elegir(DialogoFechas::EntreFechas, {2025, 2, 1}, {2025, 2, 20}); d->accept(); });
        w.findChild<QComboBox*>("comboVista")->setCurrentIndex(3);
        QCOMPARE(w.vista(), VentanaPrincipal::Vista::Rango);
        QCOMPARE(w.rango(), (QPair<QDate, QDate>{{2025, 2, 1}, {2025, 2, 20}}));
        // Volver a elegir «Entre fechas…» estando ahí pide otras fechas
        QApplication::processEvents();
        alAbrir<DialogoFechas>([](DialogoFechas* d) { d->elegir(DialogoFechas::EntreFechas, {2025, 3, 1}, {2025, 3, 31}); d->accept(); });
        emit w.findChild<QComboBox*>("comboVista")->activated(3);
        QCOMPARE(w.rango(), (QPair<QDate, QDate>{{2025, 3, 1}, {2025, 3, 31}}));
        // ‹ › con un mes entero: el mes anterior entero
        w.moverPeriodo(-1);
        QCOMPARE(w.rango(), (QPair<QDate, QDate>{{2025, 2, 1}, {2025, 2, 28}}));
        // Ir a una fecha (lo recién cargado, «Hoy») sale de «Entre fechas» y muestra su mes
        w.setPeriodo({2025, 6, 10});
        QCOMPARE(w.vista(), VentanaPrincipal::Vista::Mes);
        QCOMPARE(w.findChild<QComboBox*>("comboVista")->currentIndex(), 0);
        QCOMPARE(w.rango(), (QPair<QDate, QDate>{{2025, 6, 1}, {2025, 6, 30}}));
    }

    void exportarTodoYEntreFechas()
    {
        Cuentas c(":memory:");
        QVERIFY(c.crearMovimiento(mov(c, {2024, 12, 31}, "Sueldo", Tipo::Ingreso, 1000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 2, 1}, "Sueldo", Tipo::Ingreso, 2000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2025, 3, 15}, "Sueldo", Tipo::Ingreso, 3000)).ok);
        VentanaPrincipal w(c);
        w.setVista(VentanaPrincipal::Vista::Mes);
        w.setPeriodo({2025, 3, 1});
        const auto filas = [](const QString& ruta) {
            QFile f(ruta);
            return f.open(QIODevice::ReadOnly) ? leerCsv(decodificarArchivo(f.readAll())).filas.size() : -1;
        };
        QVERIFY(w.exportarArchivo(dir_.filePath("visto.csv")));
        QCOMPARE(filas(dir_.filePath("visto.csv")), 1);
        QVERIFY(w.exportarArchivo(dir_.filePath("todo.csv"), {}, {}));
        QCOMPARE(filas(dir_.filePath("todo.csv")), 3);
        QVERIFY(w.exportarArchivo(dir_.filePath("rango.csv"), {2024, 12, 1}, {2025, 2, 1}));
        QCOMPARE(filas(dir_.filePath("rango.csv")), 2);

        // El diálogo: por defecto lo que se ve; «hasta» antes que «desde» no se acepta
        DialogoFechas d(DialogoFechas::Modo::Exportar, "Marzo 2025", {2025, 3, 1}, {2025, 3, 31});
        QCOMPARE(d.alcance(), int(DialogoFechas::Periodo));
        QVERIFY(!d.findChild<QDateEdit*>("inputDesde")->isEnabled());
        d.elegir(DialogoFechas::EntreFechas, {2025, 3, 10}, {2025, 3, 1});
        QVERIFY(d.findChild<QDateEdit*>("inputDesde")->isEnabled());
        d.accept();
        QVERIFY(d.result() != QDialog::Accepted);
        QVERIFY(!d.findChild<QLabel*>("labelError")->text().isEmpty());
        d.elegir(DialogoFechas::Todo);
        d.accept();
        QCOMPARE(d.result(), int(QDialog::Accepted));
    }

    void copiaAutomaticaAlAbrir()
    {
        QTemporaryDir dir;
        Cuentas c(dir.filePath("cuentas.db"));
        {
            VentanaPrincipal w(c);                                // al abrir: la copia de hoy
        }
        const QString hoy = QStringLiteral("respaldos/cuentas-%1.db").arg(QDate::currentDate().toString(Qt::ISODate));
        QVERIFY(QFile::exists(dir.filePath(hoy)));
        VentanaPrincipal w(c);
        QVERIFY(!w.respaldar().hecha);                            // una por día
    }
};

QTEST_MAIN(TstVentana)
#include "tst_ventana.moc"
