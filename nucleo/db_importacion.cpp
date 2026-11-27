// Base de datos: importar movimientos (CSV) y copia de seguridad.
#include "db_interno.h"
#include <QRegularExpression>

using namespace dbi;

// ====================================================================== importar

ResultadoImportacion Cuentas::procesarImportacion(const QList<FilaImportada>& filas, bool guardar)
{
    ResultadoImportacion r;
    // Cuántas veces está cada movimiento en la base: cada fila igual del archivo «gasta» una.
    // (Así dos cafés iguales el mismo día no se toman como repetidos si en la base hay uno solo.)
    QHash<QString, int> existentes;
    for (const Movimiento& m : movimientos())
        ++existentes[claveMovimiento(m.fecha, m.tipo, m.categoria, m.monto, m.descripcion)];
    QHash<QString, int> vistasEnArchivo;

    // Cuentas por nombre (las que nombra el archivo y no están se crean)
    QHash<QString, qint64> idsCuenta;
    for (const CuentaDinero& c : cuentasDinero()) idsCuenta.insert(normalizar(c.nombre), c.id);
    const qint64 cuentaPorDefecto = cuentaVista_ ? cuentaVista_ : cuentaPrincipal();

    // Categorías por nombre (las que se vayan creando también). Con las de las transferencias: un CSV exportado
    // con transferencias vuelve a entrar en ellas (y no crea otra categoría «Transferencia»)
    QHash<QString, qint64> ids;
    for (const Categoria& c : todasLasCategorias()) ids.insert(textoBD(c.tipo) + QChar(0x1F) + normalizar(c.nombre), c.id);

    std::optional<Transaccion> t;
    if (guardar) t.emplace(db_);
    for (const FilaImportada& f : filas) {
        const QString nombreCategoria = f.categoria.simplified().isEmpty()
                                            ? (f.tipo == Tipo::Ingreso ? QStringLiteral("Ingresos Varios") : QStringLiteral("Gastos Varios"))
                                            : f.categoria.simplified();
        const QString clave = claveMovimiento(f.fecha, f.tipo, nombreCategoria, f.monto, f.descripcion);
        const int vez = ++vistasEnArchivo[clave];
        if (existentes.value(clave) > 0) {
            --existentes[clave];
            ++r.repetidos;
            continue;
        }
        const QString claveCategoria = textoBD(f.tipo) + QChar(0x1F) + normalizar(nombreCategoria);
        qint64 idCategoria = ids.value(claveCategoria);
        if (idCategoria == 0 && normalizar(nombreCategoria) == normalizar(NOMBRE_TRANSFERENCIA)) {
            // Un CSV exportado con transferencias en una base que todavía no tiene sus categorías: se crean
            idCategoria = -1;
            if (guardar) {
                const auto [salida, entrada] = asegurarCategoriasTransferencia();
                idCategoria = f.tipo == Tipo::Gasto ? salida : entrada;
            }
            ids.insert(claveCategoria, idCategoria);
        }
        if (idCategoria == 0) {
            r.categoriasNuevas << QStringLiteral("%1: %2").arg(textoTipo(f.tipo), nombreCategoria);
            idCategoria = guardar ? insertarCategoria(nombreCategoria, f.tipo) : -1;   // en la vista previa alcanza con contarla
            ids.insert(claveCategoria, idCategoria);
        }
        qint64 idCuenta = cuentaPorDefecto;
        if (const QString nombreCuenta = f.cuenta.simplified(); !nombreCuenta.isEmpty()) {
            idCuenta = idsCuenta.value(normalizar(nombreCuenta));
            if (idCuenta == 0) {
                r.cuentasNuevas << nombreCuenta;
                if (guardar) {
                    QSqlQuery c = consulta(db_, "INSERT INTO cuentas_dinero (uuid, nombre) VALUES (?, ?)", {uuidNuevo(), nombreLibreCuenta(nombreCuenta, 0)});
                    idCuenta = c.lastInsertId().toLongLong();
                    marcar("cuentas_dinero", idCuenta);
                } else {
                    idCuenta = -1;
                }
                idsCuenta.insert(normalizar(nombreCuenta), idCuenta);
            }
        }
        if (guardar) {
            // uuid sacado del contenido: si el mismo archivo se importa en la PC y en el celular,
            // al sincronizar son los mismos movimientos y no quedan duplicados
            const QString uuid = QStringLiteral("imp-") + hash32(clave + QLatin1Char('#') + QString::number(vez));
            const bool yaEsta = valor(db_, "SELECT COUNT(*) FROM movimientos WHERE uuid = ?", {uuid}).toInt() > 0;
            QSqlQuery q = consulta(db_, "INSERT INTO movimientos (uuid, fecha, categoria_id, monto, descripcion, cuenta_id) VALUES (?, ?, ?, ?, ?, ?)",
                                   {yaEsta ? uuidNuevo() : uuid, fechaBD(f.fecha), idCategoria, f.monto, texto(f.descripcion.trimmed()),
                                    idCuenta});
            marcar("movimientos", q.lastInsertId().toLongLong());
        }
        ++r.nuevos;
    }
    if (t) t->confirmar();
    return r;
}

