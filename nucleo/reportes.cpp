#include "reportes.h"
#include "dinero.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

// ====================================================================== búsqueda

std::optional<FiltroMonto> interpretarFiltroMonto(const QString& palabra)
{
    constexpr Centavos infinito = std::numeric_limits<Centavos>::max();
    Centavos a = 0, b = 0;
    FiltroMonto f;

    static const QRegularExpression comparacion(QStringLiteral(R"(^(<=|>=|<|>)(.+)$)"));
    const auto c = comparacion.match(palabra);
    if (c.hasMatch()) {
        if (!parsearPesos(c.captured(2), &a)) return std::nullopt;
        f.minimo = 0;
        f.maximo = infinito;
        const QString op = c.captured(1);
        if (op == QLatin1String("<")) f.maximo = a - 1;          // en centavos: «menos de 3000» es hasta 2999,99
        else if (op == QLatin1String("<=")) f.maximo = a;
        else if (op == QLatin1String(">")) f.minimo = a + 1;
        else f.minimo = a;
        return f;
    }

    static const QRegularExpression rango(QStringLiteral(R"(^([^-]+)-([^-]+)$)"));
    const auto r = rango.match(palabra);
    if (r.hasMatch() && parsearPesos(r.captured(1), &a) && parsearPesos(r.captured(2), &b)) {
        f.minimo = std::min(a, b);
        f.maximo = std::max(a, b);
        return f;
    }
    return std::nullopt;
}

Busqueda::Busqueda(const QString& consulta)
{
    for (const QString& p : consulta.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (const auto f = interpretarFiltroMonto(p)) montos_ << *f;
        else palabras_ << normalizar(p);
    }
}

bool Busqueda::coincide(const QString& textoNormalizado, Centavos monto) const
{
    for (const FiltroMonto& f : montos_) if (!f.coincide(monto)) return false;
    for (const QString& p : palabras_) if (!textoNormalizado.contains(p)) return false;
    return true;
}

QString textoCategoria(const Movimiento& m)
{
    if (!m.transferencia) return m.categoria;
    if (m.cuentaContraria.isEmpty()) return QStringLiteral("Transferencia");
    return m.tipo == Tipo::Gasto ? QStringLiteral("Transferencia a %1").arg(m.cuentaContraria)
                                 : QStringLiteral("Transferencia desde %1").arg(m.cuentaContraria);
}

QString textoBusqueda(const Movimiento& m)
{
    return normalizar(QStringList{textoCategoria(m), m.descripcion, m.transferencia ? QStringLiteral("Transferencia") : textoTipo(m.tipo),
                                  formatearPesos(m.monto), formatearNumero(m.monto), m.fecha.toString(QStringLiteral("dd/MM/yyyy")),
                                  m.cuenta}
                          .join(QLatin1Char(' ')));
}

// ====================================================================== etiquetas

QStringList etiquetasDe(const QString& descripcion)
{
    // # seguido de letras, números, _ o - («#vacaciones», «#auto-nuevo», «#2026»)
    static const QRegularExpression etiqueta(QStringLiteral(R"((?:^|\s)#([\w\-]+))"), QRegularExpression::UseUnicodePropertiesOption);
    QStringList lista;
    QSet<QString> vistas;
    auto it = etiqueta.globalMatch(descripcion);
    while (it.hasNext()) {
        const QString e = it.next().captured(1);
        const QString clave = normalizar(e);
        if (vistas.contains(clave)) continue;
        vistas.insert(clave);
        lista << QLatin1Char('#') + e;
    }
    return lista;
}

// ====================================================================== períodos

namespace {
bool esMesEntero(const QDate& desde, const QDate& hasta)
{
    return desde.day() == 1 && hasta == desde.addMonths(1).addDays(-1);
}
bool esAnioEntero(const QDate& desde, const QDate& hasta)
{
    return desde.month() == 1 && desde.day() == 1 && hasta == QDate(desde.year(), 12, 31);
}
QString fechaCorta(const QDate& d) { return QStringLiteral("%1/%2/%3").arg(d.day()).arg(d.month()).arg(d.year()); }
}

QPair<QDate, QDate> periodoAnterior(const QDate& desde, const QDate& hasta)
{
    if (!desde.isValid() || !hasta.isValid() || hasta < desde) return {};
    if (esMesEntero(desde, hasta)) {
        const QDate d = desde.addMonths(-1);
        return {d, d.addMonths(1).addDays(-1)};
    }
    if (esAnioEntero(desde, hasta)) return {QDate(desde.year() - 1, 1, 1), QDate(desde.year() - 1, 12, 31)};
    const qint64 dias = desde.daysTo(hasta) + 1;
    return {desde.addDays(-dias), desde.addDays(-1)};
}

QPair<QDate, QDate> rangoMovido(const QDate& desde, const QDate& hasta, int pasos)
{
    if (!desde.isValid() || !hasta.isValid() || hasta < desde) return {};
    if (desde.day() == 1 && hasta.day() == hasta.daysInMonth()) {
        const int meses = (hasta.year() - desde.year()) * 12 + hasta.month() - desde.month() + 1;
        const QDate d = desde.addMonths(pasos * meses);
        return {d, d.addMonths(meses).addDays(-1)};
    }
    const qint64 dias = desde.daysTo(hasta) + 1;
    return {desde.addDays(pasos * dias), hasta.addDays(pasos * dias)};
}

QString nombrePeriodo(const QDate& desde, const QDate& hasta)
{
    if (!desde.isValid() || !hasta.isValid()) return QStringLiteral("todo");
    if (esMesEntero(desde, hasta)) return QStringLiteral("%1 %2").arg(nombreMes(desde.month()).toLower()).arg(desde.year());
    if (esAnioEntero(desde, hasta)) return QString::number(desde.year());
    if (desde == hasta) return fechaCorta(desde);
    return QStringLiteral("%1 al %2").arg(fechaCorta(desde), fechaCorta(hasta));
}

QString textoPeriodoAnterior(const QDate& desde, const QDate& hasta)
{
    const auto [d, h] = periodoAnterior(desde, hasta);
    if (!d.isValid()) return QString();
    if (esMesEntero(desde, hasta) || esAnioEntero(desde, hasta)) return QStringLiteral("en ") + nombrePeriodo(d, h);
    const qint64 dias = d.daysTo(h) + 1;
    return dias == 1 ? QStringLiteral("el día anterior") : QStringLiteral("en los %1 días anteriores").arg(dias);
}

QString textoVariacion(Centavos actual, Centavos anterior)
{
    if (anterior <= 0) return QString();
    if (actual == anterior) return QStringLiteral("igual");
    const double pct = 100.0 * double(actual - anterior) / double(anterior);
    const long redondeado = std::lround(std::fabs(pct));
    if (redondeado == 0) return QStringLiteral("igual");
    return QStringLiteral("%1%2 %").arg(pct > 0 ? QStringLiteral("+") : QStringLiteral("−")).arg(redondeado);
}

int signoVariacion(Centavos actual, Centavos anterior)
{
    const QString t = textoVariacion(actual, anterior);
    if (t.isEmpty() || t == QLatin1String("igual")) return 0;
    return actual > anterior ? 1 : -1;
}
