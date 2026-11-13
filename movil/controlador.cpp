#include "controlador.h"
#include "csv.h"
#include "dibujo.h"
#include "dinero.h"
#include "sincronizacion.h"

#include <QFile>
#include <QJSEngine>
#include <QQmlEngine>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace {

Controlador* instancia = nullptr;

QString ruta(const QUrl& u)                     // en Android los archivos elegidos son content://…
{
    return u.isLocalFile() ? u.toLocalFile() : u.toString();
}

QString pct(Centavos parte, Centavos total)
{
    if (total <= 0) return QString();
    return QString::number(100.0 * double(parte) / double(total), 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')) + QStringLiteral(" %");
}

bool leerArchivo(const QUrl& url, LecturaCsv* lectura, QString* error)
{
    QFile f(ruta(url));
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("No se pudo abrir el archivo: %1").arg(f.errorString());
        return false;
    }
    *lectura = leerCsv(decodificarArchivo(f.readAll()));
    if (!lectura->ok) {
        *error = lectura->error;
        return false;
    }
    return true;
}

QString escribirArchivo(const QUrl& url, const QByteArray& datos)
{
    if (url.isLocalFile()) {
        QSaveFile f(url.toLocalFile());
        if (!f.open(QIODevice::WriteOnly)) return f.errorString();
        f.write(datos);
        return f.commit() ? QString() : f.errorString();
    }
    QFile f(url.toString());                    // content:// (Android): se escribe directo
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return f.errorString();
    if (f.write(datos) != datos.size()) return f.errorString();
    return QString();
}

QVariantMap filaTotal(const QString& nombre, const TotalPeriodo& t, bool marcado, bool esTotal)
{
    return QVariantMap{{"nombre", nombre}, {"ingresos", formatearPesos(t.ingresos)}, {"gastos", formatearPesos(t.gastos)},
                       {"saldo", formatearPesosConSigno(t.saldo())}, {"marcado", marcado}, {"total", esTotal}};
}

} // namespace

Controlador::Controlador(Cuentas& cuentas, QObject* parent)
    : QObject(parent), cuentas_(cuentas), modelo_(this)
{
    const QDate h = QDate::currentDate();
    periodo_ = QDate(h.year(), h.month(), 1);

    cliente_ = new ClienteSinc(cuentas_, this);
    connect(cliente_, &ClienteSinc::estado, this, [this](const QString& t) {
        estadoSinc_ = t;
        emit sincCambio();
    });
    connect(cliente_, &ClienteSinc::terminado, this, [this](bool ok, const QString& mensaje, int recibidas, int enviadas) {
        sincronizando_ = false;
        estadoSinc_ = mensaje;
        // Siempre con una sola PC: si se emparejó con otra, la anterior se olvida
        const QList<Par> pares = cuentas_.pares();
        if (ok && pares.size() > 1) {
            Par masNuevo = pares.first();
            for (const Par& p : pares) if (p.ultimaSinc > masNuevo.ultimaSinc) masNuevo = p;
            for (const Par& p : pares) if (p.dispositivo != masNuevo.dispositivo) cuentas_.eliminarPar(p.dispositivo);
        }
        if (ok && recibidas > 0) refrescar();
        // En silencio solo se avisa si trajo algo; si falla en silencio (fuera de casa) no se molesta
        if (!sincEnSilencio_ || (ok && recibidas > 0)) emit aviso(mensaje);
        Q_UNUSED(enviadas);
        sincEnSilencio_ = false;
        emit sincTerminada(ok, mensaje);
        emit sincCambio();
    });
    connect(cliente_, &ClienteSinc::encontrado, this,
            [this](const QString& nombre, const QString& host, quint16 puerto, const QString&) { emit pcEncontrada(nombre, host, puerto); });
    connect(cliente_, &ClienteSinc::busquedaTerminada, this, &Controlador::busquedaTerminada);

    temporizadorSinc_.setSingleShot(true);
    connect(&temporizadorSinc_, &QTimer::timeout, this, [this] {
        if (sincronizando_) { temporizadorSinc_.start(4000); return; }
        if (!pcEmparejada()) return;
        sincEnSilencio_ = true;
        sincronizar();
    });
    refrescar();
}

