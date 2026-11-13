#include "modelomovimientos.h"
#include "dinero.h"
#include <algorithm>

namespace {
QString diaTexto(const QDate& d)
{
    static const char* const dias[] = {"Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado", "Domingo"};
    return QStringLiteral("%1 %2 de %3 de %4")
        .arg(QString::fromUtf8(dias[d.dayOfWeek() - 1]))
        .arg(d.day())
        .arg(nombreMes(d.month()).toLower())
        .arg(d.year());
}
}

void ModeloMovimientos::cargar(const QList<Movimiento>& lista, const QColor (&paleta)[8])
{
    beginResetModel();
    lista_ = lista;
    std::copy(std::begin(paleta), std::end(paleta), std::begin(paleta_));
    endResetModel();
}

int ModeloMovimientos::rowCount(const QModelIndex& padre) const
{
    return padre.isValid() ? 0 : lista_.size();
}

QVariant ModeloMovimientos::data(const QModelIndex& i, int rol) const
{
    if (!i.isValid() || i.row() >= lista_.size()) return {};
    const Movimiento& m = lista_.at(i.row());
    switch (rol) {
    case RolId: return m.id;
    case RolDia: return m.fecha.toString(Qt::ISODate);
    case RolDiaTexto: return diaTexto(m.fecha);
    case RolCategoria: return m.categoria;
    case RolColor: return paleta_[std::clamp(m.color, 0, 7)];
    case RolIngreso: return m.tipo == Tipo::Ingreso;
    case RolMontoTexto: return formatearPesosConSigno(m.tipo == Tipo::Ingreso ? m.monto : -m.monto);
    case RolDescripcion: return m.descripcion;
    }
    return {};
}

QHash<int, QByteArray> ModeloMovimientos::roleNames() const
{
    return {{RolId, "idMov"}, {RolDia, "dia"}, {RolDiaTexto, "diaTexto"}, {RolCategoria, "categoria"}, {RolColor, "colorCategoria"},
            {RolIngreso, "ingreso"}, {RolMontoTexto, "montoTexto"}, {RolDescripcion, "descripcion"}};
}
