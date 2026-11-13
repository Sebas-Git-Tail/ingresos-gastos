#pragma once
#include "db.h"
#include "modelomovimientos.h"
#include <QDate>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ClienteSinc;
class QQmlEngine;
class QJSEngine;

/**
 * Todo lo que usan las pantallas QML del celular. En QML se llama «App» (import IngresosGastos).
 * Hace lo mismo que la ventana principal de la PC: período, saldo, totales, gráficos,
 * movimientos, categorías, CSV, copia de seguridad, y además sincroniza con la PC.
 */
class Controlador : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    // Período (0 = mes, 1 = año, 2 = todo)
    Q_PROPERTY(int vista READ vista WRITE setVista NOTIFY cambio)
    Q_PROPERTY(QString periodoTexto READ periodoTexto NOTIFY cambio)
    Q_PROPERTY(bool navegable READ navegable NOTIFY cambio)
    // Tarjetas
    Q_PROPERTY(QString saldoTexto READ saldoTexto NOTIFY cambio)
    Q_PROPERTY(QString saldoAyuda READ saldoAyuda NOTIFY cambio)
    Q_PROPERTY(QString ingresosTexto READ ingresosTexto NOTIFY cambio)
    Q_PROPERTY(QString gastosTexto READ gastosTexto NOTIFY cambio)
    Q_PROPERTY(QString resultadoTexto READ resultadoTexto NOTIFY cambio)
    // Movimientos
    Q_PROPERTY(ModeloMovimientos* movimientos READ movimientos CONSTANT)
    Q_PROPERTY(QString busqueda READ busqueda WRITE setBusqueda NOTIFY cambio)
    Q_PROPERTY(int filtroTipo READ filtroTipo WRITE setFiltroTipo NOTIFY cambio)
    Q_PROPERTY(QString cantidadTexto READ cantidadTexto NOTIFY cambio)
    // Gráficos y totales
    Q_PROPERTY(QVariantMap barras READ barras NOTIFY cambio)
    Q_PROPERTY(QVariantMap tortaGastos READ tortaGastos NOTIFY cambio)
    Q_PROPERTY(QVariantMap tortaIngresos READ tortaIngresos NOTIFY cambio)
    Q_PROPERTY(QString tituloMeses READ tituloMeses NOTIFY cambio)
    Q_PROPERTY(QVariantList totalesMeses READ totalesMeses NOTIFY cambio)
    Q_PROPERTY(QString tituloCategorias READ periodoTexto NOTIFY cambio)
    Q_PROPERTY(QVariantList totalesCategorias READ totalesCategorias NOTIFY cambio)
    Q_PROPERTY(QVariantList totalesAnios READ totalesAnios NOTIFY cambio)
    // Tema (lo pone QML según el tema del celular)
    Q_PROPERTY(bool oscuro READ oscuro WRITE setOscuro NOTIFY oscuroChanged)
    Q_PROPERTY(QVariantList colores READ colores NOTIFY oscuroChanged)
    // Sincronización
    Q_PROPERTY(bool pcEmparejada READ pcEmparejada NOTIFY sincCambio)
    Q_PROPERTY(QString nombrePc READ nombrePc NOTIFY sincCambio)
    Q_PROPERTY(QString direccionPc READ direccionPc NOTIFY sincCambio)
    Q_PROPERTY(QString ultimaSincTexto READ ultimaSincTexto NOTIFY sincCambio)
    Q_PROPERTY(bool sincronizando READ sincronizando NOTIFY sincCambio)
    Q_PROPERTY(QString estadoSinc READ estadoSinc NOTIFY sincCambio)
    Q_PROPERTY(QString nombreDispositivo READ nombreDispositivo WRITE setNombreDispositivo NOTIFY sincCambio)
    Q_PROPERTY(int puertoPorDefecto READ puertoPorDefecto CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString rutaBase READ rutaBase CONSTANT)
    // ---- 2.1.0: cuentas, presupuestos y movimientos fijos
    Q_PROPERTY(QVariantList cuentas READ cuentas NOTIFY cambio)           // [{id, nombre, saldo, saldoInicial, principal, movimientos}]
    Q_PROPERTY(qint64 cuentaVista READ cuentaVista WRITE setCuentaVista NOTIFY cambio)   // 0 = todas
    Q_PROPERTY(QVariantList presupuestos READ presupuestos NOTIFY cambio) // del mes que se mira: [{nombre, gastado, presupuesto, porcentaje, pasado}]
    Q_PROPERTY(QString avisoPresupuesto READ avisoPresupuesto NOTIFY cambio)
    Q_PROPERTY(QVariantList fijos READ fijos NOTIFY cambio)

