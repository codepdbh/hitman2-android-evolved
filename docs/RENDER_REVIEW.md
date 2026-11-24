# Revisión del renderizado: sombras y mejoras posibles (2026-11-20)

**Estado: Parcial.** Es un análisis de las llamadas que hace el juego y dos cambios pequeños **compilados pero no
probados en el teléfono** (el S25 no estaba conectado). No he visto una sombra mal dibujada: lo que sigue es lo que se
puede afirmar sin verla y lo que falta comprobar.

## Qué se midió

`python scripts/boot_probe.py --scene "C0-1/C0-1__LOAD.gms" --frames 3000` ejecuta la primera misión en el PC con el backend
nulo, que ahora imprime un histograma de combinaciones de estado por dibujo (`DRAWSTATE`). En esos 3000 fotogramas:
~104 dibujos por fotograma, ~15 000 triángulos por fotograma, 509 texturas vivas, 27 combinaciones distintas de estado.

| Hecho medido | Consecuencia |
|---|---|
| Ni `glReadPixels`, ni `glDrawPixels`, ni `glTexSubImage2D`, ni stencil (el renderizador no importa funciones de stencil) | Las sombras **no** son mapas de sombra por lectura del framebuffer ni volúmenes con stencil |
| `glTexGenf` solo con modo `SPHERE_MAP` en la unidad 1 y esa unidad activa en ~2900 dibujos (aditivos, con luz) | El texgen es para reflejos, **no** para proyección de sombras |
| `glPolygonMode` siempre en `FILL` en los dibujos | No se usa alambre, nada que perder ahí |
| Muchos dibujos con mezcla `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, `glDepthMask(false)` y función de profundidad `ALWAYS` o `LEQUAL` | Compatible con sombras «de mancha» (polígonos oscuros sobre el suelo), pero **no está identificado** cuál de estos dibujos son sombras |

Lo que **no** se sabe: cómo dibuja el juego la sombra del personaje, ni si en el teléfono se ve mal. Sin capturas del
teléfono no se puede decir más. Con el S25 conectado: `python scripts/boot_probe.py --no-build --android <serie> --scene
"C0-1/C0-1__LOAD.gms" --budget 3000000000 --frames 20000 --shot 5000` guarda un fotograma de la misión (se puede repetir con
otros números para comparar).

## Cambios hechos (compilan; sin probar en el teléfono)

1. `invariant gl_Position` en el sombreador de vértices: garantiza que dos dibujos de la misma geometría den la misma
   profundidad. Los dibujos de sombras o calcomanías coplanares con el suelo (`LEQUAL`) pueden parpadear o desaparecer
   a trozos si la profundidad difiere por redondeo entre dibujos.
2. Superficie con multimuestreo 4x cuando el dispositivo la ofrece (el `Hitman2.ini` pide `Antialias 4`), con la selección
   anterior como alternativa. Suaviza bordes dentados, incluidos los bordes de sombras proyectadas.

## Candidatos a mejora, por orden de utilidad esperada (ninguno hecho todavía)

1. **Rampa de gamma.** El juego llama a `SetDeviceGammaRamp` y el `.ini` tiene `GammaValue 0.632911`; el backend GLES la
   ignora (`gb_set_gamma` no hace nada). La imagen puede salir más oscura o más clara que la original, y eso cambia mucho
   cómo se perciben las sombras. Hace falta dibujar a un framebuffer propio y aplicar la rampa en una pasada final.
   Es el primer candidato, pero necesita pruebas en el dispositivo.
2. **Resolución interna.** Hoy 800×450 escalada a 2340×1080: borrosa. Subir a 1280×720 o más cambia el `Resolution` del
   perfil y las constantes del HUD; no se ha probado qué hace la interfaz del juego con otra resolución.
3. **Texturas comprimidas y filtrado.** `DisableDXT 1` y `Anisotropy 1` del perfil: el filtrado anisótropo no se anuncia.
4. **Entorno de textura `GL_BLEND`** (se dibuja como `MODULATE`) y **sombreado plano**: avisados una vez en el registro;
   en esta misión no aparecen.
5. **Rendimiento:** ~625 000 llamadas de dibujo en 6000 fotogramas, cada una sube uniformes y copia vértices; se puede
   agrupar si el teléfono no mantiene la fluidez.

Nada de esto cambia los resultados de las pruebas diferenciales ni el código recompilado.

## Soporte de drivers de GPU (2026-11-24)

**Estado: Compilado; sin probar en el teléfono.** Lo único que se ha ejecutado de verdad en una GPU es el S25 (el mismo
sombreador completo de siempre); los cambios siguientes no se han ejecutado en ningún dispositivo.

Hecho (todo basado en lo que el driver informa, sin tablas de «manías» por fabricante que no se han medido):

1. **Escalera de sombreadores** (`renderer/gl_gles3_backend.c`): variante 0 completa; 1 sin `invariant`; 2 reducida (`mediump`
   en fragmento y 4 luces, avisada como degradada). Si un driver rechaza una variante al compilar o enlazar se prueba la
   siguiente en vez de abandonar. Los perfiles de compatibilidad (1 Mali, 2 Unisoc) arrancan en la variante 1. La variante en
   uso se registra y aparece en la información de GPU del launcher.
2. **Informe de capacidades del driver** al arrancar: versión de GLSL, vectores uniformes de vértice y fragmento, varyings,
   y si hay `highp` en fragmento; avisa si hay menos de 96 vectores uniformes de vértice (el sombreador necesita unos 80).
3. **`glInvalidateFramebuffer` de profundidad y stencil antes de cada presentación**: en GPU de teselas (Mali, Adreno, PowerVR)
   evita escribirlos a memoria; en el resto no hace nada.
4. Multimuestreo 4x si la configuración existe y, si no, la selección anterior (arriba).

No hecho: cambios por GPU concretos, caché de estado de texturas para reducir sobrecarga del driver, y pruebas en Mali, PowerVR
o Unisoc reales. Para añadirlos hace falta un dispositivo con esa GPU y su registro (`GLES driver:` y `shader variant`).
