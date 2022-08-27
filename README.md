# proconcon
スプラトゥーン3特化マウスコンバーター<br>
<br>
概要<br>
スプラトゥーン3特化マウスコンバーター　Raspberry 4B用です。<br>
いろいろわかってる方だけ使えれば良いと思うので、細かい説明は今のところ無しです。<br>
質問等はGitHubのDiscussionへ、暇なら対応します。<br>
<br>
必要な物<br>
ラズベリーパイ 4B、POE+HAT、POE対応LAN HUB<br>
Nintendo SWITCH、スプラトゥーン3、プロコン<br>
マウス、キーボード<br>
各機材の接続ケーブル<br>
<br>
ラズベリーパイの準備<br>
USB gadgetが利用できるように設定する。<br>
SSHを有効にする。<br>
CLIモードにする。<br>
<br>
ビルド方法<br>
gcc proconcon.c -o proconcon.out -l pthread -lm  -O3 -Wall<br>
<br>
接続方法<br>
ラズベリーパイにProcon、Keybord、Mouseを接続する。<br>
ラズベリーパイをUSBケーブルでNintendo SWITCHに接続する。<br>
<br>
起動方法<br>
sudo ./load_procon.sh
sudo ./proconcon.out hidraw3 event2 event0
<br>
proconcon.out [Procon] [Keybord] [Mouse]の順に指定する。<br>
<br>
参考文献<br>
https://www.mzyy94.com/blog/2020/03/20/nintendo-switch-pro-controller-usb-gadget/<br>
https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering<br>






