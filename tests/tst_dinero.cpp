#include "dinero.h"
#include <QtTest>

class TstDinero : public QObject
{
    Q_OBJECT
private slots:
    void formatear()
    {
        QCOMPARE(formatearPesos(0), QString("$ 0,00"));
        QCOMPARE(formatearPesos(5), QString("$ 0,05"));
        QCOMPARE(formatearPesos(123456), QString("$ 1.234,56"));
        QCOMPARE(formatearPesos(123456789012), QString("$ 1.234.567.890,12"));
        QCOMPARE(formatearPesos(-5000), QString("-$ 50,00"));
        QCOMPARE(formatearPesosConSigno(150000), QString("+$ 1.500,00"));
        QCOMPARE(formatearPesosConSigno(-150000), QString("-$ 1.500,00"));
        QCOMPARE(formatearPesosConSigno(0), QString("$ 0,00"));
        QCOMPARE(formatearPesos(std::numeric_limits<Centavos>::min()).left(3), QString("-$ "));   // no desborda
    }

    void formatearCorto()
    {
        QCOMPARE(formatearPesosCorto(0), QString("$ 0"));
        QCOMPARE(formatearPesosCorto(80000), QString("$ 800"));
        QCOMPARE(formatearPesosCorto(950000), QString("$ 9.500"));
        QCOMPARE(formatearPesosCorto(1500000), QString("$ 15 mil"));
        QCOMPARE(formatearPesosCorto(25050000), QString("$ 250,5 mil"));
        QCOMPARE(formatearPesosCorto(150000000), QString("$ 1,5 M"));
        QCOMPARE(formatearPesosCorto(200000000), QString("$ 2 M"));
        QCOMPARE(formatearPesosCorto(-150000000), QString("-$ 1,5 M"));
    }

    void numeroParaCsv()
    {
        QCOMPARE(formatearNumero(123456), QString("1234,56"));
        QCOMPARE(formatearNumero(-5), QString("-0,05"));
    }

    void parsear_data()
    {
        QTest::addColumn<QString>("texto");
        QTest::addColumn<bool>("valido");
        QTest::addColumn<qint64>("centavos");
        QTest::newRow("entero") << "1234" << true << qint64(123400);
        QTest::newRow("miles") << "1.234" << true << qint64(123400);
        QTest::newRow("millones") << "1.234.567" << true << qint64(123456700);
        QTest::newRow("argentino") << "1.234,56" << true << qint64(123456);
        QTest::newRow("coma un decimal") << "1234,5" << true << qint64(123450);
        QTest::newRow("con signo $") << "$ 1.234,56" << true << qint64(123456);
        QTest::newRow("punto decimal") << "1234.56" << true << qint64(123456);
        QTest::newRow("solo centavos") << ",50" << true << qint64(50);
        QTest::newRow("cero") << "0" << true << qint64(0);
        QTest::newRow("más") << "+500" << true << qint64(50000);
        QTest::newRow("vacío") << "" << false << qint64(0);
        QTest::newRow("texto") << "mil" << false << qint64(0);
        QTest::newRow("tres decimales") << "12,345" << false << qint64(0);
        QTest::newRow("negativo sin permiso") << "-5" << false << qint64(0);
        QTest::newRow("formato inglés") << "1,234.56" << false << qint64(0);
        QTest::newRow("dos comas") << "1,2,3" << false << qint64(0);
        QTest::newRow("gigante") << "99999999999999999999" << false << qint64(0);
    }
    void parsear()
    {
        QFETCH(QString, texto);
        QFETCH(bool, valido);
        QFETCH(qint64, centavos);
        Centavos c = -1;
        QCOMPARE(parsearPesos(texto, &c), valido);
        if (valido) QCOMPARE(c, centavos);
    }

    void parsearConSigno()
    {
        Centavos c = 0;
        QVERIFY(parsearPesos("-1.234,56", &c, true));
        QCOMPARE(c, Centavos(-123456));
        QVERIFY(parsearPesos("-$ 50", &c, true));
        QCOMPARE(c, Centavos(-5000));
    }

    void sumasExactas()
    {
        // con double, 0,10 + 0,20 no da 0,30; con centavos sí
        Centavos a = 0, b = 0, c = 0;
        QVERIFY(parsearPesos("0,10", &a) && parsearPesos("0,20", &b) && parsearPesos("0,30", &c));
        QCOMPARE(a + b, c);
    }

    void fechas()
    {
        QDate d;
        QVERIFY(parsearFecha("01/11/2026", &d)); QCOMPARE(d, QDate(2026, 11, 1));
        QVERIFY(parsearFecha("1/2/2026", &d));   QCOMPARE(d, QDate(2026, 2, 1));
        QVERIFY(parsearFecha("2026-11-01", &d)); QCOMPARE(d, QDate(2026, 11, 1));
        QVERIFY(parsearFecha("31-12-2025", &d)); QCOMPARE(d, QDate(2025, 12, 31));
        QVERIFY(parsearFecha("31/01/26", &d));   QCOMPARE(d, QDate(2026, 1, 31));   // año de 2 cifras
        QVERIFY(!parsearFecha("31/02/2026", &d));
        QVERIFY(!parsearFecha("ayer", &d));
        QVERIFY(!parsearFecha("", &d));
    }

    void meses()
    {
        QCOMPARE(nombreMes(1), QString("Enero"));
        QCOMPARE(nombreMes(9), QString("Septiembre"));
        QCOMPARE(nombreMesCorto(12), QString("Dic"));
        QCOMPARE(nombreMes(13), QString());
    }

    void normalizarTexto()
    {
        QCOMPARE(normalizar("Teléfono"), QString("telefono"));
        QCOMPARE(normalizar("AÑO"), QString("ano"));
    }
};

QTEST_MAIN(TstDinero)
#include "tst_dinero.moc"
