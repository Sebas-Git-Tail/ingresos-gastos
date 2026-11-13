#include "ventanaprincipal.h"
#include "ui_main.h"
#include "csv.h"
#include "dialogocategorias.h"
#include "dialogomovimiento.h"
#include "dialogosincronizacion.h"
#include "sincronizacion.h"
#include "dinero.h"
#include "graficos.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>

namespace {

enum Columna { ColFecha, ColTipo, ColCategoria, ColDescripcion, ColMonto, COLUMNAS };
constexpr int RolId = Qt::UserRole + 1;        // id del movimiento (en la columna Fecha)
constexpr int RolOrden = Qt::UserRole + 2;     // valor para ordenar
constexpr int RolBusqueda = Qt::UserRole + 3;  // texto normalizado para el buscador
constexpr int RolTipo = Qt::UserRole + 4;      // 0 = ingreso, 1 = gasto

QIcon muestraColor(const QColor& color)
{
    QPixmap px(12, 12);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(0.5, 0.5, 11, 11), 2.5, 2.5);
    return QIcon(px);
}

QColor colorCategoria(const TemaGrafico& tema, int color)
{
    return tema.serie[std::clamp(color, 0, 7)];
}

QTableWidgetItem* celda(const QString& texto, bool numero = false, bool negrita = false)
{
    auto* it = new QTableWidgetItem(texto);
    if (numero) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (negrita) {
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
    }
    return it;
}

void ajustarTabla(QTableWidget* t)
{
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);   // que nunca se corten los montos
    t->horizontalHeader()->setStretchLastSection(true);
}

bool leerArchivo(const QString& ruta, LecturaCsv* lectura, QString* error)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("No se pudo abrir el archivo: %1").arg(f.errorString());
        return false;
    }
    *lectura = leerCsv(decodificarArchivo(f.readAll()));
    if (!lectura->ok) {
        if (error) *error = lectura->error;
        return false;
    }
    return true;
}

} // namespace

/** Buscador y filtro por tipo de la tabla de movimientos. */
class FiltroMovimientos : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setTexto(const QString& t)
    {
        palabras_ = normalizar(t).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        invalidateFilter();
    }
    void setTipo(int t)                      // 0 = todos, 1 = ingresos, 2 = gastos
    {
        tipo_ = t;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int fila, const QModelIndex& padre) const override
    {
        const QModelIndex i = sourceModel()->index(fila, ColFecha, padre);
        const int tipo = i.data(RolTipo).toInt();
        if (tipo_ == 1 && tipo != 0) return false;
        if (tipo_ == 2 && tipo != 1) return false;
        const QString texto = i.data(RolBusqueda).toString();
        for (const QString& p : palabras_) if (!texto.contains(p)) return false;
        return true;
    }

private:
    QStringList palabras_;
    int tipo_ = 0;
};

// ======================================================================

