#include "dialogotransferencia.h"
#include "ui_dialogo_transferencia.h"
#include "db.h"
#include "dinero.h"
#include <QPushButton>

DialogoTransferencia::DialogoTransferencia(Cuentas& cuentas, const Transferencia& t, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoTransferencia>()), cuentas_(cuentas), transferencia_(t)
{
    ui->setupUi(this);
    setWindowTitle(t.id ? tr("Editar transferencia") : tr("Nueva transferencia"));
    ui->botones->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    ui->botones->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));

    const QList<CuentaDinero> lista = cuentas_.cuentasDinero();
    for (const CuentaDinero& c : lista) {
        ui->comboDesde->addItem(c.nombre, c.id);
        ui->comboHasta->addItem(c.nombre, c.id);
    }
    // De entrada: sale de la cuenta que se mira (o la principal) y entra en otra
    const qint64 desde = t.desde ? t.desde : (cuentas_.cuentaVista() ? cuentas_.cuentaVista() : cuentas_.cuentaPrincipal());
    ui->comboDesde->setCurrentIndex(qMax(0, ui->comboDesde->findData(desde)));
    qint64 hasta = t.hasta;
    if (!hasta) for (const CuentaDinero& c : lista) if (c.id != desde) { hasta = c.id; break; }
    ui->comboHasta->setCurrentIndex(qMax(0, ui->comboHasta->findData(hasta)));

    ui->inputFecha->setDate(t.fecha.isValid() ? t.fecha : QDate::currentDate());
    if (t.monto > 0) ui->inputMonto->setText(formatearPesos(t.monto).mid(2));
    ui->inputDescripcion->setText(t.descripcion);

    connect(ui->botones, &QDialogButtonBox::accepted, this, &DialogoTransferencia::accept);
    connect(ui->inputMonto, &QLineEdit::textEdited, ui->labelError, &QLabel::clear);
    ui->inputMonto->setFocus();
}

DialogoTransferencia::~DialogoTransferencia() = default;

void DialogoTransferencia::accept()
{
    Centavos monto = 0;
    if (!parsearPesos(ui->inputMonto->text(), &monto) || monto <= 0) {
        ui->labelError->setText(tr("Escribí un monto mayor que cero, por ejemplo 15.430,50"));
        ui->inputMonto->setFocus();
        ui->inputMonto->selectAll();
        return;
    }
    Transferencia t = transferencia_;
    t.desde = ui->comboDesde->currentData().toLongLong();
    t.hasta = ui->comboHasta->currentData().toLongLong();
    t.monto = monto;
    t.fecha = ui->inputFecha->date();
    t.descripcion = ui->inputDescripcion->text().trimmed();
    const Resultado r = cuentas_.guardarTransferencia(t);
    if (!r.ok) {
        ui->labelError->setText(r.error);
        return;
    }
    t.id = r.id;
    transferencia_ = t;
    QDialog::accept();
}
