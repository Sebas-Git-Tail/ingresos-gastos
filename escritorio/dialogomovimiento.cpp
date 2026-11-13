#include "dialogomovimiento.h"
#include "ui_dialogo_movimiento.h"
#include "db.h"
#include "dinero.h"
#include <QInputDialog>
#include <QPushButton>

DialogoMovimiento::DialogoMovimiento(Cuentas& cuentas, const Movimiento& m, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoMovimiento>()), cuentas_(cuentas), movimiento_(m)
{
    ui->setupUi(this);
    const bool nuevo = m.id == 0;
    setWindowTitle(nuevo ? (m.tipo == Tipo::Ingreso ? tr("Nuevo ingreso") : tr("Nuevo gasto"))
                         : tr("Editar movimiento"));
    ui->botones->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    ui->botones->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));

    (m.tipo == Tipo::Ingreso ? ui->radioIngreso : ui->radioGasto)->setChecked(true);
    ui->inputFecha->setDate(m.fecha.isValid() ? m.fecha : QDate::currentDate());
    if (!nuevo) {
        ui->inputMonto->setText(formatearPesos(m.monto).mid(2));   // "1.234,56" sin "$ "
        ui->inputDescripcion->setText(m.descripcion);
    }
    cargarCategorias(m.categoriaId);

    // Cuenta: la del movimiento, o la que se está mirando, o la principal. Con una sola cuenta no se muestra.
    const QList<CuentaDinero> cuentasDinero = cuentas_.cuentasDinero();
    for (const CuentaDinero& c : cuentasDinero) ui->comboCuenta->addItem(c.nombre, c.id);
    const qint64 cuenta = m.cuentaId ? m.cuentaId : (cuentas_.cuentaVista() ? cuentas_.cuentaVista() : cuentas_.cuentaPrincipal());
    ui->comboCuenta->setCurrentIndex(qMax(0, ui->comboCuenta->findData(cuenta)));
    ui->labelCuenta->setVisible(cuentasDinero.size() > 1);
    ui->comboCuenta->setVisible(cuentasDinero.size() > 1);

    // Al cambiar de tipo cambian las categorías (las de ingresos no sirven para un gasto)
    connect(ui->radioIngreso, &QRadioButton::toggled, this, [this] { cargarCategorias(); });
    connect(ui->btnNuevaCategoria, &QToolButton::clicked, this, &DialogoMovimiento::nuevaCategoria);
    connect(ui->botones, &QDialogButtonBox::accepted, this, &DialogoMovimiento::accept);
    connect(ui->inputMonto, &QLineEdit::textEdited, ui->labelError, &QLabel::clear);

    ui->inputMonto->setFocus();
}

DialogoMovimiento::~DialogoMovimiento() = default;

Tipo DialogoMovimiento::tipoElegido() const
{
    return ui->radioIngreso->isChecked() ? Tipo::Ingreso : Tipo::Gasto;
}

void DialogoMovimiento::cargarCategorias(qint64 elegir)
{
    ui->comboCategoria->clear();
    const auto lista = cuentas_.categorias(tipoElegido());
    for (const Categoria& c : lista) ui->comboCategoria->addItem(c.nombre, c.id);
    int i = elegir ? ui->comboCategoria->findData(elegir) : -1;
    if (i < 0) {   // por defecto, la inicial y no la primera de la lista: por orden alfabético sería una de "Varios"
        i = qMax(0, ui->comboCategoria->findData(cuentas_.categoriaInicial(tipoElegido())));
    }
    ui->comboCategoria->setCurrentIndex(i);
}

void DialogoMovimiento::nuevaCategoria()
{
    bool ok = false;
    const Tipo tipo = tipoElegido();
    const QString nombre = QInputDialog::getText(this, tr("Nueva categoría"),
        tipo == Tipo::Ingreso ? tr("Nombre de la categoría de ingresos:") : tr("Nombre de la categoría de gastos:"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || nombre.isEmpty()) return;
    const Resultado r = cuentas_.crearCategoria(nombre, tipo);
    if (!r.ok) {
        ui->labelError->setText(r.error);
        return;
    }
    cargarCategorias(r.id);
}

void DialogoMovimiento::accept()
{
    Centavos monto = 0;
    if (!parsearPesos(ui->inputMonto->text(), &monto) || monto <= 0) {
        ui->labelError->setText(tr("Escribí un monto mayor que cero, por ejemplo 15.430,50"));
        ui->inputMonto->setFocus();
        ui->inputMonto->selectAll();
        return;
    }
    if (ui->comboCategoria->currentIndex() < 0) {
        ui->labelError->setText(tr("Elegí una categoría (o creá una con +)."));
        return;
    }
    Movimiento m = movimiento_;
    m.fecha = ui->inputFecha->date();
    m.tipo = tipoElegido();
    m.categoriaId = ui->comboCategoria->currentData().toLongLong();
    m.categoria = ui->comboCategoria->currentText();
    m.monto = monto;
    m.descripcion = ui->inputDescripcion->text().trimmed();
    m.cuentaId = ui->comboCuenta->currentData().toLongLong();
    m.cuenta = ui->comboCuenta->currentText();

    const Resultado r = m.id ? cuentas_.actualizarMovimiento(m) : cuentas_.crearMovimiento(m);
    if (!r.ok) {
        ui->labelError->setText(r.error);
        return;
    }
    if (!m.id) m.id = r.id;
    movimiento_ = m;
    QDialog::accept();
}