VentanaPrincipal::VentanaPrincipal(Cuentas& cuentas, QWidget* parent)
    : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()), cuentas_(cuentas)
{
    ui->setupUi(this);
    const QDate hoy = QDate::currentDate();
    periodo_ = QDate(hoy.year(), hoy.month(), 1);

    // Tabla de movimientos
    modelo_ = new QStandardItemModel(0, COLUMNAS, this);
    modelo_->setHorizontalHeaderLabels({tr("Fecha"), tr("Tipo"), tr("Categoría"), tr("Descripción"), tr("Monto")});
    modelo_->horizontalHeaderItem(ColMonto)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filtro_ = new FiltroMovimientos(this);
    filtro_->setSourceModel(modelo_);
    filtro_->setSortRole(RolOrden);
    ui->tablaMovimientos->setModel(filtro_);
    ui->tablaMovimientos->verticalHeader()->setVisible(false);
    ui->tablaMovimientos->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tablaMovimientos->horizontalHeader()->setSectionResizeMode(ColDescripcion, QHeaderView::Stretch);
    ui->tablaMovimientos->setColumnWidth(ColFecha, 110);
    ui->tablaMovimientos->setColumnWidth(ColTipo, 90);
    ui->tablaMovimientos->setColumnWidth(ColCategoria, 170);
    ui->tablaMovimientos->setColumnWidth(ColMonto, 150);
    ui->tablaMovimientos->sortByColumn(ColFecha, Qt::DescendingOrder);

    for (QTableWidget* t : {ui->tablaMeses, ui->tablaCategorias, ui->tablaAnios}) ajustarTabla(t);

    // Señales
    connect(ui->btnAnterior, &QToolButton::clicked, this, [this] { moverPeriodo(-1); });
    connect(ui->btnSiguiente, &QToolButton::clicked, this, [this] { moverPeriodo(+1); });
    connect(ui->btnHoy, &QPushButton::clicked, this, [this] { setPeriodo(QDate::currentDate()); });
    connect(ui->comboVista, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { setVista(Vista(i)); });
    connect(ui->btnNuevoIngreso, &QPushButton::clicked, this, &VentanaPrincipal::nuevoIngreso);
    connect(ui->btnNuevoGasto, &QPushButton::clicked, this, &VentanaPrincipal::nuevoGasto);
    connect(ui->btnEditar, &QPushButton::clicked, this, &VentanaPrincipal::editarSeleccionado);
    connect(ui->btnEliminar, &QPushButton::clicked, this, &VentanaPrincipal::eliminarSeleccionados);
    connect(ui->tablaMovimientos, &QTableView::doubleClicked, this, &VentanaPrincipal::editarSeleccionado);
    connect(ui->tablaMovimientos, &QTableView::customContextMenuRequested, this, &VentanaPrincipal::menuContextual);
    connect(ui->tablaMovimientos->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &VentanaPrincipal::actualizarBotones);
    connect(ui->inputBuscar, &QLineEdit::textChanged, this, [this](const QString& t) {
        filtro_->setTexto(t);
        cargarTarjetas();
    });
    connect(ui->comboFiltroTipo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        filtro_->setTipo(i);
        cargarTarjetas();
    });
    connect(ui->graficoBarras, &GraficoBarras::grupoClicado, this, &VentanaPrincipal::clicEnBarras);

    connect(ui->actionCategorias, &QAction::triggered, this, &VentanaPrincipal::abrirCategorias);
    connect(ui->actionSaldoInicial, &QAction::triggered, this, &VentanaPrincipal::pedirSaldoInicial);
    connect(ui->actionImportar, &QAction::triggered, this, &VentanaPrincipal::pedirImportar);
    connect(ui->actionExportar, &QAction::triggered, this, &VentanaPrincipal::pedirExportar);
    connect(ui->actionBackup, &QAction::triggered, this, &VentanaPrincipal::pedirBackup);
    connect(ui->actionAcerca, &QAction::triggered, this, &VentanaPrincipal::acercaDe);
    connect(ui->actionSincronizar, &QAction::triggered, this, &VentanaPrincipal::abrirSincronizacion);

    // Sincronización con el celular: si se dejó activada, arranca sola
    servidor_ = new ServidorSinc(cuentas_, this);
    connect(servidor_, &ServidorSinc::sincronizado, this, [this](const QString& nombre, int recibidas, int enviadas) {
        refrescar();
        statusBar()->showMessage(tr("Sincronizado con %1: recibidos %2, enviados %3").arg(nombre).arg(recibidas).arg(enviadas), 10000);
    });
    connect(servidor_, &ServidorSinc::emparejado, this, [this](const QString& nombre) {
        statusBar()->showMessage(tr("«%1» quedó emparejado").arg(nombre), 10000);
    });
    connect(servidor_, &ServidorSinc::fallo, this, [this](const QString& mensaje) {
        statusBar()->showMessage(tr("Sincronización: %1").arg(mensaje), 10000);
    });
    if (QSettings().value(QStringLiteral("sinc/activa"), false).toBool()) {
        QString error;
        if (!DialogoSincronizacion::activar(*servidor_, true, &error))
            statusBar()->showMessage(tr("No se pudo activar la sincronización: %1").arg(error), 10000);
    }

    armarMenu();
    armarAtajos();
    leerConfiguracion();
    refrescar();
}

VentanaPrincipal::~VentanaPrincipal() = default;

// ---------------------------------------------------------------- menú y atajos

void VentanaPrincipal::armarMenu()
{
    auto* menu = new QMenu(this);
    menu->addAction(ui->actionCategorias);
    menu->addAction(ui->actionSaldoInicial);
    menu->addSeparator();
    menu->addAction(ui->actionImportar);
    menu->addAction(ui->actionExportar);
    menu->addAction(ui->actionBackup);
    menu->addSeparator();
    menu->addAction(ui->actionSincronizar);
    menu->addSeparator();
    menu->addAction(ui->actionAcerca);
    QAction* salir = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("Salir"));
    salir->setShortcut(QKeySequence::Quit);
    connect(salir, &QAction::triggered, this, &QWidget::close);
    addAction(salir);
    ui->btnMenu->setMenu(menu);
    ui->btnMenu->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));

    ui->actionCategorias->setIcon(QIcon::fromTheme(QStringLiteral("tag")));
    ui->actionImportar->setIcon(QIcon::fromTheme(QStringLiteral("document-import")));
    ui->actionExportar->setIcon(QIcon::fromTheme(QStringLiteral("document-export")));
    ui->actionBackup->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    ui->actionAcerca->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));
    ui->actionSincronizar->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    ui->actionImportar->setShortcut(QKeySequence::Open);
    ui->actionExportar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    for (QAction* a : {ui->actionCategorias, ui->actionSaldoInicial, ui->actionImportar, ui->actionExportar,
                       ui->actionBackup, ui->actionAcerca, ui->actionSincronizar})
        addAction(a);   // así los atajos andan aunque el menú esté cerrado
}

