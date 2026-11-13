#pragma once
#include "modelos.h"
#include <QDialog>
#include <memory>

namespace Ui { class DialogoFijos; class DialogoFijo; }
class Cuentas;

/** Alta y edición de un movimiento fijo. Al aceptar, lo guarda. */
class DialogoFijo : public QDialog
{
    Q_OBJECT
public:
    DialogoFijo(Cuentas& cuentas, const Recurrente& r, QWidget* parent = nullptr);
    ~DialogoFijo() override;
    Recurrente recurrente() const { return r_; }
    void setMonto(const QString& texto);           // para las pruebas

public slots:
    void accept() override;

private:
    void cargarCategorias(qint64 elegir = 0);

    std::unique_ptr<Ui::DialogoFijo> ui;
    Cuentas& cuentas_;
    Recurrente r_;
};

/** ☰ → Movimientos fijos: la lista, con agregar, editar y eliminar. */
class DialogoFijos : public QDialog
{
    Q_OBJECT
public:
    explicit DialogoFijos(Cuentas& cuentas, QWidget* parent = nullptr);
    ~DialogoFijos() override;
    bool huboCambios() const { return cambios_; }
    int filas() const;

private:
    void cargar(qint64 elegir = 0);
    qint64 seleccionado() const;
    void editar(const Recurrente& r);

    std::unique_ptr<Ui::DialogoFijos> ui;
    Cuentas& cuentas_;
    bool cambios_ = false;
};
