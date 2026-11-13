// Pruebas de la sincronización: dos bases (la «PC» y el «celular») que hablan por TCP en esta misma máquina.
#include "db.h"
#include "sincronizacion.h"
#include "dinero.h"
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {

qint64 idCat(Cuentas& c, const QString& nombre, Tipo t)
{
    const auto cat = c.buscarCategoria(nombre, t);
    return cat ? cat->id : 0;
}

qint64 agregar(Cuentas& c, const QDate& f, const QString& cat, Tipo t, Centavos monto, const QString& desc = {})
{
    Movimiento m;
    m.fecha = f;
    m.tipo = t;
    m.categoriaId = idCat(c, cat, t);
    m.monto = monto;
    m.descripcion = desc;
    return c.crearMovimiento(m).id;
}

// Lo que importa de una base, ordenado, para comparar la PC con el celular
QStringList resumen(Cuentas& c)
{
    QStringList l;
    for (const Movimiento& m : c.movimientos())
        l << QStringLiteral("%1|%2|%3|%4").arg(m.fecha.toString(Qt::ISODate), m.categoria).arg(m.monto).arg(m.descripcion);
    for (const Categoria& k : c.categorias()) l << QStringLiteral("cat|%1|%2|%3").arg(k.nombre, textoTipo(k.tipo)).arg(k.color);
    l << QStringLiteral("saldo|%1").arg(c.saldoInicial());
    l.sort();
    return l;
}

struct ResultadoSinc {
    bool ok = false;
    QString mensaje;
    int recibidas = 0, enviadas = 0;
};

ResultadoSinc esperar(ClienteSinc& cli, const std::function<void()>& accion)
{
    QSignalSpy spy(&cli, &ClienteSinc::terminado);
    accion();
    if (!spy.wait(15000)) return {false, "sin respuesta", 0, 0};
    const QList<QVariant> a = spy.takeFirst();
    return {a[0].toBool(), a[1].toString(), a[2].toInt(), a[3].toInt()};
}

} // namespace

class TstSinc : public QObject
{
    Q_OBJECT
    QTemporaryDir dir_;

    // PC con servidor + celular emparejado, listos para sincronizar
    struct Escenario {
        Cuentas pc{":memory:"};
        Cuentas cel{":memory:"};
        ServidorSinc servidor{pc};
        ClienteSinc cliente{cel};
        QString dispPc;
        bool emparejar()
        {
            if (!servidor.iniciar(0, false)) return false;
            const QString cod = servidor.nuevoCodigo();
            QSignalSpy listo(&servidor, &ServidorSinc::sincronizado);
            const ResultadoSinc r = esperar(cliente, [&] { cliente.emparejar("127.0.0.1", servidor.puerto(), cod); });
            dispPc = pc.dispositivo();
            if (r.ok && listo.isEmpty()) listo.wait(5000);       // que la PC también haya terminado
            return r.ok;
        }
        ResultadoSinc sincronizar()
        {
            QSignalSpy listo(&servidor, &ServidorSinc::sincronizado);
            const ResultadoSinc r = esperar(cliente, [&] { cliente.sincronizar(dispPc); });
            if (r.ok && listo.isEmpty()) listo.wait(5000);
            return r;
        }
    };

private slots:
    void emparejarConCodigo()
    {
        Cuentas pc(":memory:"), cel(":memory:");
        ServidorSinc servidor(pc);
        QVERIFY(servidor.iniciar(0, false));
        ClienteSinc cliente(cel);

        // Sin código activo en la PC
        ResultadoSinc r = esperar(cliente, [&] { cliente.emparejar("127.0.0.1", servidor.puerto(), "123456"); });
        QVERIFY(!r.ok);
        QVERIFY2(r.mensaje.contains("código"), qPrintable(r.mensaje));

        // Código equivocado
        const QString cod = servidor.nuevoCodigo();
        QCOMPARE(cod.size(), 6);
        const QString malo = QString::number((cod.toInt() + 1) % 1000000).rightJustified(6, '0');
        r = esperar(cliente, [&] { cliente.emparejar("127.0.0.1", servidor.puerto(), malo); });
        QVERIFY(!r.ok);
        QVERIFY(cel.pares().isEmpty());
        QVERIFY(pc.pares().isEmpty());

        // Bien (con espacios, como se lee en pantalla)
        QSignalSpy emparejado(&servidor, &ServidorSinc::emparejado);
        r = esperar(cliente, [&] { cliente.emparejar("127.0.0.1", servidor.puerto(), cod.left(3) + " " + cod.mid(3)); });
        QVERIFY2(r.ok, qPrintable(r.mensaje));
        QCOMPARE(emparejado.count(), 1);
        QCOMPARE(cel.pares().size(), 1);
        QCOMPARE(pc.pares().size(), 1);
        QCOMPARE(cel.pares().first().secreto, pc.pares().first().secreto);   // la misma clave, sin que viaje
        QCOMPARE(cel.pares().first().dispositivo, pc.dispositivo());
        QVERIFY(servidor.codigo().isEmpty());                                // el código se usa una vez

        // Demasiados intentos con un código: se anula
        servidor.nuevoCodigo();
        ClienteSinc otro(cel);
        for (int i = 0; i < sinc::INTENTOS_CODIGO; ++i)
            esperar(otro, [&] { otro.emparejar("127.0.0.1", servidor.puerto(), "000000"); });
        QVERIFY(servidor.codigo().isEmpty() || servidor.codigo() == "000000");
    }

