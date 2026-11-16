// Base de datos: sincronización (filas que se mandan y reciben, y dispositivos emparejados).
#include "db_interno.h"

using namespace dbi;

// ====================================================================== sincronización: filas

namespace {

// Formato de cada fila en el intercambio (JSON):
//   {"t":"cat","u":uuid,"mod":ms,"aut":disp,"del":0,"n":nombre,"tp":"gasto","col":3,"pres":5000000}
//   {"t":"cta","u":uuid,"mod":ms,"aut":disp,"del":0,"n":"Banco","si":1000000}
//   {"t":"mov","u":uuid,"mod":ms,"aut":disp,"del":0,"f":"2026-11-01","cat":uuidCategoria,"mo":154350,"d":"...","cu":uuidCuenta}
//   {"t":"rec","u":uuid,"mod":ms,"aut":disp,"del":0,"cat":uuidCategoria,"cu":uuidCuenta,"mo":..,"d":..,"dia":5,
//    "de":"2026-11-05","ha":"","ac":1}
const char* const SELECT_FILA_CAT = "SELECT uuid, modificado, autor, borrado, nombre, tipo, color, presupuesto FROM categorias ";
const char* const SELECT_FILA_MOV =
    "SELECT m.uuid, m.modificado, m.autor, m.borrado, m.fecha, c.uuid, m.monto, m.descripcion, COALESCE(cu.uuid, '') "
    "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id LEFT JOIN cuentas_dinero cu ON cu.id = m.cuenta_id ";
const char* const SELECT_FILA_CTA = "SELECT uuid, modificado, autor, borrado, nombre, saldo_inicial FROM cuentas_dinero ";
const char* const SELECT_FILA_REC =
    "SELECT r.uuid, r.modificado, r.autor, r.borrado, c.uuid, COALESCE(cu.uuid, ''), r.monto, r.descripcion, r.dia, r.desde, "
    "r.hasta, r.activo FROM recurrentes r JOIN categorias c ON c.id = r.categoria_id LEFT JOIN cuentas_dinero cu ON cu.id = r.cuenta_id ";
const char* const SELECT_FILA_CFG = "SELECT clave, modificado, autor, valor FROM config ";

QJsonObject filaCategoria(const QSqlQuery& q)
{
    return QJsonObject{{"t", "cat"}, {"u", q.value(0).toString()}, {"mod", q.value(1).toLongLong()}, {"aut", q.value(2).toString()},
                       {"del", q.value(3).toInt()}, {"n", q.value(4).toString()}, {"tp", q.value(5).toString()}, {"col", q.value(6).toInt()},
                       {"pres", q.value(7).toLongLong()}};
}

QJsonObject filaCuenta(const QSqlQuery& q)
{
    return QJsonObject{{"t", "cta"}, {"u", q.value(0).toString()}, {"mod", q.value(1).toLongLong()}, {"aut", q.value(2).toString()},
                       {"del", q.value(3).toInt()}, {"n", q.value(4).toString()}, {"si", q.value(5).toLongLong()}};
}

QJsonObject filaRecurrente(const QSqlQuery& q)
{
    return QJsonObject{{"t", "rec"}, {"u", q.value(0).toString()}, {"mod", q.value(1).toLongLong()}, {"aut", q.value(2).toString()},
                       {"del", q.value(3).toInt()}, {"cat", q.value(4).toString()}, {"cu", q.value(5).toString()},
                       {"mo", q.value(6).toLongLong()}, {"d", q.value(7).toString()}, {"dia", q.value(8).toInt()},
                       {"de", q.value(9).toString()}, {"ha", q.value(10).toString()}, {"ac", q.value(11).toInt()}};
}

QJsonObject filaMovimiento(const QSqlQuery& q)
{
    return QJsonObject{{"t", "mov"}, {"u", q.value(0).toString()}, {"mod", q.value(1).toLongLong()}, {"aut", q.value(2).toString()},
                       {"del", q.value(3).toInt()}, {"f", q.value(4).toString()}, {"cat", q.value(5).toString()},
                       {"mo", q.value(6).toLongLong()}, {"d", q.value(7).toString()}, {"cu", q.value(8).toString()}};
}

QJsonObject filaConfig(const QSqlQuery& q)
{
    return QJsonObject{{"t", "cfg"}, {"u", QStringLiteral("cfg:") + q.value(0).toString()}, {"mod", q.value(1).toLongLong()},
                       {"aut", q.value(2).toString()}, {"k", q.value(0).toString()}, {"v", q.value(3).toString()}};
}

QString condicionConfig()
{
    QStringList claves;
    for (const QString& c : CONFIG_SINCRONIZADA) claves << QLatin1Char('\'') + c + QLatin1Char('\'');
    return QStringLiteral("clave IN (%1) ").arg(claves.join(QLatin1Char(',')));
}

} // namespace

