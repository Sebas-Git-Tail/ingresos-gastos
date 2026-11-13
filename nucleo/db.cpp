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

namespace {

// Columnas que tienen todas las tablas que se sincronizan
#define COLUMNAS_SINC \
    " modificado INTEGER NOT NULL DEFAULT 0," \
    " autor TEXT NOT NULL DEFAULT ''," \
    " cambio INTEGER NOT NULL DEFAULT 0"

const char* const ESQUEMA[] = {
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
const QStringList CONFIG_SINCRONIZADA = {};

// La cuenta que trae el programa: mismo uuid en todos los dispositivos, así no se duplica al sincronizar
const QString UUID_PRINCIPAL = QStringLiteral("cuenta-principal");

// Categorías con las que arranca una base nueva (se pueden editar y agregar más)
const char* const GASTOS_INICIALES[] = {"Supermercado", "Transporte", "Vestimenta", "Internet", "Teléfono",
                                        "Servicios", "Salud", "Gastos Varios"};
const char* const INGRESOS_INICIALES[] = {"Sueldo", "Ingresos Varios"};

struct ErrorBD : std::runtime_error {
    using std::runtime_error::runtime_error;
};

QSqlQuery consulta(const QSqlDatabase& db, const QString& sql, const QVariantList& params = {})
{
    QSqlQuery q(db);
    if (!q.prepare(sql)) throw ErrorBD(q.lastError().text().toStdString());
    for (const QVariant& v : params) q.addBindValue(v);
    if (!q.exec()) throw ErrorBD(q.lastError().text().toStdString());
    return q;
}

QVariant valor(const QSqlDatabase& db, const QString& sql, const QVariantList& params = {})
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

QString textoBD(Tipo t) { return t == Tipo::Ingreso ? QStringLiteral("ingreso") : QStringLiteral("gasto"); }
Tipo tipoBD(const QString& s) { return s == QLatin1String("ingreso") ? Tipo::Ingreso : Tipo::Gasto; }
QString fechaBD(const QDate& d) { return d.toString(Qt::ISODate); }
QString texto(const QString& s) { return s.isNull() ? QString(QLatin1String("")) : s; }   // NOT NULL: nunca un QString nulo
qint64 ahora() { return QDateTime::currentMSecsSinceEpoch(); }
QString uuidNuevo() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString hash32(const QString& s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(32));
}

const char* const SELECT_MOVIMIENTO =
    "SELECT m.id, m.fecha, m.categoria_id, c.nombre, c.tipo, c.color, m.monto, m.descripcion, m.cuenta_id, COALESCE(cu.nombre, '') "
    "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id LEFT JOIN cuentas_dinero cu ON cu.id = m.cuenta_id "
    "WHERE m.borrado = 0 ";

Movimiento leerMovimiento(const QSqlQuery& q)
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
    return m;
}

// Para reconocer un movimiento repetido al importar
QString claveMovimiento(const QDate& fecha, Tipo tipo, const QString& categoria, Centavos monto, const QString& descripcion)
{
    return QStringList{fechaBD(fecha), textoBD(tipo), normalizar(categoria.simplified()), QString::number(monto),
                       normalizar(descripcion.simplified())}
        .join(QChar(0x1F));
}

// Para ordenar nombres alfabéticamente en castellano: sin mayúsculas ni tildes («Teléfono» va con la T) y con la ñ
// como letra aparte, entre la n y la o (normalizar() la dejaría como una n). La ñ queda como «n{»: la llave va después de la z.
QString claveOrden(const QString& nombre)
{
    QString t = nombre.simplified().normalized(QString::NormalizationForm_C).toCaseFolded();   // junta también la ñ escrita n + ~
    t.replace(QChar(0x00F1), QStringLiteral("n{"));
    return normalizar(t);
}

