// Ingresos y Gastos para Android (Qt Quick). En la PC también se puede ejecutar, para probarla.
#include "controlador.h"
#include "Traducciones.h"
#include "db.h"
#include "ModeloApps.h"
#include <QDir>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(IngresosGastosPlugin)
Q_IMPORT_QML_PLUGIN(Comun_UiPlugin)   // comun/ui/qml

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    apps::cargarTraduccionesQt();   // botones estándar en español (comun/apps; Android y Windows no traen la traducción)
    QGuiApplication::setOrganizationName(QStringLiteral("ingresos-gastos"));
    QGuiApplication::setApplicationName(QStringLiteral("ingresos-gastos-movil"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Ingresos y Gastos"));
    QGuiApplication::setApplicationVersion(QStringLiteral(IG_VERSION));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Argentina));
    QQuickStyle::setStyle(QStringLiteral("Material"));

#ifdef Q_OS_ANDROID
    const QString ruta = Cuentas::rutaPorDefecto();
#else
    // Probándola en la PC: una base aparte, para no mezclarse con la de la versión de escritorio
    const QStringList args = app.arguments();
    const QString ruta = args.size() > 1 ? args.at(1)
                                         : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/cuentas.db");
#endif
    Cuentas cuentas(ruta);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    if (!cuentas.abierta()) {
        engine.loadData(QStringLiteral("import QtQuick; import QtQuick.Controls; ApplicationWindow { visible: true; width: 400; height: 300; "
                                       "Label { anchors.fill: parent; anchors.margins: 20; wrapMode: Text.WordWrap; text: %1 } }")
                            .arg(QStringLiteral("\"No se pudo abrir la base de datos: ") + QString(cuentas.errorApertura()).replace('"', '\'') + "\"")
                            .toUtf8());
        return app.exec();
    }

    Controlador controlador(cuentas);
    Controlador::setInstancia(&controlador);
    // «Apps y actualizaciones» (comun/apps): versión nueva de esta app y las otras para instalar
    auto *modeloApps = new apps::ModeloApps(QStringLiteral("ingresos-gastos"), &app);   // vive más que el motor de QML
    engine.rootContext()->setContextProperty(QStringLiteral("Apps"), modeloApps);
    engine.load(QUrl(QStringLiteral("qrc:/IngresosGastos/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
