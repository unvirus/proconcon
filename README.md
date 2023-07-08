## proconcon
スプラトゥーン3 マウスコンバーター  

## 概要
スプラトゥーン3 マウスコンバーター Raspberry Pi 4B用です。  
質問等はGitHubのDiscussionへお願いします。  
各自カスタマイズしてこっそり使用してください。  
ソースコードを編集すればキー配置など好きに設定できるので  
ハンディキャップがあり、ゲームコントローラーをうまく扱えない方も  
ご利用いただけるかもしれません。  
    
## 更新履歴  
Ver 0.09 2022/11/25  
旧バージョン、動作にはプロコンが必要です。  
  
Ver 0.17 2023/07/08    
横歩きモードは削除しました。
ソースコードをすこし整理しました。
コントローラーデータをファイルから読み込むようにしました。
キー配置変更しました。変更したキーはQ(discon),G,H,U,I,Lです。
機能自体はVer 0.16から変更ありません。  
**proconcon.outと同じ場所にflashrom.binを配置してください。** 
    
## 必要な物
ラズベリーパイ 4B     
Nintendo SWITCH、スプラトゥーン3  
マウス、キーボード、各機材の接続ケーブル  
  
ラズベリーパイのUSB Gadgetを利用するため、電源をUSB-C以外から得る必要があります。  
POE+HATとPOE対応LAN HUBで電源供給がよいでしょう。  
ラズベリーパイ 4Bの初期版はUSB回路に不具合があり、USBを利用しながら給電ができないものがあります。  
    
## ラズベリーパイの準備
USB gadgetが利用できるように設定する。  
  
## ビルド方法
gcc proconcon.c -o proconcon.out -l pthread -lm -O3 -Wall  
  
キーボード、マウスの選択はソースコードに記載があります。  
各自のデバイス名に合わせてください。  
  
## 接続方法
ラズベリーパイにKeyboard、Mouseを接続する。  
ラズベリーパイをUSBケーブルでNintendo SWITCHに接続する。  
  
![IMG_E1374](https://user-images.githubusercontent.com/83897755/204125349-ef4d7021-fbfd-4df3-9745-1a1058430d0c.jpg)
  
## 起動方法
sudo ./load_procon.sh  
sudo ./proconcon.out  

**proconcon.outと同じ場所にflashrom.binを配置してください。**  
**flashrom.binはコントローラーの設定ファイルで、proconcon.out起動時に利用します。** 
  
マウス感度は800-1600 DPIをあたりで調整すると良さそうです。  
本プログラムはCUI(CLI)で利用してください。  
デスクトップ環境では本プログラム使用中に範囲外のクリックなどが発生し、誤動作の原因になります。  
![IMG_E1374](https://user-images.githubusercontent.com/83897755/204680187-3678ed45-c9b6-499e-8ff4-b0cc18fd81f5.jpg)  
    
## センターリング  
試合が始まった時、1キーを1秒ほど間隔を開けて2回押してください。  
この操作で、マウスのセンターリングが行われます。  
もし、マウスの動きがゲームに正しく反映されない場合は再度センターリングを行ってください。  

## ボタン配置
デフォルト状態では下記のキー配置になっています。  

| Key           | ProCon        | Comment                                           |  
| ------------- | ------------- | ------------------------------------------------- |  
| ESC           | Home          |                                                   |
| 1             | Y             | Centering + stop walking sideways                 |  
| 2             | Capture       |                                                   |  
| 3             | -             |                                                   |  
| 4             | +             |                                                   | 
| 9             |               | Mouse Lの単射、連射入れ替え                        | 
| WASD          | Stick L       |                                                   | 
| SHIFT L       |               | Move slowly with SHIFT L + WASD                   | 
| SPACE         | B             |                                                   |
| E             | A             |                                                   | 
| R             | X             |                                                   | 
| F             | Hat Up        |                                                   | 
| C             | Hat Down      |                                                   | 
| T             | L             |                                                   | 
| Y             | R             |                                                   | 
| G             | ZL            | Added in ver 0.16                                 | 
| H             | ZR            | Added in ver 0.16                                 | 
| U             | Stick L Push  |                                                   | 
| I             | Stick R Push  |                                                   | 
| L             |               | Tesla menu open                                   | 
| Z             |               | Super jump to starting point                     | 
| Num2          | Hat Down      |                                                   | 
| Num4          | Hat Left      |                                                   | 
| Num6          | Hat Right     |                                                   | 
| Num8          | Hat Up        |                                                   | 
| Arrow Key     | Stick R       |                                                   | 
| F5            |               | X sensitivity+0.1 デバッグ用                       | 
| F6            |               | X sensitivity-0.1 デバッグ用                       | 
| F7            |               | Y sensitivity+0.1 デバッグ用                       | 
| F8            |               | Y sensitivity-0.1 デバッグ用                       | 
| F9            |               | Y following+0.1 デバッグ用                       | 
| F10           |               | Y following-0.1 デバッグ用                       | 
| Mouse R       | R             |                                                   | 
| Mouse L       | ZR            |                                                   | 
| Mouse Side    | ZL            |                                                   | 
| Mouse Extra   | ZR            | Rappid Fire                                       | 
| Mouse Wheel   | Stick R Push  |                                                   | 
| Mouse Middle  | Stick R Push  |                                                   | 
| Mouse move    | Gyro          |                                                   | 

  
## 参考文献
https://www.mzyy94.com/blog/2020/03/20/nintendo-switch-pro-controller-usb-gadget/  
https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering  
