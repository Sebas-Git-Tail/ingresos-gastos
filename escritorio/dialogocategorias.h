#pragma once
#include "db.h"
#include "modelos.h"
#include <QDialog>
#include <memory>

namespace Ui { class DialogoCategorias; }
class QListWidget;

/**
 * Editar las categorías: agregar, renombrar, cambiar el color y eliminar.
 * Para eliminar una categoría que tiene movimientos se pide a cuál pasarlos (nunca se borran movimientos).
 */
class DialogoCategorias : public QDialog
{
    Q_OBJECT
public:
    DialogoCategorias(Cuentas& cuentas, Tipo tipoInicial, QWidget* parent = nullptr);
    ~DialogoCategorias() override;

    bool huboCambios() const { return cambios_; }

    // Lo que hacen los botones, sin las ventanitas de preguntas (lo usan los botones y las pruebas)
    Tipo tipoActual() const;
    qint64 seleccionada() const;                      // 0 si no hay
    void seleccionar(qint64 id);
    Resultado agregar(const QString& nombre);
    Resultado renombrar(const QString& nombre);
    Resultado ponerColor(int color);
    Resultado eliminar(qint64 moverA = 0);

private:
    QListWidget* listaActual() const;
    void cargar(qint64 elegir = 0);
    void actualizarBotones();
    void mostrarError(const Resultado& r);
    void pedirAgregar();
    void pedirRenombrar();
    void pedirEliminar();
    void pedirPresupuesto();

    std::unique_ptr<Ui::DialogoCategorias> ui;
    Cuentas& cuentas_;
    bool cambios_ = false;
};
