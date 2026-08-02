# EASY ARCADE

USBコントローラーでアーケードゲームを遊ぶためのアダプタ EASY ARCADE の firmware コードです。

## 
以下のライブラリを使わせて頂いています。

[TinyUSB Xinput driver](https://github.com/Ryzee119/tusb_xinput)

[usb_midi_host](https://github.com/rppicomidi/usb_midi_host)

## USBマスストレージ

USBメモリはFatFSのドライブ `0:` としてマウントします。RAM使用量を抑えるため、
ドライブ数とLUN数は各1、セクタサイズは512バイトに限定しています。
対応ファイルシステムはFAT12/16/32、ロングファイル名は最大64文字です。
exFAT、複数パーティション、ファイルシステムのリエントラント機能には対応しません。
USBメモリを挿しただけでは書き込みません。

マウント中はメニューに `ImpCfg`（USBからEA2へ）と `ExpCfg`（EA2からUSBへ）が
追加されます。STARTを押しながらAを押すと、`0:/ea2_config.json` の設定を
インポートまたはエクスポートします。JSONは文書全体をRAMへ展開せず、
サイズを制限したSAXストリームとして処理します。
