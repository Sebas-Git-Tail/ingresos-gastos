#pragma once
#include "db.h"
#include "modelos.h"
#include <QDate>
#include <QMainWindow>
#include <memory>

namespace Ui { class MainWindow; }
class QStandardItemModel;
class FiltroMovimientos;
class ServidorSinc;

/**
 * Ventana principal: período elegido arriba (mes, año o todo), saldo de la cuenta,
 * tarjetas de ingresos / gastos / resultado y tres pestañas: Movimientos, Gráficos y Totales.
 */
class VentanaPrincipal : public QMainWindow
{
    Q_OBJECT
public:
    enum class Vista { Mes = 0, Anio = 1, Todo = 2 };

    explicit VentanaPrincipal(Cuentas& cuentas, QWidget* parent = nullptr);
    ~VentanaPrincipal() override;

    // Período que se está mirando
    Vista vista() const { return vista_; }
    QDate periodo() const { return periodo_; }          // día 1 del mes elegido
    void setVista(Vista v);
    void setPeriodo(const QDate& fecha);
    void moverPeriodo(int pasos);                       // -1 = anterior, +1 = siguiente
    QPair<QDate, QDate> rango() const;                  // desde / hasta (inválidas en «Todo»)
    QString textoPeriodo() const;

    // Acciones sin ventanitas de confirmación (las usan los botones y las pruebas)
    QList<qint64> idsSeleccionados() const;
    int eliminarMovimientos(const QList<qint64>& ids);
    bool exportarArchivo(const QString& ruta, QString* error = nullptr);
    ResultadoImportacion importarArchivo(const QString& ruta, QString* error = nullptr);
    bool fijarSaldoInicialTexto(const QString& texto);
    ServidorSinc& servidor() { return *servidor_; }

public slots:
    void refrescar();
    void nuevoIngreso();
    void nuevoGasto();
    void editarSeleccionado();
    void repetirSeleccionado();
    void eliminarSeleccionados();
    void abrirCategorias();
    void pedirSaldoInicial();
    void pedirImportar();
    void pedirExportar();
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
    void cargarMovimientos();
    void cargarTarjetas();
    void cargarGraficos();
    void cargarTotales();
    void actualizarBotones();
    void menuContextual(const QPoint& pos);
    void clicEnBarras(int indice);
    void mostrarImportados(const QList<FilaImportada>& filas);
    void seleccionarMovimiento(qint64 id);
    void guardarConfiguracion();
    void leerConfiguracion();

    std::unique_ptr<Ui::MainWindow> ui;
    Cuentas& cuentas_;
    QStandardItemModel* modelo_ = nullptr;
    FiltroMovimientos* filtro_ = nullptr;
    Vista vista_ = Vista::Mes;
    QDate periodo_;
    QList<int> aniosBarras_;
    ServidorSinc* servidor_ = nullptr;                  // para que el celular sincronice                            // en la vista «Todo»: qué año es cada barra
};
