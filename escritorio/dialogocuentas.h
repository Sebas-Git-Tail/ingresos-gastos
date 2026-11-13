#pragma once
#include "db.h"
#include <QDialog>
#include <memory>

namespace Ui { class DialogoCuentas; }
class Cuentas;

/** ☰ → Cuentas: agregar, renombrar, saldo inicial y eliminar (la principal no se elimina). */
class DialogoCuentas : public QDialog
{
    Q_OBJECT
public:
    explicit DialogoCuentas(Cuentas& cuentas, QWidget* parent = nullptr);
    ~DialogoCuentas() override;
    bool huboCambios() const { return cambios_; }

    // Sin ventanitas (las usan los botones y las pruebas)
    Resultado agregar(const QString& nombre, const QString& saldoInicial);
    int filas() const;

private:
    void cargar(qint64 elegir = 0);
    qint64 seleccionada() const;
    void actualizarBotones();
    void pedirAgregar();
    void pedirRenombrar();
    void pedirSaldoInicial();
    void pedirEliminar();

    std::unique_ptr<Ui::DialogoCuentas> ui;
    Cuentas& cuentas_;
    bool cambios_ = false;
};