QJsonArray Cuentas::cambiosDesde(qint64 desde) const
{
    QJsonArray filas;
    try {
        QSqlQuery q = consulta(db_, QString::fromLatin1(SELECT_FILA_CAT) + QStringLiteral("WHERE cambio > ? ORDER BY cambio"), {desde});
        while (q.next()) filas << filaCategoria(q);
        q = consulta(db_, QString::fromLatin1(SELECT_FILA_CTA) + QStringLiteral("WHERE cambio > ? ORDER BY cambio"), {desde});
        while (q.next()) filas << filaCuenta(q);
        q = consulta(db_, QString::fromLatin1(SELECT_FILA_MOV) + QStringLiteral("WHERE m.cambio > ? ORDER BY m.cambio"), {desde});
        while (q.next()) filas << filaMovimiento(q);
        q = consulta(db_, QString::fromLatin1(SELECT_FILA_REC) + QStringLiteral("WHERE r.cambio > ? ORDER BY r.cambio"), {desde});
        while (q.next()) filas << filaRecurrente(q);
        q = consulta(db_, QString::fromLatin1(SELECT_FILA_CFG) + QStringLiteral("WHERE cambio > ? AND ") + condicionConfig(), {desde});
        while (q.next()) filas << filaConfig(q);
    } catch (const std::exception&) {
    }
    return filas;
}

QJsonArray Cuentas::filasPorUuid(const QStringList& uuids) const
{
    QJsonArray filas;
    try {
        for (const QString& u : uuids) {
            if (u.startsWith(QLatin1String("cfg:"))) {
                QSqlQuery q = consulta(db_, QString::fromLatin1(SELECT_FILA_CFG) + QStringLiteral("WHERE clave = ? AND ") + condicionConfig(), {u.mid(4)});
                if (q.next()) filas << filaConfig(q);
                continue;
            }
            QSqlQuery q = consulta(db_, QString::fromLatin1(SELECT_FILA_CAT) + QStringLiteral("WHERE uuid = ?"), {u});
            if (q.next()) { filas << filaCategoria(q); continue; }
            q = consulta(db_, QString::fromLatin1(SELECT_FILA_CTA) + QStringLiteral("WHERE uuid = ?"), {u});
            if (q.next()) { filas << filaCuenta(q); continue; }
            q = consulta(db_, QString::fromLatin1(SELECT_FILA_MOV) + QStringLiteral("WHERE m.uuid = ?"), {u});
            if (q.next()) { filas << filaMovimiento(q); continue; }
            q = consulta(db_, QString::fromLatin1(SELECT_FILA_REC) + QStringLiteral("WHERE r.uuid = ?"), {u});
            if (q.next()) filas << filaRecurrente(q);
        }
    } catch (const std::exception&) {
    }
    return filas;
}

QString Cuentas::nombreLibre(const QString& nombre, Tipo tipo, qint64 excepto) const
{
    auto ocupado = [&](const QString& n) {
        const auto c = buscarCategoria(n, tipo);
        return c && c->id != excepto;
    };
    if (!ocupado(nombre)) return nombre;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 (%2)").arg(nombre).arg(i);
        if (!ocupado(n)) return n;
    }
}

