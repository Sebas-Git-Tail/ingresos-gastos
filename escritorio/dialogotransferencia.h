#pragma once
#include "modelos.h"
#include <QDialog>
#include <memory>

namespace Ui { class DialogoTransferencia; }
class Cuentas;

/**
 * Alta y edición de una transferencia entre cuentas (de qué cuenta sale, a cuál entra, monto, fecha).
 * Al aceptar, guarda en la base; transferencia() devuelve lo guardado (con el id de la salida).
 */
class DialogoTransferencia : public QDialog
{
    Q_OBJECT
public:
    // Para una nueva: t.id == 0 (se usan sus datos como valores iniciales)
    DialogoTransferencia(Cuentas& cuentas, const Transferencia& t, QWidget* parent = nullptr);
    ~DialogoTransferencia() override;

    Transferencia transferencia() const { return transferencia_; }

public slots:
    void accept() override;

private:
    std::unique_ptr<Ui::DialogoTransferencia> ui;
    Cuentas& cuentas_;
    Transferencia transferencia_;
};
