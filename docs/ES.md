# OpenSteamTool

![cpp](https://img.shields.io/badge/cpp-20%2B-green?logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green?logo=cmake)
![OnlyWindows](https://img.shields.io/badge/windows%20only-red?style=for-the-badge)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/OpenSteam001/OpenSteamTool)

OpenSteamTool es un proyecto de DLL para Windows desarrollado con CMake.

## Idiomas:
<p align="center">
  <a href="../README.md">English</a> •
  <a href="/docs/ES.md"><img src="https://flagcdn.com/256x192/es.png" width="48" alt="Bandera Española"></a>
</p>


## Características

### Desbloqueos principales
- Desbloquea una cantidad ilimitada de juegos que no poseas.
- Desbloquea todos los DLC para juegos que no poseas.
- Soporta la carga automática de claves de descifrado de depósitos(depots) desde la configuración de Lua.
- Soporta la descarga automática de manifiestos a través de las APIs ascendentes (upstream APIs) de 'opensteamtool' / 'steamrun' / 'wudrm' (por defecto es opensteamtool), o mediante un endpoint personalizado de Lua (ver [Manifest a traves de Lua](#manifest-via-lua)).
- Soporta la descarga de juegos protegidos o DLCs que requieran un token de acceso.
- Soporta la vinculación de manifiestos para evitar que juegos específicos se actualicen.