public:
    explicit Controlador(Cuentas& cuentas, QObject* parent = nullptr);
    ~Controlador() override;
    static Controlador* create(QQmlEngine*, QJSEngine*);   // para QML_SINGLETON
    static void setInstancia(Controlador* c);

    int vista() const { return vista_; }
    void setVista(int v);
    QString periodoTexto() const;
    bool navegable() const { return vista_ != 2; }
    QString saldoTexto() const { return saldoTexto_; }
    QString saldoAyuda() const { return saldoAyuda_; }
    QString ingresosTexto() const { return ingresosTexto_; }
    QString gastosTexto() const { return gastosTexto_; }
    QString resultadoTexto() const { return resultadoTexto_; }
    ModeloMovimientos* movimientos() { return &modelo_; }
    QString busqueda() const { return busqueda_; }
    void setBusqueda(const QString& b);
    int filtroTipo() const { return filtroTipo_; }
    void setFiltroTipo(int f);
    QString cantidadTexto() const { return cantidadTexto_; }
    QVariantMap barras() const { return barras_; }
    QVariantMap tortaGastos() const { return tortaGastos_; }
    QVariantMap tortaIngresos() const { return tortaIngresos_; }
    QString tituloMeses() const { return QStringLiteral("Por mes — %1").arg(periodo_.year()); }
    QVariantList totalesMeses() const { return totalesMeses_; }
    QVariantList totalesCategorias() const { return totalesCategorias_; }
    QVariantList totalesAnios() const { return totalesAnios_; }
    bool oscuro() const { return oscuro_; }
    void setOscuro(bool o);
    QVariantList colores() const;
    bool pcEmparejada() const;
    QString nombrePc() const;
    QString direccionPc() const;
    QString ultimaSincTexto() const;
    bool sincronizando() const { return sincronizando_; }
    QString estadoSinc() const { return estadoSinc_; }
    QString nombreDispositivo() const { return cuentas_.nombreDispositivo(); }
    void setNombreDispositivo(const QString& n);
    int puertoPorDefecto() const;
    QString version() const;
    QString rutaBase() const { return cuentas_.ruta(); }
    QVariantList cuentas() const;
    qint64 cuentaVista() const { return cuentas_.cuentaVista(); }
    void setCuentaVista(qint64 id);
    QVariantList presupuestos() const;
    QString avisoPresupuesto() const;
    QVariantList fijos() const;

    // ---- cuentas. Los que devuelven QString dan "" si salió bien; si no, el error
    Q_INVOKABLE QVariantMap crearCuenta(const QString& nombre, const QString& saldoInicial);   // {ok, error, id}
    Q_INVOKABLE QString renombrarCuenta(qint64 id, const QString& nombre);
    Q_INVOKABLE QString fijarSaldoInicialCuenta(qint64 id, const QString& texto);
    Q_INVOKABLE QString eliminarCuenta(qint64 id, qint64 moverA);

    // ---- presupuestos
    Q_INVOKABLE QString fijarPresupuesto(qint64 categoriaId, const QString& texto);   // vacío o 0 = sin presupuesto

    // ---- movimientos fijos
    Q_INVOKABLE QVariantMap nuevoFijo(int tipo) const;
    Q_INVOKABLE QVariantMap fijo(qint64 id) const;
    Q_INVOKABLE QVariantMap fijoDesdeMovimiento(qint64 idMovimiento) const;   // «Repetir todos los meses»
    Q_INVOKABLE QString guardarFijo(const QVariantMap& datos);
    Q_INVOKABLE QString eliminarFijo(qint64 id);
    Q_INVOKABLE int cargarFijos();                     // carga los que correspondan hasta hoy (al abrir y al volver a la app)

    QPair<QDate, QDate> rango() const;
    QDate periodo() const { return periodo_; }

    // ---- período
    Q_INVOKABLE void anterior();
    Q_INVOKABLE void siguiente();
    Q_INVOKABLE void hoy();
    Q_INVOKABLE void irAPeriodo(int indice);         // tocar una barra: ir a ese mes (o año, en «Todo»)

    // ---- movimientos ({id, tipo (0 ingreso / 1 gasto), fechaIso, fechaTexto, categoriaId, monto (texto), descripcion})
    Q_INVOKABLE QVariantMap nuevoMovimiento(int tipo) const;
    Q_INVOKABLE QVariantMap movimiento(qint64 id) const;
    Q_INVOKABLE QVariantMap repetir(qint64 id) const;
    Q_INVOKABLE QString guardarMovimiento(const QVariantMap& m);   // "" si salió bien; si no, el error
    Q_INVOKABLE QString eliminarMovimiento(qint64 id);

    // ---- categorías
    Q_INVOKABLE QVariantList categorias(int tipo) const;           // [{id, nombre, color, colorIndice, movimientos}], por orden alfabético
    Q_INVOKABLE qint64 categoriaInicial(int tipo) const;           // con la que arranca un movimiento nuevo (0 si no hay)
    Q_INVOKABLE QVariantMap crearCategoria(const QString& nombre, int tipo);   // {ok, error, id}
    Q_INVOKABLE QString renombrarCategoria(qint64 id, const QString& nombre);
    Q_INVOKABLE QString cambiarColor(qint64 id, int color);
    Q_INVOKABLE QString eliminarCategoria(qint64 id, qint64 moverA);

    // ---- saldo inicial
    Q_INVOKABLE QString saldoInicialTexto() const;
    Q_INVOKABLE bool fijarSaldoInicial(const QString& texto);

    // ---- archivos (en Android llegan como content://…)
    Q_INVOKABLE QVariantMap previsualizarImportacion(const QUrl& archivo);   // {ok, error, nuevos, repetidos, categorias, avisos}
    Q_INVOKABLE QVariantMap importar(const QUrl& archivo);
    Q_INVOKABLE QString exportar(const QUrl& archivo);
    Q_INVOKABLE QString backup(const QUrl& archivo);
    Q_INVOKABLE QString nombreExportacion() const;
    Q_INVOKABLE QString nombreBackup() const;

    // ---- ayudas para QML
    Q_INVOKABLE QString fechaTexto(const QString& iso) const;      // "Domingo 1 de noviembre de 2026"
    Q_INVOKABLE QString hoyIso() const { return QDate::currentDate().toString(Qt::ISODate); }

    // ---- sincronización
    Q_INVOKABLE void emparejar(const QString& host, int puerto, const QString& codigo);
    Q_INVOKABLE void sincronizar();
    Q_INVOKABLE void sincronizarSiCorresponde();     // al volver a la app: en silencio, si hay PC emparejada
    Q_INVOKABLE void buscarPcs();
    Q_INVOKABLE void olvidarPc();