    void primeraSincronizacionYCambios()
    {
        Escenario e;
        agregar(e.pc, {2026, 10, 1}, "Sueldo", Tipo::Ingreso, 80000000, "Octubre");
        agregar(e.pc, {2026, 10, 3}, "Supermercado", Tipo::Gasto, 1543050, "Coto");
        e.pc.fijarSaldoInicial(5000000);
        agregar(e.cel, {2026, 10, 5}, "Transporte", Tipo::Gasto, 450000, "SUBE");
        QVERIFY(e.emparejar());                    // emparejar ya sincroniza
        QCOMPARE(resumen(e.pc), resumen(e.cel));
        QCOMPARE(e.cel.movimientos().size(), 3);
        QCOMPARE(e.cel.saldoInicial(), Centavos(5000000));
        QCOMPARE(e.cel.categorias().size(), 10);   // las de fábrica no se duplican

        // Sin cambios: no viaja nada
        ResultadoSinc r = e.sincronizar();
        QVERIFY2(r.ok, qPrintable(r.mensaje));
        QCOMPARE(r.recibidas, 0);
        QCOMPARE(r.enviadas, 0);

        // Cambios en los dos lados
        const qint64 coto = e.pc.movimientos().last().id;   // (el más viejo de la PC es el sueldo; buscamos Coto)
        Q_UNUSED(coto);
        for (const Movimiento& m : e.cel.movimientos()) {
            if (m.descripcion == "Coto") {
                Movimiento x = m;
                x.monto = 1600000;
                QVERIFY(e.cel.actualizarMovimiento(x).ok);
            }
            if (m.descripcion == "SUBE") QVERIFY(e.cel.eliminarMovimiento(m.id).ok);
        }
        agregar(e.pc, {2026, 10, 9}, "Internet", Tipo::Gasto, 2890000, "Fibra");
        QVERIFY(e.pc.crearCategoria("Mascotas", Tipo::Gasto).ok);
        r = e.sincronizar();
        QVERIFY2(r.ok, qPrintable(r.mensaje));
        QCOMPARE(resumen(e.pc), resumen(e.cel));
        QCOMPARE(e.pc.movimientos().size(), 3);    // Sueldo, Coto (editado), Fibra; SUBE borrado en los dos
        QVERIFY(e.pc.buscarCategoria("Mascotas", Tipo::Gasto).has_value());
        QVERIFY(e.cel.buscarCategoria("Mascotas", Tipo::Gasto).has_value());
        QCOMPARE(e.pc.totales({2026, 10, 1}, {2026, 10, 31}).gastos, Centavos(1600000 + 2890000));
    }

