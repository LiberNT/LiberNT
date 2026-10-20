# How To Contribute

LiberNT welcomes bug reports, tests, fixes, new features and documentation. This page explains how to send your work. The engineering rules every change follows are in [AGENTS.md](AGENTS.md), and they apply to human contributors and AI tools alike.

- [What To Do?](#what-to-do)
- [How To Contribute?](#how-to-contribute)
- [Copyright and Licences](#copyright-and-licences)
- [Where To Start?](#where-to-start)

**Legal notice:** If you have seen proprietary Microsoft Windows source code, your contribution won't be accepted because of potential copyright violation. Before contributing, you must affirm that the following is true:
>I hereby swear that I have not used nor seen the source code to any version of the Windows operating system
>nor any Microsoft product that may be related to the proposed project that is under a license incompatible
>with contribution to LiberNT, including but not limited to the leaked Windows NT 3.5, NT 4 and 2000 source code
>and the Windows Research Kernel.

LiberNT is built clean-room: from the Windows SDK and WDK headers, Windows 11 public symbols, Microsoft documentation and black-box runs on Windows 11. See [Sources of truth](AGENTS.md#sources-of-truth) for what may and may not be used.

## What To Do?

### Report bugs

Report bugs in [Issues](https://github.com/LiberNT/LiberNT/issues). Give the architecture (amd64, i386, arm64 or riscv64), the build type, the commit you built and the steps that lead to the problem. A debug log from the serial port helps a lot.

### Fix bugs

Fix the underlying behavior, not the symptom, and make the smallest correct change. A fix counts only after a real boot or test run shows it working.

### Fix tests

Tests check LiberNT against Windows behavior. Many of them are not clean on Windows 11 either, so before changing LiberNT to satisfy a test, run the same test on Windows 11 and compare the results check by check. If Windows 11 fails a check too, the expectation is the bug. See [Testing against Windows 11](AGENTS.md#testing-against-windows-11).

### Test LiberNT

Run LiberNT in a virtual machine or on real hardware and report what breaks. Real hardware matters most for drivers: a virtual machine proves only that a driver installs and loads.

### Implement new things

Windows 11 is the compatibility reference. Before starting a larger piece of work, ask in [Issues](https://github.com/LiberNT/LiberNT/issues) or on [Discord](https://discord.gg/BpcKg7FZ2) whether someone is already working on it.

### Write documentation

Guides and design notes go in the [Wiki](https://github.com/LiberNT/LiberNT/wiki). Respect [clean room reverse engineering] practices, and write only what the source code, the documentation or a test run shows.

### Third-party code

Components synchronised with Wine are listed in [media/doc/WINESYNC.txt](media/doc/WINESYNC.txt), and other third-party code in [media/doc/3rd Party Files.txt](media/doc/3rd%20Party%20Files.txt). Keep LiberNT changes to that code under `#ifdef __REACTOS__`, with the original code in the `#else` branch, so the next synchronisation keeps them.

## How To Contribute?

### Pull Requests

Send your work as a [pull request] against the `main-nt10` branch. Pull requests let you tell others about changes you have pushed to a repository on GitHub. Once a pull request is opened, you can discuss and review the changes with the team and add follow-up commits before they are merged.

Before you open it:

- Build amd64, i386 and arm64, not only the architecture you work on.
- Boot or run the tests that cover your change, and say in the pull request what you ran.
- Keep architecture-specific code in the architecture layer. See [Architecture neutrality](AGENTS.md#architecture-neutrality).

### Commit style

Our commit style is defined in a __[commit template]__. Use it as a reference or turn it on using `git config commit.template .gitmessage`. This will set this template as an initial commit message for the new commits in your local repository. The full rules are in [Changes and commits](AGENTS.md#changes-and-commits).

### Use of AI

You may use AI tools. Tell us in the pull request which parts they helped with and how you tested them. AI-written code follows the same rules as any other code and is reviewed by a person.

### Rules and Recommendations

- *Use your __real name__ and __real email__.* We do not accept anonymous code contributions!
  - Every commit that changes code or translations should have author's full legal name (in latin letters, diacritics allowed).
  - It's recommended to have the same full name set in GitHub profile (in the [Name field here][GitHub Profile Settings]) that matches one specified in commits.
- There is an exception for media changes, such as changes of art (wallpapers, themes, icons, sounds) and out-of-code documentation.
  - In these specific cases it's allowed to use a nickname or alias as author's name, and it's recommended to have the same name set in GitHub profile (in the [Name field here][GitHub Profile Settings]) matching one specified in commits.
- In any case the author must use a real e-mail address, this includes git commits (`user.email` setting) and GitHub [e-mail settings][GitHub Email Settings] - the checkbox "Keep my email addresses private" must be unchecked there.
  - In order to *keep your privacy*, select appropriate "Primary email address" that will be applied to your commits in GitHub [e-mail settings][GitHub Email Settings].
- *Ensure your contribution is properly described.* Include the relevant issue number if applicable.
- *Put only related changes.* It will make reviewing easier as the reviewer needs to recall less information about the existing source code that is changed.
- *Search for similar pull requests before submitting.* It may be that a similar pull request or issue was opened previously. Comment and review on that one instead.
- *Keep your contribution small and focused on the topic.* Resist the temptation to fix unrelated issues as you come across them; open an issue for them instead.
- *Respect our __[Coding Style]__.*
- *Do not be afraid to ask questions.* Ask the team on [Discord](https://discord.gg/BpcKg7FZ2) or [Matrix](https://matrix.to/#/#libernt:matrix.org).

## Copyright and Licences

You keep the copyright on what you write. LiberNT has no copyright assignment and no contributor licence agreement.

- New files start with the header shown in [Code style](AGENTS.md#code-style), with your name and e-mail in the `COPYRIGHT` line and an SPDX licence identifier.
- When you make a major change to an existing file, you may add your own `COPYRIGHT` line under the existing ones.
- Never remove or change an existing copyright or licence notice, and never edit the licence texts in the top level directory.
- Keep the licence of the files you change. Code inherited from ReactOS keeps the licence stated in its header.
- Never copy code whose licence is incompatible with the GNU GPL 3.0, and never copy GPL code from Linux.

See [CREDITS.md](CREDITS.md) for the people behind LiberNT and ReactOS.

## Where To Start?

- Look at the open [Issues](https://github.com/LiberNT/LiberNT/issues).
- Find a test that fails, compare it with Windows 11, and make it pass.
- Read the [Wiki](https://github.com/LiberNT/LiberNT/wiki) for guides and design notes.
- Ask for help on [Discord](https://discord.gg/BpcKg7FZ2) or [Matrix](https://matrix.to/#/#libernt:matrix.org).

  [clean room reverse engineering]:                              https://en.wikipedia.org/wiki/Clean_room_design
  [pull request]:                                                https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests
  [GitHub Profile Settings]:                                     https://github.com/settings/profile
  [GitHub Email Settings]:                                       https://github.com/settings/emails
  [Coding Style]:                                                CODING_STYLE.md
  [commit template]:                                             .gitmessage

# See Also

- [Rules for managing Pull Requests](PULL_REQUEST_MANAGEMENT.md)
- [Code of conduct](CODE_OF_CONDUCT.md)
