#pragma once
#include "base.h"          // Par, ResultadoAplicar y BaseSinc (comun/sinc)
#include "csv.h"
#include "modelos.h"
#include <QDateTime>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QVector>
#include <optional>

struct Resultado {
    bool ok = true;
    QString error;
    qint64 id = 0;
};

struct ResultadoRespaldo {
    bool ok = true;
    bool hecha = false;             // false: la de hoy ya estaba
    QString ruta;                   // la copia de hoy
    QString error;
    int borradas = 0;               // copias viejas que se borraron (quedan las últimas)
};

struct ResultadoImportacion {
    int nuevos = 0;
    int repetidos = 0;              // ya estaban (misma fecha, categoría, monto y descripción): no se duplican
    QStringList categoriasNuevas;   // "Gasto: Farmacia"
    QStringList cuentasNuevas;      // las que nombra el archivo y no existen: se crean
};

/**
 * Base de datos (SQLite) de ingresos y gastos. La usan la versión de escritorio y la del celular.
 *
 * Para sincronizar, cada categoría, movimiento y el saldo inicial tienen:
 *   uuid        el mismo en todos los dispositivos
 *   modificado  cuándo se cambió por última vez (milisegundos) y autor = qué dispositivo
 *   cambio      número de cambio local (1, 2, 3...): "mandame lo que cambió después del 120"
 *   borrado     no se borra de verdad: se marca, así el otro dispositivo se entera
 * Si el mismo dato se cambió en los dos lados, gana el cambio más nuevo.
 */
class Cuentas : public BaseSinc
{
public:
    static QString rutaPorDefecto();              // Linux: ~/.local/share/ingresos-gastos/cuentas.db
    static constexpr int COLORES = 8;             // colores de la paleta de los gráficos
    static constexpr int VERSION_ESQUEMA = 3;     // 3: cuentas, presupuestos y movimientos fijos (2.1.0)

    explicit Cuentas(const QString& ruta);        // ":memory:" para pruebas
    ~Cuentas() override;
    Cuentas(const Cuentas&) = delete;
    Cuentas& operator=(const Cuentas&) = delete;

    bool abierta() const { return abierta_; }
    QString errorApertura() const { return errorApertura_; }
    QString ruta() const { return ruta_; }

    // ---- categorías
    // Ingresos primero, cada tipo por orden alfabético; con la cantidad de movimientos. Sin las dos categorías
    // internas de las transferencias (no se eligen ni se editan).
    QList<Categoria> categorias(std::optional<Tipo> tipo = std::nullopt) const;
    qint64 categoriaInicial(Tipo tipo) const;                   // con la que arranca un movimiento nuevo (ingresos: «Propinas Ariel» si existe; si no, la más antigua); 0 si no hay
    std::optional<Categoria> categoria(qint64 id) const;
    std::optional<Categoria> buscarCategoria(const QString& nombre, Tipo tipo) const;   // sin importar mayúsculas
    Resultado crearCategoria(const QString& nombre, Tipo tipo);
    Resultado renombrarCategoria(qint64 id, const QString& nombre);
    Resultado cambiarColor(qint64 id, int color);
    Resultado eliminarCategoria(qint64 id, qint64 moverA = 0);   // si tiene movimientos, hay que decir a cuál pasarlos

    // ---- movimientos
    QList<Movimiento> movimientos(const QDate& desde = {}, const QDate& hasta = {}) const;   // más nuevos primero
    std::optional<Movimiento> movimiento(qint64 id) const;
    Resultado crearMovimiento(const Movimiento& m);
    Resultado actualizarMovimiento(const Movimiento& m);
    Resultado eliminarMovimiento(qint64 id);
    Resultado restaurarMovimiento(qint64 id);        // deshace eliminarMovimiento (también llega al celular)
    // (en una transferencia, eliminar y restaurar cualquiera de las dos mitades hace lo mismo con la otra)

    // ---- transferencias entre cuentas: no son ingreso ni gasto (no cuentan en los totales, las tortas ni los
    // presupuestos), solo pasan plata de una cuenta a la otra. Se guardan como dos movimientos con dos
    // categorías internas («Transferencia», una de gasto y otra de ingreso, con uuid fijo): así se sincronizan
    // como cualquier movimiento y una versión vieja ve una salida y una entrada (los saldos le dan bien).
    Resultado guardarTransferencia(const Transferencia& t);          // id 0 = nueva; devuelve el id de la salida
    std::optional<Transferencia> transferencia(qint64 idMovimiento) const;   // desde cualquiera de las dos mitades

    // ---- cuentas de dinero (efectivo, banco, Mercado Pago...). Siempre está la principal.
    QList<CuentaDinero> cuentasDinero() const;                  // la principal primero; con saldo y movimientos
    qint64 cuentaPrincipal() const;
    Resultado crearCuentaDinero(const QString& nombre, Centavos saldoInicial = 0);
    Resultado renombrarCuentaDinero(qint64 id, const QString& nombre);
    Resultado fijarSaldoInicialCuenta(qint64 id, Centavos c);
    Resultado eliminarCuentaDinero(qint64 id, qint64 moverA = 0);   // la principal no; si tiene movimientos, a cuál pasarlos
    // Qué cuenta se está mirando (0 = todas): filtra movimientos(), totales, gráficos y saldo
    void verCuenta(qint64 id) { cuentaVista_ = id; }
    qint64 cuentaVista() const { return cuentaVista_; }

