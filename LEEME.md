# Ingresos y Gastos — C++ / Qt6 (Linux y Android)

Ingresos, gastos y saldo de la cuenta en **pesos argentinos**, con categorías editables, gráficos
e importación/exportación a CSV. Inspirada en ExpenLess! (Android).

- **Linux** (Arch / KDE Plasma): Qt Widgets, pantallas hechas en Qt Designer.
- **Android**: la misma app con pantallas táctiles (Qt Quick), en C++.
- Las dos **se sincronizan** directo, PC ↔ celular, sin nube ni cuentas.

## Linux

### Construir y probar
```bash
sudo pacman -S cmake ninja qt6-base          # una sola vez
cmake -S . -B build -G Ninja
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure    # pruebas
./build/escritorio/ingresos-gastos                                     # ejecutar
```

### Instalar como paquete de Arch
```bash
cd packaging && makepkg -si          # corre también las pruebas; para saltearlas: makepkg -si --nocheck
```
Queda en el menú de aplicaciones como **Ingresos y Gastos** (sección Oficina).
Al abrir la 2.0 por primera vez, la base de la 1.0 se actualiza sola (no se pierde nada).

## Android

### Qué hace falta (una sola vez)
1. Java 17 y Qt Creator:
   ```bash
   sudo pacman -S jdk17-openjdk qtcreator python-pipx
   ```
2. Qt para Android (6.9 o más nuevo), con `aqtinstall` (no pide cuenta). Ver qué versiones hay
   y elegir la última 6.x:
   ```bash
   pipx install aqtinstall
   aqt list-qt linux android                      # ej.: ... 6.10.0
   aqt install-qt linux android 6.10.0 android_arm64_v8a --autodesktop -O ~/Qt
   ```
   (`--autodesktop` baja también el Qt de escritorio de la misma versión, que hace falta para compilar.)
   Otra opción es el instalador de qt.io (pide cuenta): marcar *Qt 6.x → Android*.
3. En Qt Creator: **Editar → Preferencias → Dispositivos → Android**
   - *JDK*: `/usr/lib/jvm/java-17-openjdk`
   - *Android SDK*: una carpeta vacía, por ejemplo `~/Android/Sdk`, y tocar **Configurar SDK**:
     baja solo el SDK, las herramientas y el NDK que corresponden a esa versión de Qt.
   - En **Kits → Versiones de Qt** agregar `~/Qt/6.10.0/android_arm64_v8a/bin/qmake` si no apareció sola.

### Hacer el APK
1. Qt Creator → **Abrir proyecto** → el `CMakeLists.txt` de esta carpeta.
2. Elegir el kit **Android Qt 6.10.0 arm64-v8a**.
3. En el celular: *Opciones de desarrollador → Depuración USB* y conectarlo por cable.
4. **Ejecutar** (▶): compila, instala y abre la app en el celular.

El APK queda en la carpeta de compilación (`find <carpeta-de-compilación> -name "*.apk"`); se puede pasar
al celular y abrirlo para instalar. Para publicarlo o pasarlo a otros conviene firmarlo:
*Proyectos → Compilar → Build Android APK → Firmar paquete*.

Desde la terminal, sin Qt Creator:
```bash
~/Qt/6.10.0/android_arm64_v8a/bin/qt-cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=$HOME/Qt/6.10.0/gcc_64 -DANDROID_SDK_ROOT=$HOME/Android/Sdk \
    -DANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/<versión>
cmake --build build-android --target apk
packaging/firmar-apk.sh build-android   # firma: Android no instala APKs sin firmar
```
Queda `build-android/ingresos-gastos-movil-<versión>.apk`, listo para pasar al celular. La primera vez el
script crea la clave en `~/.android/ingresos-gastos.keystore` (contraseña en `~/.android/ingresos-gastos.clave`):
**guardá una copia de los dos archivos**, porque las actualizaciones tienen que firmarse con la misma clave.

