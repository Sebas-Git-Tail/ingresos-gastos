// Ayudantes internos de la base de datos: los comparten db.cpp, db_operaciones.cpp,
// db_importacion.cpp y db_sinc.cpp. No es parte de la interfaz (eso es db.h).
#pragma once

#include "db.h"
#include "dinero.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace dbi {


// Columnas que tienen todas las tablas que se sincronizan
#define COLUMNAS_SINC \
    " modificado INTEGER NOT NULL DEFAULT 0," \
    " autor TEXT NOT NULL DEFAULT ''," \
    " cambio INTEGER NOT NULL DEFAULT 0"

inline const char* const ESQUEMA[] = {
    "CREATE TABLE IF NOT EXISTS config ("
    " clave TEXT PRIMARY KEY,"
    " valor TEXT NOT NULL," COLUMNAS_SINC ")",

    "CREATE TABLE IF NOT EXISTS categorias ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " uuid TEXT NOT NULL UNIQUE,"
    " nombre TEXT NOT NULL,"
    " tipo TEXT NOT NULL CHECK (tipo IN ('ingreso', 'gasto')),"
    " color INTEGER NOT NULL DEFAULT 0,"
    " presupuesto INTEGER NOT NULL DEFAULT 0," COLUMNAS_SINC ","
    " borrado INTEGER NOT NULL DEFAULT 0)",

    // Nombre único entre las categorías que no están borradas
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_categorias_nombre ON categorias(nombre COLLATE NOCASE, tipo) WHERE borrado = 0",
    "CREATE INDEX IF NOT EXISTS idx_categorias_cambio ON categorias(cambio)",

    // monto en centavos enteros y siempre positivo: el tipo de la categoría dice si suma o resta
    "CREATE TABLE IF NOT EXISTS movimientos ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " uuid TEXT NOT NULL UNIQUE,"
    " fecha TEXT NOT NULL,"
    " categoria_id INTEGER NOT NULL REFERENCES categorias(id),"
    " monto INTEGER NOT NULL CHECK (monto > 0),"
    " descripcion TEXT NOT NULL DEFAULT '',"
    " cuenta_id INTEGER NOT NULL DEFAULT 0," COLUMNAS_SINC ","
    " borrado INTEGER NOT NULL DEFAULT 0)",

    "CREATE INDEX IF NOT EXISTS idx_movimientos_fecha ON movimientos(fecha)",
    "CREATE INDEX IF NOT EXISTS idx_movimientos_categoria ON movimientos(categoria_id)",
    "CREATE INDEX IF NOT EXISTS idx_movimientos_cambio ON movimientos(cambio)",

    // Cuentas de dinero (efectivo, banco...). La principal tiene el mismo uuid en todos los dispositivos.
    "CREATE TABLE IF NOT EXISTS cuentas_dinero ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " uuid TEXT NOT NULL UNIQUE,"
    " nombre TEXT NOT NULL,"
    " saldo_inicial INTEGER NOT NULL DEFAULT 0," COLUMNAS_SINC ","
    " borrado INTEGER NOT NULL DEFAULT 0)",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_cuentas_nombre ON cuentas_dinero(nombre COLLATE NOCASE) WHERE borrado = 0",
    "CREATE INDEX IF NOT EXISTS idx_cuentas_cambio ON cuentas_dinero(cambio)",
    "CREATE INDEX IF NOT EXISTS idx_movimientos_cuenta ON movimientos(cuenta_id)",

    // Movimientos fijos: se cargan solos todos los meses (ver generarRecurrentes)
    "CREATE TABLE IF NOT EXISTS recurrentes ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " uuid TEXT NOT NULL UNIQUE,"
    " categoria_id INTEGER NOT NULL,"
    " cuenta_id INTEGER NOT NULL DEFAULT 0,"
    " monto INTEGER NOT NULL CHECK (monto > 0),"
    " descripcion TEXT NOT NULL DEFAULT '',"
    " dia INTEGER NOT NULL CHECK (dia BETWEEN 1 AND 31),"
    " desde TEXT NOT NULL,"
    " hasta TEXT NOT NULL DEFAULT '',"
    " activo INTEGER NOT NULL DEFAULT 1," COLUMNAS_SINC ","
    " borrado INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS idx_recurrentes_cambio ON recurrentes(cambio)",

    // Dispositivos emparejados
    "CREATE TABLE IF NOT EXISTS pares ("
    " dispositivo TEXT PRIMARY KEY,"
    " nombre TEXT NOT NULL DEFAULT '',"
    " secreto TEXT NOT NULL,"
    " host TEXT NOT NULL DEFAULT '',"
    " puerto INTEGER NOT NULL DEFAULT 0,"
    " enviado INTEGER NOT NULL DEFAULT 0,"
    " recibido INTEGER NOT NULL DEFAULT 0,"
    " ultima_sinc TEXT NOT NULL DEFAULT '')",
};

