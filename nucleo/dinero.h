#pragma once
#include "modelos.h"
#include <QStringList>

// ---- pesos argentinos
QString formatearPesos(Centavos c);             // 123456 -> "$ 1.234,56" | -5000 -> "-$ 50,00"
QString formatearPesosConSigno(Centavos c);     // 123456 -> "+$ 1.234,56" (0 -> "$ 0,00")
QString formatearPesosCorto(Centavos c);        // para ejes: "$ 0" · "$ 800" · "$ 15 mil" · "$ 1,5 M"
QString formatearNumero(Centavos c);            // para CSV: "1234,56" (sin $ ni separador de miles)

// "1234" · "1.234" · "1.234,56" · "1234,5" · "$ 1.234,56" · "1234.56" -> centavos.
// conSigno: acepta un "-" adelante (para importar); si no, un negativo es inválido.
bool parsearPesos(const QString& texto, Centavos* centavos, bool conSigno = false);

// ---- fechas
QString nombreMes(int mes);                     // 1 -> "Enero"
QString nombreMesCorto(int mes);                // 1 -> "Ene"
bool parsearFecha(const QString& texto, QDate* fecha);   // dd/MM/yyyy · d/M/yy · yyyy-MM-dd · dd-MM-yyyy

// ---- búsqueda
QString normalizar(const QString& texto);       // sin mayúsculas ni tildes: "Teléfono" -> "telefono"
