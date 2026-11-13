#pragma once
#include "modelos.h"
#include <QAbstractListModel>
#include <QColor>
#include <QtQml/qqmlregistration.h>

/** Lista de movimientos para la pantalla del celular (agrupados por día con «section» en QML). */
class ModeloMovimientos : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Rol {
        RolId = Qt::UserRole + 1,
        RolDia,             // "2026-10-31": para agrupar
        RolDiaTexto,        // "Sábado 31 de octubre de 2026"
        RolCategoria,
        RolColor,
        RolIngreso,
        RolMontoTexto,      // "+$ 1.234,56" / "-$ 50,00"
        RolDescripcion,
    };

    using QAbstractListModel::QAbstractListModel;

    void cargar(const QList<Movimiento>& lista, const QColor (&paleta)[8]);
    int rowCount(const QModelIndex& padre = {}) const override;
    QVariant data(const QModelIndex& i, int rol) const override;
    QHash<int, QByteArray> roleNames() const override;
    const Movimiento& en(int fila) const { return lista_.at(fila); }

private:
    QList<Movimiento> lista_;
    QColor paleta_[8];
};
