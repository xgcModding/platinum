# Sistema de mods para macOS, Linux y Windows

El juego carga mods Lua independientes desde `mods/<nombre>/<nombre>.lua` y
mods nativos desde `mods/<nombre>/<nombre>.so` (Linux), `.dylib` (macOS) o `.dll` (Windows), junto
al ejecutable o la AppImage. También se admiten esos archivos directamente en
`mods`. Los recursos y la configuración permanecen junto al módulo.

Para crear y compartir mods Lua sin recompilación, consulta [la API Lua](LUA.md).
Los 13 mods incluidos se distribuyen como Lua. Cada uno declara sus hooks y
su lógica en su script, usando el puente genérico del motor. El empaquetado
retira sus bibliotecas antiguas y conserva la configuración y los recursos.

También puede indicarse otra carpeta mediante `POKEPLATINUM_MOD_DIR`.

## API de mods nativos

Cada mod exporta `PokePlatinumMod_GetDescriptor` y devuelve un
`PokePlatinumMod` definido en `api.h`. El descriptor separa `id`, usado para
la carpeta y la configuración, de `name`, que es el nombre legible mostrado
al jugador. La API ofrece:

- `onLoad`: inicialización y acceso a los servicios del host.
- `onFrame`: ejecución en cada frame, después de leer la entrada.
- `onEvent`: recepción de eventos con contexto opcional.
- `onUnload`: liberación de recursos.
- `registerStartMenuItem`: registro de entradas nuevas en el menú de inicio.
- `getLoadedModCount` y `getLoadedModInfo`: enumeración de mods cargados.
- `resolveSymbol`: acceso a símbolos públicos y, en Linux, resolución de
  respaldo desde la tabla ELF para símbolos internos de esta compilación.
- `installReplacementHook`: sustitución completa de una función por símbolo.
- `installTrampolineHook`: hook que devuelve una llamada segura a la función
  original. La ABI 7 reserva la entrada necesaria en el ejecutable; el mod no
  contiene bytes, tamaños de instrucciones ni datos del compilador.
- `removeHook`: restauración explícita; el cargador también restaura
  automáticamente los hooks antes de descargar su mod propietario.
- `registerOption`: registro opcional de opciones con una etiqueta, una clave
  de configuración, una lista de hasta `POKEPLATINUM_MOD_MAX_OPTION_VALUES`
  valores (32) y un callback que recibe la selección.

Todos los mods abren la misma pantalla integrada con el estilo de `OPTIONS`: el
nombre visible del mod aparece como título, las opciones se muestran en filas,
la selección se resalta en rojo e izquierda/derecha cambia una copia temporal.
`CLOSE`, al final de las opciones, y el botón B reproducen el comportamiento de
`OPTIONS`: si hay cambios, muestran el mismo diálogo de confirmación. Elegir Sí
aplica los cambios y actualiza el `.ini`; elegir No los descarta. Un mod sin
opciones muestra el título y `CLOSE`. No existe un segundo submenú para elegir
valores.

Existe una única API sin números de versión ni compatibilidad con contratos
anteriores. El ejecutable y los mods nativos deben compilarse con el mismo
`api.h`.

Cada mod que registre opciones obtiene un archivo `<id>.ini` junto a
su módulo. El cargador crea el archivo si no existe, lee sus valores al iniciar y
lo actualiza cada vez que cambia una opción. Los valores inexistentes o no
válidos se sustituyen por el valor predeterminado declarado por el mod.

Los callbacks de las entradas reciben el contexto del juego y deciden si el
menú permanece abierto, se cierra o muestra el navegador de mods. El menú de
inicio admite desplazamiento, por lo que varios mods pueden registrar entradas
sin cambiar su código fuente.

El cargador registra directamente la opción `MODS` del menú de inicio. Esta
opción forma parte del juego, está disponible aunque no haya plugins instalados
y muestra la lista de plugins cargados; no requiere `mod_browser.so`.

Los plugins nativos son bibliotecas: tienen la misma capacidad y los mismos
riesgos que cualquier programa ejecutado por el usuario. Los trampolines de la
ABI 7 se instalan en la entrada estable reservada para hooks y no dependen del
prólogo generado por una versión concreta del compilador.

## Instalación

Copiar la carpeta completa del mod en:

```text
build_linux/pokeplatinum/mods/<nombre_del_mod>/
```

Dentro debe existir `<nombre_del_mod>.lua`, `.so` o `.dylib`, acompañado de sus recursos cuando los
tenga. Se cargará automáticamente en el siguiente inicio. Para desactivarlo
basta con retirar su carpeta.

