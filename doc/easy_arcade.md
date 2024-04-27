# EASY ARCADE 2 マニュアル

EASY ARCADE 2(以下EA2)はJAMMAコネクタを採用したアーケードゲーム基板を遊ぶための簡易アダプタです（いわゆるコントロールボックス、Supergun）。
USB-PD電源とUSBコントローラーを使用することで比較的簡単に遊べることを目指しています。

## 電気的仕様
### 電源入力
USB-PDの20V出力があるものを使用してください。

### JAMMA側電源出力
- 5V: 15A以下
- 12V: 0.7A以下
- -5V: 0.2A以下

実際に出力可能な電流は入力電源のスペックによります。
たとえば、5V10Aの基板を遊ぶためには65W級の電源を用意する必要があると思われます。

## 各部の説明

todo: 写真をいれる

### JAMMAコネクタ
アーケード基板を接続します。
EA2の部品面が基板部品面です。逆挿しに注意してください。

### USB-A コネクタ
コントローラーを接続します。
右側がプレイヤー1、左側がプレイヤー2に対応します。

### USB-Cコネクタ
USB-PD電源を接続します。

### USB-Microコネクタ
ファームウェアアップデート用です。
通常は何も接続しないでください。
絶対にUSB-CとUSB-Micro双方から電源供給しないようにしてください。

### ジャンパピン
ファームウェアアップデート時に外してください。

### AVマルチコネクタ
SFC仕様のRGBケーブルを接続します。
横棒の印刷のある基板部品面側がSFC RGBコネクタの凸がある方向になります。

### 液晶画面
各種状態表示や各種設定を行うために使用します。

### POWER/CONFIG ボタン
電源のOn/Off および、メニューの開閉に使用します。

### TEST, SERVICE ボタン
JAMMA側の TEST, SERVICE に接続されています。


## 使い方

初期設定では、USB-CポートにUSB-PD電源からのケーブルを接続すると待機状態に移行します

### 電源On
POWER/CONFIG ボタンを押すと電源がJAMMA側に供給されます

### 電源Off
- 2つの方法があります
  - POWER/CONFIGボタンを5秒間押し続ける
  - コントローラーの CMD + START + COIN を押す

### メニュー

電源Onの時にPOWER/CONFIGボタンを押すことで各種設定メニューが開閉します。
メニュー表示中にはコントローラーの上下で項目を選択、左右で項目の値を変更できます。
 
**保存系の項目で保存された状態が実際に書き込まれるのはメニューを閉じたタイミングになります。設定後、電源を切る前にメニューを閉じるようにしてください。**

- BtnCfg
  - コントローラーのボタンとJAMMA側のボタン出力のマッピングを行います
  - 別項にて説明します

- PowMode
  - USB-PD電源を接続した際の電源On/Off状態を設定します
  - InitOnにすると接続時から電がが入るようになります

- DispFPS
  - Idle画面に、基板の画面出力のV-Sync周波数を表示するかどうかを設定します
  - 計測がいい加減であまり制度はありません。目安として。

- BtnDisp
  - Idle画面に、表示するボタン状態を選択します
    - Input: 入力しているボタンを表示します
    - Rapid: 連射が有効になっているボタンを表示します
    - None: ボタン状態を表示しません

- BackLit
  - バックライトの点灯設定を行います

- RapidMd
  - 連射モードを選択します
    - Softw: 単純なタイミングによる連射を使用します
    - Synchro: V-Syncに同期した連射を使用します

- SwRapid
  - 連射モードが softw の時の連射速度を設定します

- Phase A-F
  - 連射の位相(面裏)の設定をします
  - In/Out でOn/Offが逆転しています

- initRpd
  - 連射設定を初期化します

- SaveRpd
  - 現在の連射有効状態を保存し、次の電源投入時に復帰するようにします
  - 保存しない連射状態は電源Onのたびに初期化されます

- InitAll
  - ボタン設定を含むすべての状態を初期化します


## ボタン設定

BtnCfg メニューから、POWER/CONFIGボタンを２秒長押しすると、USBコントローラーとJAMMA側ボタン出力のマッピング設定モードに入ります。

PUSH ↑ などの表示に従って割り当てたいUSBコントローラーのレバーやボタンを入力することで、マッピングが行えます。
複数のボタンを押すことで、JAMMA側のボタンに対して複数の入力側のボタンを割り当てることも可能です。

POWER/CONFIG ボタンを押すとそのボタンへのマッピングをスキップすることができます。

### CMDボタン
Push CMDと表示される割り当てはJAMMA側のボタンに対応するものではなく、連射設定や電源Offに使用するボタンとなります。

## 連射設定

CMDボタンと連射したいボタンを同時に押すと連射が有効になります。
連射は入力側のボタン個別に有効にすることができるので、JAMMA側のボタンに対して入力側の複数のボタンを割り当てておくことで、連射ありと連射のないボタンの使い分けも可能です。

CMDボタンと上下で基本連射速度からの分周比を設定できます。分周比はIdle画面で確認できます。


## 対応コントローラー
- USB-HID コントローラー
  - PS3, 4, 5 のコントローラーを含む
- XBOX コントローラー
- USB-MIDI


## 免責
過電流などでも問題が起こらないように留意していますが、個人制作物ですので至らない点があり得ます。
何かしらの不具合により家財に被害が及んでも責任はとれませんので、目を離さずにご使用ください。

## 既知の問題
現状、JAMMA側出力はオープンで5Vが出力されています。オープン時にHi-Zを期待する回路は動作しない可能性があります。
今後オープンドレインに変更予定です。

