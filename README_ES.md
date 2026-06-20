<div align="center">
  <img src="docs/logo-animated.svg" width="180" alt="Logo de OpenSteamTool">

  <h1>OpenSteamTool</h1>

  <p>
    <strong>Herramienta de código abierto para desbloquear Steam</strong>
  </p>

  <p>
    <img src="https://img.shields.io/badge/C%2B%2B-20%2B-2ea44f?logo=cplusplus&logoColor=white" alt="C++ 20+">
    <img src="https://img.shields.io/badge/CMake-3.20%2B-2ea44f?logo=cmake&logoColor=white" alt="CMake 3.20+">
    <img src="https://img.shields.io/badge/Windows-only-d73a49?logo=windows&logoColor=white" alt="Solo Windows">
    <a href="https://deepwiki.com/OpenSteam001/OpenSteamTool">
      <img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki">
    </a>
  </p>

  <p>
    <a href="README.md">
      <img src="https://flagcdn.com/w40/us.png" width="22" alt="Bandera de Estados Unidos">
      English
    </a>
    &nbsp;|&nbsp;
    <a href="README_ES.md">
      <img src="https://flagcdn.com/w40/es.png" width="22" alt="Bandera de España">
      Español
    </a>
  </p>
</div>

## Características

### Desbloqueos principales
- Desbloquea una cantidad ilimitada de juegos que no poseas.
- Desbloquea todos los DLC para juegos que no poseas.
- Soporta la carga automática de claves de descifrado de depósitos(depots) desde la configuración de Lua.
- Soporta la descarga automática de manifiestos a través de las APIs ascendentes (upstream APIs) de `opensteamtool` / `steamrun` / `wudrm` (por defecto es opensteamtool), o mediante un endpoint personalizado de Lua (ver [Manifest a traves de Lua](#manifest-via-lua)).
- Soporta la descarga de juegos protegidos o DLCs que requieran un token de acceso.
- Soporta la vinculación de manifiestos para evitar que juegos específicos se actualicen.

### Recarga rápida
- Añadir, modificar, eliminar o sobrescribir archivos `.lua` en cualquier directorio supervisado activa automáticamente una recarga. No se necesita reiniciar ni alternar entre modo desconectado/conectado.

### Inyección
- Añade inyección opcional de bibliotecas en procesos de juego mediante `[inject]` en `opensteamtool.toml`.
- Configura `enabled`, `library_x64` y `library_x86`; la biblioteca inyectada debe coincidir con la arquitectura del proceso de destino.`library_x64` y `library_x86` pueden ser rutas absolutas, o rutas relativas resueltas desde el directorio raíz de Steam.

### Préstamo familiar y juego en remoto
- Omite las restricciones de Steam Family Sharing para los juegos que se hayan añadido a la biblioteca con `addappid` en Lua. Todas las cuentas de la familia de Steam que participen en el préstamo familiar deben usar OpenSteamTool para que esto funcione.

### Compatible con juegos protegidos por Denuvo y SteamStub
- Los juegos protegidos únicamente por SteamStub no requieren la configuración de `AppTicket`. OpenSteamTool puede reutilizar el ticket local de ConfigStore de Steam y falsificar el AppId solicitado a través de una vulnerabilidad de desbordamiento por cuatro (off-by-four ticket parsing vulnerability) en SteamDRMP, sin necesidad de inyectarse en el proceso del juego.
- Los juegos protegidos por Denuvo siguen requiriendo datos explícitos del ticket. En `HKEY_CURRENT_USER\Software\Valve\Steam\Apps\{AppId}`, tanto `AppTicket` como `ETicket` son valores `REG_BINARY`.
- Utiliza `setAppTicket(appid, "hex")` y `setETicket(appid, "hex")` en la configuración de Lua para escribir estos valores en el registro de forma automática.
- La verificación de Denuvo tiene una ventana de validez de 30 minutos. Cuando esta ventana expira, la autorización puede fallar con el código de error de Denuvo `88500005`; actualiza los datos del ticket antes de volver a intentarlo.
- Prioridad de AppTicket: los tickets explícitos tienen la prioridad más alta, incluyendo los tickets configurados por `setAppTicket` y los valores de `AppTicket` ya existentes en el registro. Si no hay ningún AppTicket explícito disponible, OpenSteamTool recurre a la ruta del ticket falsificado de ConfigStore local.
- Prioridad de SteamID: primero lee `SteamID` como `REG_SZ`(únicamente numérico); si no se encuentra, lo analiza a partir del `AppTicket` explícito.

### Extracción de tickets con `extract_tickets`
La herramienta `extract_tickets` vuelca las cadenas hexadecimales de `AppTicket` y `ETicket` que necesitas para `setAppTicket` / `setETicket`. Ejecútala en una máquina donde Steam esté abierto e iniciado sesión en una cuenta que sea **propietaria** del juego en cuestión.

1. Compila las herramientas (Revisa [Build](#build)); el binario se generará en `build/tools/Release/extract_tickets.exe`.
2. Ejecútalo pasando el AppId del juego como argumento (o ejecútalo sin argumentos e introduce el AppId cuando se te solicite):
   ```powershell
   extract_tickets.exe 1361510
   ```
3. La herramienta leerá la ruta de instalación de Steam desde el registro, cargará `steamclient64.dll` y escribirá todo dentro de una carpeta `<appid>/` junto al ejecutable:
   - `appticket.bin` — ticket bruto de propiedad de la aplicación (binario)
   - `eticket.bin` — ticket cifrado bruto de la aplicación (binario)
   - `tickets.txt` — resumen en texto plano con las cadenas hexadecimales:
     ```
     appid:1361510
     appticket(184 bytes):14000000...
     eticket(143 bytes):...
     ```
   Si un ticket no se puede obtener, se reportará como
   `appticket:null` / `eticket:null`.
4. Pega las cadenas hexadecimales de `tickets.txt` en tu configuración de Lua:
   ```lua
   setAppTicket(1361510, "14000000...")
   setETicket(1361510, "...")
   ```

> **Nota:** Los tickets solo son válidos cuando se extraen de una cuenta que **realmente posee** el juego.

### Estadísticas y logros
- Activa las estadísticas y los logros para los juegos que no poseas.
- Utiliza `setStat(appid, "steamid")` para configurar de qué SteamID se deben extraer los datos de los logros.
- Si no hay ningún `setStat` configurado para una aplicación, OpenSteamTool consulta `https://stats.opensteamtool.com/{appid}` cuando `[stats] enable_api = true` (valor predeterminado).
- Prioridad: `setStat` > API de estadísticas cuando está habilitada y devuelve un valor válido > SteamID predefinido `76561198028121353`.

### Online Fix(Reparacion para habilitar el Online)
- Añade `-onlinefix` a los parámetros de lanzamiento de Steam para habilitar el juego en línea basado en el AppId 480 en juegos que utilizan emparejamiento (matchmaking) por salas (lobbies). La limitación actual es que solo se puede ejecutar uno de estos juegos a la vez. Para revertirlo, simplemente elimina -onlinefix de los parámetros de lanzamiento; el juego en línea volverá a la normalidad en el próximo inicio.

## Futuro
- Soporte para la sincronización con Steam Cloud (este es un proyecto enorme).

## Uso
1. Ejecuta `build.bat` desde la raíz del proyecto para compilarlo.
2. Copia los archivos generados `dwmapi.dll`, `xinput1_4.dll` y `OpenSteamTool.dll` al directorio raíz de Steam.
3. Crea un directorio para Lua (por ejemplo, C:\steam\config\lua) y coloca allí tus scripts de Lua. La DLL los cargará y ejecutará automáticamente.
4. Ejemplo de Lua:
```lua
addappid(1361510) -- desbloquea el juego con appid 1361510

addappid(1361511, 0,"5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0") -- desbloquea el juego con appid 1361511, la clave del depósito (depotKey) es "5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0" 

addtoken(1361510,"2764735786934684318") -- añade el token de acceso ("2764735786934684318") para el juego con appid 1361510 
-- Ya no está soportado:
--pinApp(1361510) -- fija el juego con appid 1361510 para evitar que se actualice

setManifestid(1361511,"5656605350306673283") -- fija depotid:1361511 manifest_gid:5656605350306673283, el tamaño por defecto es 0
setManifestid(1361511,"5656605350306673283", 12345678) -- lo mismo, pero con un tamaño explícito

setAppTicket(1361510,"0100000000000000...") -- escribe AppTicket (REG_BINARY) en HKCU\Software\Valve\Steam\Apps\1361510\AppTicket

setETicket(1361510,"0100000000000000...") -- escribe ETicket (REG_BINARY) en HKCU\Software\Valve\Steam\Apps\1361510\ETicket

setStat(1361510, "76561197960287930") -- utiliza los datos de logros del SteamID especificado para el appid 1361510
-- Si no se configura, se utiliza la API de estadísticas cuando está habilitada; de lo contrario se usa el SteamID por defecto 76561198028121353.
```

Los nombres de todas las funciones **no distinguen entre mayúsculas y minúsculas**. `setAppTicket`, `setappticket`, `SetAppticket`, `SETAPPTICKET`, etc., son todas equivalentes. Lo mismo se aplica a cada función registrada (`addAppId`, `AddToken`, `SETManifestid`, etc.).

### Configuración (opcional)
Cambia el nombre de `opensteamtool.example.toml` a `opensteamtool.toml` y colócalo en el directorio raíz de Steam (junto a `steam.exe`).
Si no se encuentra ningún archivo de configuración, se utilizarán los valores predeterminados integrados; no se creará ninguno de forma automática.
El archivo se supervisa mientras Steam está en ejecución; los cambios válidos se recargan en caliente sin reiniciar Steam.

```toml
[log]
# Solo para compilaciones de depuración (Debug). Niveles: trace, debug, info, warn, error
level = "info"

[manifest]
# API ascendente para los códigos de solicitud de manifiestos de depósito. Opciones: "opensteamtool", "steamrun", "wudrm"
url = "opensteamtool"

# Tiempos de espera HTTP (timeouts) para las solicitudes de manifiestos (en milisegundos)
timeout_resolve_ms = 5000
timeout_connect_ms = 5000
timeout_send_ms    = 10000
timeout_recv_ms    = 10000

[stats]
# Consulta https://stats.opensteamtool.com/{appid} cuando no existe setStat en Lua.
# Prioridad: setStat > API de estadísticas > SteamID predefinido.
enable_api = true

# Directorios adicionales de configuración de Lua (opcional).
# Los archivos se cargan después de la carpeta predeterminada <Steam>/config/lua.
# La carpeta predeterminada siempre se carga al final para que los archivos del usuario tengan prioridad.
[lua]
paths = []

[inject]
# Inyección opcional de bibliotecas en procesos de juego.
# La biblioteca inyectada debe coincidir con la arquitectura del proceso de destino.
enabled = false
# library_x64 = "OpenSteamTool.GameHook.x64.dll"
# library_x86 = "OpenSteamTool.GameHook.x86.dll"

# Espejo (mirror) de metadatos opcional. Consulta "Compatibilidad con versiones de Steam" más abajo.
[remote]
# url_template = "https://tu.servidor/{channel}/{component}/{sha256}.toml"
```
### Manifiest a través de Lua

Se admiten dos funciones de código de manifiesto:

### `fetch_manifest_code(gid)`

Función básica que recibe únicamente el GID del manifiesto.

### `fetch_manifest_code_ex(app_id, depot_id, gid)` *(recomendado)*
Función extendida que recibe `app_id`, `depot_id` y `gid`. Permite construir endpoints de API que requieran la identificación de la aplicación.

El entorno de ejecución (runtime) en C++ proporciona dos funciones auxiliares de Lua:


| Function | Signature | Returns |
|----------|-----------|---------|
| `http_get`  | `http_get(url [, headers])`       | `body, status_code` |
| `http_post` | `http_post(url, body [, headers])` | `body, status_code` |

`headers` es una tabla opcional: `{["Key"]="Value", ...}`.

### Compatibilidad con versiones de Steam

OpenSteamTool ya no incluye firmas de patrones de bytes (byte-pattern signatures) dentro de la DLL. En su lugar, en cada inicio calcula el hash SHA-256 de `steamclient64.dll` y `steamui.dll` en el disco, y busca un archivo de patrones coincidente desde el rastreador ascendente en ['OpenSteam001/steam-monitor'](https://github.com/OpenSteam001/steam-monitor) (extension `pattern`).

Orden de búsqueda (en cada inicio):

1.**GitHub raw** — `https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern/....` Fuente canónica.
2.**jsDelivr CDN** — alternativa automática si GitHub raw no está disponible (conexión rechazada / tiempo de espera / error 5xx). No requiere configuración. Útil en regiones donde `raw.githubusercontent.com` está bloqueado pero jsDelivr es accesible (por ejemplo, China continental).
3.**Caché local** — `<Steam>\opensteamtool\pattern\<subdir>\<sha256>.toml`. Se utiliza **únicamente** cuando el servidor remoto no está disponible. La caché se sobrescribe tras cada consulta remota exitosa.

Se consulta al servidor remoto en cada inicio para que los usuarios obtengan automáticamente las nuevas publicaciones del proyecto principal (por ejemplo, si el bot añade una nueva firma o corrige una existente) sin tener que limpiar ninguna caché.

Si un paso devuelve un error **HTTP 404**, el bucle de espejos (mirrors) se detiene inmediatamente —todos los espejos sirven el mismo contenido, por lo que un 404 significa que el bot ascendente aún no ha publicado un archivo TOML para esa compilación específica de Steam—. En ese caso, el código recurre a la caché local si existe; de lo contrario, aparecerá una ventana emergente por única vez mostrando el nombre de la DLL no emparejada, su SHA-256, la ruta de caché esperada y la URL de origen. Solo se desactivarán los ganchos (hooks) vinculados a esa DLL; el resto de OpenSteamTool seguirá funcionando.

También puedes colocar manualmente un archivo TOML de patrones en el directorio de la caché si conoces la estructura para una compilación determinada; el nombre del archivo debe ser `<sha256>.toml`. La caché de reserva lo detectará la próxima vez que el servidor remoto sea inaccesible.

> Se realiza una breve solicitud HTTPS saliente en cada inicio (una por cada DLL: `steamclient64.dll`, `steamui.dll`). Los cuerpos descargados son diminutos (~10 KB cada uno) y el proceso se ejecuta en un hilo secundario (worker thread), por lo que nunca bloquea el cargador de Steam.

#### Uso de un espejo (mirror) diferente

Para la mayoría de los usuarios, la alternativa integrada de **GitHub -> jsDelivr** es suficiente. Si deseas utilizar un espejo privado o un servidor de intranet, configura una plantilla de URL completa. Un espejo personalizado reemplazará las fuentes remotas integradas; la alternativa de la caché local seguirá estando disponible.

La plantilla debe incluir obligatoriamente `{channel}`, `{component}` y `{sha256}`. Los canales utilizados actualmente son `pattern` e `ipc`.

```toml
[remote]
url_template = "https://tu.servidor/{channel}/{component}/{sha256}.toml"
# url_template = "https://fast.jsdelivr.net/gh/OpenSteam001/steam-monitor@{channel}/{component}/{sha256}.toml"
```

### Registro de depuración

Las compilaciones de depuración (Debug) escriben archivos de registro independientes por módulo dentro de `<Steam>/opensteamtool/`:

| Archivo | Fuente | Contenido |
|---------|--------|-----------|
| `main.log`          | General | Inicialización, carga de configuración, análisis de Lua, utilidades |
| `ipc.log`           | `LOG_IPC_*` | Comandos IPC, despacho de InterfaceCall, suplantación (spoofing) |
| `netpacket.log`     | `LOG_NETPACKET_*` | Envío/recepción de paquetes de red, despacho de eMsg |
| `manifest.log`      | `LOG_MANIFEST_*` | Descarga de manifiestos, fetch_manifest_code, vinculación de manifiestos |
| `decryptionkey.log` | `LOG_DECRYPTIONKEY_*` | Inyección de claves de descifrado de depósitos (depots) |
| `keyvalue.log`      | `LOG_KEYVALUE_*` | Parcheo de KeyValues (vinculación de manifiestos) |
| `misc.log`          | `LOG_MISC_*` | Captura de punteros del motor, sugerencias de AppId |
| `achievement.log`   | `LOG_ACHIEVEMENT_*` | Solicitudes/respuestas de UserStats, suplantación de steamid |
| `pics.log`          | `LOG_PICS_*` | Inyección de tokens de acceso PICS |
| `package.log`       | `LOG_PACKAGE_*` | Inyección de paquetes, eventos de FileWatcher |
| `onlinefix.log`     | `LOG_ONLINEFIX_*` | Parche en línea (suplantación del AppId 480) |
| `richpresence.log`  | `LOG_RICHPRESENCE_*` | Construcción e inyección de paquetes Rich Presence |
| `steamui.log`       | `LOG_STEAMUI_*` | Diagnósticos de hooks de SteamUI |
| `pipe.log`          | `LOG_PIPE_*` | Handshakes de pipe, inspección de procesos, autorización de Denuvo, inyección de bibliotecas |
| `platform.log`      | `LOG_PLATFORM_*` | Diagnósticos de utilidades de plataforma, incluidas operaciones sobre procesos remotos |

El nivel de registro se controla mediante `[log] level` en `opensteamtool.toml`.

## Compilación

### Requisitos
- Windows 10/11
- CMake 3.20+
- Visual Studio 2022 with MSVC (x64 toolchain)

### Requisitos de ejecución
- Acceso HTTPS saliente a `raw.githubusercontent.com` en el primer inicio tras una actualización de Steam (ver [Compatibilidad con versiones de Steam](#steam-version-compatibility)). Posteriormente se almacena en caché.

### Compilación rápida
```powershell
build.bat
```

### Archivos de salida
- Debug: `build/Debug/OpenSteamTool.dll`, `build/Debug/dwmapi.dll`, `build/Debug/xinput1_4.dll`

- Release: `build/Release/OpenSteamTool.dll`, `build/Release/dwmapi.dll`, `build/Release/xinput1_4.dll`

## Descargo de responsabilidad
Este proyecto se proporciona únicamente con fines de investigación y educativos. Eres responsable de cumplir con las leyes locales, los términos de servicio de la plataforma y las licencias de software correspondientes.