ResultadoImportacion Cuentas::previsualizarImportacion(const QList<FilaImportada>& filas) const
{
    try {
        return const_cast<Cuentas*>(this)->procesarImportacion(filas, false);   // sin guardar: no cambia nada
    } catch (const std::exception&) {
        return {};
    }
}

ResultadoImportacion Cuentas::importar(const QList<FilaImportada>& filas)
{
    try {
        return procesarImportacion(filas, true);
    } catch (const std::exception&) {
        return {};
    }
}

QString Cuentas::carpetaRespaldos(const QString& rutaBase)
{
    return QFileInfo(rutaBase).absolutePath() + QStringLiteral("/respaldos");
}

ResultadoRespaldo Cuentas::respaldoAutomatico(const QString& carpeta, const QDate& hoy, int cuantas)
{
    ResultadoRespaldo r;
    if (ruta_ == QLatin1String(":memory:") || !hoy.isValid()) return r;
    QDir dir(carpeta);
    if (!dir.mkpath(QStringLiteral("."))) {
        r.ok = false;
        r.error = QStringLiteral("No se pudo crear la carpeta %1").arg(carpeta);
        return r;
    }
    r.ruta = dir.filePath(QStringLiteral("cuentas-%1.db").arg(hoy.toString(Qt::ISODate)));
    if (!QFileInfo::exists(r.ruta)) {
        // Primero a un archivo aparte y después se renombra: si se corta a la mitad, no queda una copia rota
        const QString temporal = r.ruta + QStringLiteral(".tmp");
        const Resultado b = backup(temporal);
        if (!b.ok || !QFile::rename(temporal, r.ruta)) {
            QFile::remove(temporal);
            r.ok = false;
            r.error = b.ok ? QStringLiteral("No se pudo guardar %1").arg(r.ruta) : b.error;
            return r;
        }
        r.hecha = true;
    }
    // Quedan las últimas: los nombres llevan la fecha, así que ordenados por nombre quedan de la más vieja a la más nueva
    static const QRegularExpression patron(QStringLiteral(R"(^cuentas-\d{4}-\d{2}-\d{2}\.db$)"));
    QStringList copias;
    for (const QString& n : dir.entryList({QStringLiteral("cuentas-*.db")}, QDir::Files, QDir::Name))
        if (patron.match(n).hasMatch()) copias << n;
    while (copias.size() > std::max(1, cuantas))
        if (dir.remove(copias.takeFirst())) ++r.borradas;
    return r;
}

Resultado Cuentas::backup(const QString& destino)
{
    try {
        QDir().mkpath(QFileInfo(destino).absolutePath());
        QFile::remove(destino);
        QString literal = destino;
        literal.replace(QLatin1Char('\''), QStringLiteral("''"));
        consulta(db_, QStringLiteral("VACUUM INTO '%1'").arg(literal));
        return {true, QString(), 0};
    } catch (const std::exception& e) {
        return {false, QString::fromUtf8(e.what()), 0};
    }
}
