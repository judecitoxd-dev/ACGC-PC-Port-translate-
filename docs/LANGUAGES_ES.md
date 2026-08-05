# Sistema de idiomas externos para PC

## Objetivo

El port usa la ROM estadounidense compatible como base y nunca la modifica.
`English` lee los recursos originales. `Español` y los idiomas personalizados
pueden reemplazar bancos de texto completos desde `languages/<codigo>/aram/`.

## Selección

En `settings.ini`:

```ini
[Language]
language = en
```

Valores iniciales del menú:

- `en`: English, texto original de la ROM.
- `es`: Español, carga `languages/es/`.

También se acepta un código personalizado seguro, por ejemplo `pt_BR`, editando
`settings.ini` y creando `languages/pt_BR/`.

Cambiar de idioma requiere reiniciar el juego porque las direcciones de los
bancos se guardan durante la inicialización de ARAM.

## Recursos compatibles

Cada banco y su tabla deben estar juntos:

- `message_data.bin` + `message_data_table.bin`: diálogos principales.
- `select_data.bin` + `select_data_table.bin`: respuestas y elecciones.
- `string_data.bin` + `string_data_table.bin`: fechas, nombres y cadenas cortas.
- `mail_data.bin` + `mail_data_table.bin`: correo principal.
- `maila`, `mailb`, `mailc`, `ps`, `psz`, `super` y `superz`: cartas especiales.
- `npc_name_str_table.bin`: tabla independiente de nombres de NPC.

Un par ausente o inválido vuelve automáticamente al recurso inglés de la ROM.
No se mezclan una tabla española y un banco inglés.

## Por qué funcionan los códigos del diálogo

Los archivos externos conservan el mismo formato binario que el juego. El
intérprete original sigue procesando los comandos `MSGEND`, `MSGCONTINUE`,
`OPENCHOICE`, `STR_PLAYERNAME`, nombres de objetos, expresiones, sonidos,
colores y saltos de página. El port sustituye el origen de los bytes, no el
intérprete.

`tools/msg_tool.py` fue corregido para preservar los IDs numéricos, detectar
entradas duplicadas, exigir un código de terminación y evitar el fallo de
`find()` que cerraba entradas por error.

## Tipografía española

La tabla de caracteres existente ya incluye `¡`, `¿`, vocales acentuadas,
`ñ`, `ü` y sus mayúsculas. La primera versión usa la fuente original. Una
fuente externa futura deberá mantener el mismo orden de glifos y métricas.

## Límites que requieren pruebas adicionales

- Artículos, género y plural insertados dinámicamente por la lógica inglesa.
- Texto dibujado dentro de texturas.
- Diferencias de IDs entre GAFE/GAFU y GAFP (Europa Multi5).
- Líneas españolas más largas que el cuadro de diálogo.

Por ello, importar la traducción europea exige comparar IDs y códigos antes de
generar el paquete español final. No es seguro copiar el DOL o el REL europeo.
