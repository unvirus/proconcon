/*
スプラトゥーン3 マウスコンバーター for RaspberryPi 4b
2022/08/28 ぬこいばらき（unvirus）

how to build
gcc proconcon.c -o proconcon.out -l pthread -lm  -O3 -Wall 
 
Version history 
ver.0.01 2022/08/27 First release 
ver.0.02 2022/08/28 マウスズレ調整
ver.0.03 2022/09/03 デバイスの選択を自動化、ソース整理
ver.0.04 2022/09/11 イカロールを出しやすくした 
ver.0.05 2022/09/20 自動ドット打ち処理に不具合があるので削除、メイン連射追加 
ver 0.06 2022/10/04 排他処理修正、復活地点にスーパージャンプを追加 
ver 0.07 2022/10/29 スティック補正を不要にした 
ver 0.08 2022/11/01 プロコン検出処理のバグを修正、スーパージャンプのバグを修正 
ver 0.09 2022/11/25 Firmware Ver4.33で、ジャイロ加速度値が変更されているので仮対応した 
ver 0.10 2022/11/27 プロコン接続を不要にした
ver 0.11 2022/12/02 Swicthのサスペンド時のプロコンコマンドに対応、コメント追加
ver 0.12 2022/12/11 人イカ逆転モードを廃止、サブ慣性キャンセル機能を追加
ver 0.13 2022/12/16 センタリング時、少し上を向くので微調整した 
ver 0.14 2023/01/08 マウスを左右に振った時の追従性を向上 
ver 0.15 2023/02/12 自動イカロール機能を追加、反対方向入力で自動でイカロールする 
ver 0.16 2023/05/06 マウスを上下に強く動かすと座標が変になる不具合を修正、センターリングホールドモードを追加  
ver 0.17 2023/07/08 冗長なソースコードを整理しました。センターリングホールドモードは使いにくいので削除した 
ver 0.18 2023/10/17 SHIFTキーを押している間、ゆっくり動作が中断されない不具合を修正した 
ver 0.19 2024/01/28 操作中にターミナルで余計な文字が出ないようにした、64BitOSで動作確認した 
ver 0.20 2024/02/03 プログラムの終了処理を調整した 
ver 0.21 2024/05/31 低速連射モード追加 
*/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>

//debug
#define ENUM_HID_DEVICE         //list up hid input device

/*
スプラトゥーンでは、左右はジャイロの加速度で判断する
上下はジャイロの加速度と角度で判断する
マウスの座標系とは異なるのでユーザー毎に調整が必要になる
*/
#define X_SENSITIVITY           (17.2f)     //マウス操作、左右感度
#define Y_SENSITIVITY           (20.5f)     //マウス操作、上下感度
#define Y_FOLLOWING             (1.00f)     //マウス操作、上下追従補正

//スティック入力値
#define AXIS_CENTER             (1920)
#define AXIS_MAX_INPUT          (1920)

/*
スロー速度を設定する。0.83fならば全速力の83%になる
イカ速に応じて調整が必要 
*/
#define AXIS_HALF_INPUT_FACTOR  (0.65f)

#define Y_ANGLE_UPPPER_LIMIT    (3000)      //Y angle imit
#define Y_ANGLE_LOWER_LIMIT     (-1500)     //Y angle imit
#define Y_ACCEL_UPPPER_LIMIT    (16000)     //Y acceleration limit    
#define Y_ACCEL_LOWER_LIMIT     (-16000)    //Y acceleration limit 

#define MAX_NAME_LEN            (256)
#define MAX_PACKET_LEN          (64)
#define MAX_BUFFER_LEN          (512)
#define MAC_ADDRESS_LEN         (6)

#define GADGET_NAME             "/dev/hidg0"

#define GADGET_DETACH           "echo "" > /sys/kernel/config/usb_gadget/procon/UDC"
#define GADGET_ATTACH           "ls /sys/class/udc > /sys/kernel/config/usb_gadget/procon/UDC"

#define DEV_KEYBOARD            (0)
#define DEV_MOUSE               (1)

#define PAD_INPUT_WAIT          (16)    //コントローラーの入力間隔(ms)
#define PAD_INPUT_WAIT_MARGIN   (500000)

#define INERTIA_CANCEL_ENABLE			//自動サブ慣性キャンセル機能を無効にする場合はコメントアウトする
//#define SQUID_ROLL_ENABLE               //自動イカロール機能を無効にする場合はコメントアウトする

#define DELEY_FOR_AFTER_JUMP	(50)	//ジャンプ後、慣性キャンセルを行うようになるまでの時間、16ms単位
#define DELEY_FOR_AFTER_MAIN_WP	(12)	//メイン攻撃後、慣性キャンセルを行うようになるまでの時間、16ms単位
#define DELEY_FOR_AFTER_SUB_WP	(12)	//サブ攻撃後、慣性キャンセルを行うようになるまでの時間、16ms単位
#define MOVE_STOP_TIME          (12)    //動作停止までの時間、16ms単位
#define ROLL_INPUT_TIME			(15)    //イカロール受付時間、16ms単位
#define ROLL_JUMP_TIME			(25)    //イカロールジャンプ入力時間、16ms単位

#define STICK_DIR_UP            (0x08)
#define STICK_DIR_RIGHT_UP      (0x0C)
#define STICK_DIR_RIGHT         (0x04)
#define STICK_DIR_RIGHT_DOWN    (0x06)
#define STICK_DIR_DOWN          (0x02)
#define STICK_DIR_LEFT_DOWN     (0x03)
#define STICK_DIR_LEFT          (0x01)
#define STICK_DIR_LEFT_UP       (0x09)

/*
各自で利用するキーボードとマウスを指定する 
指定する名称は以下で確認する 
ls /dev/input/by-id 
 
例 
ls /dev/input/by-id 
usb-Logitech_G403_Prodigy_Gaming_Mouse_148B38643831-event-mouse
usb-Logitech_G403_Prodigy_Gaming_Mouse_148B38643831-if01-event-kbd
usb-Logitech_G403_Prodigy_Gaming_Mouse_148B38643831-mouse
usb-Nintendo_Co.__Ltd._Pro_Controller_000000000001-event-joystick
usb-Nintendo_Co.__Ltd._Pro_Controller_000000000001-joystick
usb-SIGMACHIP_USB_Keyboard-event-if01
usb-SIGMACHIP_USB_Keyboard-event-kbd
usb-Topre_Corporation_Realforce_108-event-kbd 
*/
//#define KEYBOARD_NAME       "Topre_Corporation_Realforce_108"
//#define KEYBOARD_NAME       "usb-SIGMACHIP_USB_Keyboard"
#define KEYBOARD_NAME     "usb-SINO_WEALTH_Gaming_KB"

