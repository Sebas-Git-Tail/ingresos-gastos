// Pruebas de los gráficos: escala del eje, geometría de las barras, agrupado en «Otros» y porcentajes.
#include "graficos.h"
#include <QSignalSpy>
#include <QtTest>

namespace {
TotalCategoria tc(const QString& nombre, int color, Centavos total)
{
    TotalCategoria t;
    t.nombre = nombre;
    t.color = color;
    t.total = total;
    return t;
}
QStringList meses() { return {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"}; }
}

class TstGraficos : public QObject
{
    Q_OBJECT
private slots:
    void marcasDelEje()
    {
        QCOMPARE(marcasLindas(0), QList<double>{0});
        QCOMPARE(marcasLindas(100), (QList<double>{0, 25, 50, 75, 100}));
        QCOMPARE(marcasLindas(18000), (QList<double>{0, 5000, 10000, 15000, 20000}));
        const QList<double> m = marcasLindas(1234567);
        QVERIFY(m.last() >= 1234567);
        QVERIFY(m.size() >= 3 && m.size() <= 7);
    }

    void barrasGeometria()
    {
        GraficoBarras g;
        g.resize(800, 300);
        QVector<Centavos> ing(12), gas(12);
        ing[0] = 100000; gas[0] = 50000;   // enero: $1000 / $500
        ing[5] = 20000;
        g.setDatos("2026", meses(), meses(), ing, gas);

        const QRectF ri = g.rectBarra(0, true), rg = g.rectBarra(0, false);
        QVERIFY(ri.isValid() && rg.isValid());
        QVERIFY(ri.width() <= 24.0);                          // barras finitas
        QCOMPARE(ri.bottom(), rg.bottom());                   // misma base
        QVERIFY(qAbs(ri.height() - 2 * rg.height()) < 0.5);   // proporcionales al monto
        QVERIFY(rg.left() - ri.right() >= 1.9);               // separación entre la pareja
        QCOMPARE(g.rectBarra(1, true).height(), 0.0);         // sin datos: alto 0
        QVERIFY(!g.rectBarra(12, true).isValid());            // fuera de rango

        // Toda la columna del mes cuenta como blanco del mouse (también la etiqueta, abajo de todo)
        QCOMPARE(g.grupoEn(ri.center().toPoint()), 0);
        QCOMPARE(g.grupoEn(QPoint(int(g.rectBarra(5, true).center().x()), g.height() - 3)), 5);
        QCOMPARE(g.grupoEn(QPoint(2, 150)), -1);              // sobre el eje: nada
    }

    void barrasClicYMouse()
    {
        GraficoBarras g;
        g.resize(800, 300);
        QVector<Centavos> ing(12, 1000), gas(12, 500);
        g.setDatos("2026", meses(), meses(), ing, gas);
        g.show();
        QVERIFY(QTest::qWaitForWindowExposed(&g));
        QSignalSpy spy(&g, &GraficoBarras::grupoClicado);
        const QPoint p = g.rectBarra(3, true).center().toPoint();
        QTest::mouseMove(&g, p);
        QTRY_COMPARE(g.grupoResaltado(), 3);
        QTest::mouseClick(&g, Qt::LeftButton, {}, p);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 3);
    }

    void barrasVacias()
    {
        GraficoBarras g;
        g.resize(600, 300);
        g.setDatos("2027", meses(), meses(), QVector<Centavos>(12), QVector<Centavos>(12));
        QCOMPARE(g.grupoEn(QPoint(300, 150)), -1);
        QVERIFY(!g.rectBarra(0, true).isValid());
        QImage img(g.size(), QImage::Format_ARGB32);
        g.render(&img);                                       // dibujar vacío no debe fallar
    }

    void tortaPocasCategorias()
    {
        GraficoTorta t;
        t.resize(600, 300);
        t.setDatos("Gastos", {tc("Transporte", 1, 2500), tc("Supermercado", 0, 7500), tc("Nada", 2, 0)});
        QCOMPARE(t.porciones().size(), 2);                    // la de $0 no aparece
        QCOMPARE(t.porciones()[0].nombre, QString("Supermercado"));   // de mayor a menor
        QCOMPARE(t.textoPorcentaje(0), QString("75,0 %"));
        QCOMPARE(t.textoPorcentaje(1), QString("25,0 %"));
    }

    void tortaAgrupaEnOtros()
    {
        GraficoTorta t;
        t.resize(700, 320);
        QList<TotalCategoria> l;
        for (int i = 0; i < 10; ++i) l << tc(QString("Cat %1").arg(i), i % 8, (10 - i) * 1000);
        t.setDatos("Gastos", l);
        const auto& p = t.porciones();
        QCOMPARE(p.size(), int(GraficoTorta::MAX_PORCIONES));
        QCOMPARE(p.last().nombre, QString("Otros"));
        QCOMPARE(p.last().color, -1);
        QCOMPARE(p.last().incluye.size(), 3);
        QSet<int> colores;
        for (int i = 0; i < p.size() - 1; ++i) colores.insert(p[i].color);
        QCOMPARE(colores.size(), p.size() - 1);               // nunca dos porciones del mismo color
        Centavos suma = 0;
        for (const auto& x : p) suma += x.total;
        QCOMPARE(suma, Centavos(55000));                      // no se pierde plata
    }

    void tortaColorRepetidoTomaUnoLibre()
    {
        // Más categorías que colores: las grandes nunca se esconden en «Otros» por compartir color
        GraficoTorta t;
        t.resize(600, 300);
        t.setDatos("Gastos", {tc("A", 0, 500), tc("B", 0, 300), tc("C", 1, 200)});
        QCOMPARE(t.porciones().size(), 3);
        QCOMPARE(t.porciones()[0].nombre, QString("A"));
        QCOMPARE(t.porciones()[0].color, 0);                      // la más grande conserva el suyo
        QCOMPARE(t.porciones()[1].nombre, QString("B"));
        QCOMPARE(t.porciones()[1].color, 2);                      // primer color libre
        QCOMPARE(t.porciones()[2].color, 1);
    }

    void tortaSiempreLasSieteMasGrandes()
    {
        GraficoTorta t;
        QList<TotalCategoria> l;
        for (int i = 0; i < 12; ++i) l << tc(QString("Cat %1").arg(i), 0, (12 - i) * 100);   // todas del mismo color
        t.setDatos("Gastos", l);
        QCOMPARE(t.porciones().size(), 8);
        for (int i = 0; i < 7; ++i) QCOMPARE(t.porciones()[i].nombre, QString("Cat %1").arg(i));
        QCOMPARE(t.porciones()[7].nombre, QString("Otros"));
        QSet<int> colores;
        for (int i = 0; i < 7; ++i) colores.insert(t.porciones()[i].color);
        QCOMPARE(colores.size(), 7);
    }

    void tortaOchoExactasNoUsaOtros()
    {
        GraficoTorta t;
        QList<TotalCategoria> l;
        for (int i = 0; i < 8; ++i) l << tc(QString("Cat %1").arg(i), i, (8 - i) * 100);
        t.setDatos("Gastos", l);
        QCOMPARE(t.porciones().size(), 8);
        QVERIFY(t.porciones().last().nombre != "Otros");
    }

    void tortaPorcionEn()
    {
        GraficoTorta t;
        t.resize(600, 300);
        t.setDatos("Gastos", {tc("Grande", 0, 7500), tc("Chica", 1, 2500)});
        const QRectF r = t.rectTorta();
        const QPointF c = r.center();
        const double rad = r.width() / 4;
        // Arranca a las 12 y gira en sentido horario: a la derecha (3 h) está la grande (75 %),
        // a la izquierda arriba (10:30 h) la chica (último 25 %)
        QCOMPARE(t.porcionEn(QPointF(c.x() + rad, c.y()).toPoint()), 0);
        QCOMPARE(t.porcionEn(QPointF(c.x() - rad * 0.7, c.y() - rad * 0.7).toPoint()), 1);
        QCOMPARE(t.porcionEn(QPoint(int(r.right()) + 2, int(r.top()) + 1)), -1);   // fuera de la torta y la leyenda
    }

    void tortaVacia()
    {
        GraficoTorta t;
        t.resize(500, 300);
        t.setDatos("Ingresos", {});
        QVERIFY(t.porciones().isEmpty());
        QCOMPARE(t.porcionEn(QPoint(100, 150)), -1);
        QCOMPARE(t.textoPorcentaje(0), QString());
        QImage img(t.size(), QImage::Format_ARGB32);
        t.render(&img);
    }
};

QTEST_MAIN(TstGraficos)
#include "tst_graficos.moc"
