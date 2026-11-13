#include "dialogocategorias.h"
#include "ui_dialogo_categorias.h"
#include "graficos.h"
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>

namespace {

// Cuadradito del color de la categoría (el mismo que usa en los gráficos)
QIcon muestra(const QColor& color)
{
    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(1, 1, 14, 14), 3, 3);
    return QIcon(px);
}

const char* const NOMBRES_COLORES[] = {"Azul", "Naranja", "Verde", "Amarillo", "Rosa", "Verde oscuro", "Violeta", "Rojo"};

} // namespace

DialogoCategorias::DialogoCategorias(Cuentas& cuentas, Tipo tipoInicial, QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::DialogoCategorias>()), cuentas_(cuentas)
{
    ui->setupUi(this);
    ui->tabTipos->setCurrentIndex(tipoInicial == Tipo::Gasto ? 0 : 1);

    // Menú de colores: los 8 de la paleta de los gráficos
    auto* menu = new QMenu(ui->btnColor);
    const TemaGrafico tema = temaPara(this);
    for (int i = 0; i < Cuentas::COLORES; ++i) {
        QAction* a = menu->addAction(muestra(tema.serie[i]), tr(NOMBRES_COLORES[i]));
        connect(a, &QAction::triggered, this, [this, i] { mostrarError(ponerColor(i)); });
    }
    ui->btnColor->setMenu(menu);

    connect(ui->btnAgregar, &QPushButton::clicked, this, &DialogoCategorias::pedirAgregar);
    connect(ui->btnRenombrar, &QPushButton::clicked, this, &DialogoCategorias::pedirRenombrar);
    connect(ui->btnEliminar, &QPushButton::clicked, this, &DialogoCategorias::pedirEliminar);
    connect(ui->tabTipos, &QTabWidget::currentChanged, this, &DialogoCategorias::actualizarBotones);
    for (QListWidget* l : {ui->listaGastos, ui->listaIngresos}) {
        connect(l, &QListWidget::currentRowChanged, this, &DialogoCategorias::actualizarBotones);
        connect(l, &QListWidget::itemDoubleClicked, this, &DialogoCategorias::pedirRenombrar);
    }
    cargar();
}

DialogoCategorias::~DialogoCategorias() = default;

Tipo DialogoCategorias::tipoActual() const
{
    return ui->tabTipos->currentIndex() == 0 ? Tipo::Gasto : Tipo::Ingreso;
}

QListWidget* DialogoCategorias::listaActual() const
{
    return tipoActual() == Tipo::Gasto ? ui->listaGastos : ui->listaIngresos;
}

qint64 DialogoCategorias::seleccionada() const
{
    const QListWidgetItem* it = listaActual()->currentItem();
    return it ? it->data(Qt::UserRole).toLongLong() : 0;
}

void DialogoCategorias::seleccionar(qint64 id)
{
    const auto c = cuentas_.categoria(id);
    if (!c) return;
    ui->tabTipos->setCurrentIndex(c->tipo == Tipo::Gasto ? 0 : 1);
    QListWidget* l = listaActual();
    for (int i = 0; i < l->count(); ++i)
        if (l->item(i)->data(Qt::UserRole).toLongLong() == id) l->setCurrentRow(i);
}

void DialogoCategorias::cargar(qint64 elegir)
{
    const TemaGrafico tema = temaPara(this);
    const qint64 antes = elegir ? elegir : seleccionada();
    for (Tipo t : {Tipo::Gasto, Tipo::Ingreso}) {
        QListWidget* l = t == Tipo::Gasto ? ui->listaGastos : ui->listaIngresos;
        const int fila = l->currentRow();
        l->clear();
        for (const Categoria& c : cuentas_.categorias(t)) {
            const QString cant = c.movimientos == 1 ? tr("1 movimiento") : tr("%1 movimientos").arg(c.movimientos);
            auto* it = new QListWidgetItem(muestra(tema.serie[std::clamp(c.color, 0, 7)]),
                                           QStringLiteral("%1  (%2)").arg(c.nombre, cant), l);
            it->setData(Qt::UserRole, c.id);
            it->setData(Qt::UserRole + 1, c.nombre);
        }
        if (l->count()) l->setCurrentRow(std::clamp(fila, 0, l->count() - 1));
    }
    if (antes) seleccionar(antes);
    actualizarBotones();
}