//#define MOUSE_NAME          "Logitech_G403_Prodigy_Gaming_Mouse"
#define MOUSE_NAME          "usb-Logitech_G403_HERO_Gaming_Mouse"

#define ROM_FILE_NAME       "./flashrom.bin"

typedef struct {
    short Y_Angle;                //約4200から約-4200、平置き時約-668
    short X_Angle;                //約4200から約-4200、平置き時約-28
    short Z_Angle;                //解析情報をあさるとZ角度だが何かが変、平置き時約4075
    short X_Accel;                //約16000から約-16000
    short Y_Accel;                //約16000から約-16000
    short Z_Accel;                //約16000から約-16000
} ProconGyroData;

typedef struct {
    //0
    unsigned char ReportId;         //Report ID. value is 0x30.
    //1
    unsigned char TimeStamp;        //Time stamp increase moderately.
    //2
    unsigned char ConnectNo:4;      //Controller number?
    unsigned char BatteryLevel:4;   //Battery Level?
    //3
    unsigned char Y:1;
    unsigned char X:1;
    unsigned char B:1;
    unsigned char A:1;
    unsigned char SR_R:1;           //not use
    unsigned char SL_R:1;           //not use
    unsigned char R:1;
    unsigned char ZR:1;
    //4
    unsigned char Minus:1;
    unsigned char Plus:1;
    unsigned char StickR:1;         //Right stick push.
    unsigned char StickL:1;         //Left stick push.
    unsigned char Home:1;
    unsigned char Capture:1;
    unsigned char None:1;           //not use
    unsigned char Grip:1;           //not use
    //5
    unsigned char Down:1;
    unsigned char Up:1;
    unsigned char Right:1;
    unsigned char Left:1;
    unsigned char SR_L:1;           //not use
    unsigned char SL_L:1;           //not use
    unsigned char L:1;
    unsigned char ZL:1;
    //6 - 8
    unsigned char L_Axis[3];        //12Bit単位でX値、Y値が入っている
    //9 - 11
    unsigned char R_Axis[3];        //12Bit単位でX値、Y値が入っている
    //12
    unsigned char Reserved;         //unknown value (Vibrator_input_report)
    //13 - 63
    unsigned char GyroData[51];     //Gyro sensor data is repeated 3 times. Each with 5ms sampling.
} ProconData;

typedef struct {
    int X;
    int Y;
    int Wheel;
    unsigned char L;
    unsigned char R;
    unsigned char Middle;
    unsigned char Side;             //Side button 1
    unsigned char Extra;            //Side button 2
} MouseData;

int Processing;
int fGadget;
int fKeyboard;
int fMouse;
int thKeyboardCreated;
int thMouseCreated;
int thOutputReportCreated;
int thInputReportCreated;
int YTotal;
int Slow;
int Straight;
int StraightHalf;
int Diagonal;
int DiagonalHalf;
int RapidFireCnt;
int RapidFireWait;
int MWBtnToggle;
int ReturnToBase;
int ReturnToBaseCnt;
int HidMode;
int GyroEnable;
int InertiaCancelCnt;
unsigned int RomSize;
unsigned int MainWpTick;
unsigned int SubWpTick;
unsigned int JumpTick;
unsigned int InputTick;
unsigned int RollKeyTick;
unsigned int RollTick;
unsigned int RollOn;
float XSensitivity;
float YSensitivity;
float YFollowing;
pthread_t thKeyboard;
pthread_t thMouse;
pthread_t thOutputReport;
pthread_t thInputReport;
pthread_mutex_t MouseMtx;
pthread_mutex_t UsbMtx;
MouseData MouseMap;
unsigned char *pRomBuf;
unsigned char DirPrev;
unsigned char DirPrevCnt;
unsigned char RollDirPrev;
unsigned char KeyMap[KEY_WIMAX];
unsigned char BakupProconData[11];

