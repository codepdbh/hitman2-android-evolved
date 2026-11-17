# Controles táctiles compactos

Estado: **Parcial** — implementación, ocho pruebas de lógica y comprobación inicial en una misión del S25. Reentrada tras retirar la superficie: **Probado** en el caso registrado. Evidencia: [prueba en S25](evidence/s25_resume_2026-11-17.md) y [revisión inicial](evidence/touch_hud_2026-11-17.md).

## Qué cambió

El diseño anterior mostraba ocho botones en misión, además del joystick: disparar, acción, agacharse, correr, inventario, pausa, panel y ajustes. El panel mezclaba más de veinte acciones en una cuadrícula sin etiquetas ni categorías. Al tocar fuera podía iniciarse el movimiento de cámara, y ocultar el panel podía dejar acciones inaccesibles.

El diseño compacto muestra **cinco botones y el joystick** (tres botones menos, reducción del 37,5 %):

| Control | Uso |
|---|---|
| Joystick izquierdo | Caminar; estirar hacia delante más allá del borde para correr |
| Disparar | Mantener pulsado |
| Acción | Mantener pulsado; conserva la tecla E de la instalación |
| Agacharse | Mantener pulsado |
| Pausa | Abrir/volver al menú del juego |
| ⋯ EQUIPO | Abrir las acciones secundarias y los ajustes |

El joystick activa Shift al superar 1,18 radios hacia delante y lo suelta al volver por debajo de 0,96 radios o dejar de avanzar. Esta diferencia evita alternancias cerca del borde. La ayuda aparece sobre el joystick mientras se usa. Se puede desactivar esta función y fijar el botón Correr.

## Panel y personalización

- **Equipo:** inventarios, categorías de armas, prismáticos, visión nocturna y ganzúa.
- **Acciones:** recargar, sigilo, inclinarse, soltar, correr y seleccionar acciones.
- **Vista:** mapa, informe, cámara y zoom.

Las celdas llevan icono y texto. El número de columnas y filas se adapta al espacio; el pie permite pasar de página. Tocar fuera cierra el panel y consume ese toque. Abrirlo libera las entradas activas y bloquea el control de cámara a través del panel. Una acción mantenida lo cierra al pulsar para permitir ver el mundo; una acción puntual lo cierra al soltar. El zoom y la selección por rueda permiten varias pulsaciones sin reabrirlo.

En **EQUIPO → Ajustes** se eligen y mueven los botones, se ajustan tamaño y transparencia, se configura correr con el joystick y se restaura el diseño compacto. El panel siempre permanece accesible. Los botones nuevos reciben posiciones separadas antes de entrar en edición.

**EQUIPO → Ajustes → Sensibilidad de cámara** permite ajustar de 0,5× a 8×; por defecto 3× respecto a la versión anterior. Los movimientos lentos conservan las fracciones entre eventos. El botón EQUIPO está centrado arriba para no tapar la barra de salud.

Al salir a otra actividad, el motor se suspende y al regresar se conecta la nueva superficie gráfica sin reiniciar la misión. Comprobado en el S25 tras unos 22 segundos; no implica recuperación de una partida si Android termina el proceso.

La migración cambia solamente el conjunto antiguo por defecto si sus posiciones siguen intactas. Conserva los diseños personalizados, tamaño y opacidad. Si aún aparecen muchos botones por una personalización anterior, usar **Restaurar diseño compacto**.

Los contextos de introducción y menús conservan sus controles. No se inventan detecciones de combate, arma equipada o cinemáticas: el contexto disponible procede de la escena cargada, no del estado detallado del personaje.

## Validación pendiente en Android

1. Entrar en una misión: comprobar cinco botones y espacio libre para mirar.
2. Caminar, correr estirando el joystick y volver a caminar; comprobar diagonales y agacharse.
3. Moverse, mirar y disparar con varios dedos; abrir/cerrar EQUIPO mientras hay entradas activas y comprobar que ninguna queda pulsada.
4. Probar todas las categorías y páginas, recargar, mapa, zoom e inventario mantenido; confirmar que cerrar el panel no dispara ni mueve la cámara.
5. Cambiar tamaño, fijar y mover controles, reiniciar la app y comprobar persistencia. Probar migración del diseño anterior y conservación de uno personalizado.
6. Probar pausa, cancelación de toques y cambio de escena: ninguna pulsación diferida debe reaparecer después.

No se ha ejecutado el juego original en Windows ni se han modificado sus archivos.
