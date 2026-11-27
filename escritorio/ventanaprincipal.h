#pragma once
#include "db.h"
#include "modelos.h"
#include <QDate>
#include <QMainWindow>
#include <memory>

namespace Ui { class MainWindow; }
class QStackedWidget;
namespace tema { class EstadoVacio; }
class QStandardItemModel;
class FiltroMovimientos;
class ServidorSinc;

/**
 * Ventana principal: período elegido arriba (mes, año, todo o entre dos fechas), saldo de la cuenta,
 * tarjetas de ingresos / gastos / resultado y tres pestañas: Movimientos, Gráficos y Totales.
 */
class VentanaPrincipal : public QMainWindow
{
    Q_OBJECT
public:
    enum class Vista { Mes = 0, Anio = 1, Todo = 2, Rango = 3 };

    explicit VentanaPrincipal(Cuentas& cuentas, QWidget* parent = nullptr);
    ~VentanaPrincipal() override;

    // Período que se está mirando
    Vista vista() const { return vista_; }
    QDate periodo() const { return periodo_; }          // día 1 del mes elegido
    void setVista(Vista v);
    void setPeriodo(const QDate& fecha);
    void moverPeriodo(int pasos);                       // -1 = anterior, +1 = siguiente (en «Entre fechas», los mismos meses o días antes o después)
    void verRango(const QDate& desde, const QDate& hasta);   // vista «Entre fechas»
    QPair<QDate, QDate> rango() const;                  // desde / hasta (inválidas en «Todo»)
    QString textoPeriodo() const;

    // Acciones sin ventanitas de confirmación (las usan los botones y las pruebas)
    QList<qint64> idsSeleccionados() const;
    int eliminarMovimientos(const QList<qint64>& ids);
    bool exportarArchivo(const QString& ruta, QString* error = nullptr);   // lo que se ve
    bool exportarArchivo(const QString& ruta, const QDate& desde, const QDate& hasta, QString* error = nullptr);   // inválidas = sin límite
    ResultadoRespaldo respaldar();       // copia automática del día (quedan las últimas 7)
    ResultadoImportacion importarArchivo(const QString& ruta, QString* error = nullptr);
    bool fijarSaldoInicialTexto(const QString& texto);
    ServidorSinc& servidor() { return *servidor_; }

public slots:
    void refrescar();
    void nuevoIngreso();
    void nuevoGasto();
    void nuevaTransferencia();
    void editarSeleccionado();
    void repetirSeleccionado();
    void eliminarSeleccionados();
    void abrirCategorias();
    void abrirCuentas();
    void abrirFijos();
    void repetirTodosLosMeses();         // crea un movimiento fijo a partir del elegido
    int cargarFijos();                   // carga los movimientos fijos que correspondan hasta hoy
    void pedirSaldoInicial();
    void pedirImportar();
    void pedirExportar();
    void pedirRango();                   // «Entre fechas…»
    void pedirBackup();
    void abrirSincronizacion();
    void acercaDe();

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void nuevo(Tipo tipo);
    void armarMenu();
    void armarAtajos();
    void armarEstilo();                 // tema común: íconos, botones de color, estado vacío
    void cargarMovimientos();
    void cargarTarjetas();
    void cargarGraficos();
    void cargarTotales();
    void cargarCuentas();                // el selector de cuenta de arriba
    void cargarAvisos();                 // presupuestos pasados o por pasarse en el mes que se mira
    void actualizarBotones();
    void menuContextual(const QPoint& pos);
    void clicEnBarras(int indice);
    void mostrarImportados(const QList<FilaImportada>& filas);
    void seleccionarMovimiento(qint64 id);
    bool editarTransferencia(qint64 id, bool repetir);   // abre el diálogo de transferencia; false si no es una
    void guardarConfiguracion();
    void leerConfiguracion();

    std::unique_ptr<Ui::MainWindow> ui;
    Cuentas& cuentas_;
    QStandardItemModel* modelo_ = nullptr;
    FiltroMovimientos* filtro_ = nullptr;
    Vista vista_ = Vista::Mes;
    QDate periodo_;
    QList<int> aniosBarras_;
    QList<QDate> mesesBarras_;          // en «Entre fechas»: qué mes (o año, día 1/1) es cada barra
    bool barrasPorAnio_ = false;
    QDate desdeRango_, hastaRango_;     // vista «Entre fechas»
    ServidorSinc* servidor_ = nullptr;                  // para que el celular sincronice                            // en la vista «Todo»: qué año es cada barra
    QStackedWidget* pilaTabla_ = nullptr;   // la tabla de movimientos o, si no hay ninguno, el estado vacío
    tema::EstadoVacio* vacio_ = nullptr;
};
