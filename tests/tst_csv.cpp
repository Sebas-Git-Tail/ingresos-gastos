#include "csv.h"
#include <QtTest>

class TstCsv : public QObject
{
    Q_OBJECT
    static Movimiento mov(QDate f, Tipo t, QString cat, Centavos monto, QString desc)
    {
        Movimiento m; m.fecha = f; m.tipo = t; m.categoria = cat; m.monto = monto; m.descripcion = desc; return m;
    }
private slots:
    void exportar()
    {
        const QString csv = movimientosACsv({mov({2026, 11, 1}, Tipo::Gasto, "Supermercado", 1543050, "Compra; semanal \"grande\""),
                                             mov({2026, 10, 31}, Tipo::Ingreso, "Sueldo", 85000000, "")});
        const QStringList l = csv.split("\r\n", Qt::SkipEmptyParts);
        QCOMPARE(l.size(), 3);
        QCOMPARE(l[0], QString("Fecha;Tipo;Categoría;Monto;Descripción"));
        QCOMPARE(l[1], QString("01/11/2026;Gasto;Supermercado;15430,50;\"Compra; semanal \"\"grande\"\"\""));
        QCOMPARE(l[2], QString("31/10/2026;Ingreso;Sueldo;850000,00;"));
        QVERIFY(csvParaGuardar(csv).startsWith("\xEF\xBB\xBF"));
    }

    void idaYVuelta()
    {
        const QList<Movimiento> orig = {mov({2026, 11, 1}, Tipo::Gasto, "Supermercado", 1543050, "Compra; con \"comillas\"\ny salto"),
                                        mov({2026, 10, 31}, Tipo::Ingreso, "Sueldo", 85000000, "Octubre")};
        const LecturaCsv l = leerCsv(movimientosACsv(orig));
        QVERIFY(l.ok);
        QCOMPARE(l.filas.size(), 2);
        QCOMPARE(l.filas[0].fecha, QDate(2026, 11, 1));
        QVERIFY(l.filas[0].tipo == Tipo::Gasto);
        QCOMPARE(l.filas[0].categoria, QString("Supermercado"));
        QCOMPARE(l.filas[0].monto, Centavos(1543050));
        QCOMPARE(l.filas[0].descripcion, QString("Compra; con \"comillas\"\ny salto"));
        QVERIFY(l.filas[1].tipo == Tipo::Ingreso);
        QCOMPARE(l.filas[1].monto, Centavos(85000000));
        QVERIFY(l.avisos.isEmpty());
    }

    void otroOrdenOtrosNombresYComa()
    {
        // separador coma, columnas en otro orden y con otros nombres, montos con $ y miles
        const QString t = "Concepto,Importe,Rubro,Fecha,Tipo\n"
                          "Nafta,\"$ 25.000\",Transporte,2026-11-02,Egreso\n"
                          "Changas,\"1.500,50\",,03/11/2026,Ingreso\n";
        const LecturaCsv l = leerCsv(t);
        QVERIFY2(l.ok, qPrintable(l.error));
        QCOMPARE(l.filas.size(), 2);
        QCOMPARE(l.filas[0].descripcion, QString("Nafta"));
        QCOMPARE(l.filas[0].monto, Centavos(2500000));
        QCOMPARE(l.filas[0].categoria, QString("Transporte"));
        QVERIFY(l.filas[0].tipo == Tipo::Gasto);
        QCOMPARE(l.filas[1].monto, Centavos(150050));
        QVERIFY(l.filas[1].categoria.isEmpty());
        QVERIFY(l.filas[1].tipo == Tipo::Ingreso);
    }

    void sinColumnaTipoManda_elSigno()
    {
        const LecturaCsv l = leerCsv("Fecha;Monto;Detalle\n01/11/2026;-1200;Café\n02/11/2026;50000;Venta\n");
        QVERIFY(l.ok);
        QVERIFY(l.filas[0].tipo == Tipo::Gasto);
        QCOMPARE(l.filas[0].monto, Centavos(120000));                  // siempre positivo
        QVERIFY(l.filas[1].tipo == Tipo::Ingreso);
    }

    void tabulaciones()
    {
        const LecturaCsv l = leerCsv("Fecha\tTipo\tCategoría\tMonto\n01/11/2026\tGasto\tInternet\t15999,99\n");
        QVERIFY(l.ok);
        QCOMPARE(l.filas.size(), 1);
        QCOMPARE(l.filas[0].monto, Centavos(1599999));
    }

    void renglonesMalosSeAvisanYElRestoSeLee()
    {
        const QString t = "Fecha;Tipo;Categoría;Monto\n"
                          "01/11/2026;Gasto;Internet;100\n"
                          "31/02/2026;Gasto;Internet;100\n"     // fecha imposible
                          "01/11/2026;Gasto;Internet;mucho\n"   // monto
                          "01/11/2026;Préstamo;Internet;100\n"  // tipo
                          "01/11/2026;Gasto;Internet;0\n"       // monto cero
                          "\n"
                          "02/11/2026;Ingreso;Sueldo;1000\n";
        const LecturaCsv l = leerCsv(t);
        QVERIFY(l.ok);
        QCOMPARE(l.filas.size(), 2);
        QCOMPARE(l.avisos.size(), 4);
        QVERIFY(l.avisos[0].startsWith("Línea 3"));
    }

