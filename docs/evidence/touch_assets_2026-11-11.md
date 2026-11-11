# Assets táctiles — comprobación del 2026-11-11

**Estado: Implementado (gráficos), HUD Pendiente.**

- Inspección visual de `design/touch_hud/preview.jpg` a partir de los PNG.
- Comprobación con Pillow bajo `worksafe.run`: 34 botones con 2 estados y 2
  piezas de joystick, 70 PNG en total; todos RGBA de 256 × 256, con esquina
  transparente y contenido visible. Los dos estados de cada botón tienen
  valores RGB distintos. La placa original es RGBA de 1261 × 1247, con esquina
  transparente.
- No se ha ejecutado una prueba en Android ni se ha verificado interacción,
  legibilidad en pantalla real, distribución, ni conexión a la entrada.

El catálogo, la fuente de los controles y el procedimiento de regeneración están
en `design/touch_hud/README.md`. El registro de ejecución local es
`logs/verify_touch_assets.log`.