// Claves de config que se sincronizan (las demás son de este dispositivo: su id, su nombre, la secuencia).
// Desde la 2.1.0 ninguna: el saldo inicial pasó a cada cuenta de dinero.
inline const QStringList CONFIG_SINCRONIZADA = {};

// La cuenta que trae el programa: mismo uuid en todos los dispositivos, así no se duplica al sincronizar
inline const QString UUID_PRINCIPAL = QStringLiteral("cuenta-principal");

// Transferencias entre cuentas (2.8.0): sus dos categorías internas, con el mismo uuid en todos los dispositivos.
// Cada transferencia son dos movimientos con uuid «tr-<grupo>-s» (salida) y «tr-<grupo>-e» (entrada).
inline const QString UUID_TRANSF_SALIDA = QStringLiteral("transferencia-salida");
inline const QString UUID_TRANSF_ENTRADA = QStringLiteral("transferencia-entrada");
// Nombre largo a propósito: una categoría «Transferencia» de la persona (pagos por transferencia) es otra cosa
inline const QString NOMBRE_TRANSFERENCIA = QStringLiteral("Transferencia entre cuentas");
// Para sacarlas de los totales, las tortas y los presupuestos (c = categorias)
#define SIN_TRANSFERENCIAS " AND c.uuid NOT IN ('transferencia-salida', 'transferencia-entrada') "
inline bool esUuidTransferencia(const QString& uuidCategoria)
{
    return uuidCategoria == UUID_TRANSF_SALIDA || uuidCategoria == UUID_TRANSF_ENTRADA;
}
// La otra mitad de una transferencia: «tr-x-s» <-> «tr-x-e» (vacío si el uuid no es de una transferencia armada acá)
inline QString uuidPareja(const QString& uuid)
{
    if (!uuid.startsWith(QLatin1String("tr-")) || uuid.size() < 6) return QString();
    if (uuid.endsWith(QLatin1String("-s"))) return uuid.chopped(1) + QLatin1Char('e');
    if (uuid.endsWith(QLatin1String("-e"))) return uuid.chopped(1) + QLatin1Char('s');
    return QString();
}

// Categorías con las que arranca una base nueva (se pueden editar y agregar más)
inline const char* const GASTOS_INICIALES[] = {"Supermercado", "Transporte", "Vestimenta", "Internet", "Teléfono",
                                        "Servicios", "Salud", "Gastos Varios"};
inline const char* const INGRESOS_INICIALES[] = {"Sueldo", "Ingresos Varios"};

struct ErrorBD : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline QSqlQuery consulta(const QSqlDatabase& db, const QString& sql, const QVariantList& params = {})
{
    QSqlQuery q(db);
    if (!q.prepare(sql)) throw ErrorBD(q.lastError().text().toStdString());
    for (const QVariant& v : params) q.addBindValue(v);
    if (!q.exec()) throw ErrorBD(q.lastError().text().toStdString());
    return q;
}

inline QVariant valor(const QSqlDatabase& db, const QString& sql, const QVariantList& params = {})
{
    QSqlQuery q = consulta(db, sql, params);
    return q.next() ? q.value(0) : QVariant();
}

class Transaccion
{
public:
    explicit Transaccion(QSqlDatabase db) : db_(db)
    {
        if (!db_.transaction()) throw ErrorBD(db_.lastError().text().toStdString());
    }
    ~Transaccion() { if (!confirmada_) db_.rollback(); }
    void confirmar()
    {
        if (!db_.commit()) throw ErrorBD(db_.lastError().text().toStdString());
        confirmada_ = true;
    }
private:
    QSqlDatabase db_;
    bool confirmada_ = false;
};

inline QString textoBD(Tipo t) { return t == Tipo::Ingreso ? QStringLiteral("ingreso") : QStringLiteral("gasto"); }
inline Tipo tipoBD(const QString& s) { return s == QLatin1String("ingreso") ? Tipo::Ingreso : Tipo::Gasto; }
inline QString fechaBD(const QDate& d) { return d.toString(Qt::ISODate); }
inline QString texto(const QString& s) { return s.isNull() ? QString(QLatin1String("")) : s; }   // NOT NULL: nunca un QString nulo
inline qint64 ahora() { return QDateTime::currentMSecsSinceEpoch(); }
inline QString uuidNuevo() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

inline QString hash32(const QString& s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(32));
}

