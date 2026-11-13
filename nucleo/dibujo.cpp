#include "dibujo.h"
#include "dinero.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

constexpr double MARGEN = 14;
constexpr double ANCHO_MAX_BARRA = 24;   // las barras nunca llenan el espacio: el resto es aire
constexpr double SEPARACION = 2;         // entre la barra de ingresos y la de gastos
constexpr double RADIO = 4;              // punta redondeada; la base queda recta
constexpr double LADO_MAX_VERTICAL = 200;   // torta en el celular

QFont fuente(const QFont& base, double escala, bool negrita = false)
{
    QFont f = base;
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * escala);
    else if (f.pixelSize() > 0) f.setPixelSize(std::max(1, int(std::lround(f.pixelSize() * escala))));
    f.setBold(negrita);
    return f;
}

// Barra con la punta redondeada y la base recta, apoyada en la línea de base
QPainterPath barra(const QRectF& r)
{
    QPainterPath p;
    const double rad = std::min({RADIO, r.height(), r.width() / 2});
    p.moveTo(r.left(), r.bottom());
    p.lineTo(r.left(), r.top() + rad);
    p.quadTo(r.left(), r.top(), r.left() + rad, r.top());
    p.lineTo(r.right() - rad, r.top());
    p.quadTo(r.right(), r.top(), r.right(), r.top() + rad);
    p.lineTo(r.right(), r.bottom());
    p.closeSubpath();
    return p;
}

void muestra(QPainter& p, const QRectF& r, const QColor& c)       // cuadradito de color de la leyenda
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(r, 2, 2);
}

QString porcentaje(double v)                                        // 42.34 -> "42,3 %"
{
    return QString::number(v, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')) + QStringLiteral(" %");
}

void fondo(QPainter& p, const QSizeF& tam, const TemaGrafico& t)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(t.superficie);
    p.drawRoundedRect(QRectF(QPointF(0, 0), tam).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

} // namespace

// ====================================================================== tema

TemaGrafico TemaGrafico::crear(bool oscuro)
{
    TemaGrafico t;
    t.oscuro = oscuro;
    static const char* const claro[8] = {"#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"};
    static const char* const osc[8] = {"#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#008300", "#9085e9", "#e66767"};
    for (int i = 0; i < 8; ++i) t.serie[i] = QColor(QLatin1String(oscuro ? osc[i] : claro[i]));
    if (oscuro) {
        t.superficie = QColor("#1a1a19");
        t.tintaPrimaria = QColor("#f0efec");
        t.tintaSecundaria = QColor("#c3c2b7");
        t.grilla = QColor("#2c2c2a");
        t.base = QColor("#383835");
    } else {
        t.superficie = QColor("#fcfcfb");
        t.tintaPrimaria = QColor("#0b0b0b");
        t.tintaSecundaria = QColor("#52514e");
        t.grilla = QColor("#e1e0d9");
        t.base = QColor("#c3c2b7");
    }
    t.tintaApagada = QColor("#898781");
    t.otros = QColor("#898781");
    return t;
}

QList<double> marcasLindas(double maximo, int cantidadAprox)
{
    if (maximo <= 0) return {0};
    const double crudo = maximo / cantidadAprox;
    const double potencia = std::pow(10.0, std::floor(std::log10(crudo)));
    double paso = potencia;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (m * potencia >= crudo) { paso = m * potencia; break; }
    }
    QList<double> marcas;
    for (double v = 0; v < maximo + paso * 0.999; v += paso) marcas << v;
    return marcas;
}

// ====================================================================== barras

void DibujoBarras::setDatos(const QString& titulo, const QStringList& etiquetas, const QStringList& detalles,
                            const QVector<Centavos>& ingresos, const QVector<Centavos>& gastos)
{
    titulo_ = titulo;
    etiquetas_ = etiquetas;
    detalles_ = detalles;
    ingresos_ = ingresos;
    gastos_ = gastos;
    ingresos_.resize(etiquetas_.size());
    gastos_.resize(etiquetas_.size());
    resaltado_ = -1;
}

QString DibujoBarras::detalle(int i) const
{
    if (i < 0 || i >= etiquetas_.size()) return QString();
    return i < detalles_.size() ? detalles_[i] : etiquetas_[i];
}

