#pragma once
#include "modelos.h"
#include <QDialog>
#include <memory>

namespace Ui { class DialogoMovimiento; }
class Cuentas;

/**
 * Alta y edición de un ingreso o gasto.
 * Al aceptar, guarda en la base (crear o actualizar); movimiento() devuelve lo guardado.
 */
class DialogoMovimiento : public QDialog
{
    Q_OBJECT
public:
    // Para uno nuevo: m.id == 0 (se usan m.tipo y m.fecha como valores iniciales)
    DialogoMovimiento(Cuentas& cuentas, const Movimiento& m, QWidget* parent = nullptr);
    ~DialogoMovimiento() override;

    Movimiento movimiento() const { return movimiento_; }

public slots:
    void accept() override;

private:
    Tipo tipoElegido() const;
    void cargarCategorias(qint64 elegir = 0);
    void nuevaCategoria();

    std::unique_ptr<Ui::DialogoMovimiento> ui;
    Cuentas& cuentas_;
    Movimiento movimiento_;
};