void DialogoCategorias::actualizarBotones()
{
    const bool hay = seleccionada() != 0;
    ui->btnRenombrar->setEnabled(hay);
    ui->btnColor->setEnabled(hay);
    ui->btnEliminar->setEnabled(hay && listaActual()->count() > 1);   // siempre queda al menos una
}

void DialogoCategorias::mostrarError(const Resultado& r)
{
    if (!r.ok && !r.error.isEmpty()) QMessageBox::warning(this, windowTitle(), r.error);
}

Resultado DialogoCategorias::agregar(const QString& nombre)
{
    const Resultado r = cuentas_.crearCategoria(nombre.trimmed(), tipoActual());
    if (r.ok) { cambios_ = true; cargar(r.id); }
    return r;
}

Resultado DialogoCategorias::renombrar(const QString& nombre)
{
    const qint64 id = seleccionada();
    if (!id) return {false, tr("Elegí una categoría."), 0};
    const Resultado r = cuentas_.renombrarCategoria(id, nombre.trimmed());
    if (r.ok) { cambios_ = true; cargar(id); }
    return r;
}

Resultado DialogoCategorias::ponerColor(int color)
{
    const qint64 id = seleccionada();
    if (!id) return {false, tr("Elegí una categoría."), 0};
    const Resultado r = cuentas_.cambiarColor(id, color);
    if (r.ok) { cambios_ = true; cargar(id); }
    return r;
}

Resultado DialogoCategorias::eliminar(qint64 moverA)
{
    const qint64 id = seleccionada();
    if (!id) return {false, tr("Elegí una categoría."), 0};
    const Resultado r = cuentas_.eliminarCategoria(id, moverA);
    if (r.ok) { cambios_ = true; cargar(moverA); }
    return r;
}

void DialogoCategorias::pedirAgregar()
{
    bool ok = false;
    const QString nombre = QInputDialog::getText(this, tr("Agregar categoría"),
        tipoActual() == Tipo::Gasto ? tr("Nombre de la nueva categoría de gastos:")
                                    : tr("Nombre de la nueva categoría de ingresos:"),
        QLineEdit::Normal, QString(), &ok);
    if (ok && !nombre.trimmed().isEmpty()) mostrarError(agregar(nombre));
}

void DialogoCategorias::pedirRenombrar()
{
    const QListWidgetItem* it = listaActual()->currentItem();
    if (!it) return;
    bool ok = false;
    const QString nombre = QInputDialog::getText(this, tr("Renombrar categoría"), tr("Nuevo nombre:"),
        QLineEdit::Normal, it->data(Qt::UserRole + 1).toString(), &ok);
    if (ok && !nombre.trimmed().isEmpty()) mostrarError(renombrar(nombre));
}

void DialogoCategorias::pedirEliminar()
{
    const auto c = cuentas_.categoria(seleccionada());
    if (!c) return;
    if (c->movimientos == 0) {
        if (QMessageBox::question(this, tr("Eliminar categoría"), tr("¿Eliminar la categoría «%1»?").arg(c->nombre))
            == QMessageBox::Yes)
            mostrarError(eliminar());
        return;
    }
    // Tiene movimientos: hay que pasarlos a otra categoría del mismo tipo
    QStringList nombres;
    QList<qint64> ids;
    for (const Categoria& otra : cuentas_.categorias(c->tipo)) {
        if (otra.id == c->id) continue;
        nombres << otra.nombre;
        ids << otra.id;
    }
    if (ids.isEmpty()) return;
    int porDefecto = 0;
    for (int i = 0; i < nombres.size(); ++i)
        if (nombres[i].contains(QStringLiteral("Varios"), Qt::CaseInsensitive)) porDefecto = i;
    bool ok = false;
    const QString destino = QInputDialog::getItem(this, tr("Eliminar categoría"),
        tr("«%1» tiene %2 movimiento(s).\n¿A qué categoría los paso?").arg(c->nombre).arg(c->movimientos),
        nombres, porDefecto, false, &ok);
    if (!ok) return;
    mostrarError(eliminar(ids[nombres.indexOf(destino)]));
}