### Probar la app del celular en la PC
No hace falta el celular para ver cómo queda: se compila también para Linux.
```bash
sudo pacman -S qt6-declarative               # Qt Quick
cmake -S . -B build -G Ninja -DIG_MOVIL=ON
cmake --build build
./build/movil/ingresos-gastos-movil          # usa una base aparte, no la de la versión de escritorio
```

## Sincronizar la PC y el celular
1. **PC**: ☰ → **Sincronizar con el celular…** → tildar *Permitir que el celular sincronice* → **Mostrar código**.
2. **Celular**: menú ☰ → **Sincronizar con la PC**. La PC aparece sola en la lista si están en el mismo Wi-Fi
   (si no, escribir la dirección que muestra la PC). Escribir el código de 6 números → **Emparejar**.

Listo: desde ahí se sincroniza **solo** al abrir la app en el celular y unos segundos después de cada cambio,
o a mano con el botón ⟳ de arriba. En la PC tiene que estar abierto Ingresos y Gastos (la opción queda activada).

- **Fuera de casa**: con Tailscale en la PC y en el celular funciona desde cualquier lado. Si el celular no
  encuentra la PC, escribir su dirección de Tailscale (la `100.x.x.x` que muestra la PC).
- **Si se cambia el mismo dato en los dos lados** antes de sincronizar, queda el cambio más nuevo.
  Lo borrado en un lado se borra en el otro. Si en un lado se borra una categoría mientras en el otro se cargaba
  un gasto en ella, la categoría vuelve (el gasto no se pierde).
- **Seguridad**: solo sincronizan los dispositivos emparejados. El código se usa una vez, vence a los 5 minutos y no
  viaja por la red: con él se hace un intercambio de claves (CPace), así que escuchando el Wi-Fi no se puede
  averiguar, y quien quiera adivinarlo tiene 5 intentos. De ahí sale una clave que guardan los dos y nunca viaja.
  Todo lo que se sincroniza va **cifrado** (XChaCha20-Poly1305). Igual no conviene abrir el puerto en el router.
- **Firewall**: Arch no trae uno activado. Si usás `ufw`: `sudo ufw allow 45454/tcp` y `sudo ufw allow 45455/udp`
  (el segundo es para que el celular encuentre la PC solo).
- Un celular emparejado se puede quitar en la PC (misma ventana). En el celular: *Olvidar esta PC*.
- Importar el mismo CSV (por ejemplo el de ExpenLess) en los dos lados no duplica nada al sincronizar.