int ReadCheck(int Fd)
{
    int ret;
    fd_set rfds;
    struct timeval tv;

    if (Processing == 0)
    {
        return -1;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 000000;

    FD_ZERO(&rfds);
    FD_SET(Fd, &rfds);

    ret = select(Fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0)
    {
        Processing = 0;
    }

    if (Processing == 0)
    {
        ret = -1;
    }

    //if read data incoming, returns 1.
    //0 is timeout.
    return ret;
}

void* KeybordThread(void *p)
{
    int ret;
    struct input_event event;

    printf("KeybordThread start.\n");

    while (Processing)
    {
        ret = ReadCheck(fKeyboard);
        if (ret <= 0)
        {
            continue;
        }

        ret = read(fKeyboard, &event, sizeof(event));
        if (ret != sizeof(event))
        {
            printf("Keybord read error %d.\n", errno);
            Processing = 0;
            continue;
        }

        if (event.type == EV_KEY)
        {
            //printf("code=0x%04x value=0x%08x.\n", event.code, event.value);
            //event.value is 0=Off, 1=On, 2=Repeat

            if (event.value == 2)
            {
                //do nothing
                continue;
            }

            //update keyboard data
            KeyMap[event.code] = event.value;

            if (KeyMap[KEY_Z])
            {
                //super jump to base
                ReturnToBase = 1;
            }

            if (KeyMap[KEY_7])
            {
                RapidFireWait = 4;
            }

            if (KeyMap[KEY_8])
            {
                RapidFireWait = 1;
            }

            if (KeyMap[KEY_9])
            {
                if (MWBtnToggle)
                {
                    //main weapon button(Mouse L) is single shot mode
                    MWBtnToggle = 0;
                    RapidFireCnt = 0;
                }
                else
                {
                    //main weapon button(Mouse L) is rapid fire mode
                    MWBtnToggle = 1;
                    RapidFireCnt = 0;
                }
                printf("MWBtnToggle=%d\n", MWBtnToggle);
            }

            Slow = KeyMap[KEY_LEFTSHIFT];

            //debug
            //Adjust mouse sensitivity
            if (KeyMap[KEY_F5])
            {
                XSensitivity += 0.1f;
                printf("X_SENSITIVITY=%f\n", XSensitivity);
            }

            if (KeyMap[KEY_F6])
            {
                XSensitivity -= 0.1f;
                printf("X_SENSITIVITY=%f\n", XSensitivity);
            }

            if (KeyMap[KEY_F7])
            {
                YSensitivity += 0.1f;
                printf("Y_SENSITIVITY=%f\n", YSensitivity);
            }

            if (KeyMap[KEY_F8])
            {
                YSensitivity -= 0.1f;
                printf("Y_SENSITIVITY=%f\n", YSensitivity);
            }

            if (KeyMap[KEY_F9])
            {
                YFollowing += 0.1f;
                printf("Y_FOLLOWING=%f\n", YFollowing);
            }

            if (KeyMap[KEY_F10])
            {
                YFollowing -= 0.1f;
                printf("Y_FOLLOWING=%f\n", YFollowing);
            }
        }
    }

    printf("KeybordThread exit.\n");
    return NULL;
}

void* MouseThread(void *p)
{
    int ret;
    struct input_event event;

    printf("MouseThread start.\n");

    while (Processing)
    {
        ret = ReadCheck(fMouse);
        if (ret <= 0)
        {
            continue;
        }

        ret = read(fMouse, &event, sizeof(event));
        if (ret != sizeof(event))
        {
            printf("Mouse read error %d.\n", errno);
            Processing = 0;
            continue;
        }

        if (event.type == EV_KEY)
        {
            //printf("code=0x%04x value=0x%08x.\n", event.code, event.value);
            //event.value is 0=Off, 1=On, 2=Repeat

            if (event.value == 2)
            {
                //do nothing
                continue;
            }

            pthread_mutex_lock(&MouseMtx);

            switch (event.code)
            {
            case BTN_LEFT:
                MouseMap.L = event.value;
                break;
            case BTN_RIGHT:
                MouseMap.R = event.value;
                break;
            case BTN_MIDDLE:
                MouseMap.Middle = event.value;
                break;
            case BTN_SIDE:
                MouseMap.Side = event.value;
                break;
            case BTN_EXTRA:
                MouseMap.Extra = event.value;
                break;
            default:
                break;
            }

            pthread_mutex_unlock(&MouseMtx);
        }
        else if (event.type == EV_REL)
        {
            //printf("code=0x%04x value=0x%08x.\n", event.code, event.value);

            pthread_mutex_lock(&MouseMtx);

            switch (event.code)
            {
            case REL_X:
                MouseMap.X += event.value;
                break;
            case REL_Y:
                MouseMap.Y += event.value;
                break;
            case REL_WHEEL:
                MouseMap.Wheel = event.value;
                break;
            default:
                break;
            }

            pthread_mutex_unlock(&MouseMtx);
        }
    }

    printf("MouseThread exit.\n");
    return NULL;
}

unsigned short XValGet(unsigned char *pBuf)
{
    unsigned short ret;

    ret = ((short)pBuf[1] & 0x0F) << 8;
    ret |= (short)pBuf[0];

    return ret;
}

unsigned short YValGet(unsigned char *pBuf)
{
    unsigned short ret;

    ret = (short)pBuf[1] >> 4;
    ret |= (short)pBuf[2] << 4;

    return ret;
}

void XValSet(unsigned char *pBuf, unsigned short X)
{
    pBuf[0] = (unsigned char)(X & 0x00FF);
    pBuf[1] &= 0xF0;
    pBuf[1] |= (unsigned char)((X >> 8) & 0x000F);
}

void YValSet(unsigned char *pBuf, unsigned short Y)
{
    pBuf[1] &= 0x0F;
    pBuf[1] |= (unsigned char)((Y << 4) & 0x00F0);
    pBuf[2] = (unsigned char)((Y >> 4) & 0x00FF);
}

void* OutputReportThread(void *p)
{
    int ret;
    int len;
    int i;
    unsigned int spiAddr;
    unsigned char timStamp;
    unsigned char rd[MAX_PACKET_LEN];
    unsigned char wt[MAX_PACKET_LEN];

    printf("OutputReportThread start.\n");

    timStamp = 0;
    while (Processing)
    {
        ret = ReadCheck(fGadget);
        if (ret <= 0)
        {
            continue;
        }

        ret = read(fGadget, rd, sizeof(rd));
        if (ret == -1)
        {
            printf("Gadget OutputReport read error %d.\n", errno);
            Processing = 0;
            continue;
        }

        if (ret == 0)
        {
            continue;
        }

        memset(wt, 0, sizeof(wt));
        len = 0;

        switch (rd[0])
        {
        case 0x00:
            //do nothing
            break;
        case 0x01:
            //https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md

            if (rd[10] == 0x01)
            {
                //Subcommand 0x01: Bluetooth manual pairing
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x81;
                wt[14] = rd[10];
                wt[15] = 0x03;      //saves pairing info in Joy-Con
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x02)
            {
                //Subcommand 0x02: Request device info
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x82;
                wt[14] = rd[10];
                wt[15] = 0x03;      //firm ver 3.89
                wt[16] = 0x48;      //firm ver 3.89
                wt[17] = 0x03;      //Pro Controller
                wt[18] = 0x02;      //always 0x02
                //Gyro data must be encrypted for firmware version 4.00 and above

                //MAC address in Big Endian
                memcpy(&wt[19], &pRomBuf[21], MAC_ADDRESS_LEN);
                wt[25] = 0x01;      //always 0x01
                wt[26] = 0x01;      //If 0x01, colors in SPI are used

                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x03)
            {
                //Subcommand 0x03: Set input report mode
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x04)
            {
                //Subcommand 0x04: Trigger buttons elapsed time
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x83;
                wt[14] = rd[10];
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x06)
            {
                //Subcommand 0x06: Set HCI state (disconnect/page/pair/turn off)
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x08)
            {
                //Subcommand 0x08: Set shipment low power state
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x10)
            {
                //Subcommand 0x10: SPI flash read
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x90;      //subcommand reply
                wt[14] = rd[10];    //subcommand reply

                memcpy(&spiAddr, &rd[11], 4);
                memcpy(&wt[15], &spiAddr, sizeof(spiAddr));   //spi address

                wt[19] = rd[15];    //length
                memcpy(&wt[20], &pRomBuf[spiAddr], wt[19]);
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x11)
            {
                //Subcommand 0x11: SPI flash Write
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;      //subcommand reply
                wt[14] = rd[10];    //subcommand reply
                wt[15] = 0x00;      //success=0x00, write protect=0x01
                len = MAX_PACKET_LEN;

                //write data
                memcpy(&spiAddr, &rd[11], 4);
                memcpy(&pRomBuf[spiAddr], &rd[16], rd[15]);
            }
            else if (rd[10] == 0x12)
            {
                //Subcommand 0x12: SPI sector erase
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;      //subcommand reply
                wt[14] = rd[10];    //subcommand reply
                wt[15] = 0x00;      //success=0x00, write protect=0x01
                len = MAX_PACKET_LEN;

                //erase data
                memcpy(&spiAddr, &rd[11], 4);
                memset(&pRomBuf[spiAddr], 0xFF, rd[15]);
            }
            else if (rd[10] == 0x21)
            {
                //Subcommand 0x21: Set NFC/IR MCU configuration
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = rd[11];
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x22)
            {
                //Subcommand 0x22: Set NFC/IR MCU state
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;      //suspend=0x00, resume=0x01, resume for update=0x02
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x30)
            {
                //Subcommand 0x30: Set player lights
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x33)
            {
                //https://greggman.github.io/html5-gamepad-test/
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x03;
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x40)
            {
                //Subcommand 0x40: Enable IMU (6-Axis sensor)
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;

                GyroEnable = 1;
            }
            else if (rd[10] == 0x41)
            {
                //https://greggman.github.io/html5-gamepad-test/
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;
            }
            else if (rd[10] == 0x48)
            {
                //Subcommand 0x48: Enable vibration
                wt[0] = 0x21;
                wt[1] = timStamp++;
                memcpy(&wt[2], BakupProconData, sizeof(BakupProconData));
                wt[13] = 0x80;
                wt[14] = rd[10];
                wt[15] = 0x00;
                len = MAX_PACKET_LEN;
            }
            else
            {
                //Add commands if needed
                printf("Output Report=[0]:0x%02x [10]:0x%02x\n", rd[0], rd[10]);
            }
            break;
        case 0x10:
            //do noting
            break;
        case 0x80:
            //https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/USB-HID-Notes.md

            if (rd[1] == 0x01)
            {
                //get mac address
                wt[0] = 0x81;
                wt[1] = rd[1];
                wt[2] = 0x00;
                wt[3] = 0x03;

                //FlashRom store MacAddress in reverse order.
                for (i = 0; i < MAC_ADDRESS_LEN; i++)
                {
                    wt[4 + i] = pRomBuf[26 - i];
                }

                len = MAX_PACKET_LEN;
            }
            else if (rd[1] == 0x02)
            {
                //hand shake
                wt[0] = 0x81;
                wt[1] = rd[1];
                len = MAX_PACKET_LEN;
            }
            else if (rd[1] == 0x03)
            {
                //baudrate to 3Mbit
                wt[0] = 0x81;
                wt[1] = rd[1];
                len = MAX_PACKET_LEN;
            }
            else if (rd[1] == 0x04)
            {
                //hid mode
                HidMode = 1;
                //no response
            }
            else if (rd[1] == 0x05)
            {
                //bt mode
                HidMode = 0;
                //no response
            }
            else
            {
                //Add commands if needed
                printf("Output Report=[0]:0x%02x [1]:0x%02x\n", rd[0], rd[1]);
            }
            break;
        default:
            //Add commands if needed
            printf("Output Report=[0]:0x%02x\n", rd[0]);
            break;
        }

        if (len)
        {
            pthread_mutex_lock(&UsbMtx);
            ret = write(fGadget, &wt, len);
            pthread_mutex_unlock(&UsbMtx);

            if (ret == -1)
            {
                printf("Gadget OutputReport write error %d.\n", errno);
                Processing = 0;
                continue;
            }
        }
    }

    printf("OutputReportThread exit.\n");
    return NULL;
}

void StickInputL(unsigned char *pAxis, unsigned char Dir)
{
    int stopping = 0;

    if ((Dir == 0) && (MouseMap.Side == 1))
    {
        //Player is not pressing the L stick and Squid condition
        if (DirPrevCnt <= MOVE_STOP_TIME)
        {
            //停止まで方向入力を維持
            DirPrevCnt++;
            Dir = DirPrev;
            stopping = 1;
        }
        else
        {
            //動きを止める
            DirPrev = 0;
            RollKeyTick = 0;
        }
    }
    else if ((Dir) && (MouseMap.Side == 1))
    {
        //イカダッシュ中
        DirPrevCnt = 0;
        RollKeyTick++;
    }

    if (Slow)
    {
        //ゆっくりイカ移動
        DirPrevCnt = MOVE_STOP_TIME + 1;
        RollKeyTick = 0;
    }

    if (Dir == STICK_DIR_UP)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER + StraightHalf);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER);
                YValSet(pAxis, AXIS_CENTER + (StraightHalf / 2));
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER);
                YValSet(pAxis, AXIS_CENTER + Straight);
            }
        }
    }
    else if (Dir == STICK_DIR_RIGHT_UP)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER + DiagonalHalf);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER + (DiagonalHalf / 2));
                YValSet(pAxis, AXIS_CENTER + (DiagonalHalf / 2));
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER + Diagonal);
                YValSet(pAxis, AXIS_CENTER + Diagonal);
            }
        }
    }
    else if (Dir == STICK_DIR_RIGHT)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + StraightHalf);
            YValSet(pAxis, AXIS_CENTER);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER + (StraightHalf / 2));
                YValSet(pAxis, AXIS_CENTER);
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER + Straight);
                YValSet(pAxis, AXIS_CENTER);
            }
        }

    }
    else if (Dir == STICK_DIR_RIGHT_DOWN)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER - DiagonalHalf);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER + (DiagonalHalf / 2));
                YValSet(pAxis, AXIS_CENTER - (DiagonalHalf / 2));
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER + Diagonal);
                YValSet(pAxis, AXIS_CENTER - Diagonal);
            }
        }
    }
    else if (Dir == STICK_DIR_DOWN)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER - StraightHalf);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER);
                YValSet(pAxis, AXIS_CENTER - (StraightHalf / 2));
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER);
                YValSet(pAxis, AXIS_CENTER - Straight);
            }
        }
    }
    else if (Dir == STICK_DIR_LEFT_DOWN)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER - DiagonalHalf);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER - (DiagonalHalf / 2));
                YValSet(pAxis, AXIS_CENTER - (DiagonalHalf / 2));
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER - Diagonal);
                YValSet(pAxis, AXIS_CENTER - Diagonal);
            }
        }
    }
    else if (Dir == STICK_DIR_LEFT)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - StraightHalf);
            YValSet(pAxis, AXIS_CENTER);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER - (StraightHalf / 2));
                YValSet(pAxis, AXIS_CENTER);
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER - Straight);
                YValSet(pAxis, AXIS_CENTER);
            }
        }
    }
    else if (Dir == STICK_DIR_LEFT_UP)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER + DiagonalHalf);
        }
        else
        {
            if (stopping)
            {
                XValSet(pAxis, AXIS_CENTER - (DiagonalHalf / 2));
                YValSet(pAxis, AXIS_CENTER + (DiagonalHalf / 2));
            }
            else
            {
                XValSet(pAxis, AXIS_CENTER - Diagonal);
                YValSet(pAxis, AXIS_CENTER + Diagonal);
            }
        }
    }
    else
    {
        //no input
        XValSet(pAxis, AXIS_CENTER);
        YValSet(pAxis, AXIS_CENTER);
    }

