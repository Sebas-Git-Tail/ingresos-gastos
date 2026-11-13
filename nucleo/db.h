#pragma once
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

struct ResultadoImportacion {
    int nuevos = 0;
    int repetidos = 0;              // ya estaban (misma fecha, categoría, monto y descripción): no se duplican
    QStringList categoriasNuevas;   // "Gasto: Farmacia"
};

/** Otro dispositivo con el que se sincroniza (en la PC: los celulares; en el celular: la PC). */
struct Par {
    QString dispositivo;            // identificador del otro dispositivo
    QString nombre;                 // "PC de Sebastián", "Celular"
    QString secreto;                // clave compartida que se acordó al emparejar
    QString host;                   // (celular) dirección de la PC
    int puerto = 0;
    qint64 enviado = 0;             // (celular) hasta qué cambio local ya le mandé
    qint64 recibido = 0;            // (celular) hasta qué cambio de la PC ya recibí
    QDateTime ultimaSinc;
};

struct ResultadoAplicar {
    bool ok = true;
    QString error;
    int aplicadas = 0;              // filas del otro que cambiaron algo acá
    int ignoradas = 0;              // filas que no se pudieron usar (dato roto)
    QStringList aceptadas;          // uuids que se tomaron del otro (no hace falta devolvérselos)
    QStringList ganadorasLocales;   // filas donde la versión de acá es más nueva, o que acá se tuvieron
                                    // que cambiar (nombre repetido, categoría revivida): hay que mandarlas
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
class Cuentas
{
public:
    static QString rutaPorDefecto();              // Linux: ~/.local/share/ingresos-gastos/cuentas.db
    static constexpr int COLORES = 8;             // colores de la paleta de los gráficos
    static constexpr int VERSION_ESQUEMA = 2;

    explicit Cuentas(const QString& ruta);        // ":memory:" para pruebas
    ~Cuentas();
    Cuentas(const Cuentas&) = delete;
    Cuentas& operator=(const Cuentas&) = delete;

    bool abierta() const { return abierta_; }
    QString errorApertura() const { return errorApertura_; }
    QString ruta() const { return ruta_; }

    // ---- categorías
    QList<Categoria> categorias(std::optional<Tipo> tipo = std::nullopt) const;   // ingresos primero, cada tipo por orden alfabético; con la cantidad de movimientos
    qint64 categoriaInicial(Tipo tipo) const;                   // con la que arranca un movimiento nuevo (la más antigua); 0 si no hay
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

    // ---- saldo y totales
    Centavos saldoInicial() const;
    void fijarSaldoInicial(Centavos c);
    Centavos saldo(const QDate& hasta = QDate::currentDate()) const;   // saldo inicial + ingresos - gastos hasta esa fecha
    TotalPeriodo totales(const QDate& desde, const QDate& hasta) const;
    QVector<TotalPeriodo> totalesPorMes(int anio) const;               // 12 meses
    QList<QPair<int, TotalPeriodo>> totalesPorAnio() const;            // cada año con movimientos
    QList<TotalCategoria> totalesPorCategoria(const QDate& desde, const QDate& hasta, Tipo tipo) const;   // de mayor a menor
    QDate primeraFecha() const;
    QDate ultimaFecha() const;

    // ---- importar
    ResultadoImportacion previsualizarImportacion(const QList<FilaImportada>& filas) const;
    ResultadoImportacion importar(const QList<FilaImportada>& filas);

    Resultado backup(const QString& destino);

    // ---- sincronización
    QString dispositivo() const { return dispositivo_; }
    QString nombreDispositivo() const;
    void setNombreDispositivo(const QString& nombre);
    qint64 secuencia() const;                                     // último número de cambio local
    QJsonArray cambiosDesde(qint64 desde) const;                  // todo lo que cambió después de ese número
    QJsonArray filasPorUuid(const QStringList& uuids) const;
    ResultadoAplicar aplicarCambios(const QJsonArray& filas);     // lo que mandó el otro (gana lo más nuevo)

    QList<Par> pares() const;
    std::optional<Par> par(const QString& dispositivo) const;
    void guardarPar(const Par& p);
    void eliminarPar(const QString& dispositivo);

    // Mismo nombre y tipo = mismo uuid en todos los dispositivos (así «Supermercado» de la PC
    // y «Supermercado» del celular son la misma categoría y no aparecen dos veces)
    static QString uuidCategoria(Tipo tipo, const QString& nombre);

private:
    void crearEsquema();
    void migrarDesdeV1();
    void crearCategoriasIniciales();
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
};
