#include "dialogofijos.h"
#include "ui_dialogo_fijo.h"
#include "ui_dialogo_fijos.h"
#include "db.h"
#include "dinero.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>

// ---------------------------------------------------------------- un movimiento fijo

DialogoFijo::DialogoFijo(Cuentas& cuentas, const Recurrente& r, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoFijo>()), cuentas_(cuentas), r_(r)
{
    ui->setupUi(this);
    setWindowTitle(r.id ? tr("Editar movimiento fijo") : tr("Nuevo movimiento fijo"));
    ui->botones->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    ui->botones->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    (r.tipo == Tipo::Ingreso ? ui->radioIngreso : ui->radioGasto)->setChecked(true);
    const QList<CuentaDinero> cuentasDinero = cuentas_.cuentasDinero();
    for (const CuentaDinero& c : cuentasDinero) ui->comboCuenta->addItem(c.nombre, c.id);
    ui->comboCuenta->setCurrentIndex(qMax(0, ui->comboCuenta->findData(r.cuentaId ? r.cuentaId : cuentas_.cuentaPrincipal())));
    ui->labelCuenta->setVisible(cuentasDinero.size() > 1);
    ui->comboCuenta->setVisible(cuentasDinero.size() > 1);
    if (r.monto > 0) ui->inputMonto->setText(formatearPesos(r.monto).mid(2));
    ui->inputDescripcion->setText(r.descripcion);
    ui->spinDia->setValue(std::clamp(r.dia, 1, 31));
    ui->inputDesde->setDate(r.desde.isValid() ? r.desde : QDate::currentDate());
    ui->checkHasta->setChecked(r.hasta.isValid());
    ui->inputHasta->setDate(r.hasta.isValid() ? r.hasta : QDate::currentDate().addYears(1));
    ui->checkActivo->setChecked(r.activo);
    cargarCategorias(r.categoriaId);
    connect(ui->radioIngreso, &QRadioButton::toggled, this, [this] { cargarCategorias(); });
    connect(ui->botones, &QDialogButtonBox::accepted, this, &DialogoFijo::accept);
    ui->inputMonto->setFocus();
}

DialogoFijo::~DialogoFijo() = default;

void DialogoFijo::setMonto(const QString& texto) { ui->inputMonto->setText(texto); }

void DialogoFijo::cargarCategorias(qint64 elegir)
{
    const Tipo tipo = ui->radioIngreso->isChecked() ? Tipo::Ingreso : Tipo::Gasto;
    ui->comboCategoria->clear();
    for (const Categoria& c : cuentas_.categorias(tipo)) ui->comboCategoria->addItem(c.nombre, c.id);
    int i = elegir ? ui->comboCategoria->findData(elegir) : -1;
    if (i < 0) i = qMax(0, ui->comboCategoria->findData(cuentas_.categoriaInicial(tipo)));
    ui->comboCategoria->setCurrentIndex(i);
}

void DialogoFijo::accept()
{
    Centavos monto = 0;
    if (!parsearPesos(ui->inputMonto->text(), &monto) || monto <= 0) {
        ui->labelError->setText(tr("Escribí un monto mayor que cero, por ejemplo 15.430,50"));
        ui->inputMonto->setFocus();
        return;
    }
    Recurrente r = r_;
    r.categoriaId = ui->comboCategoria->currentData().toLongLong();
    r.cuentaId = ui->comboCuenta->currentData().toLongLong();
    r.monto = monto;
    r.descripcion = ui->inputDescripcion->text().trimmed();
    r.dia = ui->spinDia->value();
    r.desde = ui->inputDesde->date();
    r.hasta = ui->checkHasta->isChecked() ? ui->inputHasta->date() : QDate();
    r.activo = ui->checkActivo->isChecked();
    const Resultado res = cuentas_.guardarRecurrente(r);
    if (!res.ok) {
        ui->labelError->setText(res.error);
        return;
    }
    r.id = res.id;
    r_ = r;
    QDialog::accept();
}