TotalPeriodo DibujoBarras::totales(int i) const
{
    if (i < 0 || i >= etiquetas_.size()) return {};
    return TotalPeriodo{ingresos_[i], gastos_[i]};
}

Centavos DibujoBarras::maximo() const
{
    Centavos m = 0;
    for (Centavos v : ingresos_) m = std::max(m, v);
    for (Centavos v : gastos_) m = std::max(m, v);
    return m;
}

QList<double> DibujoBarras::marcasEje() const
{
    return marcasLindas(double(maximo()) / 100.0);
}

QRectF DibujoBarras::areaGrafico() const
{
    const QFontMetricsF chico(fuente(fuente_, 0.9));
    const QFontMetricsF tit(fuente(fuente_, 1.1, true));
    double anchoEje = 0;
    for (double v : marcasEje()) anchoEje = std::max(anchoEje, chico.horizontalAdvance(formatearPesosCorto(Centavos(std::llround(v * 100)))));
    const double arriba = MARGEN + tit.height() + 14 + chico.height() / 2;
    const double abajo = MARGEN + chico.height() + 6;
    return QRectF(MARGEN + anchoEje + 8, arriba, tam_.width() - (MARGEN + anchoEje + 8) - MARGEN, tam_.height() - arriba - abajo);
}

QRectF DibujoBarras::rectBarra(int grupo, bool ingreso) const
{
    const int n = etiquetas_.size();
    const Centavos max = maximo();
    if (n == 0 || max <= 0 || grupo < 0 || grupo >= n) return {};
    const QRectF a = areaGrafico();
    const double tope = marcasEje().last() * 100.0;                  // el eje llega a la última marca
    const double anchoGrupo = a.width() / n;
    const double anchoBarra = std::max(2.0, std::min(ANCHO_MAX_BARRA, (anchoGrupo - 8 - SEPARACION) / 2));
    const double x0 = a.left() + grupo * anchoGrupo + (anchoGrupo - (2 * anchoBarra + SEPARACION)) / 2;
    const Centavos valor = ingreso ? ingresos_[grupo] : gastos_[grupo];
    const double alto = a.height() * double(valor) / tope;
    const double x = ingreso ? x0 : x0 + anchoBarra + SEPARACION;
    return QRectF(x, a.bottom() - alto, anchoBarra, alto);
}

int DibujoBarras::grupoEn(const QPointF& p) const
{
    const int n = etiquetas_.size();
    if (n == 0 || maximo() <= 0) return -1;
    const QRectF a = areaGrafico();
    // toda la columna del período (incluidas las etiquetas) cuenta: es más fácil apuntarle que a una barra finita
    const QRectF columnas(a.left(), a.top(), a.width(), tam_.height() - a.top());
    if (!columnas.contains(p)) return -1;
    return std::clamp(int((p.x() - a.left()) / (a.width() / n)), 0, n - 1);
}

