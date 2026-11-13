#pragma once
#include <QDate>
#include <QList>
#include <QString>

// Los montos se guardan en CENTAVOS enteros: así las sumas son exactas (con double, 0,1 + 0,2 no da 0,3)
using Centavos = qint64;

enum class Tipo { Ingreso, Gasto };

struct Categoria {
    qint64 id = 0;
    QString nombre;
    Tipo tipo = Tipo::Gasto;
    int color = 0;              // posición en la paleta de los gráficos (0 a 7)
    int movimientos = 0;        // cuántos movimientos la usan (se completa al listar)
};

struct Movimiento {
    qint64 id = 0;
    QDate fecha;
    qint64 categoriaId = 0;
    QString categoria;          // nombre (se completa al listar)
    Tipo tipo = Tipo::Gasto;    // el de su categoría
    int color = 0;              // el de su categoría
    Centavos monto = 0;         // siempre positivo; el tipo dice si suma o resta
    QString descripcion;
};

// Total de una categoría en un período (para la torta y la tabla de totales)
struct TotalCategoria {
    qint64 categoriaId = 0;
    QString nombre;
    Tipo tipo = Tipo::Gasto;
    int color = 0;
    Centavos total = 0;
};

// Ingresos y gastos de un mes o de un año
struct TotalPeriodo {
    Centavos ingresos = 0;
    Centavos gastos = 0;
    Centavos saldo() const { return ingresos - gastos; }
};

inline QString textoTipo(Tipo t) { return t == Tipo::Ingreso ? QStringLiteral("Ingreso") : QStringLiteral("Gasto"); }
