# LiberNT への貢献

> [English](../../CONTRIBUTING.md) | [中文](../zh-CN/CONTRIBUTING.md) | [Français](../fr-FR/CONTRIBUTING.md) | [Español](../es-ES/CONTRIBUTING.md) | **日本語** | [العربية](../ar-MA/CONTRIBUTING.md) | [Українська](../uk-UA/CONTRIBUTING.md)

LiberNT は、バグ報告、テスト、修正、新機能、ドキュメントを歓迎します。このページでは、作業の送り方を説明します。すべての変更が従うエンジニアリングのルールは [AGENTS.md](../../AGENTS.md) にあり、人間のコントリビューターにも AI ツールにも同じように適用されます。

- [何をすればよいか](#何をすればよいか)
- [貢献の方法](#貢献の方法)
- [著作権とライセンス](#著作権とライセンス)
- [どこから始めるか](#どこから始めるか)

**法的注意事項:** Microsoft Windows のプロプライエタリなソースコードを見たことがある場合、著作権侵害のおそれがあるため、あなたの貢献は受け付けられません。貢献する前に、次の内容が真実であることを確約する必要があります。
>私は、LiberNT への貢献と両立しないライセンスの下にある Windows オペレーティングシステムのいかなるバージョンのソースコードも、
>提案されたプロジェクトに関連しうるいかなる Microsoft 製品のソースコードも、使用したことも見たこともないことをここに誓います。
>これには、流出した Windows NT 3.5、NT 4、2000 のソースコード、
>および Windows Research Kernel が含まれますが、これらに限りません。

LiberNT はクリーンルームで開発されています。Windows SDK と WDK のヘッダー、Windows 11 の公開シンボル、Microsoft のドキュメント、Windows 11 上でのブラックボックス実行をもとにしています。使ってよいものと使ってはいけないものについては、[情報源](../../AGENTS.md#sources-of-truth)を参照してください。

## 何をすればよいか

### バグを報告する

バグは [Issues](https://github.com/LiberNT/LiberNT/issues) で報告してください。アーキテクチャ（amd64、i386、arm64、riscv64）、ビルドの種類、ビルドしたコミット、問題に至る手順を書いてください。シリアルポートのデバッグログがあると大いに役立ちます。

### バグを修正する

症状ではなく根本の動作を修正し、正しい最小限の変更にしてください。修正は、実際の起動やテスト実行で動作が確認されて初めて有効とみなされます。

### テストを修正する

テストは、LiberNT を Windows の動作と照らし合わせて確認します。多くのテストは Windows 11 でもすべては通らないため、テストを通すために LiberNT を変更する前に、同じテストを Windows 11 で実行し、結果を 1 項目ずつ比較してください。Windows 11 でも失敗する項目があれば、誤っているのはテストの期待値です。[Windows 11 との比較テスト](../../AGENTS.md#testing-against-windows-11)を参照してください。

### LiberNT をテストする

仮想マシンまたは実機で LiberNT を実行し、動かないものを報告してください。ドライバーにとっては実機が最も重要です。仮想マシンで証明できるのは、ドライバーがインストールされて読み込まれることだけです。

### 新しい機能を実装する

互換性の基準は Windows 11 です。大きな作業を始める前に、すでに誰かが取り組んでいないか [Issues](https://github.com/LiberNT/LiberNT/issues) または [Discord](https://discord.gg/BpcKg7FZ2) で確認してください。

### ドキュメントを書く

ガイドや設計メモは [Wiki](https://github.com/LiberNT/LiberNT/wiki) に置きます。[クリーンルームのリバースエンジニアリング]の慣行を守り、ソースコード、ドキュメント、テスト実行で確認できることだけを書いてください。

### サードパーティのコード

Wine と同期しているコンポーネントは [media/doc/WINESYNC.txt](../../media/doc/WINESYNC.txt) に、その他のサードパーティのコードは [media/doc/3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt) に記載されています。それらのコードに LiberNT の変更を加えるときは `#ifdef __REACTOS__` の中に入れ、元のコードを `#else` 側に残してください。そうすれば、次の同期でも変更が保たれます。

## 貢献の方法

コミュニケーションを効率的にするため、Issue での報告やコミュニティでの議論には英語を使うことをおすすめします。

### プルリクエスト

作業は `main-nt10` ブランチに対する[プルリクエスト]として送ってください。プルリクエストを使うと、GitHub 上のリポジトリにプッシュした変更をほかの人に知らせることができます。プルリクエストを開いたら、マージ前にチームと変更について議論・レビューし、追加のコミットを加えられます。

開く前に:

- 作業しているアーキテクチャだけでなく、amd64、i386、arm64 をビルドしてください。
- 変更に関係するテストを起動または実行し、何を実行したかをプルリクエストに書いてください。
- アーキテクチャ固有のコードはアーキテクチャ層に置いてください。[アーキテクチャ中立性](../../AGENTS.md#architecture-neutrality)を参照してください。

### コミットのスタイル

コミットのスタイルは __[コミットテンプレート]__ で定義されています。参考にするか、`git config commit.template .gitmessage` で有効にしてください。これにより、ローカルリポジトリでの新しいコミットの初期メッセージとしてこのテンプレートが使われます。ルールの全文は[変更とコミット](../../AGENTS.md#changes-and-commits)にあります。

### AI の利用

AI ツールを使ってもかまいません。どの部分で助けを借り、どのようにテストしたかをプルリクエストで伝えてください。AI が書いたコードもほかのコードと同じルールに従い、人間がレビューします。

### ルールと推奨事項

- *__本名__と__実在するメールアドレス__を使ってください。* 匿名のコードの貢献は受け付けません！
  - コードや翻訳を変更するコミットには、作者の正式な氏名（ラテン文字、ダイアクリティカルマーク可）が必要です。
  - GitHub のプロフィール（[こちらの Name 欄][GitHub Profile Settings]）にも、コミットと同じ氏名を設定することをおすすめします。
- メディアの変更、たとえばアート（壁紙、テーマ、アイコン、サウンド）やコード外のドキュメントの変更には例外があります。
  - これらの場合に限り、作者名としてニックネームや別名を使えます。GitHub のプロフィール（[こちらの Name 欄][GitHub Profile Settings]）にも、コミットと同じ名前を設定することをおすすめします。
- いずれの場合も、作者は実在するメールアドレスを使う必要があります。これには git のコミット（`user.email` の設定）と GitHub の[メール設定][GitHub Email Settings]が含まれ、そこで「Keep my email addresses private」のチェックを外す必要があります。
  - *プライバシーを守る*には、GitHub の[メール設定][GitHub Email Settings]で、コミットに使われる適切な「Primary email address」を選んでください。
- *貢献内容をきちんと説明してください。* 該当する場合は関連する Issue の番号を記載してください。
- *関連する変更だけを入れてください。* レビュアーが変更される既存コードについて覚えておくべきことが減り、レビューが楽になります。
- *送る前に似たプルリクエストを探してください。* 似たプルリクエストや Issue がすでに開かれているかもしれません。その場合は、そちらにコメントやレビューをしてください。
- *貢献は小さく、テーマに絞ってください。* 途中で見つけた無関係な問題を直したくなっても我慢し、代わりに Issue を開いてください。
- *__[コーディングスタイル]__を守ってください。*
- *質問をためらわないでください。* [Discord](https://discord.gg/BpcKg7FZ2) または [Matrix](https://matrix.to/#/#libernt:matrix.org) でチームに聞いてください。

## 著作権とライセンス

あなたが書いたものの著作権はあなたが保持します。LiberNT には著作権の譲渡も、コントリビューターライセンス契約もありません。

- 新しいファイルは、[コードスタイル](../../AGENTS.md#code-style)に示されたヘッダーで始め、`COPYRIGHT` 行にあなたの名前とメールアドレスを、そして SPDX ライセンス識別子を記載します。
- 既存のファイルに大きな変更を加えた場合は、既存の行の下に自分の `COPYRIGHT` 行を追加してかまいません。
- 既存の著作権表示やライセンス表示を削除・変更したり、トップレベルディレクトリのライセンス文を編集したりしないでください。
- 変更するファイルのライセンスは維持してください。ReactOS から引き継いだコードは、ヘッダーに記載されたライセンスを維持します。
- GNU GPL 3.0 と互換性のないライセンスのコードや、Linux の GPL コードは決してコピーしないでください。

LiberNT と ReactOS を支える人々については [CREDITS.md](../../CREDITS.md) を参照してください。

## どこから始めるか

- オープンな [Issues](https://github.com/LiberNT/LiberNT/issues) を見てください。
- 失敗しているテストを見つけ、Windows 11 と比較して、通るようにしてください。
- ガイドや設計メモは [Wiki](https://github.com/LiberNT/LiberNT/wiki) で読めます。
- [Discord](https://discord.gg/BpcKg7FZ2) または [Matrix](https://matrix.to/#/#libernt:matrix.org) で助けを求めてください。

  [クリーンルームのリバースエンジニアリング]:                     https://en.wikipedia.org/wiki/Clean_room_design
  [プルリクエスト]:                                              https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests
  [GitHub Profile Settings]:                                     https://github.com/settings/profile
  [GitHub Email Settings]:                                       https://github.com/settings/emails
  [コーディングスタイル]:                                        CODING_STYLE.md
  [コミットテンプレート]:                                        ../../.gitmessage

# 関連項目

- [プルリクエストの管理ルール](PULL_REQUEST_MANAGEMENT.md)
- [行動規範](CODE_OF_CONDUCT.md)
