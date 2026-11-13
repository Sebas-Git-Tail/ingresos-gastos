#include "graficos.h"
#include "dinero.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

TemaGrafico temaPara(const QWidget* w)
{
    return TemaGrafico::crear(w->palette().color(QPalette::Window).lightness() < 128);
}

// ====================================================================== barras

GraficoBarras::GraficoBarras(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

const DibujoBarras& GraficoBarras::d() const
{
    dibujo_.setLienzo(size(), font());
    return dibujo_;
}

void GraficoBarras::setDatos(const QString& titulo, const QStringList& etiquetas, const QStringList& detalles,
                             const QVector<Centavos>& ingresos, const QVector<Centavos>& gastos)
{
    dibujo_.setDatos(titulo, etiquetas, detalles, ingresos, gastos);
    setAccessibleName(titulo);
    update();
}

void GraficoBarras::setMarcado(int indice)
{
    dibujo_.setMarcado(indice);
    update();
}

void GraficoBarras::setTextoVacio(const QString& texto)
{
    dibujo_.setTextoVacio(texto);
    update();
}

void GraficoBarras::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    d().pintar(p, temaPara(this));
}

void GraficoBarras::mouseMoveEvent(QMouseEvent* e)
{
    const int g = grupoEn(e->position().toPoint());
    if (g != dibujo_.resaltado()) {
        dibujo_.setResaltado(g);
        update();
    }
    if (g < 0) {
        QToolTip::hideText();
        return;
    }
    const TotalPeriodo tot = dibujo_.totales(g);
    QToolTip::showText(e->globalPosition().toPoint(),
                       QStringLiteral("<b>%1</b><br>Ingresos: %2<br>Gastos: %3<br>Saldo: %4")
                           .arg(dibujo_.detalle(g).toHtmlEscaped(), formatearPesos(tot.ingresos), formatearPesos(tot.gastos),
                                formatearPesosConSigno(tot.saldo())),
                       this);
}

void GraficoBarras::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const int g = grupoEn(e->position().toPoint());
    if (g >= 0) emit grupoClicado(g);
}

void GraficoBarras::leaveEvent(QEvent*)
{
    dibujo_.setResaltado(-1);
    QToolTip::hideText();
    update();
}

void GraficoBarras::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange) update();
    QWidget::changeEvent(e);
}

// ====================================================================== torta

GraficoTorta::GraficoTorta(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

const DibujoTorta& GraficoTorta::d() const
{
    dibujo_.setLienzo(size(), font());
    return dibujo_;
}

void GraficoTorta::setDatos(const QString& titulo, const QList<TotalCategoria>& totales)
{
    dibujo_.setDatos(titulo, totales);
    setAccessibleName(titulo);
    update();
}

void GraficoTorta::setTextoVacio(const QString& texto)
{
    dibujo_.setTextoVacio(texto);
    update();
}

void GraficoTorta::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    d().pintar(p, temaPara(this));
}

void GraficoTorta::mouseMoveEvent(QMouseEvent* e)
{
    const int i = porcionEn(e->position().toPoint());
    if (i != dibujo_.resaltada()) {
        dibujo_.setResaltada(i);
        update();
    }
    if (i < 0) {
        QToolTip::hideText();
        return;
    }
    const Porcion& por = dibujo_.porciones()[i];
    QString texto = QStringLiteral("<b>%1</b><br>%2 (%3)").arg(por.nombre.toHtmlEscaped(), formatearPesos(por.total), textoPorcentaje(i));
    if (!por.incluye.isEmpty()) texto += QStringLiteral("<br>") + por.incluye.join(QStringLiteral(", ")).toHtmlEscaped();
    QToolTip::showText(e->globalPosition().toPoint(), texto, this);
}

void GraficoTorta::leaveEvent(QEvent*)
{
    dibujo_.setResaltada(-1);
    QToolTip::hideText();
    update();
}

void GraficoTorta::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange) update();
    QWidget::changeEvent(e);
}
