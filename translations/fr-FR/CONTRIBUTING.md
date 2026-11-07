# Contribuer à LiberNT

> [English](../../CONTRIBUTING.md) | [中文](../zh-CN/CONTRIBUTING.md) | **Français** | [Español](../es-ES/CONTRIBUTING.md) | [日本語](../ja-JP/CONTRIBUTING.md) | [العربية](../ar-MA/CONTRIBUTING.md) | [Українська](../uk-UA/CONTRIBUTING.md)

LiberNT accueille volontiers les rapports de bogues, les tests, les correctifs, les nouvelles fonctions et la documentation. Cette page explique comment envoyer votre travail. Les règles d'ingénierie que suit chaque modification se trouvent dans [AGENTS.md](../../AGENTS.md), et elles s'appliquent aussi bien aux contributeurs humains qu'aux outils d'IA.

- [Que faire ?](#que-faire)
- [Comment contribuer ?](#comment-contribuer)
- [Droits d'auteur et licences](#droits-dauteur-et-licences)
- [Par où commencer ?](#par-où-commencer)

**Avis juridique :** si vous avez vu du code source propriétaire de Microsoft Windows, votre contribution ne sera pas acceptée en raison d'une possible violation du droit d'auteur. Avant de contribuer, vous devez affirmer que ce qui suit est vrai :
>Je jure par la présente que je n'ai ni utilisé ni vu le code source d'aucune version du système d'exploitation Windows
>ni d'aucun produit Microsoft pouvant être lié au projet proposé et placé sous une licence incompatible
>avec une contribution à LiberNT, notamment, sans s'y limiter, le code source divulgué de Windows NT 3.5, NT 4 et 2000
>et le Windows Research Kernel.

LiberNT est développé en salle blanche : à partir des en-têtes du Windows SDK et du WDK, des symboles publics de Windows 11, de la documentation Microsoft et d'exécutions en boîte noire sous Windows 11. Consultez [Sources de vérité](../../AGENTS.md#sources-of-truth) pour savoir ce qui peut être utilisé ou non.

## Que faire ?

### Signaler des bogues

Signalez les bogues dans les [Issues](https://github.com/LiberNT/LiberNT/issues). Indiquez l'architecture (amd64, i386, arm64 ou riscv64), le type de compilation, le commit que vous avez compilé et les étapes qui mènent au problème. Un journal de débogage du port série aide beaucoup.

### Corriger des bogues

Corrigez le comportement sous-jacent, pas le symptôme, et faites la plus petite modification correcte. Un correctif ne compte que lorsqu'un vrai démarrage ou une exécution de tests montre qu'il fonctionne.

### Corriger des tests

Les tests vérifient LiberNT par rapport au comportement de Windows. Beaucoup ne passent pas non plus proprement sous Windows 11 ; avant de modifier LiberNT pour satisfaire un test, exécutez donc le même test sous Windows 11 et comparez les résultats vérification par vérification. Si Windows 11 échoue aussi à une vérification, c'est l'attente du test qui est fausse. Consultez [Tester par rapport à Windows 11](../../AGENTS.md#testing-against-windows-11).

### Tester LiberNT

Lancez LiberNT dans une machine virtuelle ou sur du vrai matériel et signalez ce qui ne fonctionne pas. Le vrai matériel compte surtout pour les pilotes : une machine virtuelle prouve seulement qu'un pilote s'installe et se charge.

### Implémenter des nouveautés

Windows 11 est la référence de compatibilité. Avant de commencer un travail important, demandez dans les [Issues](https://github.com/LiberNT/LiberNT/issues) ou sur [Discord](https://discord.gg/BpcKg7FZ2) si quelqu'un s'en occupe déjà.

### Écrire de la documentation

Les guides et les notes de conception vont dans le [Wiki](https://github.com/LiberNT/LiberNT/wiki). Respectez les pratiques de [rétro-ingénierie en salle blanche], et n'écrivez que ce que montrent le code source, la documentation ou une exécution de tests.

### Code tiers

Les composants synchronisés avec Wine sont listés dans [media/doc/WINESYNC.txt](../../media/doc/WINESYNC.txt), et le reste du code tiers dans [media/doc/3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt). Gardez les modifications de LiberNT dans ce code sous `#ifdef __REACTOS__`, avec le code d'origine dans la branche `#else`, afin que la prochaine synchronisation les conserve.

## Comment contribuer ?

Nous vous recommandons d'utiliser l'anglais dans les rapports de bogues et les discussions de la communauté, pour que la communication reste efficace.

### Pull requests

Envoyez votre travail sous forme de [pull request] vers la branche `main-nt10`. Les pull requests vous permettent d'informer les autres des modifications que vous avez poussées vers un dépôt sur GitHub. Une fois la pull request ouverte, vous pouvez discuter et relire les modifications avec l'équipe et ajouter des commits de suivi avant leur fusion.

Avant de l'ouvrir :

- Compilez amd64, i386 et arm64, pas seulement l'architecture sur laquelle vous travaillez.
- Démarrez ou exécutez les tests qui couvrent votre modification, et indiquez dans la pull request ce que vous avez exécuté.
- Gardez le code propre à une architecture dans la couche d'architecture. Consultez [Neutralité vis-à-vis de l'architecture](../../AGENTS.md#architecture-neutrality).

### Style des commits

Notre style de commit est défini dans un __[modèle de commit]__. Utilisez-le comme référence ou activez-le avec `git config commit.template .gitmessage`. Ce modèle servira alors de message initial pour les nouveaux commits de votre dépôt local. Les règles complètes se trouvent dans [Modifications et commits](../../AGENTS.md#changes-and-commits).

### Utilisation de l'IA

Vous pouvez utiliser des outils d'IA. Indiquez dans la pull request les parties pour lesquelles ils vous ont aidé et comment vous les avez testées. Le code écrit par l'IA suit les mêmes règles que tout autre code et il est relu par une personne.

### Règles et recommandations

- *Utilisez votre __vrai nom__ et votre __vraie adresse e-mail__.* Nous n'acceptons pas les contributions de code anonymes !
  - Chaque commit qui modifie du code ou des traductions doit porter le nom légal complet de l'auteur (en lettres latines, diacritiques autorisés).
  - Il est recommandé d'indiquer dans votre profil GitHub (dans le [champ Name ici][GitHub Profile Settings]) le même nom complet que celui des commits.
- Il existe une exception pour les modifications de ressources, comme les illustrations (fonds d'écran, thèmes, icônes, sons) et la documentation hors code.
  - Dans ces cas précis, il est permis d'utiliser un pseudonyme ou un alias comme nom d'auteur, et il est recommandé d'indiquer dans votre profil GitHub (dans le [champ Name ici][GitHub Profile Settings]) le même nom que celui des commits.
- Dans tous les cas, l'auteur doit utiliser une vraie adresse e-mail, aussi bien dans les commits git (réglage `user.email`) que dans les [paramètres d'e-mail][GitHub Email Settings] de GitHub : la case "Keep my email addresses private" doit y être décochée.
  - Pour *préserver votre vie privée*, choisissez la "Primary email address" appropriée, qui sera appliquée à vos commits, dans les [paramètres d'e-mail][GitHub Email Settings] de GitHub.
- *Décrivez correctement votre contribution.* Indiquez le numéro de l'issue concernée le cas échéant.
- *N'incluez que des modifications liées.* La relecture en sera facilitée, car le relecteur aura moins à se souvenir du code existant modifié.
- *Cherchez des pull requests similaires avant d'envoyer la vôtre.* Une pull request ou une issue semblable a peut-être déjà été ouverte. Commentez et relisez plutôt celle-là.
- *Gardez votre contribution petite et centrée sur son sujet.* Résistez à la tentation de corriger des problèmes sans rapport que vous croisez ; ouvrez plutôt une issue pour eux.
- *Respectez notre __[style de code]__.*
- *N'ayez pas peur de poser des questions.* Interrogez l'équipe sur [Discord](https://discord.gg/BpcKg7FZ2) ou sur [Matrix](https://matrix.to/#/#libernt:matrix.org).

## Droits d'auteur et licences

Vous conservez les droits d'auteur sur ce que vous écrivez. LiberNT n'exige ni cession de droits d'auteur ni accord de licence de contributeur.

- Les nouveaux fichiers commencent par l'en-tête montré dans [Style de code](../../AGENTS.md#code-style), avec votre nom et votre adresse e-mail sur la ligne `COPYRIGHT` et un identifiant de licence SPDX.
- Si vous apportez une modification importante à un fichier existant, vous pouvez ajouter votre propre ligne `COPYRIGHT` sous celles qui existent.
- Ne supprimez ni ne modifiez jamais une mention de droits d'auteur ou de licence existante, et ne modifiez jamais les textes de licence du répertoire racine.
- Conservez la licence des fichiers que vous modifiez. Le code hérité de ReactOS conserve la licence indiquée dans son en-tête.
- Ne copiez jamais de code dont la licence est incompatible avec la GNU GPL 3.0, et ne copiez jamais de code GPL de Linux.

Consultez [CREDITS.md](../../CREDITS.md) pour connaître les personnes derrière LiberNT et ReactOS.

## Par où commencer ?

- Parcourez les [Issues](https://github.com/LiberNT/LiberNT/issues) ouvertes.
- Trouvez un test qui échoue, comparez-le avec Windows 11 et faites-le passer.
- Lisez le [Wiki](https://github.com/LiberNT/LiberNT/wiki) pour les guides et les notes de conception.
- Demandez de l'aide sur [Discord](https://discord.gg/BpcKg7FZ2) ou sur [Matrix](https://matrix.to/#/#libernt:matrix.org).

  [rétro-ingénierie en salle blanche]:                           https://en.wikipedia.org/wiki/Clean_room_design
  [pull request]:                                                https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests
  [GitHub Profile Settings]:                                     https://github.com/settings/profile
  [GitHub Email Settings]:                                       https://github.com/settings/emails
  [style de code]:                                               CODING_STYLE.md
  [modèle de commit]:                                            ../../.gitmessage

# Voir aussi

- [Règles de gestion des pull requests](PULL_REQUEST_MANAGEMENT.md)
- [Code de conduite](CODE_OF_CONDUCT.md)
