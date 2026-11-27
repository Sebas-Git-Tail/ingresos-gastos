#pragma once
#include <QDate>
#include <QDialog>
#include <memory>

namespace Ui { class DialogoFechas; }

/**
 * Elegir fechas: qué exportar (lo que se ve, todo o entre dos fechas) o qué rango mirar («Entre fechas…»).
 */
class DialogoFechas : public QDialog
{
    Q_OBJECT
public:
    enum class Modo { Exportar, Rango };
    enum Alcance { Periodo = 0, Todo = 1, EntreFechas = 2 };

    // textoPeriodo: cómo se llama lo que se ve («Marzo 2026»); desde/hasta: las fechas de entrada
    DialogoFechas(Modo modo, const QString& textoPeriodo, const QDate& desde, const QDate& hasta, QWidget* parent = nullptr);
    ~DialogoFechas() override;

    int alcance() const;
    QDate desde() const;
    QDate hasta() const;
    void elegir(int alcance, const QDate& desde = {}, const QDate& hasta = {});   // como si se tocara (lo usan las pruebas)

public slots:
    void accept() override;

private:
    void actualizar();

    std::unique_ptr<Ui::DialogoFechas> ui;
    Modo modo_;
};
