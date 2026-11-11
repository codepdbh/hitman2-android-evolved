# Assets táctiles — Hitman 2: Android Evolved

**Estado: Implementado (assets); HUD funcional: Pendiente.** Son gráficos independientes,
no controles conectados ni una prueba de jugabilidad. La vista de conjunto está en
`preview.jpg`. Se dibujaron pictogramas originales sobre una placa generada con la
herramienta integrada de imágenes; no se usaron archivos del juego.

Los archivos listos para Android están en `android/app/src/main/assets/touch_hud/`:
34 botones, cada uno con variantes `*_normal.png` y `*_pressed.png`, más
`joystick_base.png` y `joystick_knob.png`. Todos son PNG RGBA de 256 × 256 píxeles.
La placa original de alta resolución está en `button_plate.png`; se puede regenerar
el paquete con `tools/generate_touch_assets.py` y Pillow, bajo WORK_SAFE_MODE. El
centro de la placa es opaco: el HUD debe ajustar la opacidad al dibujar si se
desea ver la escena a través de los botones. La vista previa no forma parte del APK.

## Acciones previstas

Equivalencias predeterminadas documentadas para PC, no enlaces implementados. El
juego permite reasignar controles, así que estos valores no son una detección de la
configuración activa. Fuente: [manual para PC distribuido en Steam, páginas 12, 16–18](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/6850/manuals/manual_en.pdf?t=1732293077),
y [manual de PC con detalle del inventario rápido](https://manualmachine.com/gamespc/hitman2silentassassin/1118255-user-manual/).

| Asset | Función prevista | Entrada predeterminada |
|---|---|---|
| joystick_base + joystick_knob | Movimiento en cuatro direcciones y diagonales | W / A / S / D |
| camera_drag | Indicador de zona de arrastre para mirar | Movimiento relativo del ratón |
| run / run_toggle | Correr mientras se mantiene / alternar carrera | Shift izquierdo / Bloq Mayús |
| crouch / sneak | Agacharse mientras se mantiene / alternar sigilo | Ctrl izquierdo / Espacio |
| lean_left / lean_right | Inclinarse; doble toque y mantener para salir | Z / C |
| fire | Disparar o usar objeto actual | Botón izquierdo del ratón |
| action | Acción contextual | E, mantener para escoger y soltar para ejecutar |
| action_next / action_previous | Elegir acción contextual siguiente / anterior | Rueda arriba / abajo, mientras se mantiene E |
| inventory | Inventario | Botón derecho del ratón |
| holster / quick_inventory | Enfundar o desenfundar / inventario rápido | Q, toque / mantener y seleccionar con ratón |
| reload / drop | Recargar / soltar arma u objeto | R / G |
| melee / pistol / smg / rifle | Accesos a categorías de armas | 1 / 2 / 3 / 4, repetir para recorrer |
| binoculars / night_vision / lockpick | Herramientas | 5 / 6 / 8 |
| map / briefing | Mapa / informe de misión | M / B |
| camera | Cambiar primera / tercera persona | F1 |
| zoom_in / zoom_out | Acercar / alejar | Rueda arriba / abajo |
| pause | Menú del juego, volver | Esc |
| nav_up / nav_down / nav_left / nav_right / accept | Navegación de menús | Flechas / Enter |

La rueda comparte función entre zoom y selección contextual. Los assets no deciden
qué función tiene prioridad; el futuro HUD tendrá que conservar la pulsación de E
mientras se cambia la selección y proporcionar interacción multitáctil. Q también
requiere mantener y soltar para seleccionar el inventario rápido. Los menús admiten
cursor y clic; las flechas no sustituyen por sí solas una implementación de cursor.
Al cancelar un toque o perder el foco se deberán liberar las entradas mantenidas.
El tamaño visual, el área táctil, la opacidad y la distribución deben validarse en
dispositivos y adaptarse a recortes y zonas seguras. Ninguna de esas conductas está
implementada ni probada por este paquete.

## Origen y reproducción

Placa: generación integrada de imágenes, con este encargo exacto:

> Use case: stylized-concept. Asset type: reusable base plate for an Android stealth game's touch control icons. Create ONE isolated circular button background centered, perfectly round, straight-on and symmetrical, no pictogram, no letters. A restrained 2002 espionage aesthetic: translucent smoked charcoal glass interior, very thin brushed gunmetal double rim, extremely subtle deep crimson accent at bottom, softly polished, high contrast edge, refined premium game HUD, clean central area suitable for overlaying a white icon. Entire circle occupies about 85 percent of the canvas, ample clear padding. Genuine transparent background outside the circle; no cast shadow outside, no checkerboard, no watermark, no surrounding interface.

Los pictogramas y variantes se generan localmente con
`tools/generate_touch_assets.py` y Pillow. Ejecutar a través de `worksafe.run`, por
ejemplo desde un proceso Python del proyecto:

```python
import sys
sys.path.insert(0, 'scripts')
import worksafe
worksafe.run([sys.executable, 'tools/generate_touch_assets.py'],
             log_name='generate_touch_assets', timeout=120, check=True)
```
