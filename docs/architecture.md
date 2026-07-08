# Arquitectura de linux-sysmon

## Diagrama de flujo

```
┌─────────────────────────────────────────────────────────┐
│                     main()                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐  │
│  │ parse    │  │ gather   │  │ display  │  │ monitor │  │
│  │ args     │─→│ all()    │─→│ info()   │─→│ loop    │  │
│  └──────────┘  └────┬─────┘  └──────────┘  └─────────┘  │
│                      │                                    │
└──────────────────────┼────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   procfs::       sysfs::        disk::
   read_all()     read_thermal_  read_all()
                  zones()
        │              │              │
        ▼              ▼              ▼
   ┌──────────┐  ┌──────────┐  ┌──────────┐
   │ mmap(2)  │  │ open/    │  │ statfs(2)│
   │ /proc/*  │  │ read(2)  │  │ /proc/   │
   │          │  │ /sys/*   │  │ mounts   │
   └──────────┘  └──────────┘  └──────────┘
```

## Capas del sistema

### 1. Capa de RAII (include/linux_sysmon/)

| Clase         | Recurso           | Operación                     |
|---------------|-------------------|-------------------------------|
| `Fd`          | file descriptor   | `open()` / `close()`          |
| `MmapRegion`  | mapped memory     | `mmap()` / `munmap()`         |
| `LineIterator`| string_view       | zero-copy line splitting      |
| `Result<T>`   | error handling    | `std::expected` (C++23)       |

### 2. Capa de lectura

#### /proc filesystem (procfs_reader.hpp)
- Usa `mmap(2)` con `MAP_PRIVATE` para mapear archivos /proc completos
- `MmapRegion` libera la memoria automáticamente al salir de ámbito
- `LineIterator` hace split por '\n' sin copias (string_view)
- `split_kv()` parsea líneas "key:        value" típicas de /proc
- `from_chars` para parseo numérico sin excepciones

#### /sys filesystem (sysfs_reader.hpp)
- Usa `open()` + `read()` de bajo nivel para leer archivos sysfs
- Iteración de directorios con `std::filesystem::directory_iterator`
- Soporte para zonas térmicas (`thermal_zone*/temp`)

#### statfs (disk_info.hpp)
- `statfs(2)` para obtener bloques/bloque tamaño de un punto de montaje
- Parseo de `/proc/mounts` via `std::ifstream` para listar montajes reales
- Filtrado de pseudo-filesystems (proc, sysfs, tmpfs, cgroup...)

#### ioctl (terminal.hpp)
- `TIOCGWINSZ` para obtener dimensiones de la terminal
- Constantes `constexpr` para códigos ANSI
- `color_pct()` que retorna color según umbral

### 3. Capa de presentación

- **Modo terminal**: formato tabular con colores ANSI
- **Modo JSON**: salida estructurada para pipelines
- **Modo monitor**: loop con `sleep_for` y `CLEAR` + `CURSOR_HIDE`

## Decisiones técnicas

### ¿Por qué mmap para /proc?
- /proc expone archivos virtuales de tamaño conocido
- `mmap` evita buffers intermedios vs `read()` tradicional
- Zero-copy: `string_view` apunta directamente a la memoria mapeada
- El kernel popula los datos bajo demanda

### ¿Por qué `std::expected` en vez de excepciones?
- Consistente con APIs Linux que retornan códigos de error
- Mejor rendimiento en paths fríos (error)
- Composable con `.value_or()`, `transform()`, `and_then()`

### ¿Por qué C++23?
- `std::expected` estable (GCC 14)
- `std::println` para output type-safe
- `std::format` para string formatting
- Concepts para constraints en templates (future extensibility)

### ¿Por qué FetchContent en vez de vcpkg/Conan?
- Zero setup: `cmake --preset debug` + `cmake --build --preset debug`
- Ideal para proyectos autónomos (sin toolchain managers)
- vcpkg.json incluido como alternativa

## Extensiones futuras

- [ ] Servicio systemd para logging periódico
- [ ] SQLite backend para histórico de métricas
- [ ] Alerta por umbrales configurables
- [ ] gRPC endpoint para consulta remota
- [ ] Soporte para /sys/class/power_supply/ (batería)
- [ ] Soporte para /proc/net/dev (tráfico de red)
- [ ] Soporte para /proc/stat (carga de CPU)