void DibujoBarras::pintar(QPainter& p, const TemaGrafico& t) const
{
    fondo(p, tam_, t);

    // Título y leyenda (la leyenda siempre está: son dos series)
    const QFont fTitulo = fuente(fuente_, 1.1, true), fChica = fuente(fuente_, 0.9), fNormal = fuente(fuente_, 1.0);
    const QFontMetricsF mt(fTitulo), m(fNormal);
    const QString nombres[2] = {QStringLiteral("Ingresos"), QStringLiteral("Gastos")};
    const double anchoLeyenda = m.horizontalAdvance(nombres[0]) + m.horizontalAdvance(nombres[1]) + 2 * 16 + 14;
    p.setFont(fTitulo);
    p.setPen(t.tintaPrimaria);
    const double anchoTitulo = tam_.width() - 2 * MARGEN - anchoLeyenda - 12;
    p.drawText(QPointF(MARGEN, MARGEN + mt.ascent()), QFontMetricsF(fTitulo).elidedText(titulo_, Qt::ElideRight, std::max(0.0, anchoTitulo)));
    {
        p.setFont(fNormal);
        double x = tam_.width() - MARGEN;
        for (int i = 1; i >= 0; --i) {
            const double ancho = m.horizontalAdvance(nombres[i]);
            x -= ancho;
            p.setPen(t.tintaSecundaria);
            p.drawText(QPointF(x, MARGEN + mt.ascent()), nombres[i]);
            x -= 16;
            muestra(p, QRectF(x, MARGEN + mt.ascent() - 10, 10, 10), t.serie[i]);
            x -= 14;
        }
    }

    const Centavos max = maximo();
    if (etiquetas_.isEmpty() || max <= 0) {
        p.setFont(fNormal);
        p.setPen(t.tintaApagada);
        p.drawText(QRectF(0, mt.height(), tam_.width(), tam_.height() - mt.height()), Qt::AlignCenter, textoVacio_);
        return;
    }

    const QRectF a = areaGrafico();
    const QList<double> marcas = marcasEje();
    const double tope = marcas.last();
    const int n = etiquetas_.size();
    const double anchoGrupo = a.width() / n;

    // Período señalado: una franja suave detrás
    if (resaltado_ >= 0 && resaltado_ < n) {
        QColor franja = t.grilla;
        franja.setAlpha(t.oscuro ? 200 : 140);
        p.setPen(Qt::NoPen);
        p.setBrush(franja);
        p.drawRoundedRect(QRectF(a.left() + resaltado_ * anchoGrupo + 2, a.top() - 6, anchoGrupo - 4, a.height() + 6), 4, 4);
    }

    // Grilla (finita y discreta) con los montos a la izquierda
    p.setFont(fChica);
    const QFontMetricsF mc(fChica);
    for (double v : marcas) {
        const double y = std::round(a.bottom() - a.height() * v / tope) + 0.5;
        p.setPen(QPen(v == 0 ? t.base : t.grilla, 1));
        p.drawLine(QPointF(a.left(), y), QPointF(a.right(), y));
        p.setPen(t.tintaApagada);
        const QString texto = formatearPesosCorto(Centavos(std::llround(v * 100)));
        p.drawText(QRectF(MARGEN, y - mc.height() / 2, a.left() - 8 - MARGEN, mc.height()), Qt::AlignRight | Qt::AlignVCenter, texto);
    }

    // Barras
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        for (bool ingreso : {true, false}) {
            const QRectF r = rectBarra(i, ingreso);
            if (r.height() <= 0) continue;
            p.setBrush(t.serie[ingreso ? 0 : 1]);
            p.drawPath(barra(r));
        }
    }

    // Etiquetas de abajo (la del período elegido, en negrita)
    for (int i = 0; i < n; ++i) {
        const bool marcado = i == marcado_;
        QFont f = fChica;
        f.setBold(marcado);
        p.setFont(f);
        p.setPen(marcado ? t.tintaPrimaria : t.tintaApagada);
        p.drawText(QRectF(a.left() + i * anchoGrupo, a.bottom() + 4, anchoGrupo, mc.height() + 2), Qt::AlignHCenter | Qt::AlignTop, etiquetas_[i]);
    }
}

// ====================================================================== torta

void DibujoTorta::setDatos(const QString& titulo, const QList<TotalCategoria>& totales)
{
    titulo_ = titulo;
    porciones_.clear();
    QList<TotalCategoria> lista;
    for (const TotalCategoria& tc : totales) if (tc.total > 0) lista << tc;
    std::stable_sort(lista.begin(), lista.end(), [](const TotalCategoria& a, const TotalCategoria& b) { return a.total > b.total; });
    // Se muestran las 7 más grandes (8 si son justo 8); el resto va a «Otros» (gris).
    QList<TotalCategoria> directas = lista.mid(0, lista.size() == MAX_PORCIONES ? MAX_PORCIONES : MAX_PORCIONES - 1);
    const QList<TotalCategoria> resto = lista.mid(directas.size());
    // Colores: cada una con el suyo; si dos de las que se muestran tienen el mismo (hay más categorías
    // que colores), la más chica toma, solo en este gráfico, un color de la paleta que nadie use.
    // Nunca se inventa un color nuevo y nunca hay dos porciones del mismo color.
    QSet<int> usados;
    QList<int> repetidas;
    for (int i = 0; i < directas.size(); ++i) {
        const int c = std::clamp(directas[i].color, 0, 7);
        if (usados.contains(c)) repetidas << i;
        else { directas[i].color = c; usados.insert(c); }
    }
    for (int i : repetidas) {
        int libre = 0;
        while (usados.contains(libre)) ++libre;
        directas[i].color = libre;
        usados.insert(libre);
    }
    for (const TotalCategoria& tc : directas) porciones_ << Porcion{tc.nombre, tc.color, tc.total, {}};
    if (!resto.isEmpty()) {
        Porcion otros{QStringLiteral("Otros"), -1, 0, {}};
        for (const TotalCategoria& tc : resto) {
            otros.total += tc.total;
            otros.incluye << tc.nombre;
        }
        porciones_ << otros;
    }
    resaltada_ = -1;
}

