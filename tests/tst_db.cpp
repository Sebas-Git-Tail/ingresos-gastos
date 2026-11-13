#include "db.h"
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QCoreApplication>
#include <QtTest>

namespace {
qint64 idDe(Cuentas& c, const QString& nombre, Tipo t) { return c.buscarCategoria(nombre, t)->id; }
Movimiento mov(Cuentas& c, QDate fecha, const QString& cat, Tipo t, Centavos monto, const QString& desc = {})
{
    Movimiento m; m.fecha = fecha; m.categoriaId = idDe(c, cat, t); m.monto = monto; m.descripcion = desc; return m;
}
FilaImportada fila(QDate f, Tipo t, QString cat, Centavos monto, QString desc = {})
{
    FilaImportada x; x.fecha = f; x.tipo = t; x.categoria = cat; x.monto = monto; x.descripcion = desc; return x;
}
}

class TstDb : public QObject
{
    Q_OBJECT
private slots:
    void categoriasIniciales()
    {
        Cuentas c(":memory:");
        QVERIFY2(c.abierta(), qPrintable(c.errorApertura()));
        QStringList gastos, ingresos;
        for (const Categoria& k : c.categorias(Tipo::Gasto)) gastos << k.nombre;
        for (const Categoria& k : c.categorias(Tipo::Ingreso)) ingresos << k.nombre;
        for (const char* n : {"Supermercado", "Transporte", "Vestimenta", "Internet", "Teléfono", "Gastos Varios"})
            QVERIFY2(gastos.contains(QString::fromUtf8(n)), n);
        QCOMPARE(ingresos, (QStringList{"Ingresos Varios", "Sueldo"}));    // por orden alfabético
        QSet<int> colores;
        for (const Categoria& k : c.categorias(Tipo::Gasto)) colores << k.color;
        QCOMPARE(colores.size(), c.categorias(Tipo::Gasto).size());      // cada gasto inicial con su color
    }

    void categoriasPorOrdenAlfabetico()
    {
        Cuentas c(":memory:");
        // Se crean desordenadas a propósito. Mayúsculas y tildes no cambian el lugar («EPE» va después de
        // «Electrodomésticos», «gimnasio» entre las G) y la ñ es una letra aparte, después de la n.
        for (const char* n : {"TGI", "Ñoquis", "gimnasio", "EPE", "Árboles", "Teléfono Ariel", "Niños", "IIBB Ariel", "Expensas",
                              "Electrodomésticos y muebles", "Obra social Carina", "Obra social", "Gastos bancarios", "Nutrición",
                              "Aguas santafesinas", "Ahorros"})
            QVERIFY2(c.crearCategoria(QString::fromUtf8(n), Tipo::Gasto).ok, n);
        const auto nombres = [&c](Tipo t) { QStringList l; for (const Categoria& k : c.categorias(t)) l << k.nombre; return l; };
        QCOMPARE(nombres(Tipo::Gasto),
                 (QStringList{"Aguas santafesinas", "Ahorros", "Árboles", "Electrodomésticos y muebles", "EPE", "Expensas",
                              "Gastos bancarios", "Gastos Varios", "gimnasio", "IIBB Ariel", "Internet", "Niños", "Nutrición",
                              "Ñoquis", "Obra social", "Obra social Carina", "Salud", "Servicios", "Supermercado", "Teléfono",
                              "Teléfono Ariel", "TGI", "Transporte", "Vestimenta"}));

        // Una nueva queda en su lugar sin hacer nada más; renombrada, se muda al lugar que le toca
        const Resultado farmacia = c.crearCategoria("Farmacia", Tipo::Gasto);
        QVERIFY(farmacia.ok);
        QStringList gastos = nombres(Tipo::Gasto);
        QCOMPARE(gastos.indexOf("Farmacia"), gastos.indexOf("Expensas") + 1);
        QVERIFY(c.renombrarCategoria(farmacia.id, "Zapatería").ok);
        QCOMPARE(nombres(Tipo::Gasto).last(), QString("Zapatería"));
        QVERIFY(c.renombrarCategoria(farmacia.id, "aaa").ok);                 // minúsculas: igual va primera
        QCOMPARE(nombres(Tipo::Gasto).first(), QString("aaa"));

        // Los ingresos también, y en la lista de todas van primero los ingresos
        QVERIFY(c.crearCategoria("Aguinaldo", Tipo::Ingreso).ok);
        QCOMPARE(nombres(Tipo::Ingreso), (QStringList{"Aguinaldo", "Ingresos Varios", "Sueldo"}));
        const QList<Categoria> todas = c.categorias();
        QCOMPARE(todas.size(), 3 + nombres(Tipo::Gasto).size());
        for (int i = 0; i < todas.size(); ++i) QVERIFY((todas[i].tipo == Tipo::Ingreso) == (i < 3));
        QCOMPARE(todas[3].nombre, QString("aaa"));
    }