#ifdef SQUID_ROLL_ENABLE
    if (MouseMap.Side == 0)
    {
        RollKeyTick = 0;
    }

    if (KeyMap[KEY_SPACE] == 1)
    {
        RollKeyTick = 0;
    }

    if (MouseMap.L == 1)
    {
        RollKeyTick = 0;
    }

    if (MouseMap.R == 1)
    {
        RollKeyTick = 0;
    }

    if (MouseMap.Extra == 1)
    {
        RollKeyTick = 0;
    }

    if (RollKeyTick >= ROLL_INPUT_TIME)
    {
        //printf("rool tick=%d\n", RollKeyTick);

        //イカロール方向を指定する

        if (RollDirPrev == STICK_DIR_UP)
        {
            //前
            if ((Dir == STICK_DIR_DOWN) || (Dir == STICK_DIR_LEFT_DOWN) || (Dir == STICK_DIR_RIGHT_DOWN))
            {
                //下、右下、左下、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_RIGHT_UP)
        {
            //右上
            if ((Dir == STICK_DIR_LEFT) || (Dir == STICK_DIR_DOWN) || (Dir == STICK_DIR_LEFT_DOWN))
            {
                //左、下、左下、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_RIGHT)
        {
            //右
            if ((Dir == STICK_DIR_LEFT) || (Dir == STICK_DIR_LEFT_DOWN) || (Dir == STICK_DIR_LEFT_UP))
            {
                //左、左下、左上、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_RIGHT_DOWN)
        {
            //右下
            if ((Dir == STICK_DIR_LEFT) || (Dir == STICK_DIR_UP) || (Dir == STICK_DIR_LEFT_UP))
            {
                //左、上、右上、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_DOWN)
        {
            //下
            if ((Dir == STICK_DIR_UP) || (Dir == STICK_DIR_LEFT_UP) || (Dir == STICK_DIR_RIGHT_UP))
            {
                //上、左上、右上、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_LEFT_DOWN)
        {
            //左下
            if ((Dir == STICK_DIR_RIGHT) || (Dir == STICK_DIR_UP) || (Dir == STICK_DIR_RIGHT_UP))
            {
                //右、上、右上、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_LEFT)
        {
            //左
            if ((Dir == STICK_DIR_RIGHT) || (Dir == STICK_DIR_RIGHT_DOWN) || (Dir == STICK_DIR_RIGHT_UP))
            {
                //右、右下、右上、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }

        if (RollDirPrev == STICK_DIR_LEFT_UP)
        {
            //左上
            if ((Dir == STICK_DIR_DOWN) || (Dir == STICK_DIR_RIGHT) || (Dir == STICK_DIR_RIGHT_DOWN))
            {
                //下、右、右下、イカロール
                RollOn = 1;
                RollTick = 0;
            }
        }
    }
#endif

    RollDirPrev = Dir;
    DirPrev = Dir;
}

void StickInputR(unsigned char *pAxis, unsigned char Dir)
{
    if (Dir == STICK_DIR_UP)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER + StraightHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER + Straight);
        }
    }
    else if (Dir == STICK_DIR_RIGHT_UP)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER + DiagonalHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER + Diagonal);
            YValSet(pAxis, AXIS_CENTER + Diagonal);
        }
    }
    else if (Dir == STICK_DIR_RIGHT)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + StraightHalf);
            YValSet(pAxis, AXIS_CENTER);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER + Straight);
            YValSet(pAxis, AXIS_CENTER);
        }

    }
    else if (Dir == STICK_DIR_RIGHT_DOWN)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER - DiagonalHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER + Diagonal);
            YValSet(pAxis, AXIS_CENTER - Diagonal);
        }
    }
    else if (Dir == STICK_DIR_DOWN)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER - StraightHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER - Straight);
        }
    }
    else if (Dir == STICK_DIR_LEFT_DOWN)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER - DiagonalHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER - Diagonal);
            YValSet(pAxis, AXIS_CENTER - Diagonal);
        }
    }
    else if (Dir == STICK_DIR_LEFT)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - StraightHalf);
            YValSet(pAxis, AXIS_CENTER);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER - Straight);
            YValSet(pAxis, AXIS_CENTER);
        }
    }
    else if (Dir == STICK_DIR_LEFT_UP)
    {
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - DiagonalHalf);
            YValSet(pAxis, AXIS_CENTER + DiagonalHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER - Diagonal);
            YValSet(pAxis, AXIS_CENTER + Diagonal);
        }
    }
    else
    {
        //no input
        XValSet(pAxis, AXIS_CENTER);
        YValSet(pAxis, AXIS_CENTER);
    }
}

