#!/usr/bin/env bash
# Firma el APK de release (Android no instala APKs sin firmar).
# Uso: packaging/firmar-apk.sh [carpeta-de-compilación]   (por defecto: build-android)
# La primera vez crea la clave en ~/.android/ingresos-gastos.keystore.
# ¡Guardá una copia de esa clave! Sin ella no se pueden instalar actualizaciones encima.
set -euo pipefail
BUILD=${1:-build-android}
KS=~/.android/ingresos-gastos.keystore
PASSF=~/.android/ingresos-gastos.clave
BT=$(ls -d "${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"/build-tools/*/ | sort -V | tail -1)

if [ ! -f "$KS" ]; then
    (umask 077; head -c 18 /dev/urandom | base64 | tr -d '/+=' > "$PASSF")
    keytool -genkeypair -keystore "$KS" -alias ingresosgastos -keyalg RSA -keysize 2048 \
        -validity 10000 -storepass "$(cat "$PASSF")" -keypass "$(cat "$PASSF")" -dname "CN=Ingresos y Gastos"
fi

IN=$(find "$BUILD" -name '*-release-unsigned.apk' -print -quit)
[ -n "$IN" ] || { echo "No encontré el APK sin firmar en $BUILD (¿compilaste con --target apk?)"; exit 1; }
# La versión se lee del APK mismo y tiene que ser la del proyecto: si la compilación falló, en
# build-android queda el APK anterior y antes se firmaba igual con el nombre de la versión nueva
VER=$("$BT/aapt2" dump badging "$IN" 2>/dev/null | grep -oP "versionName='\K[^']+" || true)
PROYECTO=$(grep -oP 'project\(ingresos-gastos VERSION \K[0-9.]+' "$(dirname "$(realpath "$0")")/../CMakeLists.txt")
[ "$VER" = "$PROYECTO" ] || { echo "El APK de $BUILD es la versión «$VER», no la $PROYECTO: ¿falló la compilación?"; exit 1; }
SALIDA="$(dirname "$(realpath "$0")")/../Salida"   # lo compilado queda en Salida/
mkdir -p "$SALIDA"
OUT="$SALIDA/ingresos-gastos-movil-$VER.apk"
# Las versiones anteriores pasan a ~/Repo/Legacy/ingresos-gastos
for viejo in "$SALIDA"/ingresos-gastos-movil-*.apk; do
    if [ -e "$viejo" ] && [ "$viejo" != "$OUT" ]; then mkdir -p ~/Repo/Legacy/ingresos-gastos && mv "$viejo" ~/Repo/Legacy/ingresos-gastos/; fi
done
TMP=$(mktemp --suffix=.apk)
"$BT/zipalign" -f -p 4 "$IN" "$TMP"
"$BT/apksigner" sign --ks "$KS" --ks-key-alias ingresosgastos --ks-pass file:"$PASSF" --out "$OUT" "$TMP" 2>/dev/null
rm -f "$TMP" "$OUT.idsig"
"$BT/apksigner" verify "$OUT" 2>/dev/null && echo "Listo: $OUT"