Controlador::~Controlador()
{
    if (instancia == this) instancia = nullptr;
}

Controlador* Controlador::create(QQmlEngine*, QJSEngine*)
{
    QJSEngine::setObjectOwnership(instancia, QJSEngine::CppOwnership);   // la crea y la borra main()
    return instancia;
}

void Controlador::setInstancia(Controlador* c)
{
    instancia = c;
}

// ---------------------------------------------------------------- período

QPair<QDate, QDate> Controlador::rango() const
{
    if (vista_ == 0) return {periodo_, periodo_.addMonths(1).addDays(-1)};
    if (vista_ == 1) return {QDate(periodo_.year(), 1, 1), QDate(periodo_.year(), 12, 31)};
    return {QDate(), QDate()};
}

QString Controlador::textoPeriodoDe(int vista, const QDate& p) const
{
    if (vista == 0) return QStringLiteral("%1 %2").arg(nombreMes(p.month())).arg(p.year());
    if (vista == 1) return QString::number(p.year());
    return QStringLiteral("Todo");
}

QString Controlador::periodoTexto() const
{
    return textoPeriodoDe(vista_, periodo_);
}

void Controlador::setVista(int v)
{
    v = std::clamp(v, 0, 2);
    if (v == vista_) return;
    vista_ = v;
    refrescar();
}

void Controlador::setPeriodo(const QDate& f)
{
    if (!f.isValid()) return;
    periodo_ = QDate(f.year(), f.month(), 1);
    refrescar();
}

void Controlador::anterior()
{
    if (vista_ == 0) setPeriodo(periodo_.addMonths(-1));
    else if (vista_ == 1) setPeriodo(periodo_.addYears(-1));
}

void Controlador::siguiente()
{
    if (vista_ == 0) setPeriodo(periodo_.addMonths(1));
    else if (vista_ == 1) setPeriodo(periodo_.addYears(1));
}

void Controlador::hoy()
{
    setPeriodo(QDate::currentDate());
}

void Controlador::irAPeriodo(int indice)
{
    if (indice < 0) return;
    if (vista_ == 2) {
        if (indice >= aniosBarras_.size()) return;
        periodo_ = QDate(aniosBarras_[indice], 1, 1);
        vista_ = 1;
    } else {
        if (indice > 11) return;
        periodo_ = QDate(periodo_.year(), indice + 1, 1);
        vista_ = 0;
    }
    refrescar();
}

void Controlador::setBusqueda(const QString& b)
{
    if (b == busqueda_) return;
    busqueda_ = b;
    cargarMovimientos();
    emit cambio();
}

void Controlador::setFiltroTipo(int f)
{
    if (f == filtroTipo_) return;
    filtroTipo_ = std::clamp(f, 0, 2);
    cargarMovimientos();
    emit cambio();
}

void Controlador::setOscuro(bool o)
{
    if (o == oscuro_) return;
    oscuro_ = o;
    emit oscuroChanged();
    refrescar();                                // los colores de las categorías dependen del tema
}

QVariantList Controlador::colores() const
{
    const TemaGrafico t = TemaGrafico::crear(oscuro_);
    QVariantList l;
    for (const QColor& c : t.serie) l << c;
    return l;
}

// ---------------------------------------------------------------- carga de datos

void Controlador::refrescar()
{
    cargarMovimientos();
    cargarTarjetas();
    cargarGraficos();
    cargarTotales();
    emit cambio();
}