    void movimientoNuevoArrancaEnLaCategoriaDeSiempreYNoEnLaPrimeraDeLaLista()
    {
        Cuentas c(":memory:");
        // Por orden alfabético las primeras son «Gastos Varios» / «Ingresos Varios»; esas quedan como opción, no como valor inicial
        QCOMPARE(c.categorias(Tipo::Gasto).first().nombre, QString("Gastos Varios"));
        QCOMPARE(c.categoriaInicial(Tipo::Gasto), idDe(c, "Supermercado", Tipo::Gasto));
        QCOMPARE(c.categorias(Tipo::Ingreso).first().nombre, QString("Ingresos Varios"));
        QCOMPARE(c.categoriaInicial(Tipo::Ingreso), idDe(c, "Sueldo", Tipo::Ingreso));
        // Una categoría nueva no la cambia, aunque vaya primera en la lista
        QVERIFY(c.crearCategoria("Aaa", Tipo::Gasto).ok);
        QCOMPARE(c.categoriaInicial(Tipo::Gasto), idDe(c, "Supermercado", Tipo::Gasto));
        // Si se borra la más antigua, pasa a ser la que le sigue; sin ninguna, 0
        QVERIFY(c.eliminarCategoria(idDe(c, "Supermercado", Tipo::Gasto)).ok);
        QCOMPARE(c.categoriaInicial(Tipo::Gasto), idDe(c, "Transporte", Tipo::Gasto));
        for (const Categoria& k : c.categorias(Tipo::Ingreso)) QVERIFY(c.eliminarCategoria(k.id).ok);
        QCOMPARE(c.categoriaInicial(Tipo::Ingreso), qint64(0));
    }

    void editarCategorias()
    {
        Cuentas c(":memory:");
        const Resultado r = c.crearCategoria("  Farmacia  ", Tipo::Gasto);
        QVERIFY(r.ok);
        QCOMPARE(c.categoria(r.id)->nombre, QString("Farmacia"));
        QVERIFY(!c.crearCategoria("farmacia", Tipo::Gasto).ok);           // repetida (sin importar mayúsculas)
        QVERIFY(c.crearCategoria("Farmacia", Tipo::Ingreso).ok);           // en el otro tipo sí se puede
        QVERIFY(!c.crearCategoria("   ", Tipo::Gasto).ok);
        QVERIFY(c.renombrarCategoria(r.id, "Farmacia y perfumería").ok);
        QVERIFY(!c.renombrarCategoria(r.id, "supermercado").ok);
        QVERIFY(c.renombrarCategoria(r.id, "FARMACIA Y PERFUMERÍA").ok);   // cambiar solo mayúsculas sí
        QVERIFY(c.cambiarColor(r.id, 5).ok);
        QCOMPARE(c.categoria(r.id)->color, 5);
        QVERIFY(!c.cambiarColor(r.id, 8).ok);
    }

    void colorDeUnaCategoriaNuevaEsElMenosUsado()
    {
        Cuentas c(":memory:");                       // ingresos iniciales usan los colores 0 y 1
        QCOMPARE(c.categoria(c.crearCategoria("Aguinaldo", Tipo::Ingreso).id)->color, 2);
        QCOMPARE(c.categoria(c.crearCategoria("Alquiler cobrado", Tipo::Ingreso).id)->color, 3);
    }