void GyroEmurate(ProconData *pPad)
{
    ProconGyroData gyro;

    memset(&gyro, 0, sizeof(gyro));

    //Z Angle do not change.
    gyro.Z_Angle = 4096;

    //Add Y Angle
    YTotal += (int32_t)((float)MouseMap.Y * YFollowing * -1);

    if (YTotal > Y_ANGLE_UPPPER_LIMIT)
    {
        //upper limit
        YTotal = Y_ANGLE_UPPPER_LIMIT;
    }

    if (YTotal < Y_ANGLE_LOWER_LIMIT)
    {
        //lower limit
        YTotal = Y_ANGLE_LOWER_LIMIT;
    }

    gyro.Y_Angle = YTotal;
    //printf("YTotal=%d\n", YTotal);

    //Up,down
    if ((gyro.Y_Angle != Y_ANGLE_UPPPER_LIMIT) && (gyro.Y_Angle != Y_ANGLE_LOWER_LIMIT))
    {
        gyro.Y_Accel = (short)((float)MouseMap.Y * YSensitivity);
    }

    gyro.Z_Accel = (short)((float)MouseMap.X * XSensitivity);
    gyro.Z_Accel *= -1;

    if (gyro.Z_Accel < Y_ACCEL_LOWER_LIMIT)
    {
        //lower limit
        gyro.Z_Accel = -Y_ACCEL_LOWER_LIMIT;
    }

    if (gyro.Z_Accel > Y_ACCEL_UPPPER_LIMIT)
    {
        //upper limmit
        gyro.Z_Accel = Y_ACCEL_UPPPER_LIMIT;
    }

    //ジャイロデータは3サンプル分（1サンプル5ms）のデータを格納する
    //コンバータでは同じデータを3つ格納する

    memcpy(&pPad->GyroData[0], &gyro, sizeof(gyro));
    memcpy(&pPad->GyroData[12], &gyro, sizeof(gyro));
    memcpy(&pPad->GyroData[24], &gyro, sizeof(gyro));

    //マウスは変化があった場合にデータが来る、よってXYに値が残っている
    MouseMap.X = 0;
    MouseMap.Y = 0;
}