void Controlador::cargarMovimientos()
{
    const auto [desde, hasta] = rango();
    const QList<Movimiento> todos = cuentas_.movimientos(desde, hasta);
    const QStringList palabras = normalizar(busqueda_).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<Movimiento> lista;
    for (const Movimiento& m : todos) {
        if (filtroTipo_ == 1 && m.tipo != Tipo::Ingreso) continue;
        if (filtroTipo_ == 2 && m.tipo != Tipo::Gasto) continue;
        if (!palabras.isEmpty()) {
            const QString texto = normalizar(QStringList{m.categoria, m.descripcion, textoTipo(m.tipo), formatearPesos(m.monto),
                                                         formatearNumero(m.monto), m.fecha.toString(QStringLiteral("dd/MM/yyyy"))}
                                                 .join(QLatin1Char(' ')));
            if (!std::all_of(palabras.begin(), palabras.end(), [&](const QString& p) { return texto.contains(p); })) continue;
        }
        lista << m;
    }
    modelo_.cargar(lista, TemaGrafico::crear(oscuro_).serie);
    cantidadTexto_ = lista.size() == todos.size()
                         ? (todos.size() == 1 ? QStringLiteral("1 movimiento") : QStringLiteral("%1 movimientos").arg(todos.size()))
                         : QStringLiteral("%1 de %2 movimientos").arg(lista.size()).arg(todos.size());
}

void Controlador::cargarTarjetas()
{
    const auto [desde, hasta] = rango();
    const TotalPeriodo t = cuentas_.totales(desde, hasta);
    ingresosTexto_ = formatearPesos(t.ingresos);
    gastosTexto_ = formatearPesos(t.gastos);
    resultadoTexto_ = formatearPesosConSigno(t.saldo());
    const QDate h = QDate::currentDate();
    saldoTexto_ = formatearPesos(cuentas_.saldo(h));
    saldoAyuda_ = QStringLiteral("Al %1: saldo inicial %2 + ingresos − gastos").arg(h.toString(QStringLiteral("dd/MM/yyyy")),
                                                                                  formatearPesos(cuentas_.saldoInicial()));
}

void Controlador::cargarGraficos()
{
    auto lista = [](const QVector<Centavos>& v) {
        QVariantList l;
        for (Centavos c : v) l << double(c);
        return l;
    };
    QStringList etiquetas, detalles;
    QVector<Centavos> ing, gas;
    if (vista_ == 2) {
        aniosBarras_.clear();
        for (const auto& [anio, t] : cuentas_.totalesPorAnio()) {
            aniosBarras_ << anio;
            etiquetas << QString::number(anio);
            ing << t.ingresos;
            gas << t.gastos;
        }
        detalles = etiquetas;
        barras_ = QVariantMap{{"titulo", QStringLiteral("Por año")}, {"marcado", int(aniosBarras_.indexOf(periodo_.year()))},
                              {"vacio", QStringLiteral("Todavía no hay movimientos")}};
    } else {
        const int anio = periodo_.year();
        const QVector<TotalPeriodo> meses = cuentas_.totalesPorMes(anio);
        for (int m = 1; m <= 12; ++m) {
            etiquetas << nombreMesCorto(m).left(1);          // en el celular entra una letra: E F M A M J...
            detalles << QStringLiteral("%1 %2").arg(nombreMes(m)).arg(anio);
            ing << meses.value(m - 1).ingresos;
            gas << meses.value(m - 1).gastos;
        }
        barras_ = QVariantMap{{"titulo", QString::number(anio)}, {"marcado", vista_ == 0 ? periodo_.month() - 1 : -1},
                              {"vacio", QStringLiteral("Sin movimientos en %1").arg(anio)}};
    }
    barras_.insert(QStringLiteral("etiquetas"), etiquetas);
    barras_.insert(QStringLiteral("detalles"), detalles);
    barras_.insert(QStringLiteral("ingresos"), lista(ing));
    barras_.insert(QStringLiteral("gastos"), lista(gas));

    const auto [desde, hasta] = rango();
    auto torta = [&](Tipo tipo, const QString& titulo, const QString& vacio) {
        QVariantList totales;
        for (const TotalCategoria& tc : cuentas_.totalesPorCategoria(desde, hasta, tipo))
            totales << QVariantMap{{"nombre", tc.nombre}, {"color", tc.color}, {"total", double(tc.total)}};
        return QVariantMap{{"titulo", titulo}, {"vacio", vacio}, {"totales", totales}};
    };
    tortaGastos_ = torta(Tipo::Gasto, QStringLiteral("Gastos por categoría"), QStringLiteral("Sin gastos en este período"));
    tortaIngresos_ = torta(Tipo::Ingreso, QStringLiteral("Ingresos por categoría"), QStringLiteral("Sin ingresos en este período"));
}