    // ---- presupuestos (por categoría de gasto, por mes; de todas las cuentas)
    Resultado fijarPresupuesto(qint64 categoriaId, Centavos mensual);   // 0 = sin presupuesto
    QList<EstadoPresupuesto> presupuestos(int anio, int mes) const;     // las que tienen presupuesto, de la más gastada a la menos

    // ---- movimientos fijos (se cargan solos todos los meses)
    QList<Recurrente> recurrentes() const;
    Resultado guardarRecurrente(const Recurrente& r);           // id 0 = uno nuevo
    Resultado eliminarRecurrente(qint64 id);
    // Carga lo que corresponda hasta hoy (incluido). Cada movimiento generado tiene un uuid que sale del
    // movimiento fijo y del mes: si la PC y el celular lo generan los dos, al sincronizar es el mismo y no
    // se duplica; y si se borra, no vuelve. Devuelve cuántos cargó.
    int generarRecurrentes(const QDate& hoy = QDate::currentDate());

    // ---- saldo y totales
    Centavos saldoInicial() const;                     // de la cuenta que se mira (o de todas)
    void fijarSaldoInicial(Centavos c);                // de la cuenta que se mira (o de la principal)
    Centavos saldo(const QDate& hasta = QDate::currentDate()) const;   // saldo inicial + ingresos - gastos hasta esa fecha
    TotalPeriodo totales(const QDate& desde, const QDate& hasta) const;
    QVector<TotalPeriodo> totalesPorMes(int anio) const;               // 12 meses
    QList<QPair<int, TotalPeriodo>> totalesPorAnio() const;            // cada año con movimientos
    QList<TotalCategoria> totalesPorCategoria(const QDate& desde, const QDate& hasta, Tipo tipo) const;   // de mayor a menor
    QList<TotalEtiqueta> totalesPorEtiqueta(const QDate& desde, const QDate& hasta) const;   // #etiquetas de las descripciones; de más a menos plata
    QDate primeraFecha() const;
    QDate ultimaFecha() const;

    // ---- importar
    ResultadoImportacion previsualizarImportacion(const QList<FilaImportada>& filas) const;
    ResultadoImportacion importar(const QList<FilaImportada>& filas);

    Resultado backup(const QString& destino);
    // Copia automática: una por día («cuentas-AAAA-MM-DD.db») en la carpeta; quedan las últimas `cuantas`.
    // Si la de hoy ya está, no hace nada. Con una base en memoria (pruebas) tampoco.
    ResultadoRespaldo respaldoAutomatico(const QString& carpeta, const QDate& hoy = QDate::currentDate(), int cuantas = 7);
    static QString carpetaRespaldos(const QString& rutaBase);   // «respaldos» al lado de la base

    // ---- sincronización
    QString dispositivo() const override { return dispositivo_; }
    QString nombreDispositivo() const override;
    void setNombreDispositivo(const QString& nombre);
    qint64 secuencia() const override;                                     // último número de cambio local
    QJsonArray cambiosDesde(qint64 desde) const override;                  // todo lo que cambió después de ese número
    QJsonArray filasPorUuid(const QStringList& uuids) const override;
    ResultadoAplicar aplicarCambios(const QJsonArray& filas) override;     // lo que mandó el otro (gana lo más nuevo)

    QList<Par> pares() const;
    std::optional<Par> par(const QString& dispositivo) const override;
    void guardarPar(const Par& p) override;
    void eliminarPar(const QString& dispositivo);

    // Mismo nombre y tipo = mismo uuid en todos los dispositivos (así «Supermercado» de la PC
    // y «Supermercado» del celular son la misma categoría y no aparecen dos veces)
    static QString uuidCategoria(Tipo tipo, const QString& nombre);

private:
    void crearEsquema();
    void migrarDesdeV1();
    void migrarDesdeV2();
    void asegurarCuentaPrincipal();
    void filtrarCuenta(QString& sql, QVariantList& params) const;   // «AND m.cuenta_id = ?» si se mira una cuenta
    QString nombreLibreCuenta(const QString& nombre, qint64 excepto) const;
    void crearCategoriasIniciales();
    QList<Categoria> todasLasCategorias(std::optional<Tipo> tipo = std::nullopt) const;   // también las de las transferencias
    QPair<qint64, qint64> asegurarCategoriasTransferencia();      // (salida, entrada); dentro de una transacción
    Centavos sumaHasta(const QDate& hasta) const;                  // ingresos - gastos con las transferencias (para el saldo)
    int colorLibre(Tipo tipo) const;
    qint64 siguienteCambio();
    void marcar(const char* tabla, qint64 id);                    // cambio local: fecha, autor y número de cambio
    qint64 insertarCategoria(const QString& nombre, Tipo tipo, const QString& uuid = QString());
    QString nombreLibre(const QString& nombre, Tipo tipo, qint64 excepto) const;
    QString config(const QString& clave, const QString& porDefecto = QString()) const;
    void fijarConfigLocal(const QString& clave, const QString& valor);
    ResultadoImportacion procesarImportacion(const QList<FilaImportada>& filas, bool guardar);

    QString ruta_;
    QString conexion_;
    mutable QSqlDatabase db_;
    bool abierta_ = false;
    QString errorApertura_;
    QString dispositivo_;
    qint64 cuentaVista_ = 0;
};