## Qué hace
| Pedido | Cómo quedó |
|---|---|
| Ingresos y gastos | PC: botones **+ Ingreso** / **− Gasto** (o Ctrl+I / Ctrl+G), doble clic o Enter para editar, Supr para borrar, clic derecho → *Repetir con fecha de hoy*. Celular: botones abajo, tocar un movimiento para editarlo, mantener apretado para *Repetir* o *Eliminar* |
| Pesos argentinos | Todo se muestra como `$ 1.234,56`. Al escribir el monto vale `15430,50`, `15.430,50` o `15430`. Internamente se guardan **centavos enteros**: las sumas dan exactas, sin errores de redondeo |
| Categorías editables | ☰ → **Categorías**: agregar, renombrar, cambiar el color y eliminar. Se muestran siempre **por orden alfabético** (sin distinguir mayúsculas ni tildes; la ñ va después de la n): una categoría nueva o renombrada queda sola en su lugar. Al cargar un movimiento nuevo arranca elegida la primera que se creó (Supermercado / Sueldo), no la primera de la lista. Vienen cargadas: Supermercado, Transporte, Vestimenta, Internet, Teléfono, Servicios, Salud, Gastos Varios / Sueldo, Ingresos Varios. También se crea una nueva con **+** al cargar un movimiento. Para borrar una categoría con movimientos, pregunta a cuál pasarlos (nunca se pierden) |
| Saldo de la cuenta | Arriba: saldo inicial + todos los ingresos − todos los gastos hasta hoy. El saldo inicial se pone en ☰ → **Saldo inicial** |
| Totales por mes y por año | Tarjetas de ingresos, gastos y resultado del período elegido; pestaña **Totales** por mes (con el total del año), por categoría (con %) y por año |
| Gráfico de barras | Ingresos y gastos de cada mes del año (o de cada año, en la vista *Todo*). PC: al pasar el mouse muestra los números y un clic abre el mes. Celular: tocar un mes muestra sus números y *Ver mes* |
| Gráficos de torta | Gastos e ingresos por categoría, con leyenda: nombre, monto y %. Siempre las 7 más grandes; el resto en «Otros» |
| Exportar / importar CSV | ☰ → **Exportar CSV** (el período que se ve) e **Importar CSV**. Antes de guardar muestra cuántos se agregan, cuántos ya estaban (**no duplica**) y qué categorías nuevas se crean. En el celular se elige el archivo con el selector de Android |
| Varias cuentas | ☰ → **Cuentas** (celular: Opciones → Cuentas): efectivo, banco, Mercado Pago… cada una con su saldo inicial. Cada movimiento va en una cuenta; con más de una, arriba se elige cuál mirar (o **Todas**): movimientos, tarjetas, gráficos, totales y saldo se ven de esa cuenta. La que ya estaba se llama **Principal** (se puede renombrar, no eliminar) y se queda con el saldo inicial de antes |
| Presupuestos | ☰ → **Categorías** → **Presupuesto…** (celular: tocar la categoría → Presupuesto por mes): cuánto pensás gastar por mes. Arriba aparece «⚠ Te pasaste del presupuesto…» (y en la PC también «Cerca del presupuesto» desde el 80 %) |
| Movimientos fijos | ☰ → **Movimientos fijos** (sueldo, alquiler, internet…): se cargan **solos** el día elegido de cada mes, al abrir el programa. También desde un movimiento: clic derecho (celular: mantener apretado) → **Repetir todos los meses…**. Si se borra uno de los que se cargaron, no vuelve; si la PC y el celular lo cargan los dos, al sincronizar queda uno solo |
| Copia de seguridad | ☰ → **Copia de seguridad**: guarda la base completa donde quieras |

Arriba se elige el período: **Mes**, **Año** o **Todo** (PC: ◀ ▶, Alt+← / Alt+→ y **Hoy**; celular: ‹ › y
tocar el nombre del período). El buscador ignora mayúsculas y tildes y busca también por monto.
Los colores de los gráficos se adaptan al tema claro u oscuro (de Plasma o del celular).

### Importar desde ExpenLess!
En ExpenLess: exportar el reporte CSV (`expenlessReporte_AAAAMMDD.csv`). En el celular se puede importar
directo (☰ → Importar CSV); en la PC, después de pasarlo a la compu. Se reconoce solo.

- Cada movimiento queda con su fecha, ingreso/gasto, monto y **categoría** (se crean las que falten).
- La **subcategoría** de ExpenLess (Gas, Agua, Medicamentos…) va al principio de la descripción:
  `Gas`, `Medicamentos — Vitamina D, ...`. Así se puede buscar por subcategoría. Las «… General» se omiten.
- «Sin categoría» va a *Gastos Varios* / *Ingresos Varios*.
- Los movimientos en otra moneda (US$, etc.) **no** se mezclan con los pesos: se avisan y se saltean.
- Se puede importar un reporte más nuevo cuando quieras: lo que ya estaba no se duplica.

### Formato del CSV
```
Fecha;Tipo;Categoría;Monto;Descripción;Cuenta
01/11/2026;Gasto;Supermercado;15430,50;Compra semanal;Principal
01/11/2026;Ingreso;Sueldo;800000,00;;Banco
```
Al importar también acepta `,` o tabulación como separador, columnas en otro orden y con otros nombres
(`Importe`, `Detalle`, `Concepto`, `Rubro`, `Date`, `Amount`…), fechas `2026-11-01` o `1/11/26`, y archivos
guardados por Excel en Windows. Si no hay columna *Tipo*, un monto negativo se toma como gasto.