void Controlador::cargarTotales()
{
    const TemaGrafico tema = TemaGrafico::crear(oscuro_);
    const int anio = periodo_.year();
    totalesMeses_.clear();
    TotalPeriodo totalAnio;
    const QVector<TotalPeriodo> meses = cuentas_.totalesPorMes(anio);
    for (int m = 0; m < 12; ++m) {
        const TotalPeriodo t = meses.value(m);
        totalAnio.ingresos += t.ingresos;
        totalAnio.gastos += t.gastos;
        totalesMeses_ << filaTotal(nombreMes(m + 1), t, vista_ == 0 && m == periodo_.month() - 1, false);
    }
    totalesMeses_ << filaTotal(QStringLiteral("Total %1").arg(anio), totalAnio, false, true);

    totalesCategorias_.clear();
    const auto [desde, hasta] = rango();
    for (Tipo tipo : {Tipo::Gasto, Tipo::Ingreso}) {
        const QList<TotalCategoria> lista = cuentas_.totalesPorCategoria(desde, hasta, tipo);
        Centavos total = 0;
        for (const TotalCategoria& tc : lista) total += tc.total;
        totalesCategorias_ << QVariantMap{{"clase", "titulo"}, {"nombre", tipo == Tipo::Gasto ? QStringLiteral("Gastos") : QStringLiteral("Ingresos")},
                                          {"total", formatearPesos(total)}, {"pct", QString()}, {"color", QColor(Qt::transparent)}};
        for (const TotalCategoria& tc : lista) {
            if (tc.total <= 0) continue;
            totalesCategorias_ << QVariantMap{{"clase", "cat"}, {"nombre", tc.nombre}, {"total", formatearPesos(tc.total)},
                                              {"pct", pct(tc.total, total)}, {"color", tema.serie[std::clamp(tc.color, 0, 7)]}};
        }
    }

    totalesAnios_.clear();
    TotalPeriodo total;
    for (const auto& [a, t] : cuentas_.totalesPorAnio()) {
        total.ingresos += t.ingresos;
        total.gastos += t.gastos;
        totalesAnios_ << filaTotal(QString::number(a), t, vista_ != 2 && a == anio, false);
    }
    totalesAnios_ << filaTotal(QStringLiteral("Total"), total, false, true);
}

// ---------------------------------------------------------------- movimientos

QVariantMap Controlador::nuevoMovimiento(int tipo) const
{
    // Fecha inicial: hoy si cae en el período que se está mirando; si no, el primer día del período
    const auto [desde, hasta] = rango();
    const QDate h = QDate::currentDate();
    const QDate fecha = (!desde.isValid() || (h >= desde && h <= hasta)) ? h : desde;
    return QVariantMap{{"id", 0}, {"tipo", tipo}, {"fechaIso", fecha.toString(Qt::ISODate)}, {"fechaTexto", fechaTexto(fecha.toString(Qt::ISODate))},
                       {"categoriaId", categoriaInicial(tipo)}, {"monto", QString()}, {"descripcion", QString()}};
}

QVariantMap Controlador::movimiento(qint64 id) const
{
    const auto m = cuentas_.movimiento(id);
    if (!m) return {};
    return QVariantMap{{"id", m->id}, {"tipo", m->tipo == Tipo::Ingreso ? 0 : 1}, {"fechaIso", m->fecha.toString(Qt::ISODate)},
                       {"fechaTexto", fechaTexto(m->fecha.toString(Qt::ISODate))}, {"categoriaId", m->categoriaId},
                       {"monto", formatearPesos(m->monto).mid(2)}, {"descripcion", m->descripcion}};
}

