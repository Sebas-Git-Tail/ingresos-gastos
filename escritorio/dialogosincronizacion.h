#pragma once
#include <QDialog>
#include <memory>

namespace Ui { class DialogoSincronizacion; }
class Cuentas;
class ServidorSinc;
class QTimer;

/** Activar la sincronización, emparejar un celular (código de 6 dígitos) y ver/quitar los emparejados. */
class DialogoSincronizacion : public QDialog
{
    Q_OBJECT
public:
    DialogoSincronizacion(Cuentas& cuentas, ServidorSinc& servidor, QWidget* parent = nullptr);
    ~DialogoSincronizacion() override;

    // Prende o apaga el servidor y lo recuerda para la próxima vez que se abra el programa
    static bool activar(ServidorSinc& servidor, bool activa, QString* error = nullptr);

private:
    void actualizar();
    void cargarPares();

    std::unique_ptr<Ui::DialogoSincronizacion> ui;
    Cuentas& cuentas_;
    ServidorSinc& servidor_;
    QTimer* reloj_ = nullptr;
};