    void eliminarCategoriaConMovimientosPideDestino()
    {
        Cuentas c(":memory:");
        const qint64 internet = idDe(c, "Internet", Tipo::Gasto);
        QVERIFY(c.crearMovimiento(mov(c, {2026, 11, 1}, "Internet", Tipo::Gasto, 1599900)).ok);
        QVERIFY(!c.eliminarCategoria(internet).ok);                                        // sin destino, no
        QVERIFY(!c.eliminarCategoria(internet, idDe(c, "Sueldo", Tipo::Ingreso)).ok);       // a otro tipo, no
        QVERIFY(!c.eliminarCategoria(internet, internet).ok);
        QVERIFY(c.eliminarCategoria(internet, idDe(c, "Gastos Varios", Tipo::Gasto)).ok);
        QVERIFY(!c.categoria(internet));
        QCOMPARE(c.movimientos().first().categoria, QString("Gastos Varios"));              // el movimiento pasó
        QVERIFY(c.eliminarCategoria(idDe(c, "Vestimenta", Tipo::Gasto)).ok);                // sin movimientos, directo
    }

    void movimientosYValidaciones()
    {
        Cuentas c(":memory:");
        Movimiento m = mov(c, {2026, 11, 1}, "Supermercado", Tipo::Gasto, 1543050, "  Compra  ");
        const Resultado r = c.crearMovimiento(m);
        QVERIFY(r.ok);
        const Movimiento g = *c.movimiento(r.id);
        QCOMPARE(g.categoria, QString("Supermercado"));
        QVERIFY(g.tipo == Tipo::Gasto);
        QCOMPARE(g.descripcion, QString("Compra"));
        m.id = r.id; m.monto = 2000000; m.categoriaId = idDe(c, "Transporte", Tipo::Gasto);
        QVERIFY(c.actualizarMovimiento(m).ok);
        QCOMPARE(c.movimiento(r.id)->monto, Centavos(2000000));
        QCOMPARE(c.movimiento(r.id)->categoria, QString("Transporte"));
        Movimiento malo = m; malo.monto = 0;
        QVERIFY(!c.crearMovimiento(malo).ok);
        malo = m; malo.fecha = QDate();
        QVERIFY(!c.crearMovimiento(malo).ok);
        malo = m; malo.categoriaId = 9999;
        QVERIFY(!c.crearMovimiento(malo).ok);
        QVERIFY(c.eliminarMovimiento(r.id).ok);
        QVERIFY(c.movimientos().isEmpty());
    }

    void movimientosPorPeriodoMasNuevosPrimero()
    {
        Cuentas c(":memory:");
        c.crearMovimiento(mov(c, {2026, 10, 15}, "Sueldo", Tipo::Ingreso, 100));
        c.crearMovimiento(mov(c, {2026, 11, 1}, "Internet", Tipo::Gasto, 200));
        c.crearMovimiento(mov(c, {2026, 11, 20}, "Internet", Tipo::Gasto, 300));
        const auto nov = c.movimientos({2026, 11, 1}, {2026, 11, 30});
        QCOMPARE(nov.size(), 2);
        QCOMPARE(nov[0].fecha, QDate(2026, 11, 20));
        QCOMPARE(c.movimientos().size(), 3);
    }

