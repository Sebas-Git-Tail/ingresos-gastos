#include "dialogocuentas.h"
#include "ui_dialogo_cuentas.h"
#include "db.h"
#include "dinero.h"
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>

DialogoCuentas::DialogoCuentas(Cuentas& cuentas, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoCuentas>()), cuentas_(cuentas)
{
    ui->setupUi(this);
    auto* t = ui->tablaCuentas;
    t->setColumnCount(3);
    t->setHorizontalHeaderLabels({tr("Cuenta"), tr("Saldo hoy"), tr("Movimientos")});
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(ui->btnAgregar, &QPushButton::clicked, this, &DialogoCuentas::pedirAgregar);
    connect(ui->btnRenombrar, &QPushButton::clicked, this, &DialogoCuentas::pedirRenombrar);
    connect(ui->btnSaldoInicial, &QPushButton::clicked, this, &DialogoCuentas::pedirSaldoInicial);
    connect(ui->btnEliminar, &QPushButton::clicked, this, &DialogoCuentas::pedirEliminar);
    connect(t, &QTableWidget::itemSelectionChanged, this, &DialogoCuentas::actualizarBotones);
    connect(t, &QTableWidget::itemDoubleClicked, this, &DialogoCuentas::pedirRenombrar);
    cargar();
}

DialogoCuentas::~DialogoCuentas() = default;

int DialogoCuentas::filas() const { return ui->tablaCuentas->rowCount(); }

void DialogoCuentas::cargar(qint64 elegir)
{
    const qint64 antes = elegir ? elegir : seleccionada();
    auto* t = ui->tablaCuentas;
    const QList<CuentaDinero> lista = cuentas_.cuentasDinero();
    t->setRowCount(int(lista.size()));
    for (int i = 0; i < lista.size(); ++i) {
        const CuentaDinero& c = lista[i];
        auto* nombre = new QTableWidgetItem(c.principal ? tr("%1 (principal)").arg(c.nombre) : c.nombre);
        nombre->setData(Qt::UserRole, c.id);
        nombre->setData(Qt::UserRole + 1, c.nombre);
        nombre->setToolTip(tr("Saldo inicial: %1").arg(formatearPesos(c.saldoInicial)));
        auto* saldo = new QTableWidgetItem(formatearPesos(c.saldo));
        saldo->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* movs = new QTableWidgetItem(QString::number(c.movimientos));
        movs->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        t->setItem(i, 0, nombre);
        t->setItem(i, 1, saldo);
        t->setItem(i, 2, movs);
        if (c.id == antes) t->selectRow(i);
    }
    if (t->selectedItems().isEmpty() && t->rowCount()) t->selectRow(0);
    actualizarBotones();
}

qint64 DialogoCuentas::seleccionada() const
{
    const auto filasSel = ui->tablaCuentas->selectionModel() ? ui->tablaCuentas->selectionModel()->selectedRows() : QModelIndexList();
    if (filasSel.isEmpty()) return 0;
    return ui->tablaCuentas->item(filasSel.first().row(), 0)->data(Qt::UserRole).toLongLong();
}

void DialogoCuentas::actualizarBotones()
{
    const qint64 id = seleccionada();
    ui->btnRenombrar->setEnabled(id != 0);
    ui->btnSaldoInicial->setEnabled(id != 0);
    ui->btnEliminar->setEnabled(id != 0 && id != cuentas_.cuentaPrincipal());
}

Resultado DialogoCuentas::agregar(const QString& nombre, const QString& saldoInicial)
{
    Centavos saldo = 0;
    if (!saldoInicial.trimmed().isEmpty() && !parsearPesos(saldoInicial, &saldo, true))
        return {false, tr("«%1» no es un monto válido. Ejemplo: 150.000,00").arg(saldoInicial), 0};
    const Resultado r = cuentas_.crearCuentaDinero(nombre, saldo);
    if (r.ok) {
        cambios_ = true;
        cargar(r.id);
    }
    return r;
}

void DialogoCuentas::pedirAgregar()
{
    bool ok = false;
    const QString nombre = QInputDialog::getText(this, tr("Nueva cuenta"), tr("Nombre (por ejemplo «Banco» o «Mercado Pago»):"),
                                                 QLineEdit::Normal, QString(), &ok);
    if (!ok || nombre.trimmed().isEmpty()) return;
    const QString saldo = QInputDialog::getText(this, tr("Nueva cuenta"),
        tr("Plata que había en «%1» antes del primer movimiento (se puede dejar vacío):").arg(nombre.trimmed()),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    const Resultado r = agregar(nombre, saldo);
    if (!r.ok) QMessageBox::warning(this, windowTitle(), r.error);
}

void DialogoCuentas::pedirRenombrar()
{
    const qint64 id = seleccionada();
    if (!id) return;
    const QString actual = ui->tablaCuentas->item(ui->tablaCuentas->currentRow(), 0)->data(Qt::UserRole + 1).toString();
    bool ok = false;
    const QString nombre = QInputDialog::getText(this, tr("Renombrar cuenta"), tr("Nombre nuevo:"), QLineEdit::Normal, actual, &ok);
    if (!ok || nombre.trimmed() == actual) return;
    const Resultado r = cuentas_.renombrarCuentaDinero(id, nombre);
    if (!r.ok) {
        QMessageBox::warning(this, windowTitle(), r.error);
        return;
    }
    cambios_ = true;
    cargar(id);
}

void DialogoCuentas::pedirSaldoInicial()
{
    const qint64 id = seleccionada();
    if (!id) return;
    Centavos actual = 0;
    for (const CuentaDinero& c : cuentas_.cuentasDinero()) if (c.id == id) actual = c.saldoInicial;
    QString texto = formatearPesos(actual).remove(QStringLiteral("$ "));
    while (true) {
        bool ok = false;
        texto = QInputDialog::getText(this, tr("Saldo inicial"),
            tr("Plata que había en la cuenta antes del primer movimiento cargado.\nPuede ser negativo (por ejemplo -5.000)."),
            QLineEdit::Normal, texto, &ok);
        if (!ok) return;
        Centavos c = 0;
        if (texto.trimmed().isEmpty() || parsearPesos(texto, &c, true)) {
            cuentas_.fijarSaldoInicialCuenta(id, c);
            cambios_ = true;
            cargar(id);
            return;
        }
        QMessageBox::warning(this, windowTitle(), tr("«%1» no es un monto válido. Ejemplo: 150.000,00").arg(texto));
    }
}

void DialogoCuentas::pedirEliminar()
{
    const qint64 id = seleccionada();
    if (!id) return;
    int movimientos = 0;
    QStringList otras;
    QList<qint64> idsOtras;
    for (const CuentaDinero& c : cuentas_.cuentasDinero()) {
        if (c.id == id) movimientos = c.movimientos;
        else { otras << c.nombre; idsOtras << c.id; }
    }
    qint64 moverA = 0;
    if (movimientos > 0) {
        bool ok = false;
        const QString destino = QInputDialog::getItem(this, tr("Eliminar cuenta"),
            tr("La cuenta tiene %n movimiento(s). ¿A qué cuenta los paso?", nullptr, movimientos), otras, 0, false, &ok);
        if (!ok) return;
        moverA = idsOtras.value(otras.indexOf(destino));
    } else if (QMessageBox::question(this, windowTitle(), tr("¿Eliminar esta cuenta?")) != QMessageBox::Yes) {
        return;
    }
    const Resultado r = cuentas_.eliminarCuentaDinero(id, moverA);
    if (!r.ok) {
        QMessageBox::warning(this, windowTitle(), r.error);
        return;
    }
    cambios_ = true;
    cargar();
}