void VentanaPrincipal::armarAtajos()
{
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), this, this, &VentanaPrincipal::nuevoIngreso);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this, this, &VentanaPrincipal::nuevoGasto);
    new QShortcut(QKeySequence::Find, this, this, [this] {
        ui->tabWidget->setCurrentWidget(ui->tabMovimientos);
        ui->inputBuscar->setFocus();
        ui->inputBuscar->selectAll();
    });
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this, this, [this] { moverPeriodo(-1); });
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this, this, [this] { moverPeriodo(+1); });
    auto* borrar = new QShortcut(QKeySequence::Delete, ui->tablaMovimientos);
    borrar->setContext(Qt::WidgetShortcut);
    connect(borrar, &QShortcut::activated, this, &VentanaPrincipal::eliminarSeleccionados);
    for (int tecla : {int(Qt::Key_Return), int(Qt::Key_Enter)}) {
        auto* editar = new QShortcut(QKeySequence(tecla), ui->tablaMovimientos);
        editar->setContext(Qt::WidgetShortcut);
        connect(editar, &QShortcut::activated, this, &VentanaPrincipal::editarSeleccionado);
    }
}

// ---------------------------------------------------------------- período

void VentanaPrincipal::setVista(Vista v)
{
    vista_ = v;
    if (ui->comboVista->currentIndex() != int(v)) {
        const QSignalBlocker b(ui->comboVista);
        ui->comboVista->setCurrentIndex(int(v));
    }
    refrescar();
}

void VentanaPrincipal::setPeriodo(const QDate& fecha)
{
    if (!fecha.isValid()) return;
    periodo_ = QDate(fecha.year(), fecha.month(), 1);
    refrescar();
}

void VentanaPrincipal::moverPeriodo(int pasos)
{
    if (vista_ == Vista::Mes) setPeriodo(periodo_.addMonths(pasos));
    else if (vista_ == Vista::Anio) setPeriodo(periodo_.addYears(pasos));
}

QPair<QDate, QDate> VentanaPrincipal::rango() const
{
    switch (vista_) {
    case Vista::Mes: return {periodo_, periodo_.addMonths(1).addDays(-1)};
    case Vista::Anio: return {QDate(periodo_.year(), 1, 1), QDate(periodo_.year(), 12, 31)};
    case Vista::Todo: break;
    }
    return {QDate(), QDate()};
}

QString VentanaPrincipal::textoPeriodo() const
{
    switch (vista_) {
    case Vista::Mes: return QStringLiteral("%1 %2").arg(nombreMes(periodo_.month())).arg(periodo_.year());
    case Vista::Anio: return QString::number(periodo_.year());
    case Vista::Todo: break;
    }
    return tr("Todos los movimientos");
}

// ---------------------------------------------------------------- carga de datos

void VentanaPrincipal::refrescar()
{
    ui->labelPeriodo->setText(textoPeriodo());
    const bool navegable = vista_ != Vista::Todo;
    ui->btnAnterior->setEnabled(navegable);
    ui->btnSiguiente->setEnabled(navegable);
    ui->btnHoy->setEnabled(navegable);
    ui->btnAnterior->setToolTip(vista_ == Vista::Anio ? tr("Año anterior (Alt+←)") : tr("Mes anterior (Alt+←)"));
    ui->btnSiguiente->setToolTip(vista_ == Vista::Anio ? tr("Año siguiente (Alt+→)") : tr("Mes siguiente (Alt+→)"));

    cargarMovimientos();
    cargarTarjetas();
    cargarGraficos();
    cargarTotales();
}

void VentanaPrincipal::cargarMovimientos()
{
    const QList<qint64> antes = idsSeleccionados();
    const TemaGrafico tema = temaPara(this);
    const auto [desde, hasta] = rango();
    modelo_->removeRows(0, modelo_->rowCount());
    const QList<Movimiento> lista = cuentas_.movimientos(desde, hasta);
    for (const Movimiento& m : lista) {
        const bool ingreso = m.tipo == Tipo::Ingreso;
        const Centavos conSigno = ingreso ? m.monto : -m.monto;

        auto* fecha = new QStandardItem(m.fecha.toString(QStringLiteral("dd/MM/yyyy")));
        fecha->setData(m.id, RolId);
        fecha->setData(m.fecha.toString(Qt::ISODate) + QStringLiteral("-%1").arg(m.id, 12, 10, QLatin1Char('0')), RolOrden);
        fecha->setData(ingreso ? 0 : 1, RolTipo);
        fecha->setData(normalizar(QStringList{m.categoria, m.descripcion, textoTipo(m.tipo), formatearPesos(m.monto),
                                              formatearNumero(m.monto), m.fecha.toString(QStringLiteral("dd/MM/yyyy"))}
                                      .join(QLatin1Char(' '))),
                       RolBusqueda);

        auto* tipo = new QStandardItem(textoTipo(m.tipo));
        tipo->setData(textoTipo(m.tipo), RolOrden);
        auto* cat = new QStandardItem(muestraColor(colorCategoria(tema, m.color)), m.categoria);
        cat->setData(normalizar(m.categoria), RolOrden);
        auto* desc = new QStandardItem(m.descripcion);
        desc->setData(normalizar(m.descripcion), RolOrden);
        auto* monto = new QStandardItem(formatearPesosConSigno(conSigno));
        monto->setData(conSigno, RolOrden);
        monto->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QList<QStandardItem*> fila{fecha, tipo, cat, desc, monto};
        for (QStandardItem* it : fila) it->setEditable(false);
        modelo_->appendRow(fila);
    }
    // Volver a elegir lo que estaba elegido
    if (!antes.isEmpty()) {
        QItemSelection sel;
        for (int r = 0; r < filtro_->rowCount(); ++r) {
            const QModelIndex i = filtro_->index(r, 0);
            if (antes.contains(i.data(RolId).toLongLong()))
                sel.select(i, filtro_->index(r, COLUMNAS - 1));
        }
        ui->tablaMovimientos->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    }
    actualizarBotones();
}