//super jump
void ReturnToBaseMacro(ProconData *pPad)
{
    if (ReturnToBase)
    {
        pPad->R = 0;
        pPad->L = 0;
        pPad->ZL = 0;
        pPad->ZR = 0;

        pPad->A = 0;
        pPad->B = 0;
        pPad->X = 0;
        pPad->Y = 0;

        pPad->Up = 0;
        pPad->Down = 0;
        pPad->Left = 0;
        pPad->Right = 0;

        switch (ReturnToBaseCnt)
        {
        case 0:
        case 1:
        case 2:
            pPad->X = 1;
            ReturnToBaseCnt++;
            break;
        case 3:
        case 4:
        case 5:
            pPad->X = 1;
            pPad->Down = 1;
            ReturnToBaseCnt++;
            break;
        case 6:
        case 7:
        case 8:
            pPad->A = 1;
            pPad->X = 1;
            pPad->Down = 1;
            ReturnToBaseCnt++;
            break;
        case 9:
        case 10:
        case 11:
            pPad->A = 1;
            pPad->X = 1;
            ReturnToBaseCnt++;
            break;
        default:
            ReturnToBaseCnt = 0;
            ReturnToBase = 0;
            break;
        }
    }
}

#ifdef INERTIA_CANCEL_ENABLE
void InertiaCancel(ProconData *pPad)
{
    switch (InertiaCancelCnt)
    {
    case 0:
    case 1:
    case 2:
        pPad->R = 1;
        InertiaCancelCnt++;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        pPad->R = 1;
        pPad->ZL = 1;
        InertiaCancelCnt++;
        break;
    default:
        pPad->ZL = 1;
    }
}
#endif

void DoSquidRoll(ProconData *pPad)
{
    if (RollOn == 0)
    {
        return;
    }

    if (RollTick < ROLL_JUMP_TIME)
    {
        //イカロールのジャンプ入力
        pPad->B = 1;
        RollTick++;
    }
    else
    {
        RollOn = 0;
        RollTick = 0;
        RollKeyTick = 0;
    }
}

unsigned char DoRapidFire(void)
{
    RapidFireCnt ++;

    if(RapidFireCnt <= RapidFireWait)
    {
        return 1;
    }
    else
    {
        if (RapidFireCnt >= (RapidFireWait << 1))
        {
            RapidFireCnt = 0;
        }
    }
    return 0;
}