ResultadoAplicar Cuentas::aplicarCambios(const QJsonArray& filas)
{
    ResultadoAplicar r;
    try {
        Transaccion t(db_);
        struct Pendiente { qint64 id; QString uuid, nombre; Tipo tipo; };
        QList<Pendiente> pendientes;
        struct CuentaPendiente { qint64 id; QString uuid, nombre; };
        QList<CuentaPendiente> cuentasPendientes;
        // Primero las categorías y las cuentas (los movimientos las nombran por uuid), después movimientos,
        // fijos y config
        const qint64 principal = cuentaPrincipal();
        const auto idCuenta = [&](const QString& uuidCuenta) -> qint64 {   // sin cuenta (o una que no está): la principal
            const QVariant id = uuidCuenta.isEmpty() ? QVariant() : valor(db_, "SELECT id FROM cuentas_dinero WHERE uuid = ?", {uuidCuenta});
            return id.isValid() ? id.toLongLong() : principal;
        };
        for (const char* tipoFila : {"cat", "cta", "mov", "rec", "cfg"}) {
            if (qstrcmp(tipoFila, "mov") == 0) {
                // Nombres definitivos de las categorías recién llegadas: si otra distinta de acá ya se llama
                // así, la que llegó queda «Nombre (2)» y ese nombre vuelve al otro dispositivo
                for (const Pendiente& p : pendientes) {
                    const QString libre = nombreLibre(p.nombre, p.tipo, p.id);
                    consulta(db_, "UPDATE categorias SET nombre = ? WHERE id = ?", {libre, p.id});
                    if (libre != p.nombre) {
                        marcar("categorias", p.id);
                        r.ganadorasLocales << p.uuid;
                    } else {
                        r.aceptadas << p.uuid;
                    }
                }
                // Lo mismo con las cuentas («Banco» creada en los dos lados)
                for (const CuentaPendiente& p : cuentasPendientes) {
                    const QString libre = nombreLibreCuenta(p.nombre, p.id);
                    consulta(db_, "UPDATE cuentas_dinero SET nombre = ? WHERE id = ?", {libre, p.id});
                    if (libre != p.nombre) {
                        marcar("cuentas_dinero", p.id);
                        r.ganadorasLocales << p.uuid;
                    } else {
                        r.aceptadas << p.uuid;
                    }
                }
            }
            for (const QJsonValue& v : filas) {
                const QJsonObject o = v.toObject();
                if (o.value(QLatin1String("t")).toString() != QLatin1String(tipoFila)) continue;
                const QString uuid = o.value(QLatin1String("u")).toString();
                const qint64 mod = o.value(QLatin1String("mod")).toVariant().toLongLong();
                const QString aut = o.value(QLatin1String("aut")).toString();
                const int del = o.value(QLatin1String("del")).toInt() ? 1 : 0;
                if (uuid.isEmpty()) { ++r.ignoradas; continue; }

                if (qstrcmp(tipoFila, "cat") == 0) {
                    const Tipo tipo = tipoBD(o.value(QLatin1String("tp")).toString());
                    QString nombre = o.value(QLatin1String("n")).toString().simplified();
                    const int color = std::clamp(o.value(QLatin1String("col")).toInt(), 0, COLORES - 1);
                    const qint64 presupuesto = std::max<qint64>(0, o.value(QLatin1String("pres")).toVariant().toLongLong());
                    if (nombre.isEmpty()) { ++r.ignoradas; continue; }
                    QSqlQuery q = consulta(db_, "SELECT id, modificado, autor FROM categorias WHERE uuid = ?", {uuid});
                    qint64 id = 0;
                    if (q.next()) {
                        id = q.value(0).toLongLong();
                        if (!ganaRemota(mod, aut, q.value(1).toLongLong(), q.value(2).toString())) {
                            if (mod != q.value(1).toLongLong() || aut != q.value(2).toString()) r.ganadorasLocales << uuid;
                            continue;
                        }
                    }
                    // El nombre definitivo se pone al final (abajo): primero con un nombre provisorio único,
                    // así un renombre que viene en el mismo paquete no choca con otro que todavía no se aplicó
                    const QString provisorio = del ? nombre : QStringLiteral("\x01") + uuid;
                    if (id) {
                        consulta(db_, "UPDATE categorias SET nombre = ?, color = ?, presupuesto = ?, borrado = ?, modificado = ?, autor = ?, cambio = ? WHERE id = ?",
                                 {provisorio, color, presupuesto, del, mod, aut, siguienteCambio(), id});
                    } else {
                        q = consulta(db_, "INSERT INTO categorias (uuid, nombre, tipo, color, presupuesto, borrado, modificado, autor, cambio) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                                     {uuid, provisorio, textoBD(tipo), color, presupuesto, del, mod, aut, siguienteCambio()});
                        id = q.lastInsertId().toLongLong();
                    }
                    if (del) r.aceptadas << uuid;
                    else pendientes << Pendiente{id, uuid, nombre, tipo};
                    ++r.aplicadas;
                } else if (qstrcmp(tipoFila, "cta") == 0) {
                    const QString nombre = o.value(QLatin1String("n")).toString().simplified();
                    const qint64 saldoIni = o.value(QLatin1String("si")).toVariant().toLongLong();
                    if (nombre.isEmpty()) { ++r.ignoradas; continue; }
                    QSqlQuery q = consulta(db_, "SELECT id, modificado, autor FROM cuentas_dinero WHERE uuid = ?", {uuid});
                    qint64 id = 0;
                    if (q.next()) {
                        id = q.value(0).toLongLong();
                        if (!ganaRemota(mod, aut, q.value(1).toLongLong(), q.value(2).toString())) {
                            if (mod != q.value(1).toLongLong() || aut != q.value(2).toString()) r.ganadorasLocales << uuid;
                            continue;
                        }
                    }
                    // Primero con un nombre provisorio único; el definitivo se pone antes de los movimientos (arriba):
                    // si otra cuenta de acá ya se llama así (las dos crearon «Banco»), la que llegó queda «Banco (2)»
                    const QString provisorio = del ? nombre : QStringLiteral("\x01") + uuid;
                    if (id) {
                        consulta(db_, "UPDATE cuentas_dinero SET nombre = ?, saldo_inicial = ?, borrado = ?, modificado = ?, autor = ?, cambio = ? WHERE id = ?",
                                 {provisorio, saldoIni, del, mod, aut, siguienteCambio(), id});
                    } else {
                        q = consulta(db_, "INSERT INTO cuentas_dinero (uuid, nombre, saldo_inicial, borrado, modificado, autor, cambio) VALUES (?, ?, ?, ?, ?, ?, ?)",
                                     {uuid, provisorio, saldoIni, del, mod, aut, siguienteCambio()});
                        id = q.lastInsertId().toLongLong();
                    }
                    if (del) r.aceptadas << uuid;
                    else cuentasPendientes << CuentaPendiente{id, uuid, nombre};
                    ++r.aplicadas;
                } else if (qstrcmp(tipoFila, "rec") == 0) {
                    const QVariant idCat = valor(db_, "SELECT id FROM categorias WHERE uuid = ?", {o.value(QLatin1String("cat")).toString()});
                    const qint64 monto = o.value(QLatin1String("mo")).toVariant().toLongLong();
                    const int dia = o.value(QLatin1String("dia")).toInt();
                    const QDate de = QDate::fromString(o.value(QLatin1String("de")).toString(), Qt::ISODate);
                    if (!idCat.isValid() || monto <= 0 || dia < 1 || dia > 31 || !de.isValid()) { ++r.ignoradas; continue; }
                    QSqlQuery q = consulta(db_, "SELECT id, modificado, autor FROM recurrentes WHERE uuid = ?", {uuid});
                    const QVariantList datos{idCat, idCuenta(o.value(QLatin1String("cu")).toString()), monto,
                                             texto(o.value(QLatin1String("d")).toString()), dia, fechaBD(de),
                                             texto(o.value(QLatin1String("ha")).toString()), o.value(QLatin1String("ac")).toInt() ? 1 : 0,
                                             del, mod, aut, siguienteCambio()};
                    if (q.next()) {
                        if (!ganaRemota(mod, aut, q.value(1).toLongLong(), q.value(2).toString())) {
                            if (mod != q.value(1).toLongLong() || aut != q.value(2).toString()) r.ganadorasLocales << uuid;
                            continue;
                        }
                        consulta(db_, "UPDATE recurrentes SET categoria_id = ?, cuenta_id = ?, monto = ?, descripcion = ?, dia = ?, desde = ?, "
                                      "hasta = ?, activo = ?, borrado = ?, modificado = ?, autor = ?, cambio = ? WHERE id = ?",
                                 QVariantList(datos) << q.value(0));
                    } else {
                        consulta(db_, "INSERT INTO recurrentes (categoria_id, cuenta_id, monto, descripcion, dia, desde, hasta, activo, borrado, "
                                      "modificado, autor, cambio, uuid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                                 QVariantList(datos) << uuid);
                    }
                    r.aceptadas << uuid;
                    ++r.aplicadas;
                } else if (qstrcmp(tipoFila, "mov") == 0) {
                    const QDate fecha = QDate::fromString(o.value(QLatin1String("f")).toString(), Qt::ISODate);
                    const qint64 monto = o.value(QLatin1String("mo")).toVariant().toLongLong();
                    const QVariant idCat = valor(db_, "SELECT id FROM categorias WHERE uuid = ?", {o.value(QLatin1String("cat")).toString()});
                    if (!fecha.isValid() || monto <= 0 || !idCat.isValid()) { ++r.ignoradas; continue; }
                    QSqlQuery q = consulta(db_, "SELECT id, modificado, autor FROM movimientos WHERE uuid = ?", {uuid});
                    const QString desc = texto(o.value(QLatin1String("d")).toString());
                    if (q.next()) {
                        if (!ganaRemota(mod, aut, q.value(1).toLongLong(), q.value(2).toString())) {
                            if (mod != q.value(1).toLongLong() || aut != q.value(2).toString()) r.ganadorasLocales << uuid;
                            continue;
                        }
                        consulta(db_, "UPDATE movimientos SET fecha = ?, categoria_id = ?, monto = ?, descripcion = ?, cuenta_id = ?, borrado = ?, "
                                      "modificado = ?, autor = ?, cambio = ? WHERE id = ?",
                                 {fechaBD(fecha), idCat, monto, desc, idCuenta(o.value(QLatin1String("cu")).toString()), del, mod, aut,
                                  siguienteCambio(), q.value(0)});
                    } else {
                        consulta(db_, "INSERT INTO movimientos (uuid, fecha, categoria_id, monto, descripcion, cuenta_id, borrado, modificado, autor, cambio) "
                                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                                 {uuid, fechaBD(fecha), idCat, monto, desc, idCuenta(o.value(QLatin1String("cu")).toString()), del, mod, aut,
                                  siguienteCambio()});
                    }
                    r.aceptadas << uuid;
                    ++r.aplicadas;
                } else {
                    const QString clave = o.value(QLatin1String("k")).toString();
                    if (!CONFIG_SINCRONIZADA.contains(clave)) { ++r.ignoradas; continue; }
                    QSqlQuery q = consulta(db_, "SELECT modificado, autor FROM config WHERE clave = ?", {clave});
                    if (q.next() && !ganaRemota(mod, aut, q.value(0).toLongLong(), q.value(1).toString())) {
                        if (mod != q.value(0).toLongLong() || aut != q.value(1).toString()) r.ganadorasLocales << uuid;
                        continue;
                    }
                    consulta(db_, "INSERT INTO config (clave, valor, modificado, autor, cambio) VALUES (?, ?, ?, ?, ?) "
                                  "ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor, modificado = excluded.modificado, "
                                  "autor = excluded.autor, cambio = excluded.cambio",
                             {clave, texto(o.value(QLatin1String("v")).toString()), mod, aut, siguienteCambio()});
                    r.aceptadas << uuid;
                    ++r.aplicadas;
                }
            }
        }
        // Un movimiento vivo en una categoría borrada (se cargó en un lado mientras en el otro se borraba
        // la categoría): la categoría vuelve, para no perder ni esconder el movimiento
        QList<qint64> revivir;
        QSqlQuery q = consulta(db_, "SELECT DISTINCT c.id FROM categorias c JOIN movimientos m ON m.categoria_id = c.id "
                                    "WHERE c.borrado = 1 AND m.borrado = 0");
        while (q.next()) revivir << q.value(0).toLongLong();
        for (qint64 id : revivir) {
            QSqlQuery c = consulta(db_, "SELECT nombre, tipo, uuid FROM categorias WHERE id = ?", {id});
            if (!c.next()) continue;
            const QString uuid = c.value(2).toString();
            r.aceptadas.removeAll(uuid);
            r.ganadorasLocales << uuid;
            consulta(db_, "UPDATE categorias SET nombre = ?, borrado = 0 WHERE id = ?",
                     {nombreLibre(c.value(0).toString(), tipoBD(c.value(1).toString()), id), id});
            marcar("categorias", id);
        }
        // Lo mismo con las cuentas: un movimiento o fijo vivo en una cuenta borrada hace que la cuenta vuelva
        QList<qint64> revivirCuentas;
        q = consulta(db_, "SELECT DISTINCT cu.id FROM cuentas_dinero cu WHERE cu.borrado = 1 AND ("
                          "EXISTS (SELECT 1 FROM movimientos m WHERE m.cuenta_id = cu.id AND m.borrado = 0) OR "
                          "EXISTS (SELECT 1 FROM recurrentes f WHERE f.cuenta_id = cu.id AND f.borrado = 0))");
        while (q.next()) revivirCuentas << q.value(0).toLongLong();
        for (qint64 id : revivirCuentas) {
            QSqlQuery c = consulta(db_, "SELECT nombre, uuid FROM cuentas_dinero WHERE id = ?", {id});
            if (!c.next()) continue;
            const QString uuid = c.value(1).toString();
            r.aceptadas.removeAll(uuid);
            r.ganadorasLocales << uuid;
            consulta(db_, "UPDATE cuentas_dinero SET nombre = ?, borrado = 0 WHERE id = ?", {nombreLibreCuenta(c.value(0).toString(), id), id});
            marcar("cuentas_dinero", id);
        }
        t.confirmar();
    } catch (const std::exception& e) {
        r = ResultadoAplicar{};
        r.ok = false;
        r.error = QString::fromUtf8(e.what());
    }
    return r;
}

// ====================================================================== sincronización: pares

QList<Par> Cuentas::pares() const
{
    QList<Par> lista;
    try {
        QSqlQuery q = consulta(db_, "SELECT dispositivo, nombre, secreto, host, puerto, enviado, recibido, ultima_sinc FROM pares ORDER BY nombre");
        while (q.next()) {
            Par p;
            p.dispositivo = q.value(0).toString();
            p.nombre = q.value(1).toString();
            p.secreto = q.value(2).toString();
            p.host = q.value(3).toString();
            p.puerto = q.value(4).toInt();
            p.enviado = q.value(5).toLongLong();
            p.recibido = q.value(6).toLongLong();
            p.ultimaSinc = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
            lista << p;
        }
    } catch (const std::exception&) {
    }
    return lista;
}

std::optional<Par> Cuentas::par(const QString& dispositivo) const
{
    for (const Par& p : pares()) if (p.dispositivo == dispositivo) return p;
    return std::nullopt;
}

void Cuentas::guardarPar(const Par& p)
{
    try {
        consulta(db_, "INSERT INTO pares (dispositivo, nombre, secreto, host, puerto, enviado, recibido, ultima_sinc) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(dispositivo) DO UPDATE SET nombre = excluded.nombre, secreto = excluded.secreto, host = excluded.host, "
                      "puerto = excluded.puerto, enviado = excluded.enviado, recibido = excluded.recibido, ultima_sinc = excluded.ultima_sinc",
                 {p.dispositivo, texto(p.nombre), texto(p.secreto), texto(p.host), p.puerto, p.enviado, p.recibido,
                  texto(p.ultimaSinc.isValid() ? p.ultimaSinc.toString(Qt::ISODate) : QString())});
    } catch (const std::exception&) {
    }
}

void Cuentas::eliminarPar(const QString& dispositivo)
{
    try {
        consulta(db_, "DELETE FROM pares WHERE dispositivo = ?", {dispositivo});
    } catch (const std::exception&) {
    }
}
