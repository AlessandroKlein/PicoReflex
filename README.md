# Trabajo Práctico Final - Microprocesadores

**Universidad Nacional del Litoral**  
**Tecnicatura en Automatización y Robótica**  
**Año:** 2026  

---

## Introducción

El desarrollo de sistemas basados en microcontroladores permite a los estudiantes explorar el diseño de hardware y software en aplicaciones prácticas. Este trabajo busca combinar habilidades técnicas, creatividad e innovación mediante la implementación de alguna de las ideas propuestas o alguna propia.

---

## Descripción del Trabajo

> **Nota:** Respecto al sistema a implementar, se debe consultar el archivo anexo correspondiente.

A partir de lo implementado, se requerirá la entrega de los siguientes ítems:

### 1. Informe Técnico
El informe debe contemplar los siguientes puntos:

* **a. Límites conceptuales del circuito y la lógica:**  
  Explicar detalladamente los alcances del proyecto (por ejemplo: cantidad de luces/LEDs a utilizar, si se generará sonido, si se utilizará algún pulsador extra para iniciar/terminar el juego o secuencia, etc.).
* **b. Descripción de la solución propuesta:**  
  Describir en detalle la solución implementada, incluyendo circuitos, código fuente y todo material complementario relevante (diagramas, fotos, porciones de código, esquemas circuitales, etc.).
* **c. Conclusión:**  
  Escribir una conclusión que incluya comentarios propios y detalle las dificultades encontradas durante el desarrollo.

### 2. Análisis de Periférico
Elegir **uno** de los periféricos utilizados en el proyecto y detallar en el informe sus aspectos técnicos de funcionamiento, configuración, protocolos aplicados y características relevantes.

### 3. Exposición
Exponer y presentar el trabajo realizado ante la cátedra.

---

## Estado del firmware (v0.9.7)

> Documentación técnica completa en `docs/` (ver `docs/ARQUITECTURA.md`,
> `docs/COMPILAR.md` y `docs/DECISIONES.md`). Historial completo de versiones en
> `CHANGELOG.md`.

- **Modo dormir configurable:** desde la web `/display` → card "Modo dormir
  (ahorro de energía)": botones Habilitar/Deshabilitar + campo "Inactividad
  (minutos)" (1..60). El dispositivo entra en reposo (DORMANT, < 2 mA) tras ese
  tiempo sin actividad y con el Wi-Fi apagado; deshabilitado = nunca duerme.
- **Clave del Access Point:** por defecto es **fija `12345678`** (igual para
  cualquier placa). Se puede cambiar desde `/wifi` (8..15 caracteres, persistida en
  Flash). La clave efectiva se ve en el display OLED (`PASS:xxxx`, línea 7) o por
  serial (`[web] Modo AP: PicoW-ReactionGame / clave xxxx / IP 192.168.4.1`).