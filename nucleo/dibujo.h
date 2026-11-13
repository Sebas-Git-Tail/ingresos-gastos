#pragma once
#include "modelos.h"
#include <QColor>
#include <QFont>
#include <QSizeF>
#include <QStringList>
#include <QVector>

class QPainter;

/**
 * Dibujo de los gráficos con QPainter, sin depender de cómo se muestran.
 * Lo usan los widgets de la versión de escritorio (escritorio/graficos.h) y los
 * elementos de Qt Quick de la versión para celular (movil/graficosquick.h): así los dos
 * se ven igual y las cuentas de geometría están en un solo lugar.
 *
 * Colores: la paleta categórica validada (8 colores, en orden fijo) en su versión clara u oscura.
 * Ingresos = color 0 (azul), gastos = color 1 (naranja). Cada categoría tiene su color guardado.
 */
struct TemaGrafico {
    bool oscuro = false;
    QColor superficie, tintaPrimaria, tintaSecundaria, tintaApagada, grilla, base, otros;
    QColor serie[8];
    static TemaGrafico crear(bool oscuro);
};

// Escala "linda" del eje (0, 5 mil, 10 mil...)
QList<double> marcasLindas(double maximo, int cantidadAprox = 4);

/** Barras de ingresos y gastos, una pareja por período (mes o año). */
class DibujoBarras
{
public:
    // etiquetas: debajo de cada pareja ("Ene"); detalles: título del cartelito ("Enero 2026")
    void setDatos(const QString& titulo, const QStringList& etiquetas, const QStringList& detalles,
                  const QVector<Centavos>& ingresos, const QVector<Centavos>& gastos);
    void setMarcado(int indice) { marcado_ = indice; }
    void setResaltado(int indice) { resaltado_ = indice; }
    void setTextoVacio(const QString& texto) { textoVacio_ = texto; }
    void setLienzo(const QSizeF& tamano, const QFont& fuente) { tam_ = tamano; fuente_ = fuente; }

    int grupoEn(const QPointF& p) const;       // -1 si no hay ninguno
    QRectF rectBarra(int grupo, bool ingreso) const;
    QList<double> marcasEje() const;            // valores de las líneas de la grilla, en pesos
    void pintar(QPainter& p, const TemaGrafico& t) const;

    const QString& titulo() const { return titulo_; }
    int cantidad() const { return etiquetas_.size(); }
    int marcado() const { return marcado_; }
    int resaltado() const { return resaltado_; }
    QString detalle(int i) const;               // "Octubre 2026"
    TotalPeriodo totales(int i) const;

private:
    QRectF areaGrafico() const;
    Centavos maximo() const;

    QSizeF tam_{400, 240};
    QFont fuente_;
    QString titulo_, textoVacio_ = QStringLiteral("Sin movimientos");
    QStringList etiquetas_, detalles_;
    QVector<Centavos> ingresos_, gastos_;
    int marcado_ = -1;
    int resaltado_ = -1;
};

/**
 * Torta de los totales por categoría: hasta 7 porciones + «Otros», con leyenda (nombre, monto y %).
 * Si el lugar es angosto (celular), la leyenda va debajo de la torta en vez de al costado.
 */
class DibujoTorta
{
public:
    struct Porcion {
        QString nombre;
        int color = 0;                          // posición en la paleta; -1 = «Otros» (gris)
        Centavos total = 0;
        QStringList incluye;                    // en «Otros»: qué categorías agrupa
    };
    static constexpr int MAX_PORCIONES = 8;     // 7 categorías + «Otros»
    static constexpr double ANCHO_VERTICAL = 520;   // más angosto que esto: leyenda debajo

    void setDatos(const QString& titulo, const QList<TotalCategoria>& totales);
    void setResaltada(int indice) { resaltada_ = indice; }
    void setTextoVacio(const QString& texto) { textoVacio_ = texto; }
    void setLienzo(const QSizeF& tamano, const QFont& fuente) { tam_ = tamano; fuente_ = fuente; }

    const QList<Porcion>& porciones() const { return porciones_; }
    int porcionEn(const QPointF& p) const;      // sobre la torta o sobre su renglón de la leyenda
    int resaltada() const { return resaltada_; }
    QRectF rectTorta() const;
    QString textoPorcentaje(int indice) const;
    Centavos total() const;
    double altoNecesario(double ancho) const;   // para la versión vertical: cuánto alto ocupa todo
    void pintar(QPainter& p, const TemaGrafico& t) const;

private:
    bool vertical() const { return tam_.width() < ANCHO_VERTICAL; }
    double arribaDeLaTorta() const;
    QRectF rectLeyenda() const;
    QRectF rectRenglon(int i) const;
    double altoRenglon() const;

    QSizeF tam_{400, 240};
    QFont fuente_;
    QString titulo_, textoVacio_ = QStringLiteral("Sin movimientos");
    QList<Porcion> porciones_;
    int resaltada_ = -1;
};
