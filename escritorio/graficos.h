#pragma once
#include "dibujo.h"
#include <QWidget>

/**
 * Gráficos de la versión de escritorio: widgets que muestran el dibujo de nucleo/dibujo.h
 * y agregan el mouse (cartelito al pasar, clic en un mes).
 */
TemaGrafico temaPara(const QWidget* w);   // claro u oscuro según la paleta del widget

/** Barras de ingresos y gastos (por mes o por año), una pareja de barras por período. */
class GraficoBarras : public QWidget
{
    Q_OBJECT
public:
    explicit GraficoBarras(QWidget* parent = nullptr);

    void setDatos(const QString& titulo, const QStringList& etiquetas, const QStringList& detalles,
                  const QVector<Centavos>& ingresos, const QVector<Centavos>& gastos);
    void setMarcado(int indice);                // el período elegido arriba (su etiqueta va en negrita)
    void setTextoVacio(const QString& texto);

    // Para las pruebas
    int grupoEn(const QPoint& p) const { return d().grupoEn(p); }
    int grupoResaltado() const { return dibujo_.resaltado(); }
    QList<double> marcasEje() const { return d().marcasEje(); }
    QRectF rectBarra(int grupo, bool ingreso) const { return d().rectBarra(grupo, ingreso); }

    QSize minimumSizeHint() const override { return {420, 220}; }

signals:
    void grupoClicado(int indice);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void changeEvent(QEvent* e) override;

private:
    const DibujoBarras& d() const;              // el dibujo con el tamaño y la letra actuales
    mutable DibujoBarras dibujo_;
};

/** Torta de los totales por categoría (hasta 7 porciones + «Otros»), con leyenda: nombre, monto y %. */
class GraficoTorta : public QWidget
{
    Q_OBJECT
public:
    using Porcion = DibujoTorta::Porcion;
    static constexpr int MAX_PORCIONES = DibujoTorta::MAX_PORCIONES;

    explicit GraficoTorta(QWidget* parent = nullptr);

    void setDatos(const QString& titulo, const QList<TotalCategoria>& totales);
    void setTextoVacio(const QString& texto);

    // Para las pruebas
    const QList<Porcion>& porciones() const { return dibujo_.porciones(); }
    int porcionEn(const QPoint& p) const { return d().porcionEn(p); }
    int porcionResaltada() const { return dibujo_.resaltada(); }
    QRectF rectTorta() const { return d().rectTorta(); }
    QString textoPorcentaje(int indice) const { return dibujo_.textoPorcentaje(indice); }

    QSize minimumSizeHint() const override { return {360, 220}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void changeEvent(QEvent* e) override;

private:
    const DibujoTorta& d() const;
    mutable DibujoTorta dibujo_;
};