public slots:
    void refrescar();

signals:
    void cambio();
    void oscuroChanged();
    void sincCambio();
    void aviso(const QString& texto);                // mensajito abajo de la pantalla
    void pcEncontrada(const QString& nombre, const QString& host, int puerto);
    void busquedaTerminada();
    void sincTerminada(bool ok, const QString& mensaje);

private:
    void setPeriodo(const QDate& f);
    void cargarMovimientos();
    void cargarTarjetas();
    void cargarGraficos();
    void cargarTotales();
    void programarSincronizacion();                  // un rato después de cada cambio, si hay PC emparejada
    QString textoPeriodoDe(int vista, const QDate& periodo) const;
    void mostrarImportados(const QList<FilaImportada>& filas);

    Cuentas& cuentas_;
    ModeloMovimientos modelo_;
    ClienteSinc* cliente_ = nullptr;
    QTimer temporizadorSinc_;
    bool sincEnSilencio_ = false;
    bool sincronizando_ = false;
    QString estadoSinc_;
    QDateTime ultimoIntentoAuto_;

    int vista_ = 0;
    QDate periodo_;
    QString busqueda_;
    int filtroTipo_ = 0;
    bool oscuro_ = false;
    QList<int> aniosBarras_;

    QString saldoTexto_, saldoAyuda_, ingresosTexto_, gastosTexto_, resultadoTexto_, cantidadTexto_;
    QVariantMap barras_, tortaGastos_, tortaIngresos_;
    QVariantList totalesMeses_, totalesCategorias_, totalesAnios_;
};