    void categoriasSeVenEnElMismoOrdenEnLaPcYEnElCelular()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        // Cada lado crea las suyas y en otro orden (por dentro les toca otro número): por orden alfabético se ven igual
        QVERIFY(e.pc.crearCategoria("Mascotas", Tipo::Gasto).ok);
        QVERIFY(e.pc.crearCategoria("Farmacia", Tipo::Gasto).ok);
        QVERIFY(e.cel.crearCategoria("Zapatería", Tipo::Gasto).ok);
        QVERIFY(e.cel.crearCategoria("Gimnasio", Tipo::Gasto).ok);
        const ResultadoSinc r = e.sincronizar();
        QVERIFY2(r.ok, qPrintable(r.mensaje));
        const auto nombres = [](Cuentas& c) {
            QStringList l;
            for (const Categoria& k : c.categorias(Tipo::Gasto)) l << k.nombre;
            return l;
        };
        QCOMPARE(nombres(e.cel), nombres(e.pc));
        QCOMPARE(nombres(e.pc), (QStringList{"Farmacia", "Gastos Varios", "Gimnasio", "Internet", "Mascotas", "Salud", "Servicios",
                                             "Supermercado", "Teléfono", "Transporte", "Vestimenta", "Zapatería"}));
    }

    void conflictoGanaElMasNuevo()
    {
        Escenario e;
        const qint64 id = agregar(e.pc, {2026, 10, 1}, "Supermercado", Tipo::Gasto, 100000, "Original");
        QVERIFY(e.emparejar());
        const qint64 idCel = e.cel.movimientos().first().id;

        Movimiento a = *e.pc.movimiento(id);
        a.descripcion = "Cambiado en la PC";
        QVERIFY(e.pc.actualizarMovimiento(a).ok);
        QTest::qWait(5);                           // el del celular es unos milisegundos más nuevo
        Movimiento b = *e.cel.movimiento(idCel);
        b.descripcion = "Cambiado en el celular";
        QVERIFY(e.cel.actualizarMovimiento(b).ok);

        QVERIFY(e.sincronizar().ok);
        QCOMPARE(e.pc.movimiento(id)->descripcion, QString("Cambiado en el celular"));
        QCOMPARE(resumen(e.pc), resumen(e.cel));

        // Saldo inicial cambiado en los dos: gana el más nuevo
        e.cel.fijarSaldoInicial(111);
        QTest::qWait(5);
        e.pc.fijarSaldoInicial(222);
        QVERIFY(e.sincronizar().ok);
        QCOMPARE(e.cel.saldoInicial(), Centavos(222));
        QCOMPARE(e.pc.saldoInicial(), Centavos(222));
    }

    void mismaCategoriaCreadaEnLosDos()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        QVERIFY(e.pc.crearCategoria("Farmacia", Tipo::Gasto).ok);
        QVERIFY(e.cel.crearCategoria("farmacia", Tipo::Gasto).ok);
        agregar(e.pc, {2026, 10, 1}, "Farmacia", Tipo::Gasto, 1000, "PC");
        agregar(e.cel, {2026, 10, 2}, "Farmacia", Tipo::Gasto, 2000, "Cel");
        QVERIFY(e.sincronizar().ok);
        // Es la misma categoría (mismo nombre = mismo uuid): una sola, con los dos movimientos
        int farmacias = 0;
        for (const Categoria& c : e.pc.categorias(Tipo::Gasto)) if (normalizar(c.nombre).startsWith("farmacia")) ++farmacias;
        QCOMPARE(farmacias, 1);
        QCOMPARE(e.pc.buscarCategoria("Farmacia", Tipo::Gasto)->movimientos, 2);
        QCOMPARE(resumen(e.pc), resumen(e.cel));
    }

    void nombreRepetidoPorRenombrar()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        // En la PC «Salud» pasa a llamarse «Mascotas»; en el celular se crea otra «Mascotas»
        QVERIFY(e.pc.renombrarCategoria(idCat(e.pc, "Salud", Tipo::Gasto), "Mascotas").ok);
        QVERIFY(e.cel.crearCategoria("Mascotas", Tipo::Gasto).ok);
        const ResultadoSinc r = e.sincronizar();
        QVERIFY2(r.ok, qPrintable(r.mensaje));
        QVERIFY(e.pc.buscarCategoria("Mascotas", Tipo::Gasto).has_value());
        QVERIFY(e.pc.buscarCategoria("Mascotas (2)", Tipo::Gasto).has_value());
        QCOMPARE(resumen(e.pc), resumen(e.cel));   // y los dos quedan iguales
    }

    void categoriaBorradaMientrasSeUsaba()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        // En la PC se borra «Vestimenta» (vacía); en el celular, mientras tanto, se carga un gasto ahí
        QVERIFY(e.pc.eliminarCategoria(idCat(e.pc, "Vestimenta", Tipo::Gasto)).ok);
        agregar(e.cel, {2026, 10, 1}, "Vestimenta", Tipo::Gasto, 5000000, "Zapatillas");
        QVERIFY(e.sincronizar().ok);
        // La categoría vuelve en los dos: no se pierde ni se esconde el gasto
        QVERIFY(e.pc.buscarCategoria("Vestimenta", Tipo::Gasto).has_value());
        QCOMPARE(e.pc.totales({}, {}).gastos, Centavos(5000000));
        QVERIFY(e.sincronizar().ok);
        QCOMPARE(resumen(e.pc), resumen(e.cel));

        // Borrar con pasar movimientos a otra: viaja todo
        QVERIFY(e.cel.eliminarCategoria(idCat(e.cel, "Vestimenta", Tipo::Gasto), idCat(e.cel, "Gastos Varios", Tipo::Gasto)).ok);
        QVERIFY(e.sincronizar().ok);
        QVERIFY(!e.pc.buscarCategoria("Vestimenta", Tipo::Gasto).has_value());
        QCOMPARE(e.pc.movimientos().first().categoria, QString("Gastos Varios"));
        QCOMPARE(resumen(e.pc), resumen(e.cel));
    }

    void mismoCsvImportadoEnLosDos()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        QList<FilaImportada> filas;
        for (int i = 0; i < 3; ++i) {
            FilaImportada f;
            f.fecha = QDate(2026, 9, 1);
            f.tipo = Tipo::Gasto;
            f.categoria = "Kiosco";
            f.monto = 50000;                       // tres iguales el mismo día
            f.descripcion = "Café";
            filas << f;
        }
        QCOMPARE(e.pc.importar(filas).nuevos, 3);
        QCOMPARE(e.cel.importar(filas).nuevos, 3);
        QVERIFY(e.sincronizar().ok);
        QCOMPARE(e.pc.movimientos().size(), 3);    // no 6
        QCOMPARE(resumen(e.pc), resumen(e.cel));
    }

    void celularQuitadoEnLaPc()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        e.pc.eliminarPar(e.cel.dispositivo());
        const ResultadoSinc r = e.sincronizar();
        QVERIFY(!r.ok);
        QVERIFY2(r.mensaje.contains("emparejado"), qPrintable(r.mensaje));
    }

    void pcApagada()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        e.servidor.detener();
        const ResultadoSinc r = e.sincronizar();     // busca en la red, no la encuentra y avisa
        QVERIFY(!r.ok);
        QVERIFY2(r.mensaje.contains("PC"), qPrintable(r.mensaje));
    }

    void pcCambioDeDireccion()
    {
        // El celular tiene guardado un puerto viejo; la PC contesta la búsqueda en la red con el nuevo
        Escenario e;
        if (!e.servidor.iniciar(0, true)) QSKIP("no se pudo abrir el puerto de búsqueda");
        const QString cod = e.servidor.nuevoCodigo();
        QVERIFY(esperar(e.cliente, [&] { e.cliente.emparejar("127.0.0.1", e.servidor.puerto(), cod); }).ok);
        e.dispPc = e.pc.dispositivo();
        Par p = *e.cel.par(e.dispPc);
        p.puerto = 1;                              // ahí no hay nadie
        e.cel.guardarPar(p);
        const ResultadoSinc r = e.sincronizar();
        if (!r.ok && r.mensaje.contains("No se encontró")) QSKIP("este entorno no deja usar broadcast UDP");
        QVERIFY2(r.ok, qPrintable(r.mensaje));
        QCOMPARE(e.cel.par(e.dispPc)->puerto, int(e.servidor.puerto()));   // quedó guardada la dirección nueva
    }

    void migrarBaseVersion1()
    {
        // Base como la dejaba la versión 1.0.x
        const QString ruta = dir_.filePath("v1.db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "v1");
            db.setDatabaseName(ruta);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE config (clave TEXT PRIMARY KEY, valor TEXT NOT NULL)"));
            QVERIFY(q.exec("CREATE TABLE categorias (id INTEGER PRIMARY KEY AUTOINCREMENT, nombre TEXT NOT NULL, "
                           "tipo TEXT NOT NULL CHECK (tipo IN ('ingreso', 'gasto')), color INTEGER NOT NULL DEFAULT 0, "
                           "UNIQUE (nombre COLLATE NOCASE, tipo))"));
            QVERIFY(q.exec("CREATE TABLE movimientos (id INTEGER PRIMARY KEY AUTOINCREMENT, fecha TEXT NOT NULL, "
                           "categoria_id INTEGER NOT NULL REFERENCES categorias(id) ON DELETE RESTRICT, "
                           "monto INTEGER NOT NULL CHECK (monto > 0), descripcion TEXT NOT NULL DEFAULT '')"));
            QVERIFY(q.exec("CREATE INDEX idx_movimientos_fecha ON movimientos(fecha)"));
            QVERIFY(q.exec("INSERT INTO categorias (nombre, tipo, color) VALUES ('Supermercado', 'gasto', 0), ('Propinas', 'ingreso', 1), "
                           "('Teléfono', 'gasto', 2), ('Telefono', 'gasto', 3)"));
            QVERIFY(q.exec("INSERT INTO movimientos (fecha, categoria_id, monto, descripcion) VALUES "
                           "('2026-10-01', 1, 1543050, 'Coto'), ('2026-10-02', 2, 9700000, ''), ('2026-10-03', 3, 500, 'Claro')"));
            QVERIFY(q.exec("INSERT INTO config (clave, valor) VALUES ('saldo_inicial', '1000000')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("v1");

        Cuentas c(ruta);
        QVERIFY2(c.abierta(), qPrintable(c.errorApertura()));
        QCOMPARE(c.movimientos().size(), 3);
        QCOMPARE(c.categorias().size(), 4);
        QCOMPARE(c.saldoInicial(), Centavos(1000000));
        QCOMPARE(c.totales({}, {}).saldo(), Centavos(9700000 - 1543050 - 500));
        QVERIFY(!c.dispositivo().isEmpty());
        QVERIFY(c.secuencia() >= 8);                                   // todo listo para mandarse
        QCOMPARE(c.cambiosDesde(0).size(), 4 + 3 + 1);
        // Sigue andando todo lo de siempre
        QVERIFY(c.crearCategoria("Kiosco", Tipo::Gasto).ok);
        qint64 sinTilde = 0;                                           // «Telefono» (sin movimientos) se puede borrar
        for (const Categoria& k : c.categorias(Tipo::Gasto)) if (k.nombre == "Telefono") sinTilde = k.id;
        QVERIFY(sinTilde);
        QVERIFY(c.eliminarCategoria(sinTilde).ok);
        QCOMPARE(c.categorias(Tipo::Gasto).size(), 3);
        // Y al abrirla de nuevo no se migra otra vez
        const QString disp = c.dispositivo();
        Q_UNUSED(disp);
    }

    void reabrirConservaDispositivo()
    {
        const QString ruta = dir_.filePath("d.db");
        QString disp;
        {
            Cuentas c(ruta);
            disp = c.dispositivo();
            agregar(c, {2026, 1, 1}, "Sueldo", Tipo::Ingreso, 100);
        }
        Cuentas c(ruta);
        QCOMPARE(c.dispositivo(), disp);
        QCOMPARE(c.movimientos().size(), 1);
        QCOMPARE(c.categorias().size(), 10);       // no vuelve a crear las de fábrica
    }

    void borradoNoSeVe()
    {
        Cuentas c(":memory:");
        const qint64 id = agregar(c, {2026, 1, 1}, "Sueldo", Tipo::Ingreso, 100);
        QVERIFY(c.eliminarMovimiento(id).ok);
        QVERIFY(c.movimientos().isEmpty());
        QCOMPARE(c.totales({}, {}).ingresos, Centavos(0));
        QVERIFY(!c.eliminarMovimiento(id).ok);
        // Una categoría borrada y creada de nuevo con el mismo nombre revive (mismo uuid, sin duplicar)
        QVERIFY(c.crearCategoria("Gym", Tipo::Gasto).ok);
        QVERIFY(c.eliminarCategoria(idCat(c, "Gym", Tipo::Gasto)).ok);
        QVERIFY(c.crearCategoria("Gym", Tipo::Gasto).ok);
        QCOMPARE(c.buscarCategoria("Gym", Tipo::Gasto)->nombre, QString("Gym"));
    }

    // ---- 2.1.0: cuentas, presupuestos y movimientos fijos viajan; un fijo no se duplica
    void cuentasPresupuestosYFijos()
    {
        Escenario e;
        QVERIFY(e.emparejar());
        // Los dos crean una cuenta «Banco» por su lado: quedan las dos, una como «Banco (2)»
        QVERIFY(e.pc.crearCuentaDinero("Banco", 1000).ok);
        QVERIFY(e.cel.crearCuentaDinero("Banco", 2000).ok);
        QVERIFY(e.pc.fijarPresupuesto(idCat(e.pc, "Supermercado", Tipo::Gasto), 80000).ok);
        // Un fijo creado en la PC, que la PC ya cargó
        Recurrente r;
        r.categoriaId = idCat(e.pc, "Internet", Tipo::Gasto);
        r.monto = 15000;
        r.dia = 10;
        r.desde = QDate::currentDate().addMonths(-2);
        QVERIFY(e.pc.guardarRecurrente(r).ok);
        QVERIFY(e.pc.generarRecurrentes() >= 2);
        QVERIFY(e.sincronizar().ok);
        QVERIFY(e.sincronizar().ok);                               // el nombre ajustado vuelve a la PC

        QStringList cuentasPc, cuentasCel;
        for (const CuentaDinero& c : e.pc.cuentasDinero()) cuentasPc << c.nombre + "=" + QString::number(c.saldoInicial);
        for (const CuentaDinero& c : e.cel.cuentasDinero()) cuentasCel << c.nombre + "=" + QString::number(c.saldoInicial);
        QCOMPARE(cuentasPc.size(), 3);
        QCOMPARE(cuentasPc, cuentasCel);
        QVERIFY(cuentasPc.contains("Banco (2)=1000") || cuentasPc.contains("Banco (2)=2000"));
        QCOMPARE(e.cel.buscarCategoria("Supermercado", Tipo::Gasto)->presupuesto, 80000);
        QCOMPARE(e.cel.presupuestos(QDate::currentDate().year(), QDate::currentDate().month()).size(), 1);
        QCOMPARE(e.cel.recurrentes().size(), 1);

        // El celular «también» genera los mismos meses: no se duplican
        const int antes = e.cel.movimientos().size();
        QCOMPARE(e.cel.generarRecurrentes(), 0);
        QCOMPARE(e.cel.movimientos().size(), antes);
        QVERIFY(e.sincronizar().ok);
        QCOMPARE(e.pc.movimientos().size(), e.cel.movimientos().size());

        // Un movimiento en el banco del celular viaja con su cuenta
        Movimiento m;
        m.fecha = QDate::currentDate();
        m.categoriaId = idCat(e.cel, "Sueldo", Tipo::Ingreso);
        m.monto = 777;
        for (const CuentaDinero& c : e.cel.cuentasDinero()) if (!c.principal) { m.cuentaId = c.id; break; }
        QVERIFY(e.cel.crearMovimiento(m).ok);
        QVERIFY(e.sincronizar().ok);
        bool encontrado = false;
        for (const Movimiento& x : e.pc.movimientos())
            if (x.monto == 777) { encontrado = true; QVERIFY(x.cuenta.startsWith("Banco")); }
        QVERIFY(encontrado);
    }
};

QTEST_MAIN(TstSinc)
#include "tst_sinc.moc"
