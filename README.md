## proconcon
スプラトゥーン3 マウスコンバーター  

## 概要
スプラトゥーン3 マウスコンバーター Raspberry Pi 4B用です。  
質問等はGitHubのDiscussionへお願いします。  
各自カスタマイズしてこっそり使用してください。  

## 必要な物
ラズベリーパイ 4B   
Nintendo SWITCH、スプラトゥーン3、プロコン  
マウス、キーボード、各機材の接続ケーブル  
  
ラズベリーパイのUSB Gadgetを利用するため、電源をUSB-C以外から得る必要があります。  
POE+HATとPOE対応LAN HUBで電源供給がよいでしょう。  
  
## ラズベリーパイの準備
USB gadgetが利用できるように設定する。  
  
## ビルド方法
gcc proconcon.c -o proconcon.out -l pthread -lm -O3 -Wall  
  
キーボード、マウスの選択はソースコードに記載があります。  
各自のデバイス名に合わせてください。  
  
## 接続方法
ラズベリーパイにProcon、Keyboard、Mouseを接続する。  
ラズベリーパイをUSBケーブルでNintendo SWITCHに接続する。  
  
![IMG_E1374](https://user-images.githubusercontent.com/83897755/189526222-d5b93a43-8da5-405b-a07e-c4226972e039.JPG)
  
## 起動方法
sudo ./load_procon.sh  
sudo ./proconcon.out  
初回起動時は、スティックの補正が必要です。

## センターリング  
試合が始まった時、1キーを1秒ほど間隔を開けて2回押してください。  
この操作で、マウスのセンターリングが行われます。  
試合中のセンターリングはQキーで行えます。  
もし、マウスの動きがゲームに正しく反映されない場合は再度センターリングを行ってください。  

## ボタン配置
デフォルト状態では下記のキー配置になっています。  

| Key           | ProCon        | Comment                                           |  
| ------------- | ------------- | ------------------------------------------------- |  
| ESC           | Home          |                                                   |
| 1             | Y             | Centering                                         |  
| 2             | Capture       |                                                   |  
| 3             | -             |                                                   |  
| 4             | +             |                                                   | 
| 8             |               | Mouse Sideの人、イカ逆転                           | 
| 9             |               | Mouse Lの単射、連射入れ替え                        | 
| WASD          | Stick L       |                                                   | 
| SHIFT L       |               | SHIFT L＋WASDで遅い動き                            | 
| SPACE         | B             |                                                   |
| Q             |               | Centering                                         | 
| E             | A             |                                                   | 
| R             | X             |                                                   | 
| F             | Hat Up        |                                                   | 
| C             | Hat Down      |                                                   | 
| T             | L             |                                                   | 
| Y             | R             |                                                   | 
| U             | Stick L Push  |                                                   | 
| O             |               | Stickで円を描く、補正用                            | 
| Num2          | Hat Down      |                                                   | 
| Num4          | Hat Left      |                                                   | 
| Num6          | Hat Right     |                                                   | 
| Num8          | Hat Up        |                                                   | 
| Arrow Key     | Stick R       |                                                   | 
| F5            |               | X感度+0.1、デバッグ用                              | 
| F6            |               | X感度-0.1、デバッグ用                              | 
| F7            |               | Y感度+0.1、デバッグ用                              | 
| F8            |               | Y感度-0.1、デバッグ用                              | 
| F9            |               | Y追従+0.1、デバッグ用                              | 
| F10           |               | Y追従-0.1、デバッグ用                              | 
| F11           |               | Keyboardモード                                     | 
| F12           |               | GamePadモード                                      | 
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
