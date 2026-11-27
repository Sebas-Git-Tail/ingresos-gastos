#include "dialogofechas.h"
#include "ui_dialogo_fechas.h"
#include <QPushButton>

DialogoFechas::DialogoFechas(Modo modo, const QString& textoPeriodo, const QDate& desde, const QDate& hasta, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoFechas>()), modo_(modo)
{
    ui->setupUi(this);
    ui->botones->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    const QDate hoy = QDate::currentDate();
    ui->inputDesde->setDate(desde.isValid() ? desde : QDate(hoy.year(), hoy.month(), 1));
    ui->inputHasta->setDate(hasta.isValid() ? hasta : hoy);
    if (modo == Modo::Exportar) {
        setWindowTitle(tr("Exportar a CSV"));
        ui->labelAyuda->setText(tr("¿Qué movimientos querés exportar?"));
        ui->radioPeriodo->setText(tr("&Lo que se ve: %1").arg(textoPeriodo));
        ui->botones->button(QDialogButtonBox::Ok)->setText(tr("Elegir archivo…"));
    } else {
        setWindowTitle(tr("Ver entre fechas"));
        ui->labelAyuda->setText(tr("Elegí desde qué día y hasta qué día mirar. Las tarjetas y los totales se comparan "
                                   "con la misma cantidad de días justo antes."));
        ui->radioPeriodo->hide();
        ui->radioTodo->hide();
        ui->radioRango->hide();
        ui->radioRango->setChecked(true);
        ui->botones->button(QDialogButtonBox::Ok)->setText(tr("Ver"));
    }
    for (QRadioButton* r : {ui->radioPeriodo, ui->radioTodo, ui->radioRango})
        connect(r, &QRadioButton::toggled, this, &DialogoFechas::actualizar);
    connect(ui->botones, &QDialogButtonBox::accepted, this, &DialogoFechas::accept);
    actualizar();
}

DialogoFechas::~DialogoFechas() = default;

void DialogoFechas::actualizar()
{
    const bool fechas = ui->radioRango->isChecked();
    for (QWidget* w : std::initializer_list<QWidget*>{ui->labelDesde, ui->inputDesde, ui->labelHasta, ui->inputHasta}) w->setEnabled(fechas);
    ui->labelError->clear();
}

int DialogoFechas::alcance() const
{
    if (ui->radioRango->isChecked()) return EntreFechas;
    return ui->radioTodo->isChecked() ? Todo : Periodo;
}

QDate DialogoFechas::desde() const { return ui->inputDesde->date(); }
QDate DialogoFechas::hasta() const { return ui->inputHasta->date(); }

void DialogoFechas::elegir(int alcance, const QDate& desde, const QDate& hasta)
{
    (alcance == EntreFechas ? ui->radioRango : alcance == Todo ? ui->radioTodo : ui->radioPeriodo)->setChecked(true);
    if (desde.isValid()) ui->inputDesde->setDate(desde);
    if (hasta.isValid()) ui->inputHasta->setDate(hasta);
}

void DialogoFechas::accept()
{
    if (alcance() == EntreFechas && desde() > hasta()) {
        ui->labelError->setText(tr("La fecha «hasta» es anterior a «desde»."));
        return;
    }
    QDialog::accept();
}
