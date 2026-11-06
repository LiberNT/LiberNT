# Reglas para gestionar las pull requests

> [English](../../PULL_REQUEST_MANAGEMENT.md) | [中文](../zh-CN/PULL_REQUEST_MANAGEMENT.md) | [Français](../fr-FR/PULL_REQUEST_MANAGEMENT.md) | **Español** | [日本語](../ja-JP/PULL_REQUEST_MANAGEMENT.md) | [العربية](../ar-MA/PULL_REQUEST_MANAGEMENT.md) | [Українська](../uk-UA/PULL_REQUEST_MANAGEMENT.md)

Para mantener un número aceptable de PR abiertas pero inactivas, conviene tener en cuenta las siguientes reglas:
- Si una PR tiene al menos una aprobación, se puede fusionar tras 1 semana de espera por si llegan más comentarios.
  - Si el cambio tiene al menos 3 aprobaciones o lo consideras lo bastante trivial, se puede fusionar de inmediato.
- Si una PR se queda demasiado tiempo en "changes requested" y el autor no indica que esté trabajando en ella, se cerrará.
  - Como referencia: 2 semanas para una PR pequeña. Puede ser más si la PR es grande.
  - La PR se puede reabrir en cualquier momento, si tienes más comentarios o se han hecho cambios nuevos.
- Si necesitas la revisión de una persona concreta, asígnale la PR. No te fíes solo de la función "review requested" de GitHub.
- Recuerda que existen las etiquetas de PR. Puedes asignar una etiqueta adecuada a una pull request para indicar su alcance, atraer más atención o simplemente facilitar la navegación.
- No te sientas obligado a comentar todo lo que ves solo por comentar. Ni en GitHub, ni en Discord ni en Matrix.

Además, para no parecer descortés con los colaboradores que ayudan, evita:
- Pedir al colaborador que haga trabajo no relacionado
- Cerrar sin dar un motivo
- Fusionar con la intención de reescribir ese código poco después

Antes de fusionar una PR, asegúrate de que sigue las [reglas de contribución](CONTRIBUTING.md#reglas-y-recomendaciones), pero sobre todo:
- Asegúrate de que el autor ha indicado un correo electrónico real en todos los commits de la PR
- Si la PR contiene código o traducciones, asegúrate de que el autor no ha usado un apodo o alias, sino su nombre legal completo, en todos los commits de la PR
- Si la PR contiene recursos (fondos de pantalla, temas, iconos, sonidos) o documentación fuera del código, asegúrate de que el autor ha indicado su nombre o alias en todos los commits de la PR
- Si la PR mezcla código con cambios de recursos, trátala como una PR con código
- Notas importantes antes de usar la estrategia "Squash and merge" en una PR:
  - Asegúrate de que el nombre del autor en su perfil de GitHub coincide con el de los commits. Si no es así, pide al autor que lo ajuste.
  - Si el autor no quiere poner su nombre en el perfil de GitHub:
    - Hay que añadir la etiqueta "no squash merge" a la PR.
	- Asegúrate de que cada mensaje de commit tiene el formato correcto, como en [.gitmessage](../../.gitmessage).
	- Por último, en este caso la PR debe fusionarse con la estrategia "Rebase and merge" o manualmente.
  - Al pulsar el botón "Squash and merge" de una PR puedes comprobar que el autor no usa un correo no-reply:
  debajo del mensaje de commit aparecerá el texto: `This commit will be authored by <address@email.com>`
