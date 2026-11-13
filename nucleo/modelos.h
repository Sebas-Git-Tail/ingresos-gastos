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
    Centavos presupuesto = 0;   // (gastos) cuánto se piensa gastar por mes; 0 = sin presupuesto
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
    qint64 cuentaId = 0;        // en qué cuenta (efectivo, banco...); 0 = la principal
    QString cuenta;             // nombre (se completa al listar)
};

// Una cuenta de dinero: efectivo, banco, Mercado Pago... Siempre hay una (la principal).
struct CuentaDinero {
    qint64 id = 0;
    QString nombre;
    Centavos saldoInicial = 0;
    Centavos saldo = 0;         // saldo inicial + ingresos - gastos hasta hoy (se completa al listar)
    int movimientos = 0;
    bool principal = false;     // la que trae el programa: no se puede eliminar
};

// Un movimiento fijo que se carga solo todos los meses (sueldo, alquiler, internet...)
struct Recurrente {
    qint64 id = 0;
    qint64 categoriaId = 0;
    QString categoria;          // nombre (se completa al listar)
    Tipo tipo = Tipo::Gasto;    // el de su categoría
    qint64 cuentaId = 0;        // 0 = la principal
    QString cuenta;
    Centavos monto = 0;
    QString descripcion;
    int dia = 1;                // día del mes (si el mes es más corto, el último día)
    QDate desde;                // desde qué fecha (incluida)
    QDate hasta;                // hasta qué fecha; inválida = sin fin
    bool activo = true;
};

// Cómo va el presupuesto de una categoría de gasto en un mes
struct EstadoPresupuesto {
    qint64 categoriaId = 0;
    QString nombre;
    int color = 0;
    Centavos presupuesto = 0;
    Centavos gastado = 0;
    int porcentaje() const { return presupuesto > 0 ? int(gastado * 100 / presupuesto) : 0; }
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