## Versiones
**2.1.0** — Varias cuentas, presupuestos por categoría y movimientos fijos (ver la tabla de arriba). La sincronización
ahora va **cifrada** y el emparejamiento no deja adivinar el código escuchando la red: **hay que emparejar de nuevo
una vez** el celular con la PC (Olvidar esta PC y emparejar con un código nuevo) y actualizar **los dos**. El CSV
tiene una columna más, **Cuenta** (al importar crea las que falten). La base se actualiza sola.

**2.0.1** — Las categorías se muestran siempre por orden alfabético (en la PC y en el celular): una categoría
nueva o renombrada queda en su lugar. Al cargar un movimiento nuevo sigue arrancando elegida la primera que
se creó, no la primera de la lista.

**2.0.0** — App para Android (C++ / Qt Quick) con las mismas funciones; sincronización directa PC ↔ celular
(ventana nueva en la PC: ☰ → *Sincronizar con el celular…*). La base de datos ahora guarda lo necesario para
sincronizar (cada dato con su identificador, fecha de cambio y marca de borrado); la de la 1.0 se actualiza sola.
Los gráficos de la PC y del celular salen del mismo código.

**1.0.1** — Importa el reporte de ExpenLess (títulos en inglés, comas y Enter dentro de las descripciones);
títulos de columnas en inglés en cualquier CSV; después de importar salta al mes importado; las tortas siempre
muestran las 7 categorías más grandes.

## Dónde está cada cosa
| Carpeta / archivo | Qué hace |
|---|---|
| `nucleo/` | Lo que comparten la PC y el celular |
| `nucleo/db.cpp` | SQLite: categorías, movimientos, saldo, totales, importación, migración y lo que se intercambia al sincronizar |
| `nucleo/sincronizacion.h` | Puertos y nombre para la sincronización; el protocolo (cifrado) está en `comun/sinc` |
| `comun/` | Código compartido con otros programas ([comun-qt](https://github.com/Sebas-Git-Tail/comun-qt), traído con `git subtree`): **no editar acá**, sino en comun-qt |
| `nucleo/dibujo.cpp` | El dibujo de los gráficos de barras y de torta |
| `nucleo/csv.cpp`, `dinero.cpp` | CSV (y ExpenLess); formato de pesos, montos y fechas |
| `escritorio/*.ui` | Las pantallas de la PC: **se editan en Qt Designer** y se convierten en código al compilar |
| `escritorio/ventanaprincipal.cpp` | Ventana principal de la PC |
| `escritorio/dialogosincronizacion.cpp` | Ventana *Sincronizar con el celular* |
| `escritorio/dialogocuentas.cpp`, `dialogofijos.cpp` | Ventanas *Cuentas* y *Movimientos fijos* |
| `movil/qml/*.qml` | Las pantallas del celular (se pueden editar con el editor visual de Qt Creator) |
| `movil/controlador.cpp` | Lo que usan las pantallas del celular (en QML se llama `App`) |
| `movil/graficosquick.cpp` | Los gráficos para Qt Quick (mismo dibujo que en la PC) |
| `movil/android/res/` | Íconos de la app de Android |
| `tests/` | 7 suites de pruebas (QtTest); `tst_sinc` prueba la sincronización y `tst_movil` las pantallas del celular |

Si renombrás un widget en Designer, hay que cambiarlo también en el `.cpp` (el compilador avisa).

## Se guarda en
- **PC**: datos en `~/.local/share/ingresos-gastos/cuentas.db`; ventana y opciones en
  `~/.config/ingresos-gastos/ingresos-gastos.conf`. Para usar otra base: `ingresos-gastos /ruta/otra.db`
- **Celular**: dentro de la app (se borra si se desinstala: sincronizá o hacé una copia de seguridad antes)