Centavos DibujoTorta::total() const
{
    Centavos t = 0;
    for (const Porcion& p : porciones_) t += p.total;
    return t;
}

QString DibujoTorta::textoPorcentaje(int i) const
{
    const Centavos t = total();
    if (t <= 0 || i < 0 || i >= porciones_.size()) return QString();
    return porcentaje(100.0 * double(porciones_[i].total) / double(t));
}

double DibujoTorta::arribaDeLaTorta() const
{
    const QFontMetricsF mt(fuente(fuente_, 1.1, true)), m(fuente(fuente_, 1.0));
    return MARGEN + mt.height() + m.height() + 12;
}

double DibujoTorta::altoRenglon() const
{
    const QFontMetricsF m(fuente(fuente_, 1.0));
    return m.height() + (vertical() ? 14 : 8);                 // en el celular, renglones cómodos para el dedo
}

QRectF DibujoTorta::rectTorta() const
{
    const double arriba = arribaDeLaTorta();
    if (vertical()) {
        const double lado = std::max(40.0, std::min(LADO_MAX_VERTICAL, tam_.width() - 2 * MARGEN));
        return QRectF((tam_.width() - lado) / 2, arriba, lado, lado);
    }
    const double alto = tam_.height() - arriba - MARGEN;
    const double lado = std::max(40.0, std::min(alto, tam_.width() * 0.42));
    return QRectF(MARGEN, arriba + (alto - lado) / 2, lado, lado);
}

QRectF DibujoTorta::rectLeyenda() const
{
    const QRectF t = rectTorta();
    if (vertical()) return QRectF(MARGEN + 4, t.bottom() + 14, tam_.width() - 2 * MARGEN - 8, altoRenglon() * porciones_.size());
    return QRectF(t.right() + 20, t.top(), tam_.width() - t.right() - 20 - MARGEN, t.height());
}

QRectF DibujoTorta::rectRenglon(int i) const
{
    const QRectF l = rectLeyenda();
    const double alto = altoRenglon();
    const double total = alto * porciones_.size();
    const double y0 = l.top() + std::max(0.0, (l.height() - total) / 2);
    return QRectF(l.left(), y0 + i * alto, l.width(), alto);
}

double DibujoTorta::altoNecesario(double ancho) const
{
    if (ancho >= ANCHO_VERTICAL) return 240;
    DibujoTorta copia = *this;
    copia.tam_ = QSizeF(ancho, 1000);
    if (porciones_.isEmpty()) return copia.arribaDeLaTorta() + 70;
    return copia.rectLeyenda().bottom() + MARGEN;
}

int DibujoTorta::porcionEn(const QPointF& pos) const
{
    if (porciones_.isEmpty()) return -1;
    for (int i = 0; i < porciones_.size(); ++i) if (rectRenglon(i).contains(pos)) return i;   // renglón de la leyenda
    const QRectF r = rectTorta();
    const QPointF c = r.center();
    const double dx = pos.x() - c.x(), dy = c.y() - pos.y();
    if (std::hypot(dx, dy) > r.width() / 2) return -1;
    // ángulo desde las 12, en el sentido de las agujas del reloj (así se dibujan las porciones)
    double grados = std::atan2(dx, dy) * 180.0 / M_PI;
    if (grados < 0) grados += 360;
    const double t = double(total());
    double acumulado = 0;
    for (int i = 0; i < porciones_.size(); ++i) {
        acumulado += 360.0 * double(porciones_[i].total) / t;
        if (grados <= acumulado + 1e-9) return i;
    }
    return porciones_.size() - 1;
}

