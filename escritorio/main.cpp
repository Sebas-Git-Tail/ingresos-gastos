// Ingresos y Gastos: punto de entrada.
#include "db.h"
#include "ventanaprincipal.h"
#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ingresos-gastos"));
    QApplication::setApplicationName(QStringLiteral("ingresos-gastos"));
    QApplication::setApplicationDisplayName(QStringLiteral("Ingresos y Gastos"));
    QApplication::setApplicationVersion(QStringLiteral(IG_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("ingresos-gastos"));   // ícono correcto en la barra de Plasma
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("ingresos-gastos")));

    // Botones estándar de Qt (Sí, No, Cancelar...) en español
    QTranslator qtbase;
    if (qtbase.load(QLocale(QStringLiteral("es_AR")), QStringLiteral("qtbase"), QStringLiteral("_"),
                    QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtbase);
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Argentina));

    // Se puede abrir otra base pasándola como argumento: ingresos-gastos /ruta/otra.db
    const QStringList args = app.arguments();
    const QString ruta = args.size() > 1 ? args.at(1) : Cuentas::rutaPorDefecto();
    Cuentas cuentas(ruta);
    if (!cuentas.abierta()) {
        QMessageBox::critical(nullptr, QStringLiteral("Ingresos y Gastos"),
                              QStringLiteral("No se pudo abrir la base de datos:\n%1\n\n%2").arg(ruta, cuentas.errorApertura()));
        return 1;
    }

    VentanaPrincipal ventana(cuentas);
    ventana.show();
    return app.exec();
}