// ---------------------------------------------------------------- la lista

DialogoFijos::DialogoFijos(Cuentas& cuentas, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoFijos>()), cuentas_(cuentas)
{
    ui->setupUi(this);
    auto* t = ui->tablaFijos;
    t->setColumnCount(5);
    t->setHorizontalHeaderLabels({tr("Día"), tr("Categoría"), tr("Descripción"), tr("Monto"), tr("Estado")});
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(ui->btnAgregar, &QPushButton::clicked, this, [this] { editar(Recurrente{}); });
    connect(ui->btnEditar, &QPushButton::clicked, this, [this] {
        for (const Recurrente& r : cuentas_.recurrentes()) if (r.id == seleccionado()) editar(r);
    });
    connect(t, &QTableWidget::itemDoubleClicked, ui->btnEditar, &QPushButton::click);
    connect(ui->btnEliminar, &QPushButton::clicked, this, [this] {
        const qint64 id = seleccionado();
        if (!id || QMessageBox::question(this, windowTitle(),
                       tr("¿Eliminar este movimiento fijo? Los que ya se cargaron quedan.")) != QMessageBox::Yes)
            return;
        const Resultado r = cuentas_.eliminarRecurrente(id);
        if (!r.ok) QMessageBox::warning(this, windowTitle(), r.error);
        cambios_ = true;
        cargar();
    });
    connect(t, &QTableWidget::itemSelectionChanged, this, [this] {
        ui->btnEditar->setEnabled(seleccionado() != 0);
        ui->btnEliminar->setEnabled(seleccionado() != 0);
    });
    cargar();
}

DialogoFijos::~DialogoFijos() = default;

int DialogoFijos::filas() const { return ui->tablaFijos->rowCount(); }

void DialogoFijos::cargar(qint64 elegir)
{
    auto* t = ui->tablaFijos;
    const QList<Recurrente> lista = cuentas_.recurrentes();
    const bool variasCuentas = cuentas_.cuentasDinero().size() > 1;
    t->setRowCount(int(lista.size()));
    for (int i = 0; i < lista.size(); ++i) {
        const Recurrente& r = lista[i];
        auto* dia = new QTableWidgetItem(tr("Día %1").arg(r.dia));
        dia->setData(Qt::UserRole, r.id);
        QString estado = r.activo ? tr("Activo") : tr("Pausado");
        if (r.hasta.isValid()) estado += tr(" hasta %1").arg(r.hasta.toString(QStringLiteral("dd/MM/yyyy")));
        if (variasCuentas) estado += QStringLiteral(" · ") + r.cuenta;
        auto* monto = new QTableWidgetItem((r.tipo == Tipo::Ingreso ? QStringLiteral("+") : QStringLiteral("−")) + formatearPesos(r.monto));
        monto->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        t->setItem(i, 0, dia);
        t->setItem(i, 1, new QTableWidgetItem(r.categoria));
        t->setItem(i, 2, new QTableWidgetItem(r.descripcion));
        t->setItem(i, 3, monto);
        t->setItem(i, 4, new QTableWidgetItem(estado));
        if (r.id == elegir) t->selectRow(i);
    }
    ui->btnEditar->setEnabled(seleccionado() != 0);
    ui->btnEliminar->setEnabled(seleccionado() != 0);
}

qint64 DialogoFijos::seleccionado() const
{
    const auto filasSel = ui->tablaFijos->selectionModel() ? ui->tablaFijos->selectionModel()->selectedRows() : QModelIndexList();
    return filasSel.isEmpty() ? 0 : ui->tablaFijos->item(filasSel.first().row(), 0)->data(Qt::UserRole).toLongLong();
}

void DialogoFijos::editar(const Recurrente& r)
{
    DialogoFijo d(cuentas_, r, this);
    if (d.exec() != QDialog::Accepted) return;
    cambios_ = true;
    cuentas_.generarRecurrentes();          // si ya corresponde cargar alguno, que aparezca enseguida
    cargar(d.recurrente().id);
}
