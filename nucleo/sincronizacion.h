#pragma once
// Sincronización PC ↔ celular de Ingresos y Gastos. El protocolo (cifrado) está en comun-qt
// (comun/sinc/sincronizacion_comun.h); acá solo van los datos propios de este programa.
#include "db.h"
#include "sincronizacion_comun.h"

namespace sinc {
constexpr quint16 PUERTO = 45454;
constexpr quint16 PUERTO_BUSQUEDA = 45455;
inline PerfilSinc perfil()
{
    return {QStringLiteral("Ingresos y Gastos"), "IG", QStringLiteral("ig"), PUERTO, PUERTO_BUSQUEDA};
}
}

/** Lado PC: escucha conexiones del celular mientras el programa está abierto. */
class ServidorSinc : public ServidorSincBase
{
    Q_OBJECT
public:
    explicit ServidorSinc(Cuentas& cuentas, QObject* parent = nullptr) : ServidorSincBase(cuentas, sinc::perfil(), parent) {}
};

/** Lado celular: se conecta a la PC para emparejar o sincronizar. */
class ClienteSinc : public ClienteSincBase
{
    Q_OBJECT
public:
    explicit ClienteSinc(Cuentas& cuentas, QObject* parent = nullptr) : ClienteSincBase(cuentas, sinc::perfil(), parent) {}
};