// Primero las de ingresos y, dentro de cada tipo, por orden alfabético. Se ordena acá y no con ORDER BY porque
// SQLite solo ignora las mayúsculas de las letras sin tilde: «Árboles» o «Ñandú» quedarían después de la Z.
void ordenarCategorias(QList<Categoria>& lista)
{
    QHash<qint64, QString> claves;      // una vez por categoría, no en cada comparación
    for (const Categoria& c : std::as_const(lista)) claves.insert(c.id, claveOrden(c.nombre));
    std::sort(lista.begin(), lista.end(), [&claves](const Categoria& a, const Categoria& b) {
        if (a.tipo != b.tipo) return a.tipo == Tipo::Ingreso;
        const QString ka = claves.value(a.id), kb = claves.value(b.id);
        if (ka != kb) return ka < kb;
        if (a.nombre != b.nombre) return a.nombre < b.nombre;      // «Telefono» y «Teléfono»: siempre en el mismo orden
        return a.id < b.id;
    });
}

QString errorAmigable(const std::exception& e)
{
    const QString msg = QString::fromUtf8(e.what());
    if (msg.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) return QStringLiteral("Ya existe una categoría con ese nombre.");
    if (msg.contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) return QStringLiteral("La categoría todavía tiene movimientos.");
    return msg;
}

// ¿Gana la versión remota? La más nueva; si son del mismo milisegundo, decide el autor (igual en los dos lados)
bool ganaRemota(qint64 modRemoto, const QString& autorRemoto, qint64 modLocal, const QString& autorLocal)
{
    if (modRemoto != modLocal) return modRemoto > modLocal;
    return autorRemoto > autorLocal;
}

} // namespace

// ====================================================================== apertura

QString Cuentas::rutaPorDefecto()
{
#ifdef Q_OS_ANDROID
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/cuentas.db");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/ingresos-gastos/cuentas.db");
#endif
}

QString Cuentas::uuidCategoria(Tipo tipo, const QString& nombre)
{
    return QStringLiteral("cat-") + hash32(QStringLiteral("ig|") + textoBD(tipo) + QLatin1Char('|') + normalizar(nombre.simplified()));
}

Cuentas::Cuentas(const QString& ruta)
    : ruta_(ruta)
{
    static std::atomic<int> contador{0};
    conexion_ = QStringLiteral("cuentas_%1").arg(contador++);
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conexion_);
    if (ruta != QLatin1String(":memory:")) QDir().mkpath(QFileInfo(ruta).absolutePath());
    db_.setDatabaseName(ruta);
    if (!db_.open()) {
        errorApertura_ = db_.lastError().text();
        return;
    }
    try {
        const int version = valor(db_, "PRAGMA user_version").toInt();
        const bool hayTablas = valor(db_, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'categorias'").toInt() > 0;
        if (version > VERSION_ESQUEMA)
            throw ErrorBD("La base es de una versión más nueva del programa. Actualizá Ingresos y Gastos.");
        if (hayTablas && version < 2) migrarDesdeV1();          // base de la versión 1.0.x
        else if (hayTablas && version == 2) migrarDesdeV2();    // base de la 2.0.x
        else crearEsquema();
        consulta(db_, "PRAGMA foreign_keys = ON");

        dispositivo_ = config(QStringLiteral("dispositivo"));
        if (dispositivo_.isEmpty()) {
            dispositivo_ = uuidNuevo();
            fijarConfigLocal(QStringLiteral("dispositivo"), dispositivo_);
        }
        if (valor(db_, "SELECT COUNT(*) FROM categorias").toInt() == 0) crearCategoriasIniciales();
        asegurarCuentaPrincipal();
        abierta_ = true;
    } catch (const std::exception& e) {
        errorApertura_ = QString::fromUtf8(e.what());
    }
}

Cuentas::~Cuentas()
{
    if (db_.isOpen()) db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(conexion_);
}

void Cuentas::crearEsquema()
{
    for (const char* sql : ESQUEMA) consulta(db_, QString::fromLatin1(sql));
    consulta(db_, QStringLiteral("PRAGMA user_version = %1").arg(VERSION_ESQUEMA));
}