// En una transferencia también trae la otra mitad (p) y su cuenta (pc)
inline const char* const SELECT_MOVIMIENTO =
    "SELECT m.id, m.fecha, m.categoria_id, c.nombre, c.tipo, c.color, m.monto, m.descripcion, m.cuenta_id, COALESCE(cu.nombre, ''), "
    "c.uuid, COALESCE(p.cuenta_id, 0), COALESCE(pc.nombre, '') "
    "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id LEFT JOIN cuentas_dinero cu ON cu.id = m.cuenta_id "
    "LEFT JOIN movimientos p ON c.uuid IN ('transferencia-salida', 'transferencia-entrada') AND m.uuid LIKE 'tr-%' AND p.borrado = 0 "
    "     AND p.uuid = substr(m.uuid, 1, length(m.uuid) - 1) || CASE substr(m.uuid, -1) WHEN 's' THEN 'e' ELSE 's' END "
    "LEFT JOIN cuentas_dinero pc ON pc.id = p.cuenta_id "
    "WHERE m.borrado = 0 ";

inline Movimiento leerMovimiento(const QSqlQuery& q)
{
    Movimiento m;
    m.id = q.value(0).toLongLong();
    m.fecha = QDate::fromString(q.value(1).toString(), Qt::ISODate);
    m.categoriaId = q.value(2).toLongLong();
    m.categoria = q.value(3).toString();
    m.tipo = tipoBD(q.value(4).toString());
    m.color = q.value(5).toInt();
    m.monto = q.value(6).toLongLong();
    m.descripcion = q.value(7).toString();
    m.cuentaId = q.value(8).toLongLong();
    m.cuenta = q.value(9).toString();
    m.transferencia = esUuidTransferencia(q.value(10).toString());
    m.cuentaContrariaId = q.value(11).toLongLong();
    m.cuentaContraria = q.value(12).toString();
    return m;
}

// Para reconocer un movimiento repetido al importar
inline QString claveMovimiento(const QDate& fecha, Tipo tipo, const QString& categoria, Centavos monto, const QString& descripcion)
{
    return QStringList{fechaBD(fecha), textoBD(tipo), normalizar(categoria.simplified()), QString::number(monto),
                       normalizar(descripcion.simplified())}
        .join(QChar(0x1F));
}

// Para ordenar nombres alfabéticamente en castellano: sin mayúsculas ni tildes («Teléfono» va con la T) y con la ñ
// como letra aparte, entre la n y la o (normalizar() la dejaría como una n). La ñ queda como «n{»: la llave va después de la z.
inline QString claveOrden(const QString& nombre)
{
    QString t = nombre.simplified().normalized(QString::NormalizationForm_C).toCaseFolded();   // junta también la ñ escrita n + ~
    t.replace(QChar(0x00F1), QStringLiteral("n{"));
    return normalizar(t);
}

// Primero las de ingresos y, dentro de cada tipo, por orden alfabético. Se ordena acá y no con ORDER BY porque
// SQLite solo ignora las mayúsculas de las letras sin tilde: «Árboles» o «Ñandú» quedarían después de la Z.
inline void ordenarCategorias(QList<Categoria>& lista)
{
    QHash<qint64, QString> claves;      // una vez por categoría, no en cada comparación
    for (const Categoria& c : std::as_const(lista)) claves.insert(c.id, claveOrden(c.nombre));
    std::sort(lista.begin(), lista.end(), [&claves](const Categoria& a, const Categoria& b) {
        if (a.tipo != b.tipo) return a.tipo == Tipo::Ingreso;
        // Pedido del usuario: «Propinas Ariel» va primera entre los ingresos, antes del orden alfabético
        static const QString fijada = claveOrden(QStringLiteral("Propinas Ariel"));
        const bool fa = a.tipo == Tipo::Ingreso && claves.value(a.id) == fijada;
        const bool fb = b.tipo == Tipo::Ingreso && claves.value(b.id) == fijada;
        if (fa != fb) return fa;
        const QString ka = claves.value(a.id), kb = claves.value(b.id);
        if (ka != kb) return ka < kb;
        if (a.nombre != b.nombre) return a.nombre < b.nombre;      // «Telefono» y «Teléfono»: siempre en el mismo orden
        return a.id < b.id;
    });
}

inline QString errorAmigable(const std::exception& e)
{
    const QString msg = QString::fromUtf8(e.what());
    if (msg.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) return QStringLiteral("Ya existe una categoría con ese nombre.");
    if (msg.contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) return QStringLiteral("La categoría todavía tiene movimientos.");
    return msg;
}

// ¿Gana la versión remota? La más nueva; si son del mismo milisegundo, decide el autor (igual en los dos lados)
inline bool ganaRemota(qint64 modRemoto, const QString& autorRemoto, qint64 modLocal, const QString& autorLocal)
{
    if (modRemoto != modLocal) return modRemoto > modLocal;
    return autorRemoto > autorLocal;
}


} // namespace dbi