    void saldoYTotales()
    {
        Cuentas c(":memory:");
        c.crearMovimiento(mov(c, {2025, 12, 31}, "Sueldo", Tipo::Ingreso, 50000000));
        c.crearMovimiento(mov(c, {2026, 1, 5}, "Sueldo", Tipo::Ingreso, 80000000));
        c.crearMovimiento(mov(c, {2026, 1, 10}, "Supermercado", Tipo::Gasto, 12000000));
        c.crearMovimiento(mov(c, {2026, 1, 20}, "Transporte", Tipo::Gasto, 3000000));
        c.crearMovimiento(mov(c, {2026, 3, 2}, "Supermercado", Tipo::Gasto, 15000000));
        c.crearMovimiento(mov(c, {2026, 3, 9}, "Ingresos Varios", Tipo::Ingreso, 2000050));
        c.crearMovimiento(mov(c, {2099, 1, 1}, "Supermercado", Tipo::Gasto, 100));      // a futuro

        QCOMPARE(c.saldo({2026, 12, 31}), Centavos(50000000 + 80000000 - 12000000 - 3000000 - 15000000 + 2000050));
        QCOMPARE(c.saldo({2026, 1, 31}), Centavos(50000000 + 80000000 - 12000000 - 3000000));
        c.fijarSaldoInicial(1000000);
        QCOMPARE(c.saldoInicial(), Centavos(1000000));
        QCOMPARE(c.saldo({2026, 1, 31}), Centavos(1000000 + 50000000 + 80000000 - 12000000 - 3000000));
        c.fijarSaldoInicial(-500);                                    // se puede arrancar en rojo
        QCOMPARE(c.saldoInicial(), Centavos(-500));

        const auto meses = c.totalesPorMes(2026);
        QCOMPARE(meses.size(), 12);
        QCOMPARE(meses[0].ingresos, Centavos(80000000));
        QCOMPARE(meses[0].gastos, Centavos(15000000));
        QCOMPARE(meses[1].ingresos, Centavos(0));
        QCOMPARE(meses[2].gastos, Centavos(15000000));
        QCOMPARE(meses[2].saldo(), Centavos(2000050 - 15000000));

        const auto anios = c.totalesPorAnio();
        QCOMPARE(anios.size(), 3);
        QCOMPARE(anios[0].first, 2025);
        QCOMPARE(anios[1].second.ingresos, Centavos(82000050));
        QCOMPARE(anios[1].second.gastos, Centavos(30000000));
        QCOMPARE(anios[2].first, 2099);

        const auto cat = c.totalesPorCategoria({2026, 1, 1}, {2026, 12, 31}, Tipo::Gasto);
        QCOMPARE(cat.size(), 2);
        QCOMPARE(cat[0].nombre, QString("Supermercado"));             // de mayor a menor
        QCOMPARE(cat[0].total, Centavos(27000000));
        QCOMPARE(cat[1].total, Centavos(3000000));
        QCOMPARE(c.totales({2026, 3, 1}, {2026, 3, 31}).ingresos, Centavos(2000050));
        QCOMPARE(c.primeraFecha(), QDate(2025, 12, 31));
        QCOMPARE(c.ultimaFecha(), QDate(2099, 1, 1));
    }

    void importarCreaCategoriasYNoDuplica()
    {
        Cuentas c(":memory:");
        c.crearMovimiento(mov(c, {2026, 11, 1}, "Supermercado", Tipo::Gasto, 150000, "Café"));
        const QList<FilaImportada> filas = {
            fila({2026, 11, 1}, Tipo::Gasto, "SUPERMERCADO", 150000, "café"),   // ya está (sin importar mayúsculas/tildes)
            fila({2026, 11, 1}, Tipo::Gasto, "Supermercado", 150000, "Café"),   // un segundo café igual: es nuevo
            fila({2026, 11, 2}, Tipo::Gasto, "Farmacia", 800000),               // categoría nueva
            fila({2026, 11, 3}, Tipo::Gasto, "farmacia", 100000),               // la misma categoría nueva
            fila({2026, 11, 4}, Tipo::Ingreso, "", 5000000),                    // sin categoría -> Ingresos Varios
        };
        const ResultadoImportacion vista = c.previsualizarImportacion(filas);
        QCOMPARE(vista.nuevos, 4);
        QCOMPARE(vista.repetidos, 1);
        QCOMPARE(vista.categoriasNuevas, QStringList{"Gasto: Farmacia"});
        QCOMPARE(c.movimientos().size(), 1);                                    // la vista previa no guarda nada
        QVERIFY(!c.buscarCategoria("Farmacia", Tipo::Gasto));

        const ResultadoImportacion r = c.importar(filas);
        QCOMPARE(r.nuevos, 4);
        QCOMPARE(r.repetidos, 1);
        QCOMPARE(c.movimientos().size(), 5);
        QCOMPARE(c.buscarCategoria("Farmacia", Tipo::Gasto)->movimientos, 2);
        QCOMPARE(c.buscarCategoria("Ingresos Varios", Tipo::Ingreso)->movimientos, 1);

        const ResultadoImportacion otraVez = c.importar(filas);                 // importar dos veces no duplica
        QCOMPARE(otraVez.nuevos, 0);
        QCOMPARE(otraVez.repetidos, 5);
        QCOMPARE(c.movimientos().size(), 5);
    }

