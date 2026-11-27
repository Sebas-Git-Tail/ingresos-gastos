# Novedades de Ingresos y Gastos

Lo que cambió en cada versión (la PC y el celular). La app lo muestra la primera vez que se abre una versión
nueva y siempre desde «Novedades» en el menú. Los cambios marcados [Sebastián] solo aparecen en sus equipos.

## 2.8.0 (26/11/2026)
- Transferencias entre cuentas: si tenés más de una cuenta (efectivo, banco, Mercado Pago…), con «Transferencia»
  (en el celular, «Transferir») pasás plata de una a otra. No cuenta como ingreso ni como gasto: solo cambia lo que
  hay en cada cuenta. Hay que actualizar la PC y el celular: con una versión vieja se ven como una salida y una
  entrada en «Transferencia entre cuentas».
- Celular: al borrar un movimiento ya no pregunta; aparece «Deshacer» por unos segundos, como en la PC.
- Celular: avisa «Cerca del presupuesto» cuando llegás al 80 %, como en la PC.
- Las tarjetas comparan con el mes anterior (o el año, o los mismos días): «+12 % que en octubre». En Totales, cada
  categoría dice cuánto subió o bajó.
- «Entre fechas…» en el período de arriba: mirás lo que quieras, por ejemplo del 1 al 15.
- Exportar CSV: además de lo que se ve, «Todo» o entre dos fechas.
- Copia automática: todos los días se guarda sola una copia de los datos (quedan las de los últimos 7 días).
- Etiquetas: escribí palabras con # en la descripción (por ejemplo «Pasajes #vacaciones»). Se pueden buscar y en
  Totales ves cuánto gastaste con cada una.
- El buscador entiende montos: «1000-5000», «>20000» o «super >10000».
- [Sebastián] Las copias diarias van en `respaldos/` al lado de cuentas.db (PC: ~/.local/share/ingresos-gastos/respaldos).
  Pruebas nuevas: ventana de sincronización y buscador por monto en tst_ventana, transferencias en tst_db/tst_sinc/tst_movil.

## 2.7.1 (25/11/2026)
- Celular: ahora necesita Android 11 o más nuevo, como las otras apps. No cambia nada más.

## 2.7.0 (21/11/2026)
- Novedades: la primera vez que abrís una versión nueva te cuenta qué cambió. Siempre están en el menú, en
  «Novedades».
- Si la app se cierra por una falla, manda un informe al servidor (sin tus montos ni tus movimientos) para que
  se pueda arreglar. También lo podés mandar a mano desde Novedades, con un comentario.

## 2.6.0 (18/11/2026)
- Modo claro y oscuro con un botón (se recuerda).

## 2.5.0 (18/11/2026)
- Los montos se ven en verde (ingresos) y rojo (gastos).

## 2.4.0 (18/11/2026)
- PC: la ventana se vuelve a abrir del mismo tamaño. Celular: puntito en las 3 rayitas cuando hay una versión
  nueva.

## 2.3.0 (17/11/2026)
- Interfaz nueva, igual en todas las apps.
- Al borrar un movimiento aparece «Deshacer» por unos segundos.