## Ejemplo

- `better_berries.lua` añade **Better Berries**, con `GROWTH SPEED` configurable
  en `0%`, `25%`, `50%`, `75%` y `100%` (0% por defecto).
  Reducen el tiempo de espera en ese porcentaje: queda el 100%, 75%, 50%, 25%
  o crecimiento instantáneo, respectivamente. Al 100%, las plantas existentes
  maduran al actualizarse sus gráficos o consultarse para interactuar, y las
  nuevas maduran al plantarlas, sin esperar al siguiente minuto del reloj.
  Entre 25% y 75% se acelera el ciclo completo, incluido el secado y la caducidad.
  Al 100% solo se salta el crecimiento: conserva la humedad y el plazo normal
  para cosechar, sin renovar dicho plazo en frutos ya maduros. Conserva los abonos.
  Se aplica también a plantas existentes y al tiempo transcurrido con el juego
  cerrado. Las fracciones de minuto se acumulan durante la sesión y se reinician
  al cambiar el ajuste o cargar partida. La selección se guarda en
  `better_berries.ini` como `growth_speed=0%`, `25%`, `50%`, `75%` o `100%`.
  `CONSUMPTION` ofrece `Default` (consumo original), `Refill` (al terminar el
  combate reequipa la baya consumida gastando una igual de la Bolsa, si queda)
  e `Inf` (la devuelve al terminar sin gastar una de la Bolsa). No se repone
  durante el combate ni se sustituye otro objeto equipado, incluidos los
  recuperados con Reciclaje. Solo afecta a los Pokémon del jugador; se excluyen
  combates de enlace, Frente Batalla, grabaciones y tutoriales. Las bayas
  robadas o intercambiadas no se consideran consumidas. Se guarda como
  `consumption=Default`, `Refill` o `Inf` en el mismo archivo.
- `infinite_honey.lua` evita consumir Honey únicamente al untar los árboles.
  Es necesario tener al menos una unidad; usarlo desde la Bolsa sigue
  consumiéndolo. `ENABLE MOD` activa o desactiva todo el mod (activado por defecto).
  `WAIT HOURS` configura la espera de 0 a 6 horas, con 6 por defecto y 0 para
  disponibilidad inmediata. El ajuste también se aplica a los árboles que ya
  tengan miel, según el tiempo transcurrido. Se conservan la caducidad a las
  24 horas y las probabilidades originales de encuentro, incluida la posibilidad
  de no atraer ningún Pokémon. Al desactivar el mod vuelven el consumo y la espera
  originales. Ambos ajustes se guardan en `infinite_honey.ini`.
- `autohm.lua` permite usar Corte, Surf, Fuerza, Golpe Roca, Cascada y
  Treparrocas sin enseñárselos a un Pokémon. Con `IGNORE PARTY REQUIREMENT` en
  `DISABLED` (valor predeterminado), al menos un Pokémon del equipo debe poder
  aprender la MO; con `ENABLED`, no se exige compatibilidad con el equipo. La
  MO debe estar en la mochila y se mantienen las comprobaciones originales de
  medalla, ubicación y estado. El ajuste se guarda en `autohm.ini`.
  También permite usar **Excavar (MT28)** y **Destello (MT70)** desde el menú
  de un Pokémon que pueda aprenderlos, aunque no los conozca, si su MT está
  en la bolsa. Para estas dos MT se exige siempre compatibilidad del Pokémon
  seleccionado, incluso con IGNORE PARTY activado. No consume la MT ni modifica
  sus movimientos. Los movimientos ya aprendidos conservan su funcionamiento
  normal aunque no esté su MT en la bolsa.


- `fast_ball_battle` se distribuye en `mods/fast_ball_battle/`, con
  `fast_ball_battle.lua` y sus seis recursos externos dentro de esa carpeta.
- La opción `MODS` pertenece al cargador y muestra todos los plugins cargados.

Si se retira la carpeta de un mod, desaparece únicamente el comportamiento
aportado por ese mod.

## Crear y compartir un mod

Un mod puede compilarse sin recompilar el juego:

```bash
mkdir -p mi_mod
gcc -shared -fPIC -I/ruta/a/pokeplatinum -o mi_mod/mi_mod.so mi_mod.c
```

Cada mod se distribuye como una carpeta cuyo nombre coincide con el nombre de su
`.so`. Aunque no use recursos externos, la distribución será
`mi_mod/mi_mod.so`. El destinatario copia la carpeta completa dentro de `mods/`
y reinicia el juego.

Los mods incluidos se compilan junto al proyecto mediante:

```bash
make linux
```