QVariantMap Controlador::repetir(qint64 id) const
{
    QVariantMap m = movimiento(id);
    if (m.isEmpty()) return m;
    m.insert(QStringLiteral("id"), 0);
    m.insert(QStringLiteral("fechaIso"), hoyIso());
    m.insert(QStringLiteral("fechaTexto"), fechaTexto(hoyIso()));
    return m;
}

QString Controlador::guardarMovimiento(const QVariantMap& datos)
{
    Centavos monto = 0;
    if (!parsearPesos(datos.value(QStringLiteral("monto")).toString(), &monto) || monto <= 0)
        return QStringLiteral("Escribí un monto mayor que cero, por ejemplo 15.430,50");
    Movimiento m;
    m.id = datos.value(QStringLiteral("id")).toLongLong();
    m.tipo = datos.value(QStringLiteral("tipo")).toInt() == 0 ? Tipo::Ingreso : Tipo::Gasto;
    m.fecha = QDate::fromString(datos.value(QStringLiteral("fechaIso")).toString(), Qt::ISODate);
    m.categoriaId = datos.value(QStringLiteral("categoriaId")).toLongLong();
    m.monto = monto;
    m.descripcion = datos.value(QStringLiteral("descripcion")).toString().trimmed();
    const auto cat = cuentas_.categoria(m.categoriaId);
    if (!cat || cat->tipo != m.tipo) return QStringLiteral("Elegí una categoría.");
    const Resultado r = m.id ? cuentas_.actualizarMovimiento(m) : cuentas_.crearMovimiento(m);
    if (!r.ok) return r.error;

    // Que se vea lo que se acaba de guardar (si quedó en otro mes)
    const auto [desde, hasta] = rango();
    if (desde.isValid() && (m.fecha < desde || m.fecha > hasta)) setPeriodo(m.fecha);
    else refrescar();
    emit aviso(QStringLiteral("%1 guardado: %2 en %3").arg(textoTipo(m.tipo), formatearPesos(m.monto), cat->nombre));
    programarSincronizacion();
    return QString();
}

QString Controlador::eliminarMovimiento(qint64 id)
{
    const Resultado r = cuentas_.eliminarMovimiento(id);
    if (!r.ok) return r.error;
    refrescar();
    emit aviso(QStringLiteral("Movimiento eliminado"));
    programarSincronizacion();
    return QString();
}

// ---------------------------------------------------------------- categorías

QVariantList Controlador::categorias(int tipo) const
{
    const TemaGrafico tema = TemaGrafico::crear(oscuro_);
    QVariantList l;
    for (const Categoria& c : cuentas_.categorias(tipo == 0 ? Tipo::Ingreso : Tipo::Gasto))
        l << QVariantMap{{"id", c.id}, {"nombre", c.nombre}, {"color", tema.serie[std::clamp(c.color, 0, 7)]},
                         {"colorIndice", c.color}, {"movimientos", c.movimientos}};
    return l;
}

qint64 Controlador::categoriaInicial(int tipo) const
{
    return cuentas_.categoriaInicial(tipo == 0 ? Tipo::Ingreso : Tipo::Gasto);
}

QVariantMap Controlador::crearCategoria(const QString& nombre, int tipo)
{
    const Resultado r = cuentas_.crearCategoria(nombre, tipo == 0 ? Tipo::Ingreso : Tipo::Gasto);
    if (r.ok) {
        refrescar();
        programarSincronizacion();
    }
    return QVariantMap{{"ok", r.ok}, {"error", r.error}, {"id", r.id}};
}

QString Controlador::renombrarCategoria(qint64 id, const QString& nombre)
{
    const Resultado r = cuentas_.renombrarCategoria(id, nombre);
    if (!r.ok) return r.error;
    refrescar();
    programarSincronizacion();
    return QString();
}

