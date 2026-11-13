#pragma once
#include "modelos.h"
#include <QByteArray>
#include <QStringList>

/**
 * Exportar e importar movimientos en CSV.
 *
 * Se exporta con «;» como separador, montos con coma decimal y fechas dd/mm/aaaa, que es como
 * Excel y LibreOffice en español abren un CSV sin preguntar nada:
 *   Fecha;Tipo;Categoría;Monto;Descripción
 *   01/11/2026;Gasto;Supermercado;15430,50;Compra semanal
 *
 * Al importar se aceptan también «,» o tabulación como separador, columnas en otro orden y con
 * otros nombres (Importe, Detalle, Concepto...). Si no hay columna Tipo, un monto negativo es un gasto.
 */

struct FilaImportada {
    QDate fecha;
    Tipo tipo = Tipo::Gasto;
    QString categoria;          // vacío = sin categoría (se usa «Gastos Varios» / «Ingresos Varios»)
    Centavos monto = 0;         // siempre positivo
    QString descripcion;
    int linea = 0;              // línea del archivo, para los avisos
};

struct LecturaCsv {
    bool ok = false;
    QString error;
    QList<FilaImportada> filas;
    QStringList avisos;         // renglones que no se pudieron leer
};

QString movimientosACsv(const QList<Movimiento>& movimientos);
QByteArray csvParaGuardar(const QString& csv);           // UTF-8 con BOM (para que Excel respete las tildes)

LecturaCsv leerCsv(const QString& texto);
QString decodificarArchivo(const QByteArray& datos);     // UTF-8 o, si no lo es, Windows-1252 (Excel en Windows)

// Expuesta para las pruebas
QList<QStringList> partirCsv(const QString& texto, QChar separador);