    void exportarEImportarEnOtraBaseDaLoMismo()
    {
        Cuentas a(":memory:");
        a.crearCategoria("Mascotas", Tipo::Gasto);
        a.crearMovimiento(mov(a, {2026, 11, 1}, "Mascotas", Tipo::Gasto, 1234567, "Alimento; balanceado"));
        a.crearMovimiento(mov(a, {2026, 11, 5}, "Sueldo", Tipo::Ingreso, 90000000, "Noviembre"));
        const LecturaCsv l = leerCsv(movimientosACsv(a.movimientos()));
        Cuentas b(":memory:");
        QCOMPARE(b.importar(l.filas).nuevos, 2);
        QCOMPARE(b.saldo({2026, 12, 31}), a.saldo({2026, 12, 31}));
        QCOMPARE(b.buscarCategoria("Mascotas", Tipo::Gasto)->movimientos, 1);
        QCOMPARE(b.movimientos().last().descripcion, QString("Alimento; balanceado"));
    }

    void seGuardaEnDiscoYBackup()
    {
        QTemporaryDir dir;
        {
            Cuentas c(dir.filePath("sub/cuentas.db"));
            QVERIFY(c.abierta());
            c.crearCategoria("Mascotas", Tipo::Gasto);
            c.crearMovimiento(mov(c, {2026, 11, 1}, "Mascotas", Tipo::Gasto, 5000));
            c.fijarSaldoInicial(777);
            QVERIFY(c.backup(dir.filePath("copias/copia.db")).ok);
        }
        Cuentas otra(dir.filePath("sub/cuentas.db"));
        QCOMPARE(otra.movimientos().size(), 1);
        QCOMPARE(otra.saldoInicial(), Centavos(777));
        QCOMPARE(otra.categorias(Tipo::Gasto).size(), 9);              // no vuelve a crear las iniciales
        Cuentas copia(dir.filePath("copias/copia.db"));
        QCOMPARE(copia.movimientos().first().monto, Centavos(5000));
    }

    // ---- 2.1.0: cuentas, presupuestos y movimientos fijos

    void cuentasDeDinero()
    {
        Cuentas c(":memory:");
        const QList<CuentaDinero> inicio = c.cuentasDinero();
        QCOMPARE(inicio.size(), 1);
        QVERIFY(inicio.first().principal);
        QCOMPARE(inicio.first().nombre, QStringLiteral("Principal"));
        const qint64 principal = c.cuentaPrincipal();
        QCOMPARE(inicio.first().id, principal);

        const Resultado banco = c.crearCuentaDinero("Banco", 100000);
        QVERIFY(banco.ok);
        QVERIFY(!c.crearCuentaDinero("banco").ok);                 // mismo nombre
        QVERIFY(!c.crearCuentaDinero("  ").ok);

        Movimiento sueldo = mov(c, QDate::currentDate(), "Sueldo", Tipo::Ingreso, 500000);
        sueldo.cuentaId = banco.id;
        QVERIFY(c.crearMovimiento(sueldo).ok);
        QVERIFY(c.crearMovimiento(mov(c, QDate::currentDate(), "Supermercado", Tipo::Gasto, 30000)).ok);   // sin cuenta: principal
        c.fijarSaldoInicial(20000);                                // mirando todas: va a la principal

        QCOMPARE(c.saldo(), 100000 + 500000 + 20000 - 30000);
        c.verCuenta(banco.id);
        QCOMPARE(c.saldo(), 100000 + 500000);
        QCOMPARE(c.movimientos().size(), 1);
        QCOMPARE(c.movimientos().first().cuenta, QStringLiteral("Banco"));
        QCOMPARE(c.totales({}, {}).gastos, 0);
        c.verCuenta(principal);
        QCOMPARE(c.saldo(), 20000 - 30000);
        c.verCuenta(0);

        // Eliminar: la principal no; una con movimientos, pasándolos a otra
        QVERIFY(!c.eliminarCuentaDinero(principal).ok);
        QVERIFY(!c.eliminarCuentaDinero(banco.id).ok);
        QVERIFY(c.eliminarCuentaDinero(banco.id, principal).ok);
        QCOMPARE(c.cuentasDinero().size(), 1);
        QCOMPARE(c.movimientos().size(), 2);
        QCOMPARE(c.saldo(), 20000 + 500000 - 30000);               // el saldo inicial del banco ya no está
        QVERIFY(c.renombrarCuentaDinero(principal, "Efectivo").ok);
        QCOMPARE(c.cuentasDinero().first().nombre, QStringLiteral("Efectivo"));
    }

