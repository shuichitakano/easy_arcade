# EASY ARCADE

USBコントローラーでアーケードゲームを遊ぶためのアダプタ EASY ARCADE のファームウェアです。

- [取扱説明書](docs/README.md)
- [ファームウェアのリリース一覧](https://github.com/shuichitakano/easy_arcade/releases)

## ビルド

CMake 3.13以降、Arm GNU Toolchain（`arm-none-eabi-gcc`）、Raspberry Pi Pico SDK 2.3.0以降が必要です。ホストテストにはPython 3と、AddressSanitizer / UndefinedBehaviorSanitizerに対応したC/C++コンパイラーも使用します。

```sh
git clone --recurse-submodules https://github.com/shuichitakano/easy_arcade.git
cd easy_arcade
# 既存のチェックアウトではサブモジュールを更新する
git submodule update --init --recursive
```

Pico SDKは別途用意し、そのサブモジュールも初期化してください。以下はSDK 2.3.0を使う例です。USBメモリ用のFatFSはSDK内の `lib/tinyusb/lib/fatfs/source` を使用します。

```sh
# /path/to/pico-sdk は用意したSDKのパスに置き換える
git clone --branch 2.3.0 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git /path/to/pico-sdk
# SDKを取得済みならcloneは省略する
git -C /path/to/pico-sdk submodule update --init --recursive
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

生成されるファームウェアは `build/arcade_play.uf2` です。書き込み方法は[取扱説明書](docs/easy_arcade.md#ファームウェアアップデート)を参照してください。

## テスト

```sh
python3 tests/run_host_tests.py
```

HID解析、Switch Pro、マクロ、XInput、USB初期化・復旧処理の回帰テストを実行します。一時ファイルは自動で削除します。実機でのUSB通信タイミングや画面表示は別途確認が必要です。

## 使用ライブラリ

- [TinyUSB](https://github.com/hathach/tinyusb)（[使用中のfork](https://github.com/shuichitakano/tinyusb)）
- [TinyUSB Xinput driver](https://github.com/Ryzee119/tusb_xinput)（[使用中のfork](https://github.com/shuichitakano/tusb_xinput)）
- [usb_midi_host](https://github.com/rppicomidi/usb_midi_host)
- [RapidJSON](https://github.com/Tencent/rapidjson)

TinyUSBとXInputドライバーは、サブモジュールで固定したforkのコミットを使用します。