void ProconInput(ProconData *pPad)
{
    unsigned char dir;

    //通常利用の範囲では桁あふれしない
    InputTick++;

    //常にON
    pPad->Grip = 1;

    //key
    if (KeyMap[KEY_1])
    {
        //視点リセット
        pPad->Y = 1;
        YTotal = 0;
    }

    if (KeyMap[KEY_2] == 1)
    {
        //キャプチャー
        pPad->Capture = 1;
    }

    if (KeyMap[KEY_3] == 1)
    {
        //マイナス
        pPad->Minus = 1;
    }

    if (KeyMap[KEY_4] == 1)
    {
        //プラス
        pPad->Plus = 1;
    }

    if (KeyMap[KEY_ESC] == 1)
    {
        //ホーム
        pPad->Home = 1;
    }

    if (KeyMap[KEY_E] == 1)
    {
        //スーパージャンプ決定
        //アサリ
        pPad->A = 1;
    }

    if (KeyMap[KEY_R] == 1)
    {
        //マップ
        pPad->X = 1;
    }

    if (KeyMap[KEY_F] == 1)
    {
        //カモン
        pPad->Up = 1;
    }

    if (KeyMap[KEY_C] == 1)
    {
        //ナイス
        pPad->Down = 1;
    }

    if (KeyMap[KEY_SPACE] == 1)
    {
        //ジャンプ
        pPad->B = 1;
        JumpTick = InputTick;
    }

    if (KeyMap[KEY_T] == 1)
    {
        //L
        pPad->L = 1;
    }

    if (KeyMap[KEY_Y] == 1)
    {
        //R
        pPad->R = 1;
    }

    if (KeyMap[KEY_G] == 1)
    {
        //ZL
        pPad->ZL = 1;
    }

    if (KeyMap[KEY_H] == 1)
    {
        //ZR
        pPad->ZR = 1;
    }

    if (KeyMap[KEY_U] == 1)
    {
        //L Stick
        pPad->StickL = 1;
    }

    if (KeyMap[KEY_I] == 1)
    {
        //R Stick
        pPad->StickR = 1;
    }

    if (KeyMap[KEY_L] == 1)
    {
        //tesla menu
        pPad->L = 1;
        pPad->Down = 1;
        pPad->StickR = 1;
    }

    if (KeyMap[KEY_KP8] == 1)
    {
        pPad->Up = 1;
    }

    if (KeyMap[KEY_KP2] == 1)
    {
        pPad->Down = 1;
    }

    if ((KeyMap[KEY_KP4] == 1) || (KeyMap[KEY_COMMA] == 1))
    {
        pPad->Left = 1;
    }

    if ((KeyMap[KEY_KP6] == 1) || (KeyMap[KEY_DOT] == 1))
    {
        pPad->Right = 1;
    }

    //printf("StickL X=%d, Y=%d\n", XValGet(pPad->L_Axis), YValGet(pPad->L_Axis));
    //printf("StickR X=%d, Y=%d\n", XValGet(pPad->R_Axis), YValGet(pPad->R_Axis));

    //L Stick assert WASD
    dir = KeyMap[KEY_W] << 3;
    dir |= KeyMap[KEY_D] << 2;
    dir |= KeyMap[KEY_S] << 1;
    dir |= KeyMap[KEY_A];

    StickInputL(pPad->L_Axis, dir);
#ifdef SQUID_ROLL_ENABLE
    DoSquidRoll(pPad);
#endif

    //R Stick assert arrow UP,DOWN,LEFT,RIGHT
    dir = KeyMap[KEY_UP] << 3;
    dir |= KeyMap[KEY_RIGHT] << 2;
    dir |= KeyMap[KEY_DOWN] << 1;
    dir |= KeyMap[KEY_LEFT];

    StickInputR(pPad->R_Axis, dir);

    //printf("StickL X=%d, Y=%d\n", XValGet(pPad->L_Axis), YValGet(pPad->L_Axis));
    //printf("StickR X=%d, Y=%d\n", XValGet(pPad->R_Axis), YValGet(pPad->R_Axis));

    pthread_mutex_lock(&MouseMtx);

    //mouse
    if (GyroEnable)
    {
        GyroEmurate(pPad);
    }

    if (MouseMap.R)
    {
        //サブ
        pPad->R = 1;
        SubWpTick = InputTick;
    }

    if (MouseMap.L)
    {

        if (MWBtnToggle == 0)
        {
            //メイン単発
            pPad->ZR = 1;
        }
        else
        {
            //メイン連射
            pPad->ZR = DoRapidFire();
        }

        MainWpTick = InputTick;
    }

#ifdef INERTIA_CANCEL_ENABLE
    if (MouseMap.Side)
    {
        if (((JumpTick + DELEY_FOR_AFTER_JUMP) < InputTick) &&
            ((MainWpTick + DELEY_FOR_AFTER_MAIN_WP) < InputTick) &&
            ((SubWpTick + DELEY_FOR_AFTER_SUB_WP) < InputTick))
        {
            RollOn = 0;
            InertiaCancel(pPad);
        }
        else
        {
            //ジャンプ、メイン、サブ実施後は慣性キャンセル無しでイカになる
            pPad->ZL = 1;
            InertiaCancelCnt = 0xFF;
        }
    }
    else
    {
        InertiaCancelCnt = 0;
    }
#else
    if (MouseMap.Side)
    {
        pPad->ZL = 1;
    }
#endif

    if (MouseMap.Extra)
    {
        if (MWBtnToggle == 0)
        {
            //メイン連射
            pPad->ZR = DoRapidFire();
        }
        else
        {
            //メイン単発
            pPad->ZR = 1;
        }
    }

    if (MouseMap.Wheel)
    {
        //スペシャル、マウスホイールを動かす
        pPad->StickR = 1;
        MouseMap.Wheel = 0;
    }

    if (MouseMap.Middle)
    {
        //スペシャル
        pPad->StickR = 1;
    }

    pthread_mutex_unlock(&MouseMtx);

    ReturnToBaseMacro(pPad);
}

void* InputReportThread(void *p)
{
    int ret;
    unsigned char *ptr;
    ProconData procon;
    unsigned char timStamp;
    struct timespec wait;

    printf("InputReportThread start.\n");

    timStamp = 0;
    wait.tv_sec = 0;
    wait.tv_nsec = PAD_INPUT_WAIT * 1000000;

    while (Processing)
    {
        nanosleep(&wait, NULL);

        if (HidMode)
        {
            memset(&procon, 0, sizeof(procon));
            timStamp += (PAD_INPUT_WAIT / 8);

            procon.ReportId = 0x30;
            procon.TimeStamp = timStamp;
            procon.ConnectNo = 1;
            procon.BatteryLevel = 9;
            ProconInput(&procon);

            ptr = (unsigned char *)&procon;
            memcpy(BakupProconData, &ptr[2], sizeof(BakupProconData));

            pthread_mutex_lock(&UsbMtx);
            ret = write(fGadget, &procon, sizeof(procon));
            pthread_mutex_unlock(&UsbMtx);

            if (ret == -1)
            {
                printf("fGadget InputReport write error %d.\n", errno);
                Processing = 0;
                continue;
            }
        }
    }

    printf("InputReportThread exit.\n");
    return NULL;
}

