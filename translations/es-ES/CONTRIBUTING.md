# Cómo colaborar

> [English](../../CONTRIBUTING.md) | [中文](../zh-CN/CONTRIBUTING.md) | [Français](../fr-FR/CONTRIBUTING.md) | **Español** | [日本語](../ja-JP/CONTRIBUTING.md) | [العربية](../ar-MA/CONTRIBUTING.md) | [Українська](../uk-UA/CONTRIBUTING.md)

LiberNT agradece los informes de errores, las pruebas, las correcciones, las nuevas funciones y la documentación. Esta página explica cómo enviar tu trabajo. Las reglas de ingeniería que sigue cada cambio están en [AGENTS.md](../../AGENTS.md), y se aplican por igual a los colaboradores humanos y a las herramientas de IA.

- [¿Qué hacer?](#qué-hacer)
- [¿Cómo contribuir?](#cómo-contribuir)
- [Derechos de autor y licencias](#derechos-de-autor-y-licencias)
- [¿Por dónde empezar?](#por-dónde-empezar)

**Aviso legal:** si has visto código fuente propietario de Microsoft Windows, tu contribución no se aceptará por una posible infracción de derechos de autor. Antes de contribuir, debes afirmar que lo siguiente es cierto:
>Juro que no he usado ni visto el código fuente de ninguna versión del sistema operativo Windows
>ni de ningún producto de Microsoft que pueda estar relacionado con el proyecto propuesto y que esté bajo una licencia incompatible
>con la contribución a LiberNT, incluido, entre otros, el código fuente filtrado de Windows NT 3.5, NT 4 y 2000
>y el Windows Research Kernel.

LiberNT se construye en sala limpia: a partir de las cabeceras del Windows SDK y del WDK, los símbolos públicos de Windows 11, la documentación de Microsoft y ejecuciones de caja negra en Windows 11. Consulta [Fuentes de verdad](../../AGENTS.md#sources-of-truth) para saber qué se puede usar y qué no.

## ¿Qué hacer?

### Informar de errores

Informa de los errores en [Issues](https://github.com/LiberNT/LiberNT/issues). Indica la arquitectura (amd64, i386, arm64 o riscv64), el tipo de compilación, el commit que compilaste y los pasos que llevan al problema. Un registro de depuración del puerto serie ayuda mucho.

### Corregir errores

Corrige el comportamiento de fondo, no el síntoma, y haz el cambio correcto más pequeño. Una corrección solo cuenta cuando un arranque real o una ejecución de pruebas demuestra que funciona.

### Corregir pruebas

Las pruebas comparan LiberNT con el comportamiento de Windows. Muchas de ellas tampoco pasan limpiamente en Windows 11, así que antes de cambiar LiberNT para que pase una prueba, ejecuta la misma prueba en Windows 11 y compara los resultados comprobación por comprobación. Si Windows 11 también falla una comprobación, el error está en lo que la prueba espera. Consulta [Pruebas frente a Windows 11](../../AGENTS.md#testing-against-windows-11).

### Probar LiberNT

Ejecuta LiberNT en una máquina virtual o en hardware real e informa de lo que falla. El hardware real es lo que más importa para los controladores: una máquina virtual solo demuestra que un controlador se instala y se carga.

### Implementar cosas nuevas

Windows 11 es la referencia de compatibilidad. Antes de empezar un trabajo grande, pregunta en [Issues](https://github.com/LiberNT/LiberNT/issues) o en [Discord](https://discord.gg/BpcKg7FZ2) si alguien ya está trabajando en ello.

### Escribir documentación

Las guías y las notas de diseño van en la [Wiki](https://github.com/LiberNT/LiberNT/wiki). Respeta las prácticas de [ingeniería inversa en sala limpia], y escribe solo lo que muestran el código fuente, la documentación o una ejecución de pruebas.

### Código de terceros

Los componentes sincronizados con Wine figuran en [media/doc/WINESYNC.txt](../../media/doc/WINESYNC.txt), y el resto del código de terceros en [media/doc/3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt). Mantén los cambios de LiberNT en ese código dentro de `#ifdef __REACTOS__`, con el código original en la rama `#else`, para que la siguiente sincronización los conserve.

## ¿Cómo contribuir?

Te recomendamos usar el inglés en los informes de errores y en las discusiones de la comunidad, para que la comunicación sea eficiente.

### Pull requests

Envía tu trabajo como una [pull request] contra la rama `main-nt10`. Las pull requests te permiten informar a los demás de los cambios que has subido a un repositorio de GitHub. Una vez abierta una pull request, puedes discutir y revisar los cambios con el equipo y añadir commits de seguimiento antes de que se fusionen.

Antes de abrirla:

- Compila amd64, i386 y arm64, no solo la arquitectura en la que trabajas.
- Arranca o ejecuta las pruebas que cubren tu cambio, e indica en la pull request qué has ejecutado.
- Mantén el código específico de una arquitectura en la capa de arquitectura. Consulta [Neutralidad de arquitectura](../../AGENTS.md#architecture-neutrality).

### Estilo de los commits

Nuestro estilo de commits está definido en una __[plantilla de commit]__. Úsala como referencia o actívala con `git config commit.template .gitmessage`. Así esta plantilla será el mensaje inicial de los nuevos commits de tu repositorio local. Las reglas completas están en [Cambios y commits](../../AGENTS.md#changes-and-commits).

### Uso de la IA

Puedes usar herramientas de IA. Dinos en la pull request en qué partes te han ayudado y cómo las has probado. El código escrito por IA sigue las mismas reglas que cualquier otro código y lo revisa una persona.

### Reglas y recomendaciones

- *Usa tu __nombre real__ y tu __correo electrónico real__.* ¡No aceptamos contribuciones de código anónimas!
  - Cada commit que cambie código o traducciones debe llevar el nombre legal completo del autor (en letras latinas, se permiten los diacríticos).
  - Se recomienda tener en el perfil de GitHub (en el [campo Name de aquí][GitHub Profile Settings]) el mismo nombre completo que figura en los commits.
- Hay una excepción para los cambios de recursos, como cambios de arte (fondos de pantalla, temas, iconos, sonidos) y la documentación fuera del código.
  - En estos casos concretos se permite usar un apodo o alias como nombre de autor, y se recomienda tener en el perfil de GitHub (en el [campo Name de aquí][GitHub Profile Settings]) el mismo nombre que figura en los commits.
- En cualquier caso, el autor debe usar una dirección de correo electrónico real, tanto en los commits de git (ajuste `user.email`) como en la [configuración de correo][GitHub Email Settings] de GitHub: la casilla "Keep my email addresses private" debe estar desmarcada.
  - Para *proteger tu privacidad*, elige la "Primary email address" adecuada que se aplicará a tus commits en la [configuración de correo][GitHub Email Settings] de GitHub.
- *Describe bien tu contribución.* Incluye el número de la issue correspondiente si procede.
- *Incluye solo cambios relacionados.* Facilita la revisión, ya que el revisor necesita recordar menos información sobre el código existente que se modifica.
- *Busca pull requests similares antes de enviar la tuya.* Puede que ya se haya abierto una pull request o una issue parecida. En ese caso, comenta y revisa esa.
- *Mantén tu contribución pequeña y centrada en el tema.* Resiste la tentación de corregir problemas no relacionados que encuentres por el camino; abre una issue para ellos.
- *Respeta nuestro __[estilo de código]__.*
- *No tengas miedo de preguntar.* Pregunta al equipo en [Discord](https://discord.gg/BpcKg7FZ2) o en [Matrix](https://matrix.to/#/#libernt:matrix.org).

## Derechos de autor y licencias

Conservas los derechos de autor de lo que escribes. LiberNT no tiene cesión de derechos de autor ni acuerdo de licencia de colaborador.

- Los archivos nuevos empiezan con la cabecera que se muestra en [Estilo de código](../../AGENTS.md#code-style), con tu nombre y tu correo en la línea `COPYRIGHT` y un identificador de licencia SPDX.
- Si haces un cambio importante en un archivo existente, puedes añadir tu propia línea `COPYRIGHT` debajo de las existentes.
- Nunca elimines ni cambies un aviso de derechos de autor o de licencia existente, y nunca edites los textos de licencia del directorio raíz.
- Conserva la licencia de los archivos que cambias. El código heredado de ReactOS conserva la licencia indicada en su cabecera.
- Nunca copies código cuya licencia sea incompatible con la GNU GPL 3.0, y nunca copies código GPL de Linux.

Consulta [CREDITS.md](../../CREDITS.md) para conocer a las personas que hay detrás de LiberNT y ReactOS.

## ¿Por dónde empezar?

- Mira las [Issues](https://github.com/LiberNT/LiberNT/issues) abiertas.
- Busca una prueba que falle, compárala con Windows 11 y haz que pase.
- Lee la [Wiki](https://github.com/LiberNT/LiberNT/wiki) para encontrar guías y notas de diseño.
- Pide ayuda en [Discord](https://discord.gg/BpcKg7FZ2) o en [Matrix](https://matrix.to/#/#libernt:matrix.org).

  [ingeniería inversa en sala limpia]:                           https://en.wikipedia.org/wiki/Clean_room_design
  [pull request]:                                                https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests
  [GitHub Profile Settings]:                                     https://github.com/settings/profile
  [GitHub Email Settings]:                                       https://github.com/settings/emails
  [estilo de código]:                                            CODING_STYLE.md
  [plantilla de commit]:                                         ../../.gitmessage

# Véase también

- [Reglas para gestionar las pull requests](PULL_REQUEST_MANAGEMENT.md)
- [Código de conducta](CODE_OF_CONDUCT.md)
