#include "dinero.h"
#include <QRegularExpression>
#include <limits>

namespace {

QString agruparMiles(QString digitos)
{
    QString salida;
    for (int i = 0; i < digitos.size(); ++i) {
        if (i > 0 && (digitos.size() - i) % 3 == 0) salida += QLatin1Char('.');
        salida += digitos[i];
    }
    return salida;
}

QString sinSigno(Centavos c)       // |c| como "1.234,56"
{
    const quint64 a = c < 0 ? quint64(-(c + 1)) + 1 : quint64(c);   // sin desbordar en el mínimo
    return agruparMiles(QString::number(a / 100)) + QLatin1Char(',') + QStringLiteral("%1").arg(a % 100, 2, 10, QLatin1Char('0'));
}

} // namespace

QString formatearPesos(Centavos c)
{
    return (c < 0 ? QStringLiteral("-$ ") : QStringLiteral("$ ")) + sinSigno(c);
}

QString formatearPesosConSigno(Centavos c)
{
    if (c == 0) return formatearPesos(0);
    return (c < 0 ? QStringLiteral("-$ ") : QStringLiteral("+$ ")) + sinSigno(c);
}

QString formatearPesosCorto(Centavos c)
{
    const QString signo = c < 0 ? QStringLiteral("-") : QString();
    const double pesos = std::abs(double(c)) / 100.0;
    auto conDecimal = [](double v) {             // 1.5 -> "1,5" | 2.0 -> "2"
        QString s = QString::number(v, 'f', 1);
        if (s.endsWith(QLatin1String(".0"))) s.chop(2);
        return s.replace(QLatin1Char('.'), QLatin1Char(','));
    };
    if (pesos >= 1e9) return signo + QStringLiteral("$ ") + conDecimal(pesos / 1e9) + QStringLiteral(" mil M");
    if (pesos >= 1e6) return signo + QStringLiteral("$ ") + conDecimal(pesos / 1e6) + QStringLiteral(" M");
    if (pesos >= 1e4) return signo + QStringLiteral("$ ") + conDecimal(pesos / 1e3) + QStringLiteral(" mil");
    return signo + QStringLiteral("$ ") + agruparMiles(QString::number(qint64(pesos + 0.5)));
}

QString formatearNumero(Centavos c)
{
    const quint64 a = c < 0 ? quint64(-(c + 1)) + 1 : quint64(c);
    return (c < 0 ? QStringLiteral("-") : QString()) + QString::number(a / 100) + QLatin1Char(',')
           + QStringLiteral("%1").arg(a % 100, 2, 10, QLatin1Char('0'));
}

bool parsearPesos(const QString& texto, Centavos* centavos, bool conSigno)
{
    QString t = texto;
    t.remove(QLatin1Char('$'));
    t.remove(QLatin1Char(' '));
    t.remove(QChar(0x00A0));
    t = t.trimmed();
    bool negativo = false;
    if (t.startsWith(QLatin1Char('-')) || t.startsWith(QChar(0x2212))) {
        if (!conSigno) return false;
        negativo = true;
        t.remove(0, 1);
        t.remove(QLatin1Char('$'));
    } else if (t.startsWith(QLatin1Char('+'))) {
        t.remove(0, 1);
    }
    if (t.isEmpty()) return false;

    // Separar parte entera y decimal según el formato argentino (con tolerancia al punto decimal)
    static const QRegularExpression soloMiles(QStringLiteral(R"(^\d{1,3}(\.\d{3})+$)"));
    QString entera = t, decimal;
    if (t.contains(QLatin1Char(',')) && t.contains(QLatin1Char('.'))) {
        if (t.lastIndexOf(QLatin1Char(',')) < t.lastIndexOf(QLatin1Char('.'))) return false;   // "1,234.56": no es formato argentino
        t.remove(QLatin1Char('.'));                                   // 1.234,56
        entera = t.section(QLatin1Char(','), 0, 0);
        decimal = t.section(QLatin1Char(','), 1);
    } else if (t.contains(QLatin1Char(','))) {                        // 1234,5
        entera = t.section(QLatin1Char(','), 0, 0);
        decimal = t.section(QLatin1Char(','), 1);
    } else if (soloMiles.match(t).hasMatch()) {                       // 1.234 -> mil doscientos
        entera = t.remove(QLatin1Char('.'));
    } else if (t.count(QLatin1Char('.')) == 1) {                      // 1234.56 (punto decimal)
        entera = t.section(QLatin1Char('.'), 0, 0);
        decimal = t.section(QLatin1Char('.'), 1);
    }
    static const QRegularExpression digitos(QStringLiteral(R"(^\d*$)"));
    if (!digitos.match(entera).hasMatch() || !digitos.match(decimal).hasMatch()) return false;
    if (entera.isEmpty() && decimal.isEmpty()) return false;
    if (decimal.size() > 2) return false;                             // más de 2 decimales: no son centavos
    if (entera.size() > 15) return false;                             // fuera de rango
    const Centavos valor = entera.toLongLong() * 100 + (decimal.isEmpty() ? 0 : decimal.leftJustified(2, QLatin1Char('0')).toLongLong());
    if (centavos) *centavos = negativo ? -valor : valor;
    return true;
}

QString nombreMes(int mes)
{
    static const char* const nombres[] = {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio",
                                          "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"};
    return mes >= 1 && mes <= 12 ? QString::fromUtf8(nombres[mes - 1]) : QString();
}

QString nombreMesCorto(int mes)
{
    return nombreMes(mes).left(3);
}

bool parsearFecha(const QString& texto, QDate* fecha)
{
    const QString t = texto.trimmed();
    static const char* const formatos[] = {"dd/MM/yyyy", "d/M/yyyy", "yyyy-MM-dd", "dd-MM-yyyy", "d-M-yyyy", "dd/MM/yy", "d/M/yy"};
    for (const char* f : formatos) {
        const QString formato = QString::fromLatin1(f);
        QDate d = QDate::fromString(t, formato);
        if (!d.isValid()) continue;
        if (formato.contains(QLatin1String("yyyy")) && d.year() < 1000) continue;   // "26" no es el año 26
        if (formato.endsWith(QLatin1String("/yy"))) d = d.addYears(100);   // Qt toma "26" como 1926
        if (fecha) *fecha = d;
        return true;
    }
    return false;
}

QString normalizar(const QString& texto)
{
    const QString d = texto.normalized(QString::NormalizationForm_D);
    QString salida;
    for (const QChar c : d) if (c.category() != QChar::Mark_NonSpacing) salida += c;
    return salida.toCaseFolded();
}