void VentanaPrincipal::cargarTarjetas()
{
    const auto [desde, hasta] = rango();
    const TotalPeriodo t = cuentas_.totales(desde, hasta);   // en «Todo» las fechas son inválidas = sin límite
    ui->labelIngresos->setText(formatearPesos(t.ingresos));
    ui->labelGastos->setText(formatearPesos(t.gastos));
    ui->labelResultado->setText(formatearPesosConSigno(t.saldo()));

    const QDate hoy = QDate::currentDate();
    ui->labelSaldo->setText(formatearPesos(cuentas_.saldo(hoy)));
    ui->labelSaldoTitulo->setText(tr("Saldo de la cuenta al %1").arg(hoy.toString(QStringLiteral("dd/MM/yyyy"))));
    QString ayuda = tr("Saldo inicial %1 + todos los ingresos − todos los gastos hasta hoy.")
                        .arg(formatearPesos(cuentas_.saldoInicial()));
    const QDate ultima = cuentas_.ultimaFecha();
    if (ultima.isValid() && ultima > hoy)
        ayuda += QLatin1Char('\n') + tr("Con los movimientos con fecha futura queda en %1.")
                                         .arg(formatearPesos(cuentas_.saldo(ultima)));
    ui->labelSaldo->setToolTip(ayuda);
    ui->labelSaldoTitulo->setToolTip(ayuda);

    // Rayita del color de la serie en cada tarjeta (como en los gráficos)
    const TemaGrafico tema = temaPara(this);
    const QString estilo = QStringLiteral("#%1 { border: 1px solid %2; border-left: 4px solid %3; border-radius: 3px; }");
    ui->frameIngresos->setStyleSheet(estilo.arg(ui->frameIngresos->objectName(), tema.grilla.name(), tema.serie[0].name()));
    ui->frameGastos->setStyleSheet(estilo.arg(ui->frameGastos->objectName(), tema.grilla.name(), tema.serie[1].name()));
    ui->frameResultado->setStyleSheet(QStringLiteral("#frameResultado { border: 1px solid %1; border-radius: 3px; }").arg(tema.grilla.name()));

    const int visibles = filtro_->rowCount();
    const int total = modelo_->rowCount();
    statusBar()->showMessage(visibles == total ? tr("%n movimiento(s)", nullptr, total)
                                               : tr("%1 de %2 movimientos (filtrados)").arg(visibles).arg(total));
}

void VentanaPrincipal::cargarGraficos()
{
    const QString periodo = textoPeriodo();
    if (vista_ == Vista::Todo) {
        aniosBarras_.clear();
        QStringList etiquetas;
        QVector<Centavos> ing, gas;
        for (const auto& [anio, t] : cuentas_.totalesPorAnio()) {
            aniosBarras_ << anio;
            etiquetas << QString::number(anio);
            ing << t.ingresos;
            gas << t.gastos;
        }
        ui->graficoBarras->setDatos(tr("Ingresos y gastos por año"), etiquetas, etiquetas, ing, gas);
        ui->graficoBarras->setMarcado(aniosBarras_.indexOf(periodo_.year()));
        ui->graficoBarras->setTextoVacio(tr("Todavía no hay movimientos"));
        ui->graficoBarras->setToolTip(tr("Clic en un año para verlo"));
    } else {
        const int anio = periodo_.year();
        const QVector<TotalPeriodo> meses = cuentas_.totalesPorMes(anio);
        QStringList etiquetas, detalles;
        QVector<Centavos> ing, gas;
        for (int m = 1; m <= 12; ++m) {
            etiquetas << nombreMesCorto(m);
            detalles << QStringLiteral("%1 %2").arg(nombreMes(m)).arg(anio);
            ing << meses.value(m - 1).ingresos;
            gas << meses.value(m - 1).gastos;
        }
        ui->graficoBarras->setDatos(tr("Ingresos y gastos por mes — %1").arg(anio), etiquetas, detalles, ing, gas);
        ui->graficoBarras->setMarcado(vista_ == Vista::Mes ? periodo_.month() - 1 : -1);
        ui->graficoBarras->setTextoVacio(tr("Sin movimientos en %1").arg(anio));
        ui->graficoBarras->setToolTip(QString());
    }

    const auto [desde, hasta] = rango();
    ui->tortaGastos->setDatos(tr("Gastos por categoría — %1").arg(periodo),
                              cuentas_.totalesPorCategoria(desde, hasta, Tipo::Gasto));
    ui->tortaGastos->setTextoVacio(tr("Sin gastos en este período"));
    ui->tortaIngresos->setDatos(tr("Ingresos por categoría — %1").arg(periodo),
                                cuentas_.totalesPorCategoria(desde, hasta, Tipo::Ingreso));
    ui->tortaIngresos->setTextoVacio(tr("Sin ingresos en este período"));
}

