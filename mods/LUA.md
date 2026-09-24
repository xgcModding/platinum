# Mods Lua

Un mod Lua se distribuye como `mods/<nombre>/<nombre>.lua`; también se admiten
archivos `mods/<nombre>.lua`. Basta copiar el archivo y reiniciar el juego.
No se compila el mod ni se registra su nombre, sus funciones o sus hooks en el
código fuente del juego. Cada mod tiene su propio estado Lua.

El ejecutable incorpora Lua 5.4 y un puente genérico con libffi. Los 13 mods incluidos son Lua; el cargador sigue admitiendo mods nativos externos. Si hay un Lua y una biblioteca con el mismo
nombre en la misma carpeta, se prefiere el Lua sin abrir la biblioteca antigua.

Soporte actual del cargador: macOS, Linux y Windows de 64 bits. Windows conserva
la tabla de símbolos COFF del ejecutable para resolver también funciones internas.
No se debe eliminar esa tabla con `strip`.

## Ejemplo completo

Este archivo puede guardarse como `mods/mi_mod/mi_mod.lua` y modificarse sin
recompilar. El símbolo y su firma se declaran aquí, dentro del mod:

```lua
assert(game.api_version == 1)
return {
    id = "mi_mod", name = "Mi mod", version = "1.0",
    on_load = function()
        game.hook("HMCutIn_StartTask", "ptr", {"ptr", "i32", "ptr", "i32"},
            function(original, fieldSystem, isFly, pokemon, gender)
                game.log("El juego ha llamado a HMCutIn_StartTask")
                return original(fieldSystem, isFly, pokemon, gender)
            end)
    end,
}
```

`game.hook` resuelve ese símbolo e instala el trampoline nativo hacia el callback
Lua. El primer argumento del callback, `original`, llama al hook anterior o a
la función original si no había otro hook. Solo es válido durante esa invocación;
no debe guardarse para usarlo más tarde. Omitir esa llamada sustituye el
comportamiento. Varios mods pueden encadenar hooks del mismo símbolo.

## Llamadas al juego

```lua
local quantity = game.bind("Bag_GetItemQuantity", "u16", {"ptr", "u16", "i32"})
local amount = quantity(bagPointer, itemId, heapId)
```

`bind` se utiliza desde `on_load` o callbacks posteriores. No hay una lista
cerrada de funciones: utiliza el mismo resolvedor de símbolos que los mods C.
El autor declara la firma C del símbolo de la versión de juego que está usando.
Los símbolos ausentes y los tipos no soportados producen un error de carga.

Tipos: `void` (solo retorno), `ptr`, `u8`, `i8`, `u16`, `i16`, `u32`, `i32`,
`u64`, `i64`, `float` y `double`. Se admiten hasta 24 argumentos de tamaño fijo;
no se admiten varargs ni estructuras pasadas por valor. Las estructuras por
puntero se leen y escriben con la API de memoria. `BOOL` del juego es `i32`,
no un booleano de Lua; los enums se declaran como su entero C correspondiente.
Los valores `u64` conservan sus 64 bits en un entero Lua con signo.

Los hooks Lua se ejecutan en el hilo que cargó los mods (el hilo del juego).
Una llamada desde otro hilo continúa por el original sin entrar en Lua.

## Memoria y declaraciones

- `game.sizeof(tipo)` devuelve tamaño y alineación del tipo en la plataforma.
- `game.alloc(bytes)` crea un buffer de Lua inicializado a cero.
- `game.read(puntero, tipo, offset=0)` lee un valor.
- `game.write(puntero, tipo, valor, offset=0)` escribe un valor.
- `game.address(buffer_o_puntero, offset=0)` obtiene una dirección.
- `game.copy(destino, origen, bytes)` copia memoria.
- `nil` representa un puntero nulo.

Los buffers pertenecen a Lua. Hay que mantener una referencia mientras el juego
conserve su dirección. Los punteros devueltos por el juego pertenecen al juego;
si una función transfiere su propiedad, el mod debe llamar a su destructor C.
Los accesos directos a buffers tienen comprobación de límites; los punteros
prestados tienen las mismas obligaciones que en un mod nativo. Una firma,
un layout o un puntero incorrecto puede provocar un fallo: esta API no es una
sandbox para ejecutar código no confiable.

AutoHM incluye sus constantes y declaraciones de estructuras en `autohm.lua`.
Su función local `struct` calcula offsets usando las alineaciones del host. Un
mod nuevo puede declarar sus propias estructuras del mismo modo, sin añadir
bindings al juego. Las declaraciones deben corresponder a la versión del
juego; Lua elimina la compilación por plataforma, no los cambios de ABI entre
versiones del motor.

