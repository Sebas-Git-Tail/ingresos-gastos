#include "graficosquick.h"
#include "dinero.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>

namespace {
QVector<Centavos> centavos(const QVariant& v)
{
    QVector<Centavos> r;
    for (const QVariant& x : v.toList()) r << Centavos(std::llround(x.toDouble()));
    return r;
}
}

// ====================================================================== barras

GraficoBarrasQuick::GraficoBarrasQuick(QQuickItem* parent)
    : QQuickPaintedItem(parent), fuente_(QGuiApplication::font())
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void GraficoBarrasQuick::setDatos(const QVariantMap& d)
{
    datos_ = d;
    dibujo_.setDatos(d.value(QStringLiteral("titulo")).toString(), d.value(QStringLiteral("etiquetas")).toStringList(),
                     d.value(QStringLiteral("detalles")).toStringList(), centavos(d.value(QStringLiteral("ingresos"))),
                     centavos(d.value(QStringLiteral("gastos"))));
    dibujo_.setMarcado(d.value(QStringLiteral("marcado"), -1).toInt());
    if (d.contains(QStringLiteral("vacio"))) dibujo_.setTextoVacio(d.value(QStringLiteral("vacio")).toString());
    emit datosChanged();
    emit seleccionadoChanged();
    update();
}

void GraficoBarrasQuick::setOscuro(bool o)
{
    if (o == oscuro_) return;
    oscuro_ = o;
    emit oscuroChanged();
    update();
}

void GraficoBarrasQuick::setFuente(const QFont& f)
{
    fuente_ = f;
    emit fuenteChanged();
    update();
}

void GraficoBarrasQuick::setSeleccionado(int i)
{
    if (i == dibujo_.resaltado()) return;
    dibujo_.setResaltado(i >= 0 && i < dibujo_.cantidad() ? i : -1);
    emit seleccionadoChanged();
    update();
}

QString GraficoBarrasQuick::detalleSeleccionado() const
{
    const int i = dibujo_.resaltado();
    if (i < 0) return QString();
    const TotalPeriodo t = dibujo_.totales(i);
    return QStringLiteral("%1 · Ingresos %2 · Gastos %3 · Saldo %4")
        .arg(dibujo_.detalle(i), formatearPesos(t.ingresos), formatearPesos(t.gastos), formatearPesosConSigno(t.saldo()));
}

int GraficoBarrasQuick::grupoEn(qreal x, qreal y)
{
    dibujo_.setLienzo(size(), fuente_);
    return dibujo_.grupoEn(QPointF(x, y));
}

void GraficoBarrasQuick::paint(QPainter* p)
{
    dibujo_.setLienzo(size(), fuente_);
    dibujo_.pintar(*p, TemaGrafico::crear(oscuro_));
}

void GraficoBarrasQuick::mousePressEvent(QMouseEvent* e)
{
    e->accept();                                    // para recibir el soltar
}

void GraficoBarrasQuick::mouseReleaseEvent(QMouseEvent* e)
{
    const int g = grupoEn(e->position().x(), e->position().y());
    if (g < 0) return;
    setSeleccionado(g == dibujo_.resaltado() ? -1 : g);   // tocar de nuevo la misma: se des-señala
    emit grupoTocado(g);
}

// ====================================================================== torta

GraficoTortaQuick::GraficoTortaQuick(QQuickItem* parent)
    : QQuickPaintedItem(parent), fuente_(QGuiApplication::font())
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void GraficoTortaQuick::setDatos(const QVariantMap& d)
{
    datos_ = d;
    QList<TotalCategoria> lista;
    for (const QVariant& v : d.value(QStringLiteral("totales")).toList()) {
        const QVariantMap m = v.toMap();
        TotalCategoria tc;
        tc.nombre = m.value(QStringLiteral("nombre")).toString();
        tc.color = m.value(QStringLiteral("color")).toInt();
        tc.total = Centavos(std::llround(m.value(QStringLiteral("total")).toDouble()));
        lista << tc;
    }
    dibujo_.setDatos(d.value(QStringLiteral("titulo")).toString(), lista);
    if (d.contains(QStringLiteral("vacio"))) dibujo_.setTextoVacio(d.value(QStringLiteral("vacio")).toString());
    emit datosChanged();
    emit altoNecesarioChanged();
    emit seleccionadaChanged();
    update();
}

void GraficoTortaQuick::setOscuro(bool o)
{
    if (o == oscuro_) return;
    oscuro_ = o;
    emit oscuroChanged();
    update();
}

void GraficoTortaQuick::setFuente(const QFont& f)
{
    fuente_ = f;
    emit fuenteChanged();
    emit altoNecesarioChanged();
    update();
}

qreal GraficoTortaQuick::altoNecesario() const
{
    DibujoTorta d = dibujo_;
    d.setLienzo(QSizeF(width(), 1000), fuente_);
    return d.altoNecesario(width() > 0 ? width() : 360);
}

QString GraficoTortaQuick::detalleSeleccionada() const
{
    const int i = dibujo_.resaltada();
    if (i < 0 || i >= dibujo_.porciones().size()) return QString();
    const DibujoTorta::Porcion& p = dibujo_.porciones()[i];
    QString t = QStringLiteral("%1: %2 (%3)").arg(p.nombre, formatearPesos(p.total), dibujo_.textoPorcentaje(i));
    if (!p.incluye.isEmpty()) t += QStringLiteral(" — ") + p.incluye.join(QStringLiteral(", "));
    return t;
}

void GraficoTortaQuick::paint(QPainter* p)
{
    dibujo_.setLienzo(size(), fuente_);
    dibujo_.pintar(*p, TemaGrafico::crear(oscuro_));
}

void GraficoTortaQuick::mousePressEvent(QMouseEvent* e)
{
    e->accept();
}

void GraficoTortaQuick::mouseReleaseEvent(QMouseEvent* e)
{
    dibujo_.setLienzo(size(), fuente_);
    const int i = dibujo_.porcionEn(e->position());
    dibujo_.setResaltada(i == dibujo_.resaltada() ? -1 : i);
    emit seleccionadaChanged();
    update();
}

void GraficoTortaQuick::geometryChange(const QRectF& nueva, const QRectF& vieja)
{
    QQuickPaintedItem::geometryChange(nueva, vieja);
    if (nueva.width() != vieja.width()) emit altoNecesarioChanged();
}