void VentanaPrincipal::cargarTotales()
{
    const TemaGrafico tema = temaPara(this);

    // Por mes (del año elegido)
    const int anio = periodo_.year();
    ui->labelTotalesMeses->setText(tr("Por mes — %1").arg(anio));
    const QVector<TotalPeriodo> meses = cuentas_.totalesPorMes(anio);
    ui->tablaMeses->setRowCount(13);
    TotalPeriodo totalAnio;
    for (int m = 0; m < 12; ++m) {
        const TotalPeriodo t = meses.value(m);
        totalAnio.ingresos += t.ingresos;
        totalAnio.gastos += t.gastos;
        const bool elegido = vista_ == Vista::Mes && m == periodo_.month() - 1;
        ui->tablaMeses->setItem(m, 0, celda(nombreMes(m + 1), false, elegido));
        ui->tablaMeses->setItem(m, 1, celda(formatearPesos(t.ingresos), true, elegido));
        ui->tablaMeses->setItem(m, 2, celda(formatearPesos(t.gastos), true, elegido));
        ui->tablaMeses->setItem(m, 3, celda(formatearPesosConSigno(t.saldo()), true, elegido));
    }
    ui->tablaMeses->setItem(12, 0, celda(tr("Total %1").arg(anio), false, true));
    ui->tablaMeses->setItem(12, 1, celda(formatearPesos(totalAnio.ingresos), true, true));
    ui->tablaMeses->setItem(12, 2, celda(formatearPesos(totalAnio.gastos), true, true));
    ui->tablaMeses->setItem(12, 3, celda(formatearPesosConSigno(totalAnio.saldo()), true, true));

    // Por categoría (del período elegido)
    ui->labelTotalesCategorias->setText(tr("Por categoría — %1").arg(textoPeriodo()));
    const auto [desde, hasta] = rango();
    ui->tablaCategorias->setRowCount(0);
    auto agregarFila = [this](QTableWidgetItem* a, QTableWidgetItem* b, QTableWidgetItem* c) {
        const int r = ui->tablaCategorias->rowCount();
        ui->tablaCategorias->insertRow(r);
        ui->tablaCategorias->setItem(r, 0, a);
        ui->tablaCategorias->setItem(r, 1, b);
        ui->tablaCategorias->setItem(r, 2, c);
    };
    for (Tipo tipo : {Tipo::Gasto, Tipo::Ingreso}) {
        const QList<TotalCategoria> lista = cuentas_.totalesPorCategoria(desde, hasta, tipo);
        Centavos total = 0;
        for (const TotalCategoria& tc : lista) total += tc.total;
        if (ui->tablaCategorias->rowCount() > 0) agregarFila(celda(QString()), celda(QString()), celda(QString()));
        agregarFila(celda(tipo == Tipo::Gasto ? tr("Gastos") : tr("Ingresos"), false, true),
                    celda(formatearPesos(total), true, true), celda(QString()));
        for (const TotalCategoria& tc : lista) {
            if (tc.total <= 0) continue;
            auto* nombre = celda(tc.nombre);
            nombre->setIcon(muestraColor(colorCategoria(tema, tc.color)));
            const double pct = total > 0 ? 100.0 * double(tc.total) / double(total) : 0;
            agregarFila(nombre, celda(formatearPesos(tc.total), true),
                        celda(QString::number(pct, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')) + QStringLiteral(" %"), true));
        }
    }

    // Por año
    const auto anios = cuentas_.totalesPorAnio();
    ui->tablaAnios->setRowCount(anios.size() + 1);
    TotalPeriodo total;
    for (int i = 0; i < anios.size(); ++i) {
        const auto& [a, t] = anios[i];
        total.ingresos += t.ingresos;
        total.gastos += t.gastos;
        const bool elegido = vista_ != Vista::Todo && a == anio;
        ui->tablaAnios->setItem(i, 0, celda(QString::number(a), false, elegido));
        ui->tablaAnios->setItem(i, 1, celda(formatearPesos(t.ingresos), true, elegido));
        ui->tablaAnios->setItem(i, 2, celda(formatearPesos(t.gastos), true, elegido));
        ui->tablaAnios->setItem(i, 3, celda(formatearPesosConSigno(t.saldo()), true, elegido));
    }
    const int r = anios.size();
    ui->tablaAnios->setItem(r, 0, celda(tr("Total"), false, true));
    ui->tablaAnios->setItem(r, 1, celda(formatearPesos(total.ingresos), true, true));
    ui->tablaAnios->setItem(r, 2, celda(formatearPesos(total.gastos), true, true));
    ui->tablaAnios->setItem(r, 3, celda(formatearPesosConSigno(total.saldo()), true, true));
}

void VentanaPrincipal::actualizarBotones()
{
    const int n = idsSeleccionados().size();
    ui->btnEditar->setEnabled(n == 1);
    ui->btnEliminar->setEnabled(n > 0);
}

void VentanaPrincipal::clicEnBarras(int indice)
{
    if (indice < 0) return;
    if (vista_ == Vista::Todo) {
        if (indice >= aniosBarras_.size()) return;
        periodo_ = QDate(aniosBarras_[indice], 1, 1);
        setVista(Vista::Anio);
    } else {
        periodo_ = QDate(periodo_.year(), indice + 1, 1);
        setVista(Vista::Mes);
    }
}

// ---------------------------------------------------------------- movimientos

QList<qint64> VentanaPrincipal::idsSeleccionados() const
{
    QList<qint64> ids;
    if (!ui->tablaMovimientos->selectionModel()) return ids;
    for (const QModelIndex& i : ui->tablaMovimientos->selectionModel()->selectedRows(ColFecha))
        ids << i.data(RolId).toLongLong();
    return ids;
}

void VentanaPrincipal::seleccionarMovimiento(qint64 id)
{
    for (int r = 0; r < filtro_->rowCount(); ++r) {
        const QModelIndex i = filtro_->index(r, ColFecha);
        if (i.data(RolId).toLongLong() == id) {
            ui->tablaMovimientos->selectionModel()->select(
                i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            ui->tablaMovimientos->setCurrentIndex(i);
            ui->tablaMovimientos->scrollTo(i);
            return;
        }
    }
}

void VentanaPrincipal::nuevoIngreso() { nuevo(Tipo::Ingreso); }
void VentanaPrincipal::nuevoGasto() { nuevo(Tipo::Gasto); }

void VentanaPrincipal::nuevo(Tipo tipo)
{
    Movimiento m;
    m.tipo = tipo;
    // Fecha inicial: hoy si cae en el período que se está mirando; si no, el primer día del período
    const auto [desde, hasta] = rango();
    const QDate hoy = QDate::currentDate();
    m.fecha = (!desde.isValid() || (hoy >= desde && hoy <= hasta)) ? hoy : desde;
    DialogoMovimiento d(cuentas_, m, this);
    if (d.exec() != QDialog::Accepted) return;
    ui->tabWidget->setCurrentWidget(ui->tabMovimientos);
    const Movimiento guardado = d.movimiento();
    refrescar();
    seleccionarMovimiento(guardado.id);
    statusBar()->showMessage(tr("%1 guardado: %2 en %3").arg(textoTipo(guardado.tipo), formatearPesos(guardado.monto),
                                                           guardado.categoria), 5000);
}

void VentanaPrincipal::editarSeleccionado()
{
    const QList<qint64> ids = idsSeleccionados();
    if (ids.size() != 1) return;
    const auto m = cuentas_.movimiento(ids.first());
    if (!m) return;
    DialogoMovimiento d(cuentas_, *m, this);
    if (d.exec() != QDialog::Accepted) return;
    refrescar();
    seleccionarMovimiento(m->id);
}

void VentanaPrincipal::repetirSeleccionado()
{
    const QList<qint64> ids = idsSeleccionados();
    if (ids.size() != 1) return;
    auto m = cuentas_.movimiento(ids.first());
    if (!m) return;
    m->id = 0;
    m->fecha = QDate::currentDate();
    DialogoMovimiento d(cuentas_, *m, this);
    d.setWindowTitle(tr("Repetir movimiento"));
    if (d.exec() != QDialog::Accepted) return;
    const qint64 id = d.movimiento().id;
    setPeriodo(d.movimiento().fecha);   // que se vea lo que se acaba de cargar
    seleccionarMovimiento(id);
}

int VentanaPrincipal::eliminarMovimientos(const QList<qint64>& ids)
{
    int borrados = 0;
    for (qint64 id : ids) if (cuentas_.eliminarMovimiento(id).ok) ++borrados;
    refrescar();
    return borrados;
}

void VentanaPrincipal::eliminarSeleccionados()
{
    const QList<qint64> ids = idsSeleccionados();
    if (ids.isEmpty()) return;
    QString pregunta;
    if (ids.size() == 1) {
        const auto m = cuentas_.movimiento(ids.first());
        if (!m) return;
        pregunta = tr("¿Eliminar este movimiento?\n\n%1 · %2 · %3\n%4")
                       .arg(m->fecha.toString(QStringLiteral("dd/MM/yyyy")), m->categoria, formatearPesos(m->monto), m->descripcion);
    } else {
        pregunta = tr("¿Eliminar los %1 movimientos elegidos?").arg(ids.size());
    }
    if (QMessageBox::question(this, tr("Eliminar"), pregunta.trimmed()) != QMessageBox::Yes) return;
    const int n = eliminarMovimientos(ids);
    statusBar()->showMessage(tr("%n movimiento(s) eliminado(s)", nullptr, n), 5000);
}

void VentanaPrincipal::menuContextual(const QPoint& pos)
{
    const QModelIndex i = ui->tablaMovimientos->indexAt(pos);
    if (i.isValid() && !ui->tablaMovimientos->selectionModel()->isRowSelected(i.row(), QModelIndex()))
        ui->tablaMovimientos->selectRow(i.row());
    const int n = idsSeleccionados().size();
    QMenu menu(this);
    menu.addAction(tr("Nuevo ingreso"), this, &VentanaPrincipal::nuevoIngreso);
    menu.addAction(tr("Nuevo gasto"), this, &VentanaPrincipal::nuevoGasto);
    menu.addSeparator();
    menu.addAction(tr("Editar…"), this, &VentanaPrincipal::editarSeleccionado)->setEnabled(n == 1);
    menu.addAction(tr("Repetir con fecha de hoy…"), this, &VentanaPrincipal::repetirSeleccionado)->setEnabled(n == 1);
    menu.addAction(tr("Eliminar"), this, &VentanaPrincipal::eliminarSeleccionados)->setEnabled(n > 0);
    menu.exec(ui->tablaMovimientos->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------- menú ☰

void VentanaPrincipal::abrirCategorias()
{
    DialogoCategorias d(cuentas_, ui->comboFiltroTipo->currentIndex() == 1 ? Tipo::Ingreso : Tipo::Gasto, this);
    d.exec();
    if (d.huboCambios()) refrescar();
}

bool VentanaPrincipal::fijarSaldoInicialTexto(const QString& texto)
{
    Centavos c = 0;
    if (texto.trimmed().isEmpty()) c = 0;
    else if (!parsearPesos(texto, &c, true)) return false;
    cuentas_.fijarSaldoInicial(c);
    refrescar();
    return true;
}

void VentanaPrincipal::pedirSaldoInicial()
{
    QString texto = formatearPesos(cuentas_.saldoInicial());
    texto.remove(QStringLiteral("$ "));
    while (true) {
        bool ok = false;
        texto = QInputDialog::getText(this, tr("Saldo inicial"),
            tr("Plata que había en la cuenta antes del primer movimiento cargado.\n"
               "Se suma al saldo. Puede ser negativo (por ejemplo -5.000)."),
            QLineEdit::Normal, texto, &ok);
        if (!ok) return;
        if (fijarSaldoInicialTexto(texto)) return;
        QMessageBox::warning(this, tr("Saldo inicial"), tr("«%1» no es un monto válido. Ejemplo: 150.000,00").arg(texto));
    }
}

bool VentanaPrincipal::exportarArchivo(const QString& ruta, QString* error)
{
    const auto [desde, hasta] = rango();
    QList<Movimiento> lista = cuentas_.movimientos(desde, hasta);
    std::reverse(lista.begin(), lista.end());   // en el archivo, del más viejo al más nuevo
    QSaveFile f(ruta);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(csvParaGuardar(movimientosACsv(lista)));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

void VentanaPrincipal::pedirExportar()
{
    QSettings s;
    const QString carpeta = s.value(QStringLiteral("carpetaCsv"),
                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    QString nombre = QStringLiteral("movimientos");
    if (vista_ == Vista::Mes) nombre += QStringLiteral("-%1-%2").arg(periodo_.year()).arg(periodo_.month(), 2, 10, QLatin1Char('0'));
    else if (vista_ == Vista::Anio) nombre += QStringLiteral("-%1").arg(periodo_.year());
    QString ruta = QFileDialog::getSaveFileName(this, tr("Exportar %1 a CSV").arg(textoPeriodo()),
                                                carpeta + QLatin1Char('/') + nombre + QStringLiteral(".csv"),
                                                tr("CSV (*.csv);;Todos los archivos (*)"));
    if (ruta.isEmpty()) return;
    if (QFileInfo(ruta).suffix().isEmpty()) ruta += QStringLiteral(".csv");
    s.setValue(QStringLiteral("carpetaCsv"), QFileInfo(ruta).absolutePath());
    QString error;
    if (!exportarArchivo(ruta, &error)) {
        QMessageBox::warning(this, tr("Exportar"), tr("No se pudo guardar el archivo:\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Exportado: %1").arg(QFileInfo(ruta).fileName()), 6000);
}

// Si el período que se está viendo quedó vacío, ir al mes del movimiento importado más nuevo
// (así se ve enseguida lo que entró, por ejemplo un reporte de ExpenLess que termina el mes pasado)
void VentanaPrincipal::mostrarImportados(const QList<FilaImportada>& filas)
{
    refrescar();
    if (modelo_->rowCount() > 0 || vista_ == Vista::Todo) return;
    QDate ultima;
    for (const FilaImportada& f : filas) if (!ultima.isValid() || f.fecha > ultima) ultima = f.fecha;
    if (ultima.isValid()) setPeriodo(ultima);
}

ResultadoImportacion VentanaPrincipal::importarArchivo(const QString& ruta, QString* error)
{
    LecturaCsv lectura;
    if (!leerArchivo(ruta, &lectura, error)) return {};
    const ResultadoImportacion r = cuentas_.importar(lectura.filas);
    mostrarImportados(lectura.filas);
    return r;
}

void VentanaPrincipal::pedirImportar()
{
    QSettings s;
    const QString carpeta = s.value(QStringLiteral("carpetaCsv"),
                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString ruta = QFileDialog::getOpenFileName(this, tr("Importar movimientos desde CSV"), carpeta,
                                                      tr("CSV (*.csv *.txt);;Todos los archivos (*)"));
    if (ruta.isEmpty()) return;
    s.setValue(QStringLiteral("carpetaCsv"), QFileInfo(ruta).absolutePath());

    LecturaCsv lectura;
    QString error;
    if (!leerArchivo(ruta, &lectura, &error)) {
        QMessageBox::warning(this, tr("Importar"), tr("No se pudo leer «%1»:\n%2").arg(QFileInfo(ruta).fileName(), error));
        return;
    }
    // Antes de guardar, mostrar qué va a pasar
    const ResultadoImportacion previa = cuentas_.previsualizarImportacion(lectura.filas);
    QString texto = tr("Se van a agregar %n movimiento(s).", nullptr, previa.nuevos);
    if (previa.repetidos) texto += QLatin1Char('\n') + tr("%n ya estaba(n) cargado(s) y no se duplica(n).", nullptr, previa.repetidos);
    if (!previa.categoriasNuevas.isEmpty())
        texto += QStringLiteral("\n\n") + tr("Categorías nuevas: %1").arg(previa.categoriasNuevas.join(QStringLiteral(", ")));
    QMessageBox caja(QMessageBox::Question, tr("Importar"), texto, QMessageBox::NoButton, this);
    if (!lectura.avisos.isEmpty()) {
        caja.setInformativeText(tr("%n renglón(es) no se pudo(pudieron) leer y se saltea(n).", nullptr, lectura.avisos.size()));
        caja.setDetailedText(lectura.avisos.join(QLatin1Char('\n')));
    }
    if (previa.nuevos == 0) {
        caja.setIcon(QMessageBox::Information);
        caja.setText(tr("No hay movimientos nuevos para agregar.") + (previa.repetidos ? QLatin1Char('\n') + texto.section(QLatin1Char('\n'), 1) : QString()));
        caja.addButton(QMessageBox::Ok);
        caja.exec();
        return;
    }
    QPushButton* si = caja.addButton(tr("Importar"), QMessageBox::AcceptRole);
    caja.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    caja.exec();
    if (caja.clickedButton() != si) return;

    const ResultadoImportacion r = cuentas_.importar(lectura.filas);
    mostrarImportados(lectura.filas);
    statusBar()->showMessage(tr("Importados %n movimiento(s)", nullptr, r.nuevos), 8000);
}

void VentanaPrincipal::pedirBackup()
{
    const QString nombre = QStringLiteral("cuentas-%1.db").arg(QDate::currentDate().toString(Qt::ISODate));
    const QString ruta = QFileDialog::getSaveFileName(this, tr("Guardar copia de seguridad"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + nombre,
        tr("Base de datos (*.db)"));
    if (ruta.isEmpty()) return;
    const Resultado r = cuentas_.backup(ruta);
    if (!r.ok) QMessageBox::warning(this, tr("Copia de seguridad"), r.error);
    else statusBar()->showMessage(tr("Copia guardada en %1").arg(ruta), 8000);
}

void VentanaPrincipal::abrirSincronizacion()
{
    DialogoSincronizacion d(cuentas_, *servidor_, this);
    d.exec();
}

void VentanaPrincipal::acercaDe()
{
    QMessageBox::about(this, tr("Acerca de Ingresos y Gastos"),
        tr("<h3>Ingresos y Gastos %1</h3>"
           "<p>Ingresos, gastos y saldo de la cuenta, en pesos argentinos.</p>"
           "<p>Los datos se guardan en:<br><code>%2</code></p>"
           "<p>Hecho con C++ y Qt %3.</p>")
            .arg(QApplication::applicationVersion(), cuentas_.ruta().toHtmlEscaped(), QString::fromLatin1(qVersion())));
}

// ---------------------------------------------------------------- configuración

void VentanaPrincipal::guardarConfiguracion()
{
    QSettings s;
    s.setValue(QStringLiteral("ventana/geometria"), saveGeometry());
    s.setValue(QStringLiteral("ventana/estado"), saveState());
    s.setValue(QStringLiteral("ventana/columnas"), ui->tablaMovimientos->horizontalHeader()->saveState());
    s.setValue(QStringLiteral("ventana/pestania"), ui->tabWidget->currentIndex());
    s.setValue(QStringLiteral("ventana/vista"), int(vista_));
}

void VentanaPrincipal::leerConfiguracion()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("ventana/geometria")).toByteArray());
    restoreState(s.value(QStringLiteral("ventana/estado")).toByteArray());
    ui->tablaMovimientos->horizontalHeader()->restoreState(s.value(QStringLiteral("ventana/columnas")).toByteArray());
    ui->tabWidget->setCurrentIndex(s.value(QStringLiteral("ventana/pestania"), 0).toInt());
    const int v = std::clamp(s.value(QStringLiteral("ventana/vista"), 0).toInt(), 0, 2);
    vista_ = Vista(v);
    const QSignalBlocker b(ui->comboVista);
    ui->comboVista->setCurrentIndex(v);
}

void VentanaPrincipal::closeEvent(QCloseEvent* e)
{
    guardarConfiguracion();
    e->accept();
}

void VentanaPrincipal::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) refrescar();   // cambió el tema claro/oscuro: recolorear
}