    void sinTitulosNecesarios()
    {
        QVERIFY(!leerCsv("").ok);
        const LecturaCsv l = leerCsv("Nombre;Apellido\nJuan;Pérez\n");
        QVERIFY(!l.ok);
        QVERIFY(l.error.contains("Fecha"));
    }

    void archivoDeExcelEnWindows1252()
    {
        // "Categoría" y "Teléfono" en Windows-1252 (Excel en Windows): í = 0xED, é = 0xE9
        const QByteArray datos("Fecha;Tipo;Categor\xED" "a;Monto\r\n01/11/2026;Gasto;Tel\xE9" "fono;8000\r\n");
        const QString texto = decodificarArchivo(datos);
        QVERIFY(texto.contains(QString("Teléfono")));
        const LecturaCsv l = leerCsv(texto);
        QVERIFY(l.ok);
        QCOMPARE(l.filas[0].categoria, QString("Teléfono"));
        QCOMPARE(decodificarArchivo(QByteArray("\xEF\xBB\xBF") + QString("Año").toUtf8()), QString("Año"));   // UTF-8 con BOM
    }
    void reporteDeExpenless()
    {
        // Como lo genera ExpenLess: títulos en inglés, sin comillas, comas y Enter dentro del texto
        const QString t = QString::fromUtf8(
            "Account Id,Account Description,Transaction Date,Creation date,Transaction Id,Transaction Name,"
            "Transaction Description,Currency Id,Currency symbol,Expense or Income,Amount,Category Id,Category Name,"
            "Subcategory Id,Subcategory Name\n"
            "1,1,31/10/2026,Sun Nov 01 12:40:11 GMT-03:00 2026,30,,,2,$,1,97000,22,Sueldo Juan ,74,Sueldo Juan  General\n"
            "1,1,23/10/2026,Sat Oct 10 16:22:00 GMT-03:00 2026,29,,,2,$,-1,11290.5,5,Domiciliarios,10,Gas\n"
            "1,1,21/9/2026,Mon Sep 21 20:11:52 GMT-03:00 2026,28,Cinto, medias.,Cinto, medias.,2,$,-1,13500,10,Vestimenta,32,Vestimenta \n"
            "1,1,16/6/2026,Tue Jun 16 14:57:10 GMT-03:00 2026,27,Un jogging\n"
            ",Un jogging\n"
            ",2,$,-1,22500,11,Cosas mías ,38,Ropa\n"
            "1,1,1/6/2026,Mon Jun 01 10:00:00 GMT-03:00 2026,26,,,3,US$,-1,50,9,Viajes,28,Viajes General\n"
            "1,1,31/8/2023,Fri Sep 01 11:50:41 GMT-03:00 2023,1,,,2,$,1,255856,2,Sin categoría,2,Sin categoría\n");
        const LecturaCsv l = leerCsv(t);
        QVERIFY2(l.ok, qPrintable(l.error));
        QCOMPARE(l.filas.size(), 5);
        QCOMPARE(l.avisos.size(), 1);                              // el de dólares no se mezcla con los pesos
        QVERIFY(l.avisos.first().contains("US$"));

        QCOMPARE(l.filas[0].tipo, Tipo::Ingreso);
        QCOMPARE(l.filas[0].categoria, QString("Sueldo Juan"));    // sin el espacio de más
        QCOMPARE(l.filas[0].monto, Centavos(9700000));
        QCOMPARE(l.filas[0].descripcion, QString());               // «... General» no se repite
        QCOMPARE(l.filas[1].tipo, Tipo::Gasto);
        QCOMPARE(l.filas[1].monto, Centavos(1129050));             // punto decimal
        QCOMPARE(l.filas[1].descripcion, QString("Gas"));          // subcategoría
        QCOMPARE(l.filas[2].fecha, QDate(2026, 9, 21));
        QCOMPARE(l.filas[2].descripcion, QString("Cinto, medias."));   // comas sin comillas, texto repetido
        QCOMPARE(l.filas[3].categoria, QString("Cosas mías"));     // movimiento partido en 3 líneas
        QCOMPARE(l.filas[3].descripcion, QString::fromUtf8("Ropa — Un jogging"));
        QCOMPARE(l.filas[3].monto, Centavos(2250000));
        QCOMPARE(l.filas[3].linea, 5);
        QCOMPARE(l.filas[4].categoria, QString());                 // «Sin categoría» va a «Ingresos Varios»
    }

    void titulosEnIngles()
    {
        const LecturaCsv l = leerCsv("Date,Type,Category,Amount,Note\n01/11/2026,Expense,Comida,150.5,Pizza\n02/11/2026,Income,Sueldo,1000,\n");
        QVERIFY2(l.ok, qPrintable(l.error));
        QCOMPARE(l.filas.size(), 2);
        QCOMPARE(l.filas[0].tipo, Tipo::Gasto);
        QCOMPARE(l.filas[0].monto, Centavos(15050));
        QCOMPARE(l.filas[1].tipo, Tipo::Ingreso);
    }
};

QTEST_MAIN(TstCsv)
#include "tst_csv.moc"