    void migraBaseDeLa2_0()
    {
        const QString ruta = QDir::temp().filePath(QStringLiteral("ig_v2_%1.db").arg(QCoreApplication::applicationPid()));
        QFile::remove(ruta);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "v2");
            db.setDatabaseName(ruta);
            QVERIFY(db.open());
            QSqlQuery q(db);
            for (const char* s : {
                     "CREATE TABLE config (clave TEXT PRIMARY KEY, valor TEXT NOT NULL, modificado INTEGER NOT NULL DEFAULT 0, autor TEXT NOT NULL DEFAULT '', cambio INTEGER NOT NULL DEFAULT 0)",
                     "CREATE TABLE categorias (id INTEGER PRIMARY KEY AUTOINCREMENT, uuid TEXT NOT NULL UNIQUE, nombre TEXT NOT NULL, tipo TEXT NOT NULL, color INTEGER NOT NULL DEFAULT 0, modificado INTEGER NOT NULL DEFAULT 0, autor TEXT NOT NULL DEFAULT '', cambio INTEGER NOT NULL DEFAULT 0, borrado INTEGER NOT NULL DEFAULT 0)",
                     "CREATE TABLE movimientos (id INTEGER PRIMARY KEY AUTOINCREMENT, uuid TEXT NOT NULL UNIQUE, fecha TEXT NOT NULL, categoria_id INTEGER NOT NULL REFERENCES categorias(id), monto INTEGER NOT NULL CHECK (monto > 0), descripcion TEXT NOT NULL DEFAULT '', modificado INTEGER NOT NULL DEFAULT 0, autor TEXT NOT NULL DEFAULT '', cambio INTEGER NOT NULL DEFAULT 0, borrado INTEGER NOT NULL DEFAULT 0)",
                     "CREATE TABLE pares (dispositivo TEXT PRIMARY KEY, nombre TEXT NOT NULL DEFAULT '', secreto TEXT NOT NULL, host TEXT NOT NULL DEFAULT '', puerto INTEGER NOT NULL DEFAULT 0, enviado INTEGER NOT NULL DEFAULT 0, recibido INTEGER NOT NULL DEFAULT 0, ultima_sinc TEXT NOT NULL DEFAULT '')",
                     "INSERT INTO config (clave, valor, modificado, autor) VALUES ('saldo_inicial', '150000', 1234, 'otro'), ('dispositivo', 'yo', 0, '')",
                     "INSERT INTO categorias (id, uuid, nombre, tipo) VALUES (1, 'c1', 'Supermercado', 'gasto'), (2, 'c2', 'Sueldo', 'ingreso')",
                     "INSERT INTO movimientos (uuid, fecha, categoria_id, monto) VALUES ('m1', '2026-10-01', 2, 900000), ('m2', '2026-10-02', 1, 40000)",
                     "PRAGMA user_version = 2"})
                QVERIFY2(q.exec(QString::fromLatin1(s)), qPrintable(q.lastError().text()));
        }
        QSqlDatabase::removeDatabase("v2");
        Cuentas c(ruta);
        QVERIFY2(c.abierta(), qPrintable(c.errorApertura()));
        QCOMPARE(c.cuentasDinero().size(), 1);
        QCOMPARE(c.cuentasDinero().first().saldoInicial, 150000);
        QCOMPARE(c.saldo(QDate(2026, 12, 31)), 150000 + 900000 - 40000);
        for (const Movimiento& m : c.movimientos()) QCOMPARE(m.cuentaId, c.cuentaPrincipal());
        QCOMPARE(c.categorias(Tipo::Gasto).first().presupuesto, 0);
        QFile::remove(ruta);
    }

    void presupuestos()
    {
        Cuentas c(":memory:");
        const qint64 super = idDe(c, "Supermercado", Tipo::Gasto);
        QVERIFY(c.fijarPresupuesto(super, 100000).ok);
        QVERIFY(!c.fijarPresupuesto(idDe(c, "Sueldo", Tipo::Ingreso), 1000).ok);   // solo gastos
        QVERIFY(!c.fijarPresupuesto(super, -5).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2026, 11, 3}, "Supermercado", Tipo::Gasto, 60000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2026, 11, 20}, "Supermercado", Tipo::Gasto, 45000)).ok);
        QVERIFY(c.crearMovimiento(mov(c, {2026, 10, 20}, "Supermercado", Tipo::Gasto, 99999)).ok);   // otro mes
        const QList<EstadoPresupuesto> p = c.presupuestos(2026, 11);
        QCOMPARE(p.size(), 1);
        QCOMPARE(p.first().gastado, 105000);
        QCOMPARE(p.first().porcentaje(), 105);
        QCOMPARE(c.presupuestos(2026, 9).first().gastado, 0);
        QVERIFY(c.fijarPresupuesto(super, 0).ok);
        QVERIFY(c.presupuestos(2026, 11).isEmpty());
    }

    void movimientosFijos()
    {
        Cuentas c(":memory:");
        Recurrente alquiler;
        alquiler.categoriaId = idDe(c, "Servicios", Tipo::Gasto);
        alquiler.monto = 250000;
        alquiler.descripcion = "Alquiler";
        alquiler.dia = 31;
        alquiler.desde = QDate(2026, 1, 10);
        const Resultado r = c.guardarRecurrente(alquiler);
        QVERIFY2(r.ok, qPrintable(r.error));
        alquiler.id = r.id;

        QCOMPARE(c.generarRecurrentes(QDate(2026, 3, 15)), 2);     // 31/1 y 28/2 (marzo todavía no)
        QCOMPARE(c.generarRecurrentes(QDate(2026, 3, 15)), 0);     // no duplica
        QStringList fechas;
        for (const Movimiento& m : c.movimientos()) fechas << m.fecha.toString(Qt::ISODate);
        QCOMPARE(fechas, (QStringList{"2026-02-28", "2026-01-31"}));

        // Borrar uno generado: no vuelve
        QVERIFY(c.eliminarMovimiento(c.movimientos().first().id).ok);
        QCOMPARE(c.generarRecurrentes(QDate(2026, 3, 31)), 1);     // solo marzo
        QCOMPARE(c.movimientos().size(), 2);

        // Con fecha de fin y pausado
        alquiler.hasta = QDate(2026, 4, 30);
        QVERIFY(c.guardarRecurrente(alquiler).ok);
        QCOMPARE(c.generarRecurrentes(QDate(2026, 8, 1)), 1);      // abril y nada más
        alquiler.activo = false;
        alquiler.hasta = QDate();
        QVERIFY(c.guardarRecurrente(alquiler).ok);
        QCOMPARE(c.generarRecurrentes(QDate(2026, 12, 31)), 0);

        QCOMPARE(c.recurrentes().size(), 1);
        QVERIFY(c.eliminarRecurrente(alquiler.id).ok);
        QVERIFY(c.recurrentes().isEmpty());
        QCOMPARE(c.movimientos().size(), 3);                       // los cargados quedan

        Recurrente malo;
        malo.categoriaId = idDe(c, "Sueldo", Tipo::Ingreso);
        malo.monto = 10;
        malo.dia = 32;
        malo.desde = QDate(2026, 1, 1);
        QVERIFY(!c.guardarRecurrente(malo).ok);
    }
};

QTEST_MAIN(TstDb)
#include "tst_db.moc"
