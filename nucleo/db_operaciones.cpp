// Base de datos: categorías, movimientos, saldos y totales, cuentas de dinero, presupuestos y movimientos fijos.
#include "db_interno.h"
#include "reportes.h"

using namespace dbi;

// ====================================================================== categorías

namespace {
QList<Categoria> listarCategorias(const QSqlDatabase& db, std::optional<Tipo> tipo, bool conTransferencias)
{
    QList<Categoria> lista;
    try {
        QString sql = QStringLiteral(
            "SELECT c.id, c.nombre, c.tipo, c.color, "
            "(SELECT COUNT(*) FROM movimientos m WHERE m.categoria_id = c.id AND m.borrado = 0), c.presupuesto "
            "FROM categorias c WHERE c.borrado = 0 ");
        if (!conTransferencias) sql += QStringLiteral(SIN_TRANSFERENCIAS);
        QVariantList params;
        if (tipo) {
            sql += QStringLiteral("AND c.tipo = ? ");
            params << textoBD(*tipo);
        }
        QSqlQuery q = consulta(db, sql, params);
        while (q.next())
            lista << Categoria{q.value(0).toLongLong(), q.value(1).toString(), tipoBD(q.value(2).toString()), q.value(3).toInt(),
                               q.value(4).toInt(), q.value(5).toLongLong()};
    } catch (const std::exception&) {
    }
    ordenarCategorias(lista);       // así una categoría nueva (o renombrada) queda en su lugar sin hacer nada más
    return lista;
}
}

QList<Categoria> Cuentas::categorias(std::optional<Tipo> tipo) const
{
    return listarCategorias(db_, tipo, false);
}

// También las dos internas de las transferencias: para que nadie más use su nombre (crear, renombrar,
// sincronizar) y para que un CSV exportado con transferencias vuelva a entrar en ellas
QList<Categoria> Cuentas::todasLasCategorias(std::optional<Tipo> tipo) const
{
    return listarCategorias(db_, tipo, true);
}

// La más antigua (Supermercado / Sueldo en una base nueva). No sirve «la primera de la lista»: por orden alfabético
// sería «Gastos Varios» / «Ingresos Varios», que quedan como opción y no como valor inicial.
qint64 Cuentas::categoriaInicial(Tipo tipo) const
{
    // Pedido del usuario: los ingresos nuevos arrancan en «Propinas Ariel» si existe
    if (tipo == Tipo::Ingreso)
        if (const auto propinas = buscarCategoria(QStringLiteral("Propinas Ariel"), Tipo::Ingreso)) return propinas->id;
    try {
        return valor(db_, "SELECT MIN(id) FROM categorias c WHERE borrado = 0 AND tipo = ?" SIN_TRANSFERENCIAS, {textoBD(tipo)}).toLongLong();   // sin ninguna: NULL, o sea 0
    } catch (const std::exception&) {
        return 0;
    }
}

std::optional<Categoria> Cuentas::categoria(qint64 id) const
{
    for (const Categoria& c : categorias()) if (c.id == id) return c;
    return std::nullopt;
}

std::optional<Categoria> Cuentas::buscarCategoria(const QString& nombre, Tipo tipo) const
{
    const QString buscado = normalizar(nombre.simplified());
    for (const Categoria& c : todasLasCategorias(tipo)) if (normalizar(c.nombre) == buscado) return c;
    return std::nullopt;
}

int Cuentas::colorLibre(Tipo tipo) const
{
    int usos[COLORES] = {};
    for (const Categoria& c : categorias(tipo)) if (c.color >= 0 && c.color < COLORES) ++usos[c.color];
    int mejor = 0;
    for (int i = 1; i < COLORES; ++i) if (usos[i] < usos[mejor]) mejor = i;
    return mejor;
}

// Crea (o revive, si estaba borrada) una categoría. Tiene que llamarse dentro de una transacción o sola.
qint64 Cuentas::insertarCategoria(const QString& nombre, Tipo tipo, const QString& uuidPedido)
{
    QString uuid = uuidPedido.isEmpty() ? uuidCategoria(tipo, nombre) : uuidPedido;
    QSqlQuery q = consulta(db_, "SELECT id, borrado FROM categorias WHERE uuid = ?", {uuid});
    if (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        if (q.value(1).toInt() == 1) {                          // estaba borrada: vuelve
            consulta(db_, "UPDATE categorias SET nombre = ?, color = ?, borrado = 0 WHERE id = ?", {nombre, colorLibre(tipo), id});
            marcar("categorias", id);
            return id;
        }
        uuid = uuidNuevo();                                     // ese uuid lo usa otra que se renombró
    }
    q = consulta(db_, "INSERT INTO categorias (uuid, nombre, tipo, color) VALUES (?, ?, ?, ?)", {uuid, nombre, textoBD(tipo), colorLibre(tipo)});
    const qint64 id = q.lastInsertId().toLongLong();
    marcar("categorias", id);
    return id;
}

Resultado Cuentas::crearCategoria(const QString& nombre, Tipo tipo)
{
    const QString n = nombre.simplified();
    if (n.isEmpty()) return {false, QStringLiteral("La categoría necesita un nombre."), 0};
    if (buscarCategoria(n, tipo)) return {false, QStringLiteral("Ya existe una categoría con ese nombre."), 0};
    try {
        Transaccion t(db_);
        const qint64 id = insertarCategoria(n, tipo);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), 0};
    }
}