void DibujoTorta::pintar(QPainter& p, const TemaGrafico& t) const
{
    fondo(p, tam_, t);

    const QFont fTitulo = fuente(fuente_, 1.1, true), fNormal = fuente(fuente_, 1.0);
    const QFontMetricsF mt(fTitulo), m(fNormal);
    p.setFont(fTitulo);
    p.setPen(t.tintaPrimaria);
    p.drawText(QPointF(MARGEN, MARGEN + mt.ascent()), mt.elidedText(titulo_, Qt::ElideRight, tam_.width() - 2 * MARGEN));

    const Centavos tot = total();
    if (porciones_.isEmpty() || tot <= 0) {
        p.setFont(fNormal);
        p.setPen(t.tintaApagada);
        p.drawText(QRectF(0, mt.height(), tam_.width(), tam_.height() - mt.height()), Qt::AlignCenter, textoVacio_);
        return;
    }
    p.setFont(fNormal);
    p.setPen(t.tintaSecundaria);
    p.drawText(QPointF(MARGEN, MARGEN + mt.height() + 4 + m.ascent()), QStringLiteral("Total: ") + formatearPesos(tot));

    auto color = [&](const Porcion& por) { return por.color < 0 ? t.otros : t.serie[por.color % 8]; };

    // Porciones: desde las 12, en el sentido de las agujas del reloj, separadas por 2 px del color del fondo
    const QRectF r = rectTorta();
    double inicio = 90.0;                                   // Qt: grados antihorarios desde las 3
    for (int i = 0; i < porciones_.size(); ++i) {
        const double barrido = -360.0 * double(porciones_[i].total) / double(tot);
        QColor c = color(porciones_[i]);
        if (resaltada_ >= 0 && resaltada_ != i) c.setAlphaF(0.30);   // al señalar una, las demás se apagan
        p.setBrush(c);
        p.setPen(porciones_.size() > 1 ? QPen(t.superficie, 2) : Qt::NoPen);
        if (porciones_.size() == 1) p.drawEllipse(r);
        else p.drawPie(r, int(std::round(inicio * 16)), int(std::round(barrido * 16)));
        inicio += barrido;
    }

    // Leyenda: muestra de color, nombre (tinta principal), monto y porcentaje (nunca texto del color de la serie)
    for (int i = 0; i < porciones_.size(); ++i) {
        const QRectF fila = rectRenglon(i);
        if (i == resaltada_) {
            QColor f = t.grilla;
            f.setAlpha(t.oscuro ? 220 : 160);
            p.setPen(Qt::NoPen);
            p.setBrush(f);
            p.drawRoundedRect(fila.adjusted(-6, 0, 6, 0), 4, 4);
        }
        muestra(p, QRectF(fila.left(), fila.center().y() - 5, 10, 10), color(porciones_[i]));
        const QString pct = textoPorcentaje(i);
        const QString monto = formatearPesos(porciones_[i].total);
        const double anchoPct = m.horizontalAdvance(QStringLiteral("100,0 %"));
        const double anchoMonto = m.horizontalAdvance(monto);
        const QRectF rPct(fila.right() - anchoPct, fila.top(), anchoPct, fila.height());
        const QRectF rMonto(rPct.left() - 12 - anchoMonto, fila.top(), anchoMonto, fila.height());
        const QRectF rNombre(fila.left() + 18, fila.top(), rMonto.left() - 10 - (fila.left() + 18), fila.height());
        p.setFont(fNormal);
        p.setPen(t.tintaPrimaria);
        p.drawText(rNombre, Qt::AlignLeft | Qt::AlignVCenter, m.elidedText(porciones_[i].nombre, Qt::ElideRight, std::max(0.0, rNombre.width())));
        p.setPen(t.tintaSecundaria);
        p.drawText(rMonto, Qt::AlignRight | Qt::AlignVCenter, monto);
        p.setPen(t.tintaApagada);
        p.drawText(rPct, Qt::AlignRight | Qt::AlignVCenter, pct);
    }
}
