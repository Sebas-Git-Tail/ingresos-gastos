#include "csv.h"
#include "dinero.h"

#include <QLocale>
#include <QStringDecoder>
#include <cmath>

namespace {

enum Columna { ColFecha, ColTipo, ColCategoria, ColMonto, ColDescripcion, ColCuenta, ColNinguna };

Columna columnaDeTitulo(const QString& titulo)
{
    const QString t = normalizar(titulo).simplified();
    // también los títulos en inglés que usan otras apps (Date, Type, Category, Amount, Description / Note)
    if (t.startsWith(QLatin1String("fecha")) || t == QLatin1String("dia") || t == QLatin1String("date")) return ColFecha;
    if (t.startsWith(QLatin1String("tipo")) || t == QLatin1String("movimiento") || t == QLatin1String("type")) return ColTipo;
    if (t.startsWith(QLatin1String("categ")) || t.startsWith(QLatin1String("rubro"))) return ColCategoria;
    if (t.startsWith(QLatin1String("monto")) || t.startsWith(QLatin1String("importe")) || t.startsWith(QLatin1String("valor"))
        || t.startsWith(QLatin1String("cantidad")) || t == QLatin1String("amount")) return ColMonto;
    if (t.startsWith(QLatin1String("descrip")) || t.startsWith(QLatin1String("detalle")) || t.startsWith(QLatin1String("concepto"))
        || t.startsWith(QLatin1String("nota")) || t.startsWith(QLatin1String("comentario")) || t == QLatin1String("note")) return ColDescripcion;
    if (t == QLatin1String("cuenta") || t == QLatin1String("account") || t.startsWith(QLatin1String("billetera"))) return ColCuenta;
    return ColNinguna;
}

bool tipoDesdeTexto(const QString& texto, Tipo* tipo)
{
    const QString t = normalizar(texto).trimmed();
    if (t.startsWith(QLatin1String("ingreso")) || t == QLatin1String("i") || t == QLatin1String("entrada") || t == QLatin1String("+")
        || t == QLatin1String("income")) {
        *tipo = Tipo::Ingreso;
        return true;
    }
    if (t.startsWith(QLatin1String("gasto")) || t.startsWith(QLatin1String("egreso")) || t == QLatin1String("g") || t == QLatin1String("e")
        || t == QLatin1String("salida") || t == QLatin1String("-") || t == QLatin1String("expense")) {
        *tipo = Tipo::Gasto;
        return true;
    }
    return false;
}

QString campo(const QString& s)
{
    if (!s.contains(QLatin1Char(';')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n'))
        && !s.contains(QLatin1Char('\r')))
        return s;
    QString c = s;
    c.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + c + QLatin1Char('"');
}

// El separador es el que más aparece en la primera línea (fuera de comillas)
QChar detectarSeparador(const QString& texto)
{
    const QString primera = texto.section(QLatin1Char('\n'), 0, 0);
    int puntoYComa = 0, coma = 0, tab = 0;
    bool comillas = false;
    for (const QChar c : primera) {
        if (c == QLatin1Char('"')) comillas = !comillas;
        else if (!comillas && c == QLatin1Char(';')) ++puntoYComa;
        else if (!comillas && c == QLatin1Char(',')) ++coma;
        else if (!comillas && c == QLatin1Char('\t')) ++tab;
    }
    if (tab > puntoYComa && tab > coma) return QLatin1Char('\t');
    if (coma > puntoYComa) return QLatin1Char(',');
    return QLatin1Char(';');
}


// ---------------------------------------------------------------- ExpenLess!
//
// El reporte CSV de ExpenLess (Android) tiene títulos en inglés y NO usa comillas: si la descripción
// tiene comas, quedan columnas de más, y si tiene un Enter, el movimiento sigue en las líneas siguientes.
//   Account Id,Account Description,Transaction Date,Creation date,Transaction Id,Transaction Name,
//   Transaction Description,Currency Id,Currency symbol,Expense or Income,Amount,Category Id,Category Name,
//   Subcategory Id,Subcategory Name
// Por eso se lee distinto: las columnas de antes del nombre y las de después de la descripción son fijas
// (nunca tienen comas) y lo que queda en el medio es el texto. ExpenLess repite el mismo texto en
// "Name" y "Description", así que si las dos mitades son iguales se usa una sola.

bool esReporteExpenless(const QString& primeraLinea)
{
    const QString t = normalizar(primeraLinea);
    return t.contains(QLatin1String("transaction date")) && t.contains(QLatin1String("expense or income"))
           && t.contains(QLatin1String("transaction name")) && t.contains(QLatin1String("transaction description"));
}

LecturaCsv leerExpenless(const QString& texto)
{
    LecturaCsv res;
    const QStringList lineas = texto.split(QLatin1Char('\n'));
    QStringList titulos;
    for (const QString& s : lineas.first().split(QLatin1Char(','))) titulos << normalizar(s).simplified();
    const int iNombre = titulos.indexOf(QStringLiteral("transaction name"));
    const int iDesc = titulos.indexOf(QStringLiteral("transaction description"));
    const int iFecha = titulos.indexOf(QStringLiteral("transaction date"));
    const int iTipo = titulos.indexOf(QStringLiteral("expense or income"));
    const int iMonto = titulos.indexOf(QStringLiteral("amount"));
    const int iCat = titulos.indexOf(QStringLiteral("category name"));
    const int iSub = titulos.indexOf(QStringLiteral("subcategory name"));
    const int iMoneda = titulos.indexOf(QStringLiteral("currency symbol"));
    if (iDesc != iNombre + 1 || iFecha < 0 || iFecha >= iNombre || iTipo <= iDesc || iMonto <= iDesc || iCat <= iDesc) {
        res.error = QStringLiteral("Parece un reporte de ExpenLess, pero sus columnas no son las esperadas.");
        return res;
    }
    const int antes = iNombre;                         // columnas fijas al principio
    const int despues = titulos.size() - iDesc - 1;    // columnas fijas al final
    auto deAtras = [&](const QStringList& p, int indice) {   // columna del final, contando desde atrás
        return indice < 0 ? QString() : p.value(p.size() - (titulos.size() - indice)).trimmed();
    };

    // 1) Juntar las líneas de cada movimiento: uno nuevo empieza con «número,cuenta,fecha,...»
    struct Registro { int linea; QString texto; };
    QList<Registro> registros;
    for (int i = 1; i < lineas.size(); ++i) {
        QString l = lineas[i];
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        const QStringList p = l.split(QLatin1Char(','));
        bool numero = false;
        p.first().trimmed().toLongLong(&numero);
        if (numero && p.size() > iFecha && parsearFecha(p[iFecha], nullptr)) {
            registros << Registro{i + 1, l};
        } else if (!registros.isEmpty()) {
            registros.last().texto += QLatin1Char('\n') + l;     // sigue el movimiento anterior
        } else if (!l.trimmed().isEmpty()) {
            res.avisos << QStringLiteral("Línea %1: no se entiende, no se importó.").arg(i + 1);
        }
    }

    // 2) Leer cada movimiento
    for (const Registro& r : registros) {
        const QStringList p = r.texto.split(QLatin1Char(','));
        if (p.size() < antes + despues) {
            res.avisos << QStringLiteral("Línea %1: faltan columnas, no se importó.").arg(r.linea);
            continue;
        }
        FilaImportada fila;
        fila.linea = r.linea;
        parsearFecha(p[iFecha], &fila.fecha);

        const QString moneda = deAtras(p, iMoneda);
        if (iMoneda >= 0 && !moneda.isEmpty() && moneda != QLatin1String("$")) {
            res.avisos << QStringLiteral("Línea %1: está en %2, no en pesos; no se importó.").arg(r.linea).arg(moneda);
            continue;
        }
        const QString tipo = deAtras(p, iTipo);
        if (tipo == QLatin1String("1")) fila.tipo = Tipo::Ingreso;
        else if (tipo == QLatin1String("-1")) fila.tipo = Tipo::Gasto;
        else {
            res.avisos << QStringLiteral("Línea %1: tipo «%2» no es ingreso (1) ni gasto (-1), no se importó.").arg(r.linea).arg(tipo);
            continue;
        }
        // Monto con punto decimal ("97000", "1500.5", y por las dudas "1.0E7")
        const QString textoMonto = deAtras(p, iMonto);
        bool ok = false;
        const double valor = QLocale::c().toDouble(textoMonto, &ok);
        if (!ok || !std::isfinite(valor) || std::abs(valor) >= 1e13 || std::llround(std::abs(valor) * 100) == 0) {
            res.avisos << QStringLiteral("Línea %1: monto no válido «%2», no se importó.").arg(r.linea).arg(textoMonto);
            continue;
        }
        fila.monto = std::llround(std::abs(valor) * 100);

        fila.categoria = deAtras(p, iCat).simplified();
        if (normalizar(fila.categoria) == QLatin1String("sin categoria")) fila.categoria.clear();   // va a «Varios»

        // Texto del medio (nombre + descripción)
        QStringList medio;
        for (int j = antes; j < p.size() - despues; ++j) medio << p[j].simplified();
        const int n = medio.size();
        QString nota;
        if (n % 2 == 0 && medio.mid(0, n / 2) == medio.mid(n / 2)) nota = medio.mid(0, n / 2).join(QStringLiteral(", "));
        else nota = medio.join(QStringLiteral(", "));
        nota = nota.trimmed();
        if (nota == QLatin1String(",")) nota.clear();

        // La subcategoría (Gas, Agua, Medicamentos...) va al principio de la descripción;
        // las «<Categoría> General» no dicen nada y se omiten
        const QString sub = deAtras(p, iSub).simplified();
        const QString subNorm = normalizar(sub);
        const QString catNorm = normalizar(fila.categoria);
        const bool subInutil = sub.isEmpty() || subNorm == catNorm || subNorm == catNorm + QLatin1String(" general")
                               || subNorm == QLatin1String("sin categoria") || subNorm == QLatin1String("general");
        if (!subInutil) fila.descripcion = nota.isEmpty() ? sub : sub + QStringLiteral(" — ") + nota;
        else fila.descripcion = nota;

        res.filas << fila;
    }
    if (registros.isEmpty()) {
        res.error = QStringLiteral("El reporte de ExpenLess no tiene movimientos.");
        return res;
    }
    res.ok = true;
    return res;
}

} // namespace

QString movimientosACsv(const QList<Movimiento>& movimientos)
{
    QStringList lineas;
    lineas << QStringLiteral("Fecha;Tipo;Categoría;Monto;Descripción;Cuenta");
    for (const Movimiento& m : movimientos) {
        lineas << QStringList{m.fecha.toString(QStringLiteral("dd/MM/yyyy")), textoTipo(m.tipo), campo(m.categoria),
                              formatearNumero(m.monto), campo(m.descripcion), campo(m.cuenta)}
                      .join(QLatin1Char(';'));
    }
    return lineas.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QByteArray csvParaGuardar(const QString& csv)
{
    return QByteArray("\xEF\xBB\xBF") + csv.toUtf8();
}

QString decodificarArchivo(const QByteArray& datos)
{
    QByteArray d = datos;
    if (d.startsWith("\xEF\xBB\xBF")) d.remove(0, 3);
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString texto = utf8.decode(d);
    if (!utf8.hasError()) return texto;
    // No es UTF-8: Excel en Windows guarda los CSV en Windows-1252 (casi igual a Latin-1)
    QStringDecoder windows("windows-1252");
    if (windows.isValid()) return windows.decode(d);
    return QString::fromLatin1(d);
}

QList<QStringList> partirCsv(const QString& texto, QChar separador)
{
    QList<QStringList> filas;
    QStringList fila;
    QString actual;
    bool comillas = false, inicio = true;
    const int n = texto.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = texto[i];
        if (comillas) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && texto[i + 1] == QLatin1Char('"')) { actual += QLatin1Char('"'); ++i; }
                else comillas = false;
            } else {
                actual += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && inicio) {
            comillas = true;
            inicio = false;
        } else if (c == separador) {
            fila << actual;
            actual.clear();
            inicio = true;
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < n && texto[i + 1] == QLatin1Char('\n')) ++i;
            fila << actual;
            filas << fila;
            fila.clear();
            actual.clear();
            inicio = true;
        } else {
            actual += c;
            inicio = false;
        }
    }
    if (!actual.isEmpty() || !fila.isEmpty()) {
        fila << actual;
        filas << fila;
    }
    return filas;
}