int InputDevNameGet(int DevType, char *pSearchName, char *pDevName)
{
    int found;
    DIR *dir;
    struct dirent *dp;
    char *p;

    dir = opendir("/dev/input/by-id");
    if (dir == NULL)
    {
        printf("opendir error %d", errno);
        return -1;
    }

    found = -1;
    dp = readdir(dir);
    while (dp != NULL)
    {
        if (DevType == DEV_KEYBOARD)
        {
            //Keyboard
            p = strstr(dp->d_name, "event-kbd");
            if (p != NULL)
            {
#ifdef ENUM_HID_DEVICE
                printf("enum keyboard:%s\n", dp->d_name);
#endif
                p = strstr(dp->d_name, pSearchName);
                if (p != NULL)
                {
                    sprintf(pDevName, "/dev/input/by-id/%s", dp->d_name);
                    printf("Keyboard:%s\n", pDevName);
                    found = 0;
                    break;
                }
            }
        }
        else
        {
            //Mouse
            p = strstr(dp->d_name, "event-mouse");
            if (p != NULL)
            {
#ifdef ENUM_HID_DEVICE
                printf("enum mouse:%s\n", dp->d_name);
#endif
                p = strstr(dp->d_name, pSearchName);
                if (p != NULL)
                {
                    sprintf(pDevName, "/dev/input/by-id/%s", dp->d_name);
                    printf("Mouse:%s\n", pDevName);
                    found = 0;
                    break;
                }
            }
        }

        dp = readdir(dir);
    }

    closedir(dir);
    return found;
}

void Echo(int enable)
{
    struct termios term;

    tcgetattr(STDIN_FILENO, &term);

    if (enable)
    {
        term.c_lflag |= ECHO;
    }
    else
    {
        term.c_lflag &= ~ECHO;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void SigIntHandler()
{
    printf("SIGINT detect.\n");
    system(GADGET_DETACH);
    Processing = 0;
}

int main(int argc, char *argv[])
{
    int ret;
    int fd;
    struct stat fst;
    char devName[MAX_NAME_LEN];

    printf("Procon Converter start.\n");

    XSensitivity = X_SENSITIVITY;
    YSensitivity = Y_SENSITIVITY;
    YFollowing = Y_FOLLOWING;
    Processing = 1;
    fKeyboard = -1;
    fMouse = -1;
    fGadget = -1;
    YTotal = 0;
    Straight = AXIS_MAX_INPUT;
    StraightHalf = (int)((float)Straight * AXIS_HALF_INPUT_FACTOR);
    Diagonal = (int)(0.7071f * (float)AXIS_MAX_INPUT); //0.7071 is cos 45
    DiagonalHalf = (int)((float)Diagonal * AXIS_HALF_INPUT_FACTOR);

    HidMode = 0;
    GyroEnable = 0;
    memset(BakupProconData, 0, sizeof(BakupProconData));

    RapidFireCnt = 0;
    RapidFireWait = 1;

    Echo(0);
    signal(SIGINT, SigIntHandler);

    pthread_mutex_init(&MouseMtx, NULL);
    pthread_mutex_init(&UsbMtx, NULL);

    if (stat(ROM_FILE_NAME, &fst) < 0)
    {
        printf("%s not found.\n", ROM_FILE_NAME);
        Processing = 0;
        goto EXIT;
    }
    RomSize = fst.st_size;

    pRomBuf = malloc(RomSize);
    if (pRomBuf == NULL)
    {
        printf("malloc error.\n");
        Processing = 0;
        goto EXIT;
    }

    fd = open(ROM_FILE_NAME, O_RDONLY);
    if (fd < 0)
    {
        printf("open error.\n");
        Processing = 0;
        goto EXIT;
    }

    //https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/spi_flash_notes.md
    if (read(fd, pRomBuf, (size_t)RomSize) <= 0)
    {
        printf("read error.\n");
        close(fd);
        Processing = 0;
        goto EXIT;
    }

    close(fd);

    ret = InputDevNameGet(DEV_KEYBOARD, KEYBOARD_NAME, devName);
    if (ret == -1)
    {
        printf("Keybord not found.\n");
        Processing = 0;
        goto EXIT;
    }

    fKeyboard = open(devName, O_RDONLY);
    if (fKeyboard == -1)
    {
        printf("Keybord open error %d.\n", errno);
        Processing = 0;
        goto EXIT;
    }

    ret = InputDevNameGet(DEV_MOUSE, MOUSE_NAME, devName);
    if (ret == -1)
    {
        printf("Mouse not found.\n");
        Processing = 0;
        goto EXIT;
    }

    fMouse = open(devName, O_RDONLY);
    if (fMouse == -1)
    {
        printf("Mouse open error %d.\n", errno);
        Processing = 0;
        goto EXIT;
    }

    system(GADGET_DETACH);
    sleep(2);
    system(GADGET_ATTACH);

    fGadget = open(GADGET_NAME, O_RDWR);
    if (fGadget == -1)
    {
        printf("Gadget open error %d.\n", errno);
        Processing = 0;
        goto EXIT;
    }

    ret = pthread_create(&thInputReport, NULL, InputReportThread, NULL);
    if (ret != 0)
    {
        printf("InputReport thread create error %d.\n", ret);
        Processing = 0;
        goto EXIT;
    }
    thInputReportCreated = 1;

    ret = pthread_create(&thOutputReport, NULL, OutputReportThread, NULL);
    if (ret != 0)
    {
        printf("OutputReport thread create error %d.\n", ret);
        Processing = 0;
        goto EXIT;
    }
    thOutputReportCreated = 1;

    ret = pthread_create(&thKeyboard, NULL, KeybordThread, NULL);
    if (ret != 0)
    {
        printf("Keybord thread create error %d.\n", ret);
        Processing = 0;
        goto EXIT;
    }
    thKeyboardCreated = 1;

    ret = pthread_create(&thMouse, NULL, MouseThread, NULL);
    if (ret != 0)
    {
        printf("Mouse thread create error %d.\n", ret);
        Processing = 0;
        goto EXIT;
    }
    thMouseCreated = 1;

EXIT:

    if (thKeyboardCreated)
    {
        pthread_join(thKeyboard, NULL);
    }

    if (thMouseCreated)
    {
        pthread_join(thMouse, NULL);
    }

    if (thOutputReportCreated)
    {
        pthread_join(thOutputReport, NULL);
    }

    if (thInputReportCreated)
    {
        pthread_join(thInputReport, NULL);
    }

    if (fKeyboard != -1)
    {
        close(fKeyboard);
    }

    if (fMouse != -1)
    {
        close(fMouse);
    }

    if (fGadget != -1)
    {
        close(fGadget);
    }

    if (pRomBuf)
    {
        free(pRomBuf);
    }

    pthread_mutex_destroy(&MouseMtx);
    pthread_mutex_destroy(&UsbMtx);

    Echo(1);

    printf("Procon Converter exit.\n");
    return 0;
}