QString Controlador::cambiarColor(qint64 id, int color)
{
    const Resultado r = cuentas_.cambiarColor(id, color);
    if (!r.ok) return r.error;
    refrescar();
    programarSincronizacion();
    return QString();
}

QString Controlador::eliminarCategoria(qint64 id, qint64 moverA)
{
    const Resultado r = cuentas_.eliminarCategoria(id, moverA);
    if (!r.ok) return r.error;
    refrescar();
    programarSincronizacion();
    return QString();
}

// ---------------------------------------------------------------- saldo inicial

QString Controlador::saldoInicialTexto() const
{
    return formatearPesos(cuentas_.saldoInicial()).remove(QStringLiteral("$ "));
}

bool Controlador::fijarSaldoInicial(const QString& texto)
{
    Centavos c = 0;
    if (!texto.trimmed().isEmpty() && !parsearPesos(texto, &c, true)) return false;
    cuentas_.fijarSaldoInicial(c);
    refrescar();
    programarSincronizacion();
    return true;
}

// ---------------------------------------------------------------- archivos

QVariantMap Controlador::previsualizarImportacion(const QUrl& archivo)
{
    LecturaCsv lectura;
    QString error;
    if (!leerArchivo(archivo, &lectura, &error)) return QVariantMap{{"ok", false}, {"error", error}};
    const ResultadoImportacion r = cuentas_.previsualizarImportacion(lectura.filas);
    return QVariantMap{{"ok", true}, {"nuevos", r.nuevos}, {"repetidos", r.repetidos},
                       {"categorias", r.categoriasNuevas.join(QStringLiteral(", "))}, {"avisos", lectura.avisos.size()},
                       {"detalleAvisos", QStringList(lectura.avisos.mid(0, 30)).join(QLatin1Char('\n'))}};
}

void Controlador::mostrarImportados(const QList<FilaImportada>& filas)
{
    refrescar();
    if (vista_ == 2 || modelo_.rowCount() > 0) return;
    QDate ultima;
    for (const FilaImportada& f : filas) if (!ultima.isValid() || f.fecha > ultima) ultima = f.fecha;
    if (ultima.isValid()) setPeriodo(ultima);    // el período que se veía quedó vacío: ir a lo importado
}

QVariantMap Controlador::importar(const QUrl& archivo)
{
    LecturaCsv lectura;
    QString error;
    if (!leerArchivo(archivo, &lectura, &error)) return QVariantMap{{"ok", false}, {"error", error}};
    const ResultadoImportacion r = cuentas_.importar(lectura.filas);
    mostrarImportados(lectura.filas);
    emit aviso(r.nuevos == 1 ? QStringLiteral("Se importó 1 movimiento") : QStringLiteral("Se importaron %1 movimientos").arg(r.nuevos));
    programarSincronizacion();
    return QVariantMap{{"ok", true}, {"nuevos", r.nuevos}, {"repetidos", r.repetidos}};
}

QString Controlador::exportar(const QUrl& archivo)
{
    const auto [desde, hasta] = rango();
    QList<Movimiento> lista = cuentas_.movimientos(desde, hasta);
    std::reverse(lista.begin(), lista.end());   // en el archivo, del más viejo al más nuevo
    const QString error = escribirArchivo(archivo, csvParaGuardar(movimientosACsv(lista)));
    if (error.isEmpty()) emit aviso(QStringLiteral("Exportado: %1 movimientos").arg(lista.size()));
    return error;
}

QString Controlador::backup(const QUrl& archivo)
{
    const QString temporal = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/copia-temporal.db");
    const Resultado r = cuentas_.backup(temporal);
    if (!r.ok) return r.error;
    QFile f(temporal);
    if (!f.open(QIODevice::ReadOnly)) return f.errorString();
    const QString error = escribirArchivo(archivo, f.readAll());
    f.close();
    QFile::remove(temporal);
    if (error.isEmpty()) emit aviso(QStringLiteral("Copia de seguridad guardada"));
    return error;
}