## Ciclo de vida y opciones

El script devuelve `id`, `name` y `version` (cadenas no vacías), más los callbacks
opcionales `on_load()`, `on_frame()`, `on_event(evento, puntero)` y `on_unload()`.
`on_load` puede devolver `false` o generar un error para rechazar la carga.
Los hooks instalados durante una carga fallida se retiran. Al cerrar el juego
se llama a `on_unload`, se retiran los hooks y se destruye el estado Lua.

```lua
game.option("enabled", "ENABLE MOD", {"DISABLED", "ENABLED"}, 1,
    function(selected) enabled = selected == 1 end)
```

Las opciones y hooks se registran dentro de `on_load`. La selección es un índice
**desde cero**, aunque la tabla de valores Lua comienza en uno. La pantalla del
juego y el `.ini` usan el sistema común de opciones. `game.log(texto)` escribe un
mensaje identificado con el id del mod.

`on_event` recibe los números y estructuras de `mods/mod_api.h`: 1 frame,
6 experiencia, 7 objeto de subida de nivel, 8 partida cargada. El puntero del
evento es prestado y válido durante el callback. Los límites de capacidad del
cargador nativo también se aplican a Lua.

Un error de callback se registra y desactiva ese callback. Un hook que falla
continúa por el original; si ya lo llamó, conserva ese resultado y no repite
sus efectos. Esto no revierte escrituras que el mod ya haya hecho en memoria.
Hay un presupuesto de instrucciones para detectar bucles accidentales.

## Compilación del motor y pruebas

El soporte común del motor se compila una vez con `make osx` o `make linux`.
Crear, editar, instalar o compartir un mod no ejecuta esos comandos. Lua se
enlaza estáticamente. macOS utiliza `/usr/lib/libffi.dylib`, suministrada por el
sistema; en Linux se enlaza libffi estáticamente y se necesita `libffi-dev` al
compilar el motor. El Dockerfile ya incluye ese paquete.

Después de la build, sustituyendo `build_osx` por `build_linux_old` cuando proceda:

```sh
build_osx/lua_mod_test tools/tests/lua_mods/first.lua tools/tests/lua_mods/second.lua tools/tests/lua_mods/failure.lua tools/tests/lua_mods/rejected.lua
build_osx/autohm_abi_test mods/autohm/autohm.lua
build_osx/autohm_behavior_test mods/autohm/autohm.lua
python3 tools/tests/test_lua_mod_loader.py build_osx/pokeplatinum/main
```

Estas pruebas cubren firmas, memoria, cadenas de hooks, errores, desmontaje,
constantes y layouts reales, comportamiento de AutoHM con fixtures y la carga
de scripts independientes sobre el mismo ejecutable con trampolines reales.
Los fixtures no modifican una partida.

## Recursos, callbacks y opciones especiales

- `game.symbol(nombre)` obtiene la dirección de un símbolo del motor.
- `game.callback(retorno, argumentos, función)` crea un puntero a un callback
  nativo durante `on_load`. Vive hasta descargar el mod. El mod debe cancelar
  las tareas que lo usen en `on_unload`. Solo entra en Lua desde el hilo del juego;
  en otro hilo o tras un error devuelve cero.
- `game.read_file(nombre)` lee un archivo situado junto al script y devuelve sus
  bytes como cadena, o `nil` si no existe.
- `game.bytes(cadena)` crea un buffer con esos bytes y un cero final.
- El sexto argumento de `game.option` contiene los flags de la API: `1` para
  solo lectura (`callback=nil`) y `2` para una acción (`clave=nil`).
- `game.option_label(clave, texto)` actualiza el valor visible de una opción
  de solo lectura.

Los scripts distribuidos usan los layouts de esta versión del port de 64 bits.
`lua_mods_behavior_test` los comprueba contra las cabeceras C y ejecuta callbacks
Lua con datos de prueba, incluidos límites de experiencia, equipos, bayas,
consumo de MT y miel, Pokédex, marcadores y captura.

```sh
build_osx/lua_mods_behavior_test build_osx/pokeplatinum/mods
```

`BattleItemUse` contiene cuatro bytes enteros y ocupa un registro entero tanto
en ARM64 como en x86-64. Los mods de captura lo pasan como `u32`; el test del
puente comprueba esta llamada contra la estructura C. Otras estructuras por
valor siguen sin estar soportadas.
