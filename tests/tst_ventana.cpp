// Pruebas de las pantallas: ventana principal, diálogo de movimiento y diálogo de categorías.
#include "db.h"
#include "dialogocategorias.h"
#include "dialogomovimiento.h"
#include "graficos.h"
#include "ventanaprincipal.h"
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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
};

QTEST_MAIN(TstVentana)
#include "tst_ventana.moc"