QString Controlador::nombreExportacion() const
{
    QString n = QStringLiteral("movimientos");
    if (vista_ == 0) n += QStringLiteral("-%1-%2").arg(periodo_.year()).arg(periodo_.month(), 2, 10, QLatin1Char('0'));
    else if (vista_ == 1) n += QStringLiteral("-%1").arg(periodo_.year());
    return n + QStringLiteral(".csv");
}

QString Controlador::nombreBackup() const
{
    return QStringLiteral("cuentas-%1.db").arg(QDate::currentDate().toString(Qt::ISODate));
}

QString Controlador::fechaTexto(const QString& iso) const
{
    const QDate d = QDate::fromString(iso, Qt::ISODate);
    if (!d.isValid()) return QString();
    static const char* const dias[] = {"Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado", "Domingo"};
    return QStringLiteral("%1 %2 de %3 de %4").arg(QString::fromUtf8(dias[d.dayOfWeek() - 1])).arg(d.day())
        .arg(nombreMes(d.month()).toLower()).arg(d.year());
}

// ---------------------------------------------------------------- sincronización

bool Controlador::pcEmparejada() const
{
    return !cuentas_.pares().isEmpty();
}

QString Controlador::nombrePc() const
{
    const QList<Par> p = cuentas_.pares();
    return p.isEmpty() ? QString() : p.first().nombre;
}

QString Controlador::direccionPc() const
{
    const QList<Par> p = cuentas_.pares();
    return p.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(p.first().host).arg(p.first().puerto);
}

QString Controlador::ultimaSincTexto() const
{
    const QList<Par> p = cuentas_.pares();
    if (p.isEmpty() || !p.first().ultimaSinc.isValid()) return QStringLiteral("Todavía no se sincronizó");
    return QStringLiteral("Última vez: %1").arg(p.first().ultimaSinc.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
}

void Controlador::setNombreDispositivo(const QString& n)
{
    if (n.trimmed().isEmpty()) return;
    cuentas_.setNombreDispositivo(n);
    emit sincCambio();
}

int Controlador::puertoPorDefecto() const
{
    return sinc::PUERTO;
}

QString Controlador::version() const
{
    return QStringLiteral(IG_VERSION);
}

void Controlador::emparejar(const QString& host, int puerto, const QString& codigo)
{
    if (sincronizando_) return;
    sincronizando_ = true;
    sincEnSilencio_ = false;
    estadoSinc_ = QStringLiteral("Emparejando…");
    emit sincCambio();
    cliente_->emparejar(host, quint16(puerto > 0 ? puerto : sinc::PUERTO), codigo);
}

void Controlador::sincronizar()
{
    if (sincronizando_) return;
    const QList<Par> p = cuentas_.pares();
    if (p.isEmpty()) {
        emit aviso(QStringLiteral("Primero emparejá el celular con la PC (menú → Sincronizar con la PC)"));
        return;
    }
    temporizadorSinc_.stop();
    sincronizando_ = true;
    estadoSinc_ = QStringLiteral("Conectando…");
    emit sincCambio();
    cliente_->sincronizar(p.first().dispositivo);
}

void Controlador::sincronizarSiCorresponde()
{
    if (sincronizando_ || !pcEmparejada()) return;
    const QDateTime ahora = QDateTime::currentDateTime();
    if (ultimoIntentoAuto_.isValid() && ultimoIntentoAuto_.secsTo(ahora) < 60) return;   // no más de una vez por minuto
    ultimoIntentoAuto_ = ahora;
    sincEnSilencio_ = true;
    sincronizar();
}

void Controlador::programarSincronizacion()
{
    if (pcEmparejada()) temporizadorSinc_.start(4000);
}

void Controlador::buscarPcs()
{
    cliente_->buscar(2500);
}

void Controlador::olvidarPc()
{
    for (const Par& p : cuentas_.pares()) cuentas_.eliminarPar(p.dispositivo);
    estadoSinc_.clear();
    emit sincCambio();
}