Resultado Cuentas::renombrarCategoria(qint64 id, const QString& nombre)
{
    const QString n = nombre.simplified();
    if (n.isEmpty()) return {false, QStringLiteral("La categoría necesita un nombre."), id};
    const auto actual = categoria(id);
    if (!actual) return {false, QStringLiteral("La categoría ya no existe."), id};
    if (const auto otra = buscarCategoria(n, actual->tipo); otra && otra->id != id)
        return {false, QStringLiteral("Ya existe una categoría con ese nombre."), id};
    if (n == actual->nombre) return {true, QString(), id};
    try {
        Transaccion t(db_);
        consulta(db_, "UPDATE categorias SET nombre = ? WHERE id = ?", {n, id});
        marcar("categorias", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

Resultado Cuentas::cambiarColor(qint64 id, int color)
{
    if (color < 0 || color >= COLORES) return {false, QStringLiteral("Color no válido."), id};
    if (!categoria(id)) return {false, QStringLiteral("La categoría ya no existe."), id};
    try {
        Transaccion t(db_);
        consulta(db_, "UPDATE categorias SET color = ? WHERE id = ?", {color, id});
        marcar("categorias", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

Resultado Cuentas::eliminarCategoria(qint64 id, qint64 moverA)
{
    const auto cat = categoria(id);
    if (!cat) return {false, QStringLiteral("La categoría ya no existe."), id};
    if (cat->movimientos > 0) {
        const auto destino = moverA ? categoria(moverA) : std::nullopt;
        if (!destino) return {false, QStringLiteral("La categoría tiene %1 movimientos: elegí a qué categoría pasarlos.").arg(cat->movimientos), id};
        if (destino->id == id || destino->tipo != cat->tipo)
            return {false, QStringLiteral("Los movimientos solo se pueden pasar a otra categoría del mismo tipo."), id};
    }
    try {
        Transaccion t(db_);
        if (cat->movimientos > 0) {
            QList<qint64> ids;
            QSqlQuery q = consulta(db_, "SELECT id FROM movimientos WHERE categoria_id = ? AND borrado = 0", {id});
            while (q.next()) ids << q.value(0).toLongLong();
            for (qint64 m : ids) {
                consulta(db_, "UPDATE movimientos SET categoria_id = ? WHERE id = ?", {moverA, m});
                marcar("movimientos", m);
            }
        }
        consulta(db_, "UPDATE categorias SET borrado = 1 WHERE id = ?", {id});
        marcar("categorias", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

// ====================================================================== movimientos

QList<Movimiento> Cuentas::movimientos(const QDate& desde, const QDate& hasta) const
{
    QList<Movimiento> lista;
    try {
        QString sql = QString::fromLatin1(SELECT_MOVIMIENTO);
        QVariantList params;
        if (desde.isValid()) { sql += QStringLiteral("AND m.fecha >= ? "); params << fechaBD(desde); }
        if (hasta.isValid()) { sql += QStringLiteral("AND m.fecha <= ? "); params << fechaBD(hasta); }
        filtrarCuenta(sql, params);
        sql += QStringLiteral("ORDER BY m.fecha DESC, m.id DESC");
        QSqlQuery q = consulta(db_, sql, params);
        while (q.next()) lista << leerMovimiento(q);
    } catch (const std::exception&) {
    }
    return lista;
}

std::optional<Movimiento> Cuentas::movimiento(qint64 id) const
{
    try {
        QSqlQuery q = consulta(db_, QString::fromLatin1(SELECT_MOVIMIENTO) + QStringLiteral("AND m.id = ?"), {id});
        if (q.next()) return leerMovimiento(q);
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

static QString validarMovimiento(const Movimiento& m)
{
    if (!m.fecha.isValid()) return QStringLiteral("La fecha no es válida.");
    if (m.monto <= 0) return QStringLiteral("El monto tiene que ser mayor que cero.");
    if (m.categoriaId <= 0) return QStringLiteral("Elegí una categoría.");
    return QString();
}

Resultado Cuentas::crearMovimiento(const Movimiento& m)
{
    if (const QString e = validarMovimiento(m); !e.isEmpty()) return {false, e, 0};
    if (!categoria(m.categoriaId)) return {false, QStringLiteral("La categoría ya no existe."), 0};
    try {
        Transaccion t(db_);
        const qint64 cuenta = m.cuentaId > 0 ? m.cuentaId : cuentaPrincipal();
        if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {cuenta}).isValid())
            return {false, QStringLiteral("La cuenta ya no existe."), 0};
        QSqlQuery q = consulta(db_, "INSERT INTO movimientos (uuid, fecha, categoria_id, monto, descripcion, cuenta_id) VALUES (?, ?, ?, ?, ?, ?)",
                               {uuidNuevo(), fechaBD(m.fecha), m.categoriaId, m.monto, texto(m.descripcion.trimmed()), cuenta});
        const qint64 id = q.lastInsertId().toLongLong();
        marcar("movimientos", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), 0};
    }
}

Resultado Cuentas::actualizarMovimiento(const Movimiento& m)
{
    if (const QString e = validarMovimiento(m); !e.isEmpty()) return {false, e, m.id};
    if (!categoria(m.categoriaId)) return {false, QStringLiteral("La categoría ya no existe."), m.id};
    if (!movimiento(m.id)) return {false, QStringLiteral("El movimiento ya no existe."), m.id};
    try {
        Transaccion t(db_);
        const qint64 cuenta = m.cuentaId > 0 ? m.cuentaId : cuentaPrincipal();
        if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {cuenta}).isValid())
            return {false, QStringLiteral("La cuenta ya no existe."), m.id};
        consulta(db_, "UPDATE movimientos SET fecha = ?, categoria_id = ?, monto = ?, descripcion = ?, cuenta_id = ? WHERE id = ?",
                 {fechaBD(m.fecha), m.categoriaId, m.monto, texto(m.descripcion.trimmed()), cuenta, m.id});
        marcar("movimientos", m.id);
        t.confirmar();
        return {true, QString(), m.id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), m.id};
    }
}

Resultado Cuentas::eliminarMovimiento(qint64 id)
{
    if (!movimiento(id)) return {false, QStringLiteral("El movimiento ya no existe."), id};
    try {
        Transaccion t(db_);
        consulta(db_, "UPDATE movimientos SET borrado = 1 WHERE id = ?", {id});
        marcar("movimientos", id);
        // En una transferencia, también la otra mitad
        const QString pareja = uuidPareja(valor(db_, "SELECT uuid FROM movimientos WHERE id = ?", {id}).toString());
        if (!pareja.isEmpty()) {
            const QVariant otra = valor(db_, "SELECT id FROM movimientos WHERE uuid = ? AND borrado = 0", {pareja});
            if (otra.isValid()) {
                consulta(db_, "UPDATE movimientos SET borrado = 1 WHERE id = ?", {otra});
                marcar("movimientos", otra.toLongLong());
            }
        }
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

Resultado Cuentas::restaurarMovimiento(qint64 id)
{
    try {
        Transaccion t(db_);
        QSqlQuery q = consulta(db_, "UPDATE movimientos SET borrado = 0 WHERE id = ? AND borrado = 1", {id});
        if (q.numRowsAffected() == 0) return {false, QStringLiteral("El movimiento no se puede recuperar."), id};
        marcar("movimientos", id);
        const QString pareja = uuidPareja(valor(db_, "SELECT uuid FROM movimientos WHERE id = ?", {id}).toString());
        if (!pareja.isEmpty()) {
            const QVariant otra = valor(db_, "SELECT id FROM movimientos WHERE uuid = ? AND borrado = 1", {pareja});
            if (otra.isValid()) {
                consulta(db_, "UPDATE movimientos SET borrado = 0 WHERE id = ?", {otra});
                marcar("movimientos", otra.toLongLong());
            }
        }
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

// ====================================================================== transferencias

QPair<qint64, qint64> Cuentas::asegurarCategoriasTransferencia()
{
    const QString uuids[2] = {UUID_TRANSF_SALIDA, UUID_TRANSF_ENTRADA};
    const Tipo tipos[2] = {Tipo::Gasto, Tipo::Ingreso};
    qint64 ids[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        QSqlQuery q = consulta(db_, "SELECT id, borrado FROM categorias WHERE uuid = ?", {uuids[i]});
        if (q.next()) {
            ids[i] = q.value(0).toLongLong();
            if (q.value(1).toInt() == 1) {          // la borraron (en una versión vieja): vuelve
                consulta(db_, "UPDATE categorias SET nombre = ?, borrado = 0 WHERE id = ?",
                         {nombreLibre(NOMBRE_TRANSFERENCIA, tipos[i], ids[i]), ids[i]});
                marcar("categorias", ids[i]);
            }
            continue;
        }
        // Misma fecha (1) y sin autor en todos los dispositivos, como las categorías de fábrica: si la PC y el
        // celular las crean los dos, son la misma y no se pisan
        q = consulta(db_, "INSERT INTO categorias (uuid, nombre, tipo, color, modificado, autor, cambio) VALUES (?, ?, ?, ?, 1, '', ?)",
                     {uuids[i], nombreLibre(NOMBRE_TRANSFERENCIA, tipos[i], 0), textoBD(tipos[i]), COLORES - 1, siguienteCambio()});
        ids[i] = q.lastInsertId().toLongLong();
    }
    return {ids[0], ids[1]};
}

Resultado Cuentas::guardarTransferencia(const Transferencia& t)
{
    if (!t.fecha.isValid()) return {false, QStringLiteral("La fecha no es válida."), t.id};
    if (t.monto <= 0) return {false, QStringLiteral("El monto tiene que ser mayor que cero."), t.id};
    if (t.desde <= 0 || t.hasta <= 0) return {false, QStringLiteral("Elegí de qué cuenta sale la plata y a cuál entra."), t.id};
    if (t.desde == t.hasta) return {false, QStringLiteral("La cuenta de la que sale y a la que entra tienen que ser distintas."), t.id};
    try {
        for (qint64 c : {t.desde, t.hasta})
            if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {c}).isValid())
                return {false, QStringLiteral("La cuenta ya no existe."), t.id};
        Transaccion tr(db_);
        const auto [catSalida, catEntrada] = asegurarCategoriasTransferencia();
        const QString desc = texto(t.descripcion.trimmed());
        qint64 idSalida = 0, idEntrada = 0;
        if (t.id > 0) {
            QSqlQuery q = consulta(db_, "SELECT m.uuid, c.uuid FROM movimientos m JOIN categorias c ON c.id = m.categoria_id "
                                        "WHERE m.id = ? AND m.borrado = 0", {t.id});
            if (!q.next() || !esUuidTransferencia(q.value(1).toString()))
                return {false, QStringLiteral("La transferencia ya no existe."), t.id};
            const bool esSalida = q.value(1).toString() == UUID_TRANSF_SALIDA;
            const QString pareja = uuidPareja(q.value(0).toString());
            const QVariant otra = pareja.isEmpty() ? QVariant() : valor(db_, "SELECT id FROM movimientos WHERE uuid = ? AND borrado = 0", {pareja});
            if (otra.isValid()) {
                idSalida = esSalida ? t.id : otra.toLongLong();
                idEntrada = esSalida ? otra.toLongLong() : t.id;
            } else {
                // Una mitad sola (vino de un CSV, o la otra se borró en una versión vieja): se borra y se arma
                // una transferencia entera
                consulta(db_, "UPDATE movimientos SET borrado = 1 WHERE id = ?", {t.id});
                marcar("movimientos", t.id);
            }
        }
        if (idSalida && idEntrada) {
            const char* sql = "UPDATE movimientos SET fecha = ?, categoria_id = ?, monto = ?, descripcion = ?, cuenta_id = ? WHERE id = ?";
            consulta(db_, sql, {fechaBD(t.fecha), catSalida, t.monto, desc, t.desde, idSalida});
            consulta(db_, sql, {fechaBD(t.fecha), catEntrada, t.monto, desc, t.hasta, idEntrada});
        } else {
            const QString grupo = QStringLiteral("tr-") + uuidNuevo();
            const char* sql = "INSERT INTO movimientos (uuid, fecha, categoria_id, monto, descripcion, cuenta_id) VALUES (?, ?, ?, ?, ?, ?)";
            idSalida = consulta(db_, sql, {grupo + QStringLiteral("-s"), fechaBD(t.fecha), catSalida, t.monto, desc, t.desde})
                           .lastInsertId().toLongLong();
            idEntrada = consulta(db_, sql, {grupo + QStringLiteral("-e"), fechaBD(t.fecha), catEntrada, t.monto, desc, t.hasta})
                            .lastInsertId().toLongLong();
        }
        marcar("movimientos", idSalida);
        marcar("movimientos", idEntrada);
        tr.confirmar();
        return {true, QString(), idSalida};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), t.id};
    }
}

std::optional<Transferencia> Cuentas::transferencia(qint64 idMovimiento) const
{
    const auto m = movimiento(idMovimiento);
    if (!m || !m->transferencia) return std::nullopt;
    Transferencia t;
    t.id = m->id;
    t.fecha = m->fecha;
    t.monto = m->monto;
    t.descripcion = m->descripcion;
    t.desde = m->tipo == Tipo::Gasto ? m->cuentaId : m->cuentaContrariaId;     // 0 si la otra mitad no está
    t.hasta = m->tipo == Tipo::Gasto ? m->cuentaContrariaId : m->cuentaId;
    return t;
}

// ====================================================================== saldo y totales

Centavos Cuentas::saldoInicial() const
{
    try {
        if (cuentaVista_)
            return valor(db_, "SELECT saldo_inicial FROM cuentas_dinero WHERE id = ?", {cuentaVista_}).toLongLong();
        return valor(db_, "SELECT COALESCE(SUM(saldo_inicial), 0) FROM cuentas_dinero WHERE borrado = 0").toLongLong();
    } catch (const std::exception&) {
        return 0;
    }
}

void Cuentas::fijarSaldoInicial(Centavos c)
{
    fijarSaldoInicialCuenta(cuentaVista_ ? cuentaVista_ : cuentaPrincipal(), c);
}

void Cuentas::filtrarCuenta(QString& sql, QVariantList& params) const
{
    if (!cuentaVista_) return;
    sql += QStringLiteral("AND m.cuenta_id = ? ");
    params << cuentaVista_;
}

TotalPeriodo Cuentas::totales(const QDate& desde, const QDate& hasta) const
{
    TotalPeriodo t;
    try {
        QString sql = QStringLiteral("SELECT c.tipo, SUM(m.monto) FROM movimientos m JOIN categorias c ON c.id = m.categoria_id WHERE m.borrado = 0 "
                                     SIN_TRANSFERENCIAS);
        QVariantList params;
        if (desde.isValid()) { sql += QStringLiteral("AND m.fecha >= ? "); params << fechaBD(desde); }
        if (hasta.isValid()) { sql += QStringLiteral("AND m.fecha <= ? "); params << fechaBD(hasta); }
        filtrarCuenta(sql, params);
        sql += QStringLiteral("GROUP BY c.tipo");
        QSqlQuery q = consulta(db_, sql, params);
        while (q.next()) (tipoBD(q.value(0).toString()) == Tipo::Ingreso ? t.ingresos : t.gastos) = q.value(1).toLongLong();
    } catch (const std::exception&) {
    }
    return t;
}

Centavos Cuentas::saldo(const QDate& hasta) const
{
    return saldoInicial() + sumaHasta(hasta);
}

// Como totales() pero con las transferencias: mirando una cuenta, lo que salió o entró por una transferencia
// cambia su saldo (mirando todas, la salida y la entrada se compensan)
Centavos Cuentas::sumaHasta(const QDate& hasta) const
{
    try {
        QString sql = QStringLiteral("SELECT COALESCE(SUM(CASE WHEN c.tipo = 'ingreso' THEN m.monto ELSE -m.monto END), 0) "
                                     "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id WHERE m.borrado = 0 ");
        QVariantList params;
        if (hasta.isValid()) { sql += QStringLiteral("AND m.fecha <= ? "); params << fechaBD(hasta); }
        filtrarCuenta(sql, params);
        return valor(db_, sql, params).toLongLong();
    } catch (const std::exception&) {
        return 0;
    }
}

QVector<TotalPeriodo> Cuentas::totalesPorMes(int anio) const
{
    QVector<TotalPeriodo> meses(12);
    try {
        QString sql = QStringLiteral(
            "SELECT CAST(substr(m.fecha, 6, 2) AS INTEGER), c.tipo, SUM(m.monto) "
            "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id "
            "WHERE m.borrado = 0 AND m.fecha >= ? AND m.fecha <= ? " SIN_TRANSFERENCIAS);
        QVariantList params{fechaBD(QDate(anio, 1, 1)), fechaBD(QDate(anio, 12, 31))};
        filtrarCuenta(sql, params);
        QSqlQuery q = consulta(db_, sql + QStringLiteral("GROUP BY 1, 2"), params);
        while (q.next()) {
            const int mes = q.value(0).toInt();
            if (mes < 1 || mes > 12) continue;
            (tipoBD(q.value(1).toString()) == Tipo::Ingreso ? meses[mes - 1].ingresos : meses[mes - 1].gastos) = q.value(2).toLongLong();
        }
    } catch (const std::exception&) {
    }
    return meses;
}

QList<QPair<int, TotalPeriodo>> Cuentas::totalesPorAnio() const
{
    QList<QPair<int, TotalPeriodo>> anios;
    try {
        QString sql = QStringLiteral(
            "SELECT CAST(substr(m.fecha, 1, 4) AS INTEGER), c.tipo, SUM(m.monto) "
            "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id WHERE m.borrado = 0 " SIN_TRANSFERENCIAS);
        QVariantList params;
        filtrarCuenta(sql, params);
        QSqlQuery q = consulta(db_, sql + QStringLiteral("GROUP BY 1, 2 ORDER BY 1"), params);
        while (q.next()) {
            const int anio = q.value(0).toInt();
            if (anios.isEmpty() || anios.last().first != anio) anios.append({anio, TotalPeriodo{}});
            (tipoBD(q.value(1).toString()) == Tipo::Ingreso ? anios.last().second.ingresos : anios.last().second.gastos) = q.value(2).toLongLong();
        }
    } catch (const std::exception&) {
    }
    return anios;
}

QList<TotalCategoria> Cuentas::totalesPorCategoria(const QDate& desde, const QDate& hasta, Tipo tipo) const
{
    QList<TotalCategoria> lista;
    try {
        QString sql = QStringLiteral(
            "SELECT c.id, c.nombre, c.color, SUM(m.monto) AS total FROM movimientos m JOIN categorias c ON c.id = m.categoria_id "
            "WHERE m.borrado = 0 AND c.tipo = ? " SIN_TRANSFERENCIAS);
        QVariantList params{textoBD(tipo)};
        if (desde.isValid()) { sql += QStringLiteral("AND m.fecha >= ? "); params << fechaBD(desde); }
        if (hasta.isValid()) { sql += QStringLiteral("AND m.fecha <= ? "); params << fechaBD(hasta); }
        filtrarCuenta(sql, params);
        sql += QStringLiteral("GROUP BY c.id ORDER BY total DESC, c.nombre COLLATE NOCASE");
        QSqlQuery q = consulta(db_, sql, params);
        while (q.next()) lista << TotalCategoria{q.value(0).toLongLong(), q.value(1).toString(), tipo, q.value(2).toInt(), q.value(3).toLongLong()};
    } catch (const std::exception&) {
    }
    return lista;
}

QList<TotalEtiqueta> Cuentas::totalesPorEtiqueta(const QDate& desde, const QDate& hasta) const
{
    QList<TotalEtiqueta> lista;
    QHash<QString, int> posicion;           // etiqueta normalizada -> lugar en la lista
    for (const Movimiento& m : movimientos(desde, hasta)) {
        if (m.transferencia) continue;
        for (const QString& e : etiquetasDe(m.descripcion)) {
            const QString clave = normalizar(e);
            int i = posicion.value(clave, -1);
            if (i < 0) {
                i = int(lista.size());
                posicion.insert(clave, i);
                lista << TotalEtiqueta{e, 0, 0, 0};
            }
            (m.tipo == Tipo::Ingreso ? lista[i].ingresos : lista[i].gastos) += m.monto;
            ++lista[i].movimientos;
        }
    }
    std::sort(lista.begin(), lista.end(), [](const TotalEtiqueta& a, const TotalEtiqueta& b) {
        const Centavos ta = a.ingresos + a.gastos, tb = b.ingresos + b.gastos;
        if (ta != tb) return ta > tb;
        return claveOrden(a.nombre) < claveOrden(b.nombre);
    });
    return lista;
}

QDate Cuentas::primeraFecha() const
{
    try {
        return QDate::fromString(valor(db_, "SELECT MIN(fecha) FROM movimientos WHERE borrado = 0").toString(), Qt::ISODate);
    } catch (const std::exception&) {
        return {};
    }
}

QDate Cuentas::ultimaFecha() const
{
    try {
        return QDate::fromString(valor(db_, "SELECT MAX(fecha) FROM movimientos WHERE borrado = 0").toString(), Qt::ISODate);
    } catch (const std::exception&) {
        return {};
    }
}

// ====================================================================== cuentas de dinero

qint64 Cuentas::cuentaPrincipal() const
{
    try {
        return valor(db_, "SELECT id FROM cuentas_dinero WHERE uuid = ?", {UUID_PRINCIPAL}).toLongLong();
    } catch (const std::exception&) {
        return 0;
    }
}

QList<CuentaDinero> Cuentas::cuentasDinero() const
{
    QList<CuentaDinero> lista;
    try {
        QSqlQuery q = consulta(db_,
            "SELECT cu.id, cu.nombre, cu.saldo_inicial, cu.uuid, "
            "COALESCE((SELECT SUM(CASE WHEN c.tipo = 'ingreso' THEN m.monto ELSE -m.monto END) FROM movimientos m "
            "          JOIN categorias c ON c.id = m.categoria_id WHERE m.cuenta_id = cu.id AND m.borrado = 0 AND m.fecha <= ?), 0), "
            "(SELECT COUNT(*) FROM movimientos m WHERE m.cuenta_id = cu.id AND m.borrado = 0) "
            "FROM cuentas_dinero cu WHERE cu.borrado = 0",
            {fechaBD(QDate::currentDate())});
        while (q.next()) {
            CuentaDinero c;
            c.id = q.value(0).toLongLong();
            c.nombre = q.value(1).toString();
            c.saldoInicial = q.value(2).toLongLong();
            c.principal = q.value(3).toString() == UUID_PRINCIPAL;
            c.saldo = c.saldoInicial + q.value(4).toLongLong();
            c.movimientos = q.value(5).toInt();
            lista << c;
        }
    } catch (const std::exception&) {
    }
    std::sort(lista.begin(), lista.end(), [](const CuentaDinero& a, const CuentaDinero& b) {
        if (a.principal != b.principal) return a.principal;
        return claveOrden(a.nombre) < claveOrden(b.nombre);
    });
    return lista;
}

QString Cuentas::nombreLibreCuenta(const QString& nombre, qint64 excepto) const
{
    auto ocupado = [&](const QString& n) {
        const QVariant id = valor(db_, "SELECT id FROM cuentas_dinero WHERE nombre = ? COLLATE NOCASE AND borrado = 0", {n});
        return id.isValid() && id.toLongLong() != excepto;
    };
    if (!ocupado(nombre)) return nombre;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 (%2)").arg(nombre).arg(i);
        if (!ocupado(n)) return n;
    }
}

Resultado Cuentas::crearCuentaDinero(const QString& nombre, Centavos saldoInicial)
{
    const QString n = nombre.simplified();
    if (n.isEmpty()) return {false, QStringLiteral("Escribí un nombre."), 0};
    try {
        if (valor(db_, "SELECT id FROM cuentas_dinero WHERE nombre = ? COLLATE NOCASE AND borrado = 0", {n}).isValid())
            return {false, QStringLiteral("Ya hay una cuenta que se llama así."), 0};
        Transaccion t(db_);
        QSqlQuery q = consulta(db_, "INSERT INTO cuentas_dinero (uuid, nombre, saldo_inicial) VALUES (?, ?, ?)", {uuidNuevo(), n, saldoInicial});
        const qint64 id = q.lastInsertId().toLongLong();
        marcar("cuentas_dinero", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), 0};
    }
}

Resultado Cuentas::renombrarCuentaDinero(qint64 id, const QString& nombre)
{
    const QString n = nombre.simplified();
    if (n.isEmpty()) return {false, QStringLiteral("Escribí un nombre."), id};
    try {
        const QVariant otra = valor(db_, "SELECT id FROM cuentas_dinero WHERE nombre = ? COLLATE NOCASE AND borrado = 0", {n});
        if (otra.isValid() && otra.toLongLong() != id) return {false, QStringLiteral("Ya hay una cuenta que se llama así."), id};
        Transaccion t(db_);
        consulta(db_, "UPDATE cuentas_dinero SET nombre = ? WHERE id = ? AND borrado = 0", {n, id});
        marcar("cuentas_dinero", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

Resultado Cuentas::fijarSaldoInicialCuenta(qint64 id, Centavos c)
{
    try {
        if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {id}).isValid())
            return {false, QStringLiteral("La cuenta ya no existe."), id};
        Transaccion t(db_);
        consulta(db_, "UPDATE cuentas_dinero SET saldo_inicial = ? WHERE id = ?", {c, id});
        marcar("cuentas_dinero", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

Resultado Cuentas::eliminarCuentaDinero(qint64 id, qint64 moverA)
{
    try {
        if (id == cuentaPrincipal()) return {false, QStringLiteral("La cuenta principal no se puede eliminar (sí renombrar)."), id};
        if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {id}).isValid())
            return {false, QStringLiteral("La cuenta ya no existe."), id};
        const int usados = valor(db_, "SELECT COUNT(*) FROM movimientos WHERE cuenta_id = ? AND borrado = 0", {id}).toInt()
                           + valor(db_, "SELECT COUNT(*) FROM recurrentes WHERE cuenta_id = ? AND borrado = 0", {id}).toInt();
        if (usados > 0 && (moverA <= 0 || moverA == id
                           || !valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {moverA}).isValid()))
            return {false, QStringLiteral("La cuenta tiene movimientos: elegí a qué cuenta pasarlos."), id};
        Transaccion t(db_);
        QList<qint64> movidos, fijos;
        QSqlQuery q = consulta(db_, "SELECT id FROM movimientos WHERE cuenta_id = ? AND borrado = 0", {id});
        while (q.next()) movidos << q.value(0).toLongLong();
        q = consulta(db_, "SELECT id FROM recurrentes WHERE cuenta_id = ? AND borrado = 0", {id});
        while (q.next()) fijos << q.value(0).toLongLong();
        for (qint64 m : movidos) {
            consulta(db_, "UPDATE movimientos SET cuenta_id = ? WHERE id = ?", {moverA, m});
            marcar("movimientos", m);
        }
        for (qint64 f : fijos) {
            consulta(db_, "UPDATE recurrentes SET cuenta_id = ? WHERE id = ?", {moverA, f});
            marcar("recurrentes", f);
        }
        consulta(db_, "UPDATE cuentas_dinero SET borrado = 1 WHERE id = ?", {id});
        marcar("cuentas_dinero", id);
        if (cuentaVista_ == id) cuentaVista_ = 0;
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

// ====================================================================== presupuestos

Resultado Cuentas::fijarPresupuesto(qint64 categoriaId, Centavos mensual)
{
    if (mensual < 0) return {false, QStringLiteral("El presupuesto no puede ser negativo."), categoriaId};
    const auto c = categoria(categoriaId);
    if (!c) return {false, QStringLiteral("La categoría ya no existe."), categoriaId};
    if (c->tipo != Tipo::Gasto) return {false, QStringLiteral("El presupuesto es para las categorías de gastos."), categoriaId};
    try {
        Transaccion t(db_);
        consulta(db_, "UPDATE categorias SET presupuesto = ? WHERE id = ?", {mensual, categoriaId});
        marcar("categorias", categoriaId);
        t.confirmar();
        return {true, QString(), categoriaId};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), categoriaId};
    }
}

QList<EstadoPresupuesto> Cuentas::presupuestos(int anio, int mes) const
{
    QList<EstadoPresupuesto> lista;
    const QDate desde(anio, mes, 1);
    if (!desde.isValid()) return lista;
    try {
        QSqlQuery q = consulta(db_,
            "SELECT c.id, c.nombre, c.color, c.presupuesto, "
            "COALESCE((SELECT SUM(m.monto) FROM movimientos m WHERE m.categoria_id = c.id AND m.borrado = 0 "
            "          AND m.fecha >= ? AND m.fecha <= ?), 0) "
            "FROM categorias c WHERE c.borrado = 0 AND c.tipo = 'gasto' AND c.presupuesto > 0",
            {fechaBD(desde), fechaBD(desde.addMonths(1).addDays(-1))});
        while (q.next())
            lista << EstadoPresupuesto{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(),
                                       q.value(3).toLongLong(), q.value(4).toLongLong()};
    } catch (const std::exception&) {
    }
    std::sort(lista.begin(), lista.end(), [](const EstadoPresupuesto& a, const EstadoPresupuesto& b) {
        if (a.porcentaje() != b.porcentaje()) return a.porcentaje() > b.porcentaje();
        return claveOrden(a.nombre) < claveOrden(b.nombre);
    });
    return lista;
}

// ====================================================================== movimientos fijos

namespace {
const char* const SELECT_RECURRENTE =
    "SELECT r.id, r.categoria_id, c.nombre, c.tipo, r.cuenta_id, COALESCE(cu.nombre, ''), r.monto, r.descripcion, r.dia, "
    "r.desde, r.hasta, r.activo, r.uuid FROM recurrentes r JOIN categorias c ON c.id = r.categoria_id "
    "LEFT JOIN cuentas_dinero cu ON cu.id = r.cuenta_id WHERE r.borrado = 0 ";

Recurrente leerRecurrente(const QSqlQuery& q)
{
    Recurrente r;
    r.id = q.value(0).toLongLong();
    r.categoriaId = q.value(1).toLongLong();
    r.categoria = q.value(2).toString();
    r.tipo = tipoBD(q.value(3).toString());
    r.cuentaId = q.value(4).toLongLong();
    r.cuenta = q.value(5).toString();
    r.monto = q.value(6).toLongLong();
    r.descripcion = q.value(7).toString();
    r.dia = q.value(8).toInt();
    r.desde = QDate::fromString(q.value(9).toString(), Qt::ISODate);
    r.hasta = QDate::fromString(q.value(10).toString(), Qt::ISODate);
    r.activo = q.value(11).toInt() != 0;
    return r;
}

// La fecha de un movimiento fijo en un mes: su día, o el último del mes si es más corto
QDate fechaEnMes(int anio, int mes, int dia)
{
    const QDate primero(anio, mes, 1);
    return QDate(anio, mes, std::min(dia, primero.daysInMonth()));
}
}

QList<Recurrente> Cuentas::recurrentes() const
{
    QList<Recurrente> lista;
    try {
        QSqlQuery q = consulta(db_, QString::fromLatin1(SELECT_RECURRENTE) + QStringLiteral("ORDER BY r.dia, c.nombre COLLATE NOCASE"));
        while (q.next()) lista << leerRecurrente(q);
    } catch (const std::exception&) {
    }
    return lista;
}

Resultado Cuentas::guardarRecurrente(const Recurrente& r)
{
    if (r.monto <= 0) return {false, QStringLiteral("El monto tiene que ser mayor que cero."), r.id};
    if (r.dia < 1 || r.dia > 31) return {false, QStringLiteral("El día tiene que ser del 1 al 31."), r.id};
    if (!r.desde.isValid()) return {false, QStringLiteral("Elegí desde qué fecha."), r.id};
    if (r.hasta.isValid() && r.hasta < r.desde) return {false, QStringLiteral("La fecha de fin es anterior a la de inicio."), r.id};
    if (!categoria(r.categoriaId)) return {false, QStringLiteral("Elegí una categoría."), r.id};
    const qint64 cuenta = r.cuentaId > 0 ? r.cuentaId : cuentaPrincipal();
    try {
        if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE id = ? AND borrado = 0", {cuenta}).isValid())
            return {false, QStringLiteral("La cuenta ya no existe."), r.id};
        Transaccion t(db_);
        qint64 id = r.id;
        const QVariantList datos{r.categoriaId, cuenta, r.monto, texto(r.descripcion.trimmed()), r.dia, fechaBD(r.desde),
                                 r.hasta.isValid() ? fechaBD(r.hasta) : QString(QLatin1String("")), r.activo ? 1 : 0};
        if (id > 0) {
            if (!valor(db_, "SELECT id FROM recurrentes WHERE id = ? AND borrado = 0", {id}).isValid())
                return {false, QStringLiteral("El movimiento fijo ya no existe."), id};
            consulta(db_, "UPDATE recurrentes SET categoria_id = ?, cuenta_id = ?, monto = ?, descripcion = ?, dia = ?, desde = ?, "
                          "hasta = ?, activo = ? WHERE id = ?", QVariantList(datos) << id);
        } else {
            QSqlQuery q = consulta(db_, "INSERT INTO recurrentes (categoria_id, cuenta_id, monto, descripcion, dia, desde, hasta, activo, uuid) "
                                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", QVariantList(datos) << uuidNuevo());
            id = q.lastInsertId().toLongLong();
        }
        marcar("recurrentes", id);
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), r.id};
    }
}

Resultado Cuentas::eliminarRecurrente(qint64 id)
{
    try {
        if (!valor(db_, "SELECT id FROM recurrentes WHERE id = ? AND borrado = 0", {id}).isValid())
            return {false, QStringLiteral("El movimiento fijo ya no existe."), id};
        Transaccion t(db_);
        consulta(db_, "UPDATE recurrentes SET borrado = 1 WHERE id = ?", {id});
        marcar("recurrentes", id);
        t.confirmar();
        return {true, QString(), id};      // los que ya se cargaron quedan
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
}

int Cuentas::generarRecurrentes(const QDate& hoy)
{
    int cargados = 0;
    try {
        Transaccion t(db_);
        QSqlQuery q = consulta(db_, QString::fromLatin1(SELECT_RECURRENTE) + QStringLiteral("AND r.activo = 1"));
        struct Pendiente { Recurrente r; QString uuid; };
        QList<Pendiente> lista;
        while (q.next()) lista << Pendiente{leerRecurrente(q), q.value(12).toString()};
        for (const Pendiente& p : std::as_const(lista)) {
            const Recurrente& r = p.r;
            if (!r.desde.isValid() || r.desde > hoy) continue;
            for (QDate mes(r.desde.year(), r.desde.month(), 1); mes <= hoy; mes = mes.addMonths(1)) {
                const QDate fecha = fechaEnMes(mes.year(), mes.month(), r.dia);
                if (fecha < r.desde) continue;
                if (fecha > hoy || (r.hasta.isValid() && fecha > r.hasta)) break;
                // Mismo uuid en todos los dispositivos para el mismo fijo y el mismo mes (si ya está,
                // aunque se haya borrado, no se vuelve a cargar)
                const QString uuid = QStringLiteral("fijo-") + hash32(p.uuid + QLatin1Char('|') + mes.toString(QStringLiteral("yyyy-MM")));
                if (valor(db_, "SELECT id FROM movimientos WHERE uuid = ?", {uuid}).isValid()) continue;
                // Fecha de cambio fija (la del día que corresponde) y sin autor: si la PC y el celular lo cargan
                // los dos, son idénticos; y cualquier cambio o borrado que haga la persona es más nuevo y gana
                const qint64 mod = QDateTime(fecha, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
                consulta(db_, "INSERT INTO movimientos (uuid, fecha, categoria_id, monto, descripcion, cuenta_id, modificado, autor, cambio) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, '', ?)",
                         {uuid, fechaBD(fecha), r.categoriaId, r.monto, texto(r.descripcion), r.cuentaId, mod, siguienteCambio()});
                ++cargados;
            }
        }
        t.confirmar();
    } catch (const std::exception&) {
        return 0;
    }
    return cargados;
}
