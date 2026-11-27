#pragma once
#include "modelos.h"
#include <QPair>
#include <QStringList>
#include <optional>

// Lo que comparten la PC y el celular para buscar, etiquetar y comparar períodos.

// ---- búsqueda: cada palabra tiene que aparecer (sin mayúsculas ni tildes) y los filtros por monto, cumplirse
struct FiltroMonto {
    Centavos minimo = 0, maximo = 0;    // los dos incluidos
    bool coincide(Centavos c) const { return c >= minimo && c <= maximo; }
};
// «2000-5000» · «<3000» · «<=3.000,50» · «>2000» · «>=2000» -> filtro por monto; si no es una de esas, nullopt.
// Un número suelto («15.000») no es un filtro: se sigue buscando como texto (en el monto y en la descripción).
std::optional<FiltroMonto> interpretarFiltroMonto(const QString& palabra);

class Busqueda
{
public:
    explicit Busqueda(const QString& consulta = QString());
    bool vacia() const { return palabras_.isEmpty() && montos_.isEmpty(); }
    // textoNormalizado: el de textoBusqueda(); monto: siempre positivo
    bool coincide(const QString& textoNormalizado, Centavos monto) const;

private:
    QStringList palabras_;
    QList<FiltroMonto> montos_;
};

// Todo lo que se puede buscar de un movimiento, ya normalizado: categoría, descripción (con sus #etiquetas),
// tipo, monto, fecha y cuenta
QString textoBusqueda(const Movimiento& m);

// Cómo se muestra la categoría: la suya o, en una transferencia, «Transferencia a Banco» / «Transferencia desde Efectivo»
QString textoCategoria(const Movimiento& m);

// ---- etiquetas: las palabras con # de la descripción («Pasajes #vacaciones #2026»)
QStringList etiquetasDe(const QString& descripcion);     // sin repetir (sin importar mayúsculas), como se escribieron

// ---- períodos de los reportes
// El período con el que se compara: un mes entero -> el mes anterior; un año entero -> el año anterior;
// cualquier otro rango -> la misma cantidad de días justo antes
QPair<QDate, QDate> periodoAnterior(const QDate& desde, const QDate& hasta);
// "octubre 2026" · "2025" · "1/3/2026 al 15/4/2026"
QString nombrePeriodo(const QDate& desde, const QDate& hasta);
// Con qué se compara, para escribir «+12 % que en octubre 2026»: "en octubre 2026" · "en 2025" · "en los 10 días anteriores"
QString textoPeriodoAnterior(const QDate& desde, const QDate& hasta);
// "+12 %" · "−5 %" · "igual"; vacío si antes no hubo nada (no hay contra qué comparar)
QString textoVariacion(Centavos actual, Centavos anterior);
// Signo de la variación para pintarla: 1 si subió, -1 si bajó, 0 igual o sin comparación
int signoVariacion(Centavos actual, Centavos anterior);
