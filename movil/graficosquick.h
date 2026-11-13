#pragma once
#include "dibujo.h"
#include <QFont>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

/**
 * Los mismos gráficos que en la PC (nucleo/dibujo.h), como elementos de Qt Quick.
 * En el celular no hay "pasar el mouse": se toca una barra o una porción para señalarla.
 */
class GraficoBarrasQuick : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GraficoBarras)
    // {titulo, etiquetas[], detalles[], ingresos[], gastos[] (centavos), marcado, vacio}
    Q_PROPERTY(QVariantMap datos READ datos WRITE setDatos NOTIFY datosChanged)
    Q_PROPERTY(bool oscuro READ oscuro WRITE setOscuro NOTIFY oscuroChanged)
    Q_PROPERTY(QFont fuente READ fuente WRITE setFuente NOTIFY fuenteChanged)
    Q_PROPERTY(int seleccionado READ seleccionado WRITE setSeleccionado NOTIFY seleccionadoChanged)
    Q_PROPERTY(QString detalleSeleccionado READ detalleSeleccionado NOTIFY seleccionadoChanged)
public:
    explicit GraficoBarrasQuick(QQuickItem* parent = nullptr);

    QVariantMap datos() const { return datos_; }
    void setDatos(const QVariantMap& d);
    bool oscuro() const { return oscuro_; }
    void setOscuro(bool o);
    QFont fuente() const { return fuente_; }
    void setFuente(const QFont& f);
    int seleccionado() const { return dibujo_.resaltado(); }
    void setSeleccionado(int i);
    QString detalleSeleccionado() const;

    void paint(QPainter* p) override;
    Q_INVOKABLE int grupoEn(qreal x, qreal y);   // para las pruebas

signals:
    void datosChanged();
    void oscuroChanged();
    void fuenteChanged();
    void seleccionadoChanged();
    void grupoTocado(int indice);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QVariantMap datos_;
    bool oscuro_ = false;
    QFont fuente_;
    DibujoBarras dibujo_;
};

class GraficoTortaQuick : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GraficoTorta)
    // {titulo, vacio, totales: [{nombre, color (0-7), total (centavos)}]}
    Q_PROPERTY(QVariantMap datos READ datos WRITE setDatos NOTIFY datosChanged)
    Q_PROPERTY(bool oscuro READ oscuro WRITE setOscuro NOTIFY oscuroChanged)
    Q_PROPERTY(QFont fuente READ fuente WRITE setFuente NOTIFY fuenteChanged)
    Q_PROPERTY(qreal altoNecesario READ altoNecesario NOTIFY altoNecesarioChanged)
    Q_PROPERTY(int seleccionada READ seleccionada NOTIFY seleccionadaChanged)
    Q_PROPERTY(QString detalleSeleccionada READ detalleSeleccionada NOTIFY seleccionadaChanged)
public:
    explicit GraficoTortaQuick(QQuickItem* parent = nullptr);

    QVariantMap datos() const { return datos_; }
    void setDatos(const QVariantMap& d);
    bool oscuro() const { return oscuro_; }
    void setOscuro(bool o);
    QFont fuente() const { return fuente_; }
    void setFuente(const QFont& f);
    qreal altoNecesario() const;
    int seleccionada() const { return dibujo_.resaltada(); }
    QString detalleSeleccionada() const;
    int cantidadPorciones() const { return dibujo_.porciones().size(); }

    void paint(QPainter* p) override;

signals:
    void datosChanged();
    void oscuroChanged();
    void fuenteChanged();
    void altoNecesarioChanged();
    void seleccionadaChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void geometryChange(const QRectF& nueva, const QRectF& vieja) override;

private:
    QVariantMap datos_;
    bool oscuro_ = false;
    QFont fuente_;
    DibujoTorta dibujo_;
};
