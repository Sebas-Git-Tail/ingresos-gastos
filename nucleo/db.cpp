// Base de datos: apertura, esquema, migraciones y configuración del dispositivo.
#include "db_interno.h"

using namespace dbi;

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