// La 1.0.x no tenía uuid, fechas de cambio ni borrado lógico: se rehacen las tablas con todo adentro
void Cuentas::migrarDesdeV1()
{
    consulta(db_, "PRAGMA foreign_keys = OFF");
    Transaccion t(db_);
    consulta(db_, "ALTER TABLE categorias RENAME TO categorias_v1");
    consulta(db_, "ALTER TABLE movimientos RENAME TO movimientos_v1");
    consulta(db_, "ALTER TABLE config RENAME TO config_v1");
    consulta(db_, "DROP INDEX IF EXISTS idx_movimientos_fecha");
    consulta(db_, "DROP INDEX IF EXISTS idx_movimientos_categoria");
    for (const char* sql : ESQUEMA) consulta(db_, QString::fromLatin1(sql));

    dispositivo_ = uuidNuevo();
    const qint64 mod = ahora();
    qint64 cambio = 0;
    consulta(db_, "INSERT INTO config (clave, valor, modificado, autor, cambio) SELECT clave, valor, ?, ?, 0 FROM config_v1",
             {mod, dispositivo_});
    consulta(db_, "UPDATE config SET cambio = ? WHERE clave = 'saldo_inicial'", {++cambio});

    QSet<QString> usados;
    QSqlQuery q = consulta(db_, "SELECT id, nombre, tipo, color FROM categorias_v1 ORDER BY id");
    while (q.next()) {
        const Tipo tipo = tipoBD(q.value(2).toString());
        QString uuid = uuidCategoria(tipo, q.value(1).toString());
        if (usados.contains(uuid)) uuid = uuidNuevo();          // "Teléfono" y "Telefono": la segunda, al azar
        usados.insert(uuid);
        consulta(db_, "INSERT INTO categorias (id, uuid, nombre, tipo, color, modificado, autor, cambio) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                 {q.value(0), uuid, q.value(1), q.value(2), q.value(3), mod, dispositivo_, ++cambio});
    }
    q = consulta(db_, "SELECT id, fecha, categoria_id, monto, descripcion FROM movimientos_v1 ORDER BY id");
    while (q.next())
        consulta(db_, "INSERT INTO movimientos (id, uuid, fecha, categoria_id, monto, descripcion, modificado, autor, cambio) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                 {q.value(0), uuidNuevo(), q.value(1), q.value(2), q.value(3), q.value(4), mod, dispositivo_, ++cambio});

    consulta(db_, "DROP TABLE movimientos_v1");
    consulta(db_, "DROP TABLE categorias_v1");
    consulta(db_, "DROP TABLE config_v1");
    consulta(db_, "INSERT OR REPLACE INTO config (clave, valor) VALUES ('dispositivo', ?)", {dispositivo_});
    consulta(db_, "INSERT OR REPLACE INTO config (clave, valor) VALUES ('secuencia', ?)", {QString::number(cambio)});
    consulta(db_, QStringLiteral("PRAGMA user_version = %1").arg(VERSION_ESQUEMA));
    t.confirmar();
}

// 2.0.x -> 2.1.0: presupuesto en las categorías, cuenta en los movimientos y las tablas nuevas
void Cuentas::migrarDesdeV2()
{
    Transaccion t(db_);
    consulta(db_, "ALTER TABLE categorias ADD COLUMN presupuesto INTEGER NOT NULL DEFAULT 0");
    consulta(db_, "ALTER TABLE movimientos ADD COLUMN cuenta_id INTEGER NOT NULL DEFAULT 0");
    for (const char* sql : ESQUEMA) consulta(db_, QString::fromLatin1(sql));     // las tablas e índices que falten
    consulta(db_, QStringLiteral("PRAGMA user_version = %1").arg(VERSION_ESQUEMA));
    t.confirmar();
}

// La cuenta principal: si no está, se crea con el saldo inicial de antes (que era uno solo) y los
// movimientos sin cuenta pasan a ella. Mismo uuid en todos los dispositivos y la misma fecha de cambio
// que tenía el saldo inicial (que ya estaba sincronizado): así la PC y el celular la crean igual.
void Cuentas::asegurarCuentaPrincipal()
{
    Transaccion t(db_);
    if (!valor(db_, "SELECT id FROM cuentas_dinero WHERE uuid = ?", {UUID_PRINCIPAL}).isValid()) {
        qint64 mod = 1;
        QString autor, saldo = QStringLiteral("0");
        QSqlQuery q = consulta(db_, "SELECT valor, modificado, autor FROM config WHERE clave = 'saldo_inicial'");
        if (q.next()) {
            saldo = q.value(0).toString();
            mod = qMax<qint64>(1, q.value(1).toLongLong());
            autor = q.value(2).toString();
        }
        consulta(db_, "INSERT INTO cuentas_dinero (uuid, nombre, saldo_inicial, modificado, autor, cambio) VALUES (?, ?, ?, ?, ?, ?)",
                 {UUID_PRINCIPAL, nombreLibreCuenta(QStringLiteral("Principal"), 0), saldo.toLongLong(), mod, texto(autor), siguienteCambio()});
    }
    // Movimientos y fijos sin cuenta (de antes, o que llegaron sin ella): a la principal. No es un cambio
    // que haya que sincronizar: en todos lados «sin cuenta» quiere decir la principal.
    const qint64 principal = cuentaPrincipal();
    consulta(db_, "UPDATE movimientos SET cuenta_id = ? WHERE cuenta_id = 0", {principal});
    consulta(db_, "UPDATE recurrentes SET cuenta_id = ? WHERE cuenta_id = 0", {principal});
    t.confirmar();
}

void Cuentas::crearCategoriasIniciales()
{
    // Mismo uuid, misma fecha (1) y sin autor en todos los dispositivos: al sincronizar por primera vez
    // las categorías de fábrica de la PC y del celular son idénticas y no se duplican
    Transaccion t(db_);
    for (Tipo tipo : {Tipo::Gasto, Tipo::Ingreso}) {
        int color = 0;
        const auto agregar = [&](const char* n) {
            const QString nombre = QString::fromUtf8(n);
            consulta(db_, "INSERT INTO categorias (uuid, nombre, tipo, color, modificado, autor, cambio) VALUES (?, ?, ?, ?, 1, '', ?)",
                     {uuidCategoria(tipo, nombre), nombre, textoBD(tipo), color++ % COLORES, siguienteCambio()});
        };
        if (tipo == Tipo::Gasto) for (const char* n : GASTOS_INICIALES) agregar(n);
        else for (const char* n : INGRESOS_INICIALES) agregar(n);
    }
    t.confirmar();
}

QString Cuentas::config(const QString& clave, const QString& porDefecto) const
{
    const QVariant v = valor(db_, "SELECT valor FROM config WHERE clave = ?", {clave});
    return v.isValid() ? v.toString() : porDefecto;
}

void Cuentas::fijarConfigLocal(const QString& clave, const QString& v)
{
    consulta(db_, "INSERT INTO config (clave, valor) VALUES (?, ?) ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor",
             {clave, texto(v)});
}

qint64 Cuentas::siguienteCambio()
{
    const qint64 n = config(QStringLiteral("secuencia"), QStringLiteral("0")).toLongLong() + 1;
    fijarConfigLocal(QStringLiteral("secuencia"), QString::number(n));
    return n;
}

qint64 Cuentas::secuencia() const
{
    try {
        return config(QStringLiteral("secuencia"), QStringLiteral("0")).toLongLong();
    } catch (const std::exception&) {
        return 0;
    }
}

void Cuentas::marcar(const char* tabla, qint64 id)
{
    // Nunca más viejo que la versión que reemplaza (por si el reloj del otro dispositivo estaba adelantado)
    consulta(db_, QStringLiteral("UPDATE %1 SET modificado = MAX(?, modificado + 1), autor = ?, cambio = ? WHERE id = ?")
                      .arg(QLatin1String(tabla)),
             {ahora(), dispositivo_, siguienteCambio(), id});
}

QString Cuentas::nombreDispositivo() const
{
    try {
        QString porDefecto;
#ifdef Q_OS_ANDROID
        porDefecto = QStringLiteral("Celular");
#else
        porDefecto = QStringLiteral("PC (%1)").arg(QSysInfo::machineHostName());
#endif
        return config(QStringLiteral("nombre_dispositivo"), porDefecto);
    } catch (const std::exception&) {
        return QString();
    }
}

void Cuentas::setNombreDispositivo(const QString& nombre)
{
    try {
        fijarConfigLocal(QStringLiteral("nombre_dispositivo"), nombre.simplified());
    } catch (const std::exception&) {
    }
}

// ====================================================================== categorías

QList<Categoria> Cuentas::categorias(std::optional<Tipo> tipo) const
{
    QList<Categoria> lista;
    try {
        QString sql = QStringLiteral(
            "SELECT c.id, c.nombre, c.tipo, c.color, "
            "(SELECT COUNT(*) FROM movimientos m WHERE m.categoria_id = c.id AND m.borrado = 0), c.presupuesto "
            "FROM categorias c WHERE c.borrado = 0 ");
        QVariantList params;
        if (tipo) {
            sql += QStringLiteral("AND c.tipo = ? ");
            params << textoBD(*tipo);
        }
        QSqlQuery q = consulta(db_, sql, params);
        while (q.next())
            lista << Categoria{q.value(0).toLongLong(), q.value(1).toString(), tipoBD(q.value(2).toString()), q.value(3).toInt(),
                               q.value(4).toInt(), q.value(5).toLongLong()};
    } catch (const std::exception&) {
    }
    ordenarCategorias(lista);       // así una categoría nueva (o renombrada) queda en su lugar sin hacer nada más
    return lista;
}

// La más antigua (Supermercado / Sueldo en una base nueva). No sirve «la primera de la lista»: por orden alfabético
// sería «Gastos Varios» / «Ingresos Varios», que quedan como opción y no como valor inicial.
qint64 Cuentas::categoriaInicial(Tipo tipo) const
{
    try {
        return valor(db_, "SELECT MIN(id) FROM categorias WHERE borrado = 0 AND tipo = ?", {textoBD(tipo)}).toLongLong();   // sin ninguna: NULL, o sea 0
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
    for (const Categoria& c : categorias(tipo)) if (normalizar(c.nombre) == buscado) return c;
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
        t.confirmar();
        return {true, QString(), id};
    } catch (const std::exception& e) {
        return {false, errorAmigable(e), id};
    }
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
        QString sql = QStringLiteral("SELECT c.tipo, SUM(m.monto) FROM movimientos m JOIN categorias c ON c.id = m.categoria_id WHERE m.borrado = 0 ");
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
    return saldoInicial() + totales({}, hasta).saldo();
}

QVector<TotalPeriodo> Cuentas::totalesPorMes(int anio) const
{
    QVector<TotalPeriodo> meses(12);
    try {
        QString sql = QStringLiteral(
            "SELECT CAST(substr(m.fecha, 6, 2) AS INTEGER), c.tipo, SUM(m.monto) "
            "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id "
            "WHERE m.borrado = 0 AND m.fecha >= ? AND m.fecha <= ? ");
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
            "FROM movimientos m JOIN categorias c ON c.id = m.categoria_id WHERE m.borrado = 0 ");
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
            "WHERE m.borrado = 0 AND c.tipo = ? ");
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

    // Categorías por nombre (las que se vayan creando también)
    QHash<QString, qint64> ids;
    for (const Categoria& c : categorias()) ids.insert(textoBD(c.tipo) + QChar(0x1F) + normalizar(c.nombre), c.id);

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
        if (idCategoria == 0) {
            r.categoriasNuevas << QStringLiteral("%1: %2").arg(textoTipo(f.tipo), nombreCategoria);
            idCategoria = guardar ? insertarCategoria(nombreCategoria, f.tipo) : -1;   // en la vista previa alcanza con contarla
            ids.insert(claveCategoria, idCategoria);
        }
        if (guardar) {
            // uuid sacado del contenido: si el mismo archivo se importa en la PC y en el celular,
            // al sincronizar son los mismos movimientos y no quedan duplicados
            const QString uuid = QStringLiteral("imp-") + hash32(clave + QLatin1Char('#') + QString::number(vez));
            const bool yaEsta = valor(db_, "SELECT COUNT(*) FROM movimientos WHERE uuid = ?", {uuid}).toInt() > 0;
            QSqlQuery q = consulta(db_, "INSERT INTO movimientos (uuid, fecha, categoria_id, monto, descripcion, cuenta_id) VALUES (?, ?, ?, ?, ?, ?)",
                                   {yaEsta ? uuidNuevo() : uuid, fechaBD(f.fecha), idCategoria, f.monto, texto(f.descripcion.trimmed()),
                                    cuentaVista_ ? cuentaVista_ : cuentaPrincipal()});
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
