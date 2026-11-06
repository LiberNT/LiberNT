# Règles de gestion des pull requests

> [English](../../PULL_REQUEST_MANAGEMENT.md) | [中文](../zh-CN/PULL_REQUEST_MANAGEMENT.md) | **Français** | [Español](../es-ES/PULL_REQUEST_MANAGEMENT.md) | [日本語](../ja-JP/PULL_REQUEST_MANAGEMENT.md) | [العربية](../ar-MA/PULL_REQUEST_MANAGEMENT.md) | [Українська](../uk-UA/PULL_REQUEST_MANAGEMENT.md)

Afin de garder un nombre acceptable de PR ouvertes mais inactives, il convient de tenir compte des règles suivantes :
- Si une PR a au moins une approbation, elle peut être fusionnée après une semaine d'attente d'éventuels commentaires supplémentaires.
  - Si la modification a au moins 3 approbations ou si vous la jugez suffisamment triviale, elle peut être fusionnée immédiatement.
- Si une PR reste trop longtemps à l'état "changes requested" et que rien n'indique que l'auteur y travaille, elle sera fermée.
  - À titre indicatif : 2 semaines pour une petite PR. Cela peut être plus long si la PR est importante.
  - La PR peut être rouverte à tout moment, si vous avez d'autres commentaires ou si de nouvelles modifications ont été faites.
- Si vous avez besoin de la relecture d'une personne précise, assignez-lui la PR. Ne vous fiez pas seulement à la fonction "review requested" de GitHub.
- N'oubliez pas que les étiquettes de PR existent. Vous pouvez attribuer une étiquette appropriée à une pull request pour indiquer sa portée, attirer davantage l'attention ou simplement faciliter la navigation.
- Ne vous sentez pas obligé de commenter tout ce que vous voyez juste pour commenter. Que ce soit sur GitHub, Discord ou Matrix.

Par ailleurs, pour ne pas paraître impoli envers des contributeurs de bonne volonté, évitez :
- De demander au contributeur un travail sans rapport
- De fermer sans donner de raison
- De fusionner avec l'intention de réécrire ce code peu après

Avant de fusionner une PR, assurez-vous qu'elle respecte les [règles de contribution](CONTRIBUTING.md#règles-et-recommandations), mais surtout :
- Assurez-vous que l'auteur a indiqué une vraie adresse e-mail dans tous les commits de la PR
- Si la PR contient du code ou des traductions, assurez-vous que l'auteur n'a pas indiqué de pseudonyme ou d'alias, mais son nom légal complet, dans tous les commits de la PR
- Si la PR contient des ressources (fonds d'écran, thèmes, icônes, sons) ou de la documentation hors code, assurez-vous que l'auteur a indiqué son nom ou son alias dans tous les commits de la PR
- Si la PR mêle du code et des modifications de ressources, traitez-la comme une PR avec du code
- Remarques importantes avant d'utiliser la stratégie "Squash and merge" sur une PR :
  - Assurez-vous que le nom de l'auteur dans son profil GitHub correspond à celui des commits. Si ce n'est pas le cas, demandez à l'auteur de le régler en conséquence.
  - Si l'auteur ne souhaite pas indiquer son nom dans son profil GitHub :
    - Il faut ajouter l'étiquette "no squash merge" à la PR.
	- Assurez-vous que chaque message de commit est correctement formaté, comme dans [.gitmessage](../../.gitmessage).
	- Enfin, dans ce cas, la PR doit être fusionnée soit avec la stratégie "Rebase and merge", soit manuellement.
  - En appuyant sur le bouton "Squash and merge" d'une PR, vous pouvez vérifier que l'auteur n'utilise pas d'adresse no-reply :
  sous le message de commit apparaîtra le texte : `This commit will be authored by <address@email.com>`
