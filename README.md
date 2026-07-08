# linux-sysmon — Linux System Monitor

Monitor de sistema Linux de bajo nivel que lee información hardware usando APIs del kernel: **sysfs**, **procfs**, **ioctl**, **mmap** y **statfs**.

## Features

- ✅ **CPU**: modelo, núcleos físicos/lógicos, frecuencia desde `/proc/cpuinfo`
- ✅ **Memoria**: total, disponible, usada (%) vía `mmap(/proc/meminfo)`
- ✅ **Temperatura**: sensores térmicos desde `/sys/class/thermal/*` (sysfs)
- ✅ **Discos**: uso por punto de montaje vía `statfs(2)`
- ✅ **Uptime**: desde `/proc/uptime` con formato días/hh:mm:ss
- ✅ **Hostname + Kernel**: `gethostname(2)` + `uname(2)`
- ✅ **Modo monitor**: refresco continuo tipo `watch`
- ✅ **Salida JSON**: integrable con scripts y herramientas
- ✅ **Colores ANSI**: resaltado por umbrales (temperatura, uso de disco/memoria)
- ✅ **RAII en todo**: Fd, MmapRegion, Database pattern
- ✅ **Error handling**: `std::expected` sin excepciones
- ✅ **C++23**: concepts, ranges, string_view, std::println, std::format
- ✅ **Logging**: spdlog estructurado por colores
- ✅ **Tests**: Catch2 v3 con tests unitarios, integración y benchmarks

## Stack tecnológico

| Componente     | Tecnología                          |
|----------------|--------------------------------------|
| Lenguaje       | C++23 (GCC 14)                      |
| Build          | CMake 3.28+ / Ninja                 |
| Logging        | spdlog (FetchContent)               |
| Tests          | Catch2 v3 (FetchContent)            |
| APIs Linux     | open/read/mmap (procfs), statfs(2), |
|                | ioctl(TIOCGWINSZ), sysfs            |
| Salida         | Terminal ANSI / JSON                |
| Contenedor     | Docker multi-stage (gcc:14)         |

## Requisitos

- **Compiler**: GCC 14+ (C++23 completo: std::expected, std::println)
- **CMake**: ≥ 3.28
- **Ninja**: recomendado
- **Sistema**: Linux (lee /proc, /sys)

## Compilación

```bash
# Usando CMake presets (recomendado)
cmake --preset debug
cmake --build --preset debug

# O manualmente
mkdir -p build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .

# Solo tests
cmake --build --preset debug --target test_sysmon
ctest --preset debug
```

## Uso

```bash
# Vista única
./build/debug/linux-sysmon

# Modo monitor continuo (✓)
./build/debug/linux-sysmon --monitor

# Con intervalo personalizado
./build/debug/linux-sysmon -m --interval 5

# Salida JSON
./build/debug/linux-sysmon --json

# Sin colores
./build/debug/linux-sysmon --no-color

# Ayuda
./build/debug/linux-sysmon --help
```

### Docker

```bash
# Construir
docker build -t linux-sysmon .

# Ejecutar (necesita --privileged para /proc y /sys completos)
docker run --rm -it --privileged linux-sysmon --monitor
```

## Arquitectura

Ver [docs/architecture.md](docs/architecture.md) para diagramas y decisiones técnicas.

## Tests

```bash
# Tests unitarios + integración + benchmarks
cmake --build --preset debug
ctest --preset debug -V

# Solo benchmarks
./build/debug/test_sysmon "[benchmark]"
```

## Estructura del proyecto

```
2026-07-08-linux-sysmon/
├── CMakeLists.txt            # Build C++23, FetchContent
├── CMakePresets.json         # Debug (ASan+UBSan), Release (LTO)
├── Dockerfile                # Multi-stage gcc:14
├── vcpkg.json                # Manifest mode
├── .gitignore / .env.example
├── README.md
├── include/linux_sysmon/
│   ├── sysmon.hpp            # Tipos principales, Result<T>
│   ├── procfs_reader.hpp     # mmap /proc/*, RAII Fd/MmapRegion
│   ├── sysfs_reader.hpp      # sysfs thermal zones
│   ├── terminal.hpp          # ioctl + ANSI escapes
│   ├── disk_info.hpp         # statfs(2) disk usage
│   └── version.hpp
├── src/
│   └── main.cpp              # CLI + display + main loop
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp         # Unit + integration + benchmark
└── docs/
    └── architecture.md
```

## Licencia

MIT