LecturaCsv leerCsv(const QString& texto)
{
    LecturaCsv res;
    QString t = texto;
    if (t.startsWith(QChar(0xFEFF))) t.remove(0, 1);
    if (esReporteExpenless(t.section(QLatin1Char('\n'), 0, 0))) return leerExpenless(t);
    QList<QStringList> filas = partirCsv(t, detectarSeparador(t));
    if (filas.isEmpty()) {
        res.error = QStringLiteral("El archivo está vacío.");
        return res;
    }

    // Títulos: hacen falta al menos la fecha y el monto
    QList<Columna> columnas;
    bool hayFecha = false, hayMonto = false, hayTipo = false;
    for (const QString& titulo : filas.first()) {
        const Columna c = columnaDeTitulo(titulo);
        columnas << c;
        hayFecha |= c == ColFecha;
        hayMonto |= c == ColMonto;
        hayTipo |= c == ColTipo;
    }
    if (!hayFecha || !hayMonto) {
        res.error = QStringLiteral("No encontré las columnas «Fecha» y «Monto» en la primera línea del archivo.");
        return res;
    }

    for (int i = 1; i < filas.size(); ++i) {
        const QStringList& f = filas[i];
        if (f.join(QString()).trimmed().isEmpty()) continue;           // renglón vacío
        const int linea = i + 1;
        QString textoFecha, textoTipo, textoMonto;
        FilaImportada fila;
        fila.linea = linea;
        for (int j = 0; j < f.size() && j < columnas.size(); ++j) {
            const QString v = f[j].trimmed();
            switch (columnas[j]) {
            case ColFecha: textoFecha = v; break;
            case ColTipo: textoTipo = v; break;
            case ColCategoria: fila.categoria = v; break;
            case ColMonto: textoMonto = v; break;
            case ColDescripcion: fila.descripcion = v; break;
            case ColCuenta: fila.cuenta = v; break;
            case ColNinguna: break;
            }
        }
        if (!parsearFecha(textoFecha, &fila.fecha)) {
            res.avisos << QStringLiteral("Línea %1: fecha no válida «%2», no se importó.").arg(linea).arg(textoFecha);
            continue;
        }
        Centavos monto = 0;
        if (!parsearPesos(textoMonto, &monto, true) || monto == 0) {
            res.avisos << QStringLiteral("Línea %1: monto no válido «%2», no se importó.").arg(linea).arg(textoMonto);
            continue;
        }
        if (hayTipo) {
            if (!tipoDesdeTexto(textoTipo, &fila.tipo)) {
                res.avisos << QStringLiteral("Línea %1: tipo «%2» no es Ingreso ni Gasto, no se importó.").arg(linea).arg(textoTipo);
                continue;
            }
        } else {
            fila.tipo = monto < 0 ? Tipo::Gasto : Tipo::Ingreso;          // sin columna Tipo, el signo manda
        }
        fila.monto = monto < 0 ? -monto : monto;
        res.filas << fila;
    }
    res.ok = true;
    return res;
}
