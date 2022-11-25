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

/*
スプラトゥーンでは、左右はジャイロの加速度で判断する
上下はジャイロの加速度と角度で判断する
マウスの座標系とは異なるのでユーザー毎に調整が必要になる
*/
#define X_SENSITIVITY           (20.0f)     //マウス操作、左右感度
#define Y_SENSITIVITY           (23.0f)     //マウス操作、上下感度
#define Y_FOLLOWING             (1.50f)     //マウス操作、上下追従補正

//スティック入力値
#define AXIS_CENTER             (1920)
#define AXIS_MAX_INPUT          (1920)

/*
スロー速度を設定する。0.83fならば全速力の83%になる
イカ速に応じて調整が必要 
*/
#define AXIS_HALF_INPUT_FACTOR  (0.65f)

#define MOUSE_READ_COUNTER      (4)         //指定した回数に達した場合マウス操作がされていない事を示す
#define Y_ANGLE_UPPPER_LIMIT    (3000)      //Y上限
#define Y_ANGLE_LOWER_LIMIT     (-1500)     //Y下限

/*
キーボードの特性上、進行方向と逆方向に移行する場合、最速（15ms）で行わないと
無入力期間が入りイカダッシュの停止と判定されイカロールが失敗する
このため、方向入力を指定分だけ延長することで対処する
値が4なら4*15msの延長となる
値を大きくすれば、イカロールが出しやすくなるがイカダッシュの停止が遅れる
*/
#define DIR_FOLLOWING			(4)		//イカロールを行いやすくする

#define MAX_NAME_LEN            (256)
#define MAX_PACKET_LEN          (64)
#define MAX_BUFFER_LEN          (512)

#define GADGET_NAME             "/dev/hidg0"

#define GADGET_DETACH           "echo "" > /sys/kernel/config/usb_gadget/procon/UDC"
#define GADGET_ATTACH           "ls /sys/class/udc > /sys/kernel/config/usb_gadget/procon/UDC"

#define DEV_KEYBOARD            (0)
#define DEV_MOUSE               (1)

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
#define KEYBOARD_NAME       "Topre_Corporation_Realforce_108"
//#define KEYBOARD_NAME       "usb-SIGMACHIP_USB_Keyboard"

//#define MOUSE_NAME          "Logitech_G403_Prodigy_Gaming_Mouse"
#define MOUSE_NAME          "usb-Logitech_G403_HERO_Gaming_Mouse"

#define PROCON_NAME         "Nintendo Co., Ltd. Pro Controller"     //Procon名は固定

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

int GamePadMode;
int Processing;
int fProcon;
int fGadget;
int fKeyboard;
int fMouse;
int thKeyboardCreated;
int thMouseCreated;
int thOutputReportCreated;
int thInputReportCreated;
int thBannerCreated;
int MouseReadCnt;
int YTotal;
int Slow;
int IkaToggle;
int Straight;
int StraightHalf;
int Diagonal;
int DiagonalHalf;
int RappidFire;
int MWButtonToggle;
int ReturnToBase;
int ReturnToBaseCnt;
float XSensitivity;
float YSensitivity;
float YFollowing;
double CircleAngle;
pthread_t thKeyboard;
pthread_t thMouse;
pthread_t thOutputReport;
pthread_t thInputReport;
pthread_mutex_t MouseMtx;
MouseData MouseMap;
unsigned char DirPrev;
unsigned char DirPrevCnt;
unsigned char KeyMap[KEY_WIMAX];

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

    return ret;
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
            //event.valueの値は、0=Off、1=On、2＝Repeat

            if (event.value == 2)
            {
                //リピート時は何もしない
                continue;
            }

            KeyMap[event.code] = event.value;

            //設定処理
            if (KeyMap[KEY_Z])
            {
                //復活地点へスーパージャンプ
                ReturnToBase = 1;
            }

            if (KeyMap[KEY_8])
            {
                if (IkaToggle)
                {
                    IkaToggle = 0;
                }
                else
                {
                    IkaToggle = 1;
                }
                printf("IkaToggle=%d\n", IkaToggle);
            }

            if (KeyMap[KEY_9])
            {
                //メイン単発、連射ボタンを入れ替える
                if (MWButtonToggle)
                {
                    MWButtonToggle = 0;
                }
                else
                {
                    MWButtonToggle = 1;
                }
                printf("MWButtonToggle=%d\n", MWButtonToggle);
            }

            Slow = KeyMap[KEY_LEFTSHIFT];

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

            if (KeyMap[KEY_F11] == 1)
            {
                printf("Keybord mode.\n");
                GamePadMode = 0;
            }

            if (KeyMap[KEY_F12] == 1)
            {
                printf("GamePad mode.\n");
                GamePadMode = 1;
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
            //event.valueの値は、0=Off、1=On、2＝Repeat

            if (event.value == 2)
            {
                //リピート時は何もしない
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

            MouseReadCnt = 0;
            pthread_mutex_unlock(&MouseMtx);
        }
        else if (event.type == EV_REL)
        {
            //printf("code=0x%04x value=0x%08x.\n", event.code, event.value);

            pthread_mutex_lock(&MouseMtx);

            switch (event.code)
            {
            case REL_X:
                MouseMap.X = event.value;
                break;
            case REL_Y:
                MouseMap.Y = event.value;
                break;
            case REL_WHEEL:
                MouseMap.Wheel = event.value;
                break;
            default:
                break;
            }

            MouseReadCnt = 0;
            pthread_mutex_unlock(&MouseMtx);
        }
    }

    printf("MouseThread exit.\n");
    return NULL;
}

void* OutputReportThread(void *p)
{
    int ret;
    int len;
    unsigned char buf[MAX_PACKET_LEN];

    printf("OutputReportThread start.\n");

    while (Processing)
    {
        ret = ReadCheck(fGadget);
        if (ret <= 0)
        {
            continue;
        }

        ret = read(fGadget, buf, sizeof(buf));
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

        len = ret;
        ret = write(fProcon, &buf, len);
        if (ret == -1)
        {
            printf("Procon OutputReport write error %d.\n", errno);
            Processing = 0;
            continue;
        }
    }

    printf("OutputReportThread exit.\n");
    return NULL;
}

void StickDrawCircle(ProconData *pPad)
{
    double c;
    unsigned short Lx, Ly, Rx, Ry;

    CircleAngle += 0.05;

    c = sin(CircleAngle) * (double)AXIS_CENTER;
    Lx = (unsigned short)(c + (double)AXIS_CENTER);

    c = cos(CircleAngle) * (double)AXIS_CENTER;
    Ly = (unsigned short)(c + (double)AXIS_CENTER);

    c = sin(CircleAngle) * (double)AXIS_CENTER;
    Rx = (unsigned short)(c + (double)AXIS_CENTER);

    c = cos(CircleAngle) * (double)AXIS_CENTER;
    Ry = (unsigned short)(c + (double)AXIS_CENTER);

    XValSet(pPad->L_Axis, Lx);
    YValSet(pPad->L_Axis, Ly);
    XValSet(pPad->R_Axis, Rx);
    YValSet(pPad->R_Axis, Ry);
}

void StickInput(unsigned char *pAxis, unsigned char Dir)
{
    if (Dir == 0x08)
    {
        //上
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
    else if (Dir == 0x0c)
    {
        //右上
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
    else if (Dir == 0x04)
    {
        //右
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
    else if (Dir == 0x06)
    {
        //右下
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
    else if (Dir == 0x02)
    {
        //下
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
    else if (Dir == 0x03)
    {
        //左下
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
    else if (Dir == 0x01)
    {
        //左
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
    else if (Dir == 0x09)
    {
        //左上
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
        //入力無し
        XValSet(pAxis, AXIS_CENTER);
        YValSet(pAxis, AXIS_CENTER);
    }
}

void GyroEmurate(ProconData *pPad)
{
    ProconGyroData gyro;

    memset(&gyro, 0, sizeof(gyro));

    //X角度、Z角度変化しない
    gyro.Z_Angle = 4096;

    //Y角度合算
    YTotal += (int32_t)((float)MouseMap.Y * YFollowing * -1);

    if (YTotal > Y_ANGLE_UPPPER_LIMIT)
    {
        //これ以上進まないようにする
        YTotal = Y_ANGLE_UPPPER_LIMIT;
        MouseMap.Y = 0;
    }

    if (YTotal < Y_ANGLE_LOWER_LIMIT)
    {
        //これ以上進まないようにする
        YTotal = Y_ANGLE_LOWER_LIMIT;
        MouseMap.Y = 0;
    }

    gyro.Y_Angle = YTotal;
    //printf("YTotal=%d\n", YTotal);

    //上下
    gyro.Y_Accel = (short)((float)MouseMap.Y * YSensitivity);

    //左右
    gyro.Z_Accel = (short)((float)MouseMap.X * XSensitivity);
    //加速方向がマウスと逆なので逆転させる
    gyro.Z_Accel *= -1;

    //ジャイロデータは3サンプル分（1サンプル5ms）のデータを格納する
    //コンバータでは同じデータを3つ格納する
    memcpy(&pPad->GyroData[0], &gyro, sizeof(gyro));
    memcpy(&pPad->GyroData[12], &gyro, sizeof(gyro));
    memcpy(&pPad->GyroData[24], &gyro, sizeof(gyro));

    MouseReadCnt++;
    if (MouseReadCnt > MOUSE_READ_COUNTER)
    {
        //マウス操作無し、XYを0にする
        //マウスは変化があった場合にデータが来る、よってXYに値が残っている
        MouseMap.X = 0;
        MouseMap.Y = 0;
        MouseReadCnt = 0;
    }
}

/*
マクロサンプル
スタート地点へスーパージャンプする
ProconInputの呼び出し間隔は15msなのでProconInput内で呼ぶReturnToBaseMacroも 
15ms間隔で呼ばれることになる 
*/
void ReturnToBaseMacro(ProconData *pPad)
{
    if (ReturnToBase)
    {
        switch (ReturnToBaseCnt)
        {
        case 0:
        case 1:
        case 2:
            pPad->R = 0;
            pPad->L = 0;
            pPad->ZL = 0;
            pPad->ZR = 0;

            pPad->A = 0;
            pPad->B = 0;
            pPad->X = 1;
            pPad->Y = 0;

            pPad->Up = 0;
            pPad->Down = 0;
            pPad->Left = 0;
            pPad->Right = 0;

            ReturnToBaseCnt++;
            break;
        case 3:
        case 4:
        case 5:
            pPad->R = 0;
            pPad->L = 0;
            pPad->ZL = 0;
            pPad->ZR = 0;

            pPad->A = 0;
            pPad->B = 0;
            pPad->X = 1;
            pPad->Y = 0;

            pPad->Up = 0;
            pPad->Down = 1;
            pPad->Left = 0;
            pPad->Right = 0;

            ReturnToBaseCnt++;
            break;
        case 6:
        case 7:
        case 8:
            pPad->R = 0;
            pPad->L = 0;
            pPad->ZL = 0;
            pPad->ZR = 0;

            pPad->A = 1;
            pPad->B = 0;
            pPad->X = 1;
            pPad->Y = 0;

            pPad->Up = 0;
            pPad->Down = 1;
            pPad->Left = 0;
            pPad->Right = 0;

            ReturnToBaseCnt++;
            break;
        case 9:
        case 10:
        case 11:
            pPad->R = 0;
            pPad->L = 0;
            pPad->ZL = 0;
            pPad->ZR = 0;

            pPad->A = 1;
            pPad->B = 0;
            pPad->X = 1;
            pPad->Y = 0;

            pPad->Up = 0;
            pPad->Down = 0;
            pPad->Left = 0;
            pPad->Right = 0;

            ReturnToBaseCnt++;
            break;
        default:
            ReturnToBaseCnt = 0;
            ReturnToBase = 0;
            break;
        }
    }
}

void ProconInput(ProconData *pPad)
{
    unsigned char dir;

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
    }

    if (KeyMap[KEY_Q] == 1)
    {
        //視点センターリング
        YTotal = 0;
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

    if (KeyMap[KEY_U] == 1)
    {
        //L Stick
        pPad->StickL = 1;
    }

    if (KeyMap[KEY_KP8] == 1)
    {
        pPad->Up = 1;
    }

    if (KeyMap[KEY_KP2] == 1)
    {
        pPad->Down = 1;
    }

    if (KeyMap[KEY_KP4] == 1)
    {
        pPad->Left = 1;
    }

    if (KeyMap[KEY_KP6] == 1)
    {
        pPad->Right = 1;
    }

    //printf("StickL X=%d, Y=%d\n", XValGet(pPad->L_Axis), YValGet(pPad->L_Axis));
    //printf("StickR X=%d, Y=%d\n", XValGet(pPad->R_Axis), YValGet(pPad->R_Axis));

    //左スティック
    dir = KeyMap[KEY_W] << 3;
    dir |= KeyMap[KEY_D] << 2;
    dir |= KeyMap[KEY_S] << 1;
    dir |= KeyMap[KEY_A];

    if ((dir == 0) && (DirPrevCnt <= DIR_FOLLOWING))
    {
        DirPrevCnt++;
        StickInput(pPad->L_Axis, DirPrev);
    }
    else
    {
        StickInput(pPad->L_Axis, dir);
        DirPrev = dir;
        DirPrevCnt = 0;
    }

    //右スティック
    dir = KeyMap[KEY_UP] << 3;
    dir |= KeyMap[KEY_RIGHT] << 2;
    dir |= KeyMap[KEY_DOWN] << 1;
    dir |= KeyMap[KEY_LEFT];
    StickInput(pPad->R_Axis, dir);

    if (KeyMap[KEY_0] == 1)
    {
        //スティック補正のとき、0キーを押し続けてスティックぐるぐるをおこなう。
        StickDrawCircle(pPad);
    }
   
    pthread_mutex_lock(&MouseMtx);

    //mouse
    GyroEmurate(pPad);

    if (MouseMap.R)
    {
        //サブ
        pPad->R = 1;
    }

    if (MouseMap.L)
    {

        if (MWButtonToggle == 0)
        {
            //メイン単発
            pPad->ZR = 1;
        }
        else
        {
            //メイン連射
            if (RappidFire != 0)
            {
                pPad->ZR = 1;
                RappidFire = 0;
            }
            else
            {
                RappidFire = 1;
            }
        }
    }

    if (IkaToggle)
    {
        pPad->ZL = 1;
    }

    if (MouseMap.Side)
    {
        //イカ
        if (IkaToggle == 0)
        {
            pPad->ZL = 1;
        }
        else
        {
            pPad->ZL = 0;
        }
    }

    if (MouseMap.Extra)
    {
        if (MWButtonToggle == 0)
        {
            //メイン連射
            if (RappidFire != 0)
            {
                pPad->ZR = 1;
                RappidFire = 0;
            }
            else
            {
                RappidFire = 1;
            }
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
    int len;
    unsigned char buf[MAX_PACKET_LEN];

    printf("InputReportThread start.\n");

    while (Processing)
    {
        ret = ReadCheck(fProcon);
        if (ret <= 0)
        {
            continue;
        }

        ret = read(fProcon, buf, sizeof(buf));
        if (ret == -1)
        {
            printf("fProcon InputReport read error %d.\n", errno);
            Processing = 0;
            continue;
        }
        len = ret;

        if (len == 0)
        {
            continue;
        }


        if (buf[0] == 0x30)
        {
            if (GamePadMode == 0)
            {
                ProconInput((ProconData *)buf);
            }
            else 
            {
#if 0
                ProconGyroData gyro;
                memcpy(&gyro, ((ProconData *)buf)->GyroData, sizeof(gyro));
                printf("x1=%d y1=%d z1=%d x2=%d y2=%d z2=%d.\n", 
                       gyro.X_Angle,gyro.Y_Angle, gyro.Z_Angle,
                       gyro.X_Accel, gyro.Y_Accel, gyro.Z_Accel);
#endif
            }
        }
        else
        {
            if (buf[0] == 0x21)
            {
                if ((buf[13] == 0x90) && (buf[14] == 0x10))
                {
                    if ((buf[15] == 0x10) && (buf[16] == 0x80))
                    {
                        //スティック補正情報を変更する

                        //SPI address 0x8010, Magic 0xxB2 0xxA1 for user available calibration
                        buf[20] = 0xB2;
                        buf[21] = 0xA1;

                        //SPI address 0x8012, Actual User Left Stick Calibration data

                        //XValSet(&buf[22], AXIS_MAX_INPUT - 1);
                        //YValSet(&buf[22], AXIS_MAX_INPUT - 1);
                        XValSet(&buf[22], 0);
                        XValSet(&buf[22], 0);
                        XValSet(&buf[25], AXIS_CENTER);
                        YValSet(&buf[25], AXIS_CENTER);
                        XValSet(&buf[28], AXIS_MAX_INPUT);
                        YValSet(&buf[28], AXIS_MAX_INPUT);

                        //SPI address 0x801B, Magic 0xB2 0xA1 for user available calibration
                        buf[31] = 0xB2;
                        buf[32] = 0xA1;

                        //SPI address 0x801D, Actual user Right Stick Calibration data
                        XValSet(&buf[33], AXIS_CENTER);
                        YValSet(&buf[33], AXIS_CENTER);
                        XValSet(&buf[36], AXIS_MAX_INPUT);
                        YValSet(&buf[36], AXIS_MAX_INPUT);
                        XValSet(&buf[39], 0);
                        YValSet(&buf[39], 0);

                        //XValSet(&buf[39], AXIS_MAX_INPUT - 1);
                        //YValSet(&buf[39], AXIS_MAX_INPUT - 1);

                        buf[42] = 0xB2;
                        buf[43] = 0xA1;
                    }
                }

                if ((buf[13] == 0x82) && (buf[14] == 0x02))
                {
                    //restore previous firmware version
                    printf("FirmVer=%d.%d\n", buf[15], buf[16]);
                    buf[15] = 0x03;
                    buf[16] = 0x48;
                }
            }
        }

        /*
        USB給電対応のためUSBFがVBUS切断検知を行わない
        このため、Nintendo Switch <-> Raspberry Pi間のUSBケーブルを切断するタイミングで
        write()を行うと処理が戻らない
        USB Gadget Driverを改造すれば解消される
        無改造の場合、再接続時のBusResetのときにエラーが返る
        */
        ret = write(fGadget, buf, len);
        if (ret == -1)
        {
            printf("fGadget InputReport write error %d.\n", errno);
            Processing = 0;
            continue;
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

int ProconHidrawNameGet(char *HidrawName)
{
    int ret;
    int found;
    int fd;
    DIR *dir;
    struct dirent *dp;
    char *p;
    char buf[MAX_BUFFER_LEN];

    dir = opendir("/sys/class/hidraw");
    if (dir == NULL)
    {
        printf("opendir error %d", errno);
        return -1;
    }

    found = -1;
    dp = readdir(dir);
    while (dp != NULL)
    {
        if (dp->d_type == DT_LNK)
        {
            sprintf(buf, "/sys/class/hidraw/%s/device/uevent", dp->d_name);
            fd = open(buf, O_RDONLY);
            if (fd != -1)
            {
                ret = read(fd, buf, MAX_BUFFER_LEN);
                close(fd);

                if (ret > 0)
                {
                    p = strstr(buf, PROCON_NAME);
                    if (p)
                    {
                        sprintf(HidrawName, "/dev/%s", dp->d_name);
                        printf("Procon:%s\n", HidrawName);
                        found = 0;
                        break;
                    }
                }
            }
        }

        dp = readdir(dir);
    }

    closedir(dir);
    return found;
}

int main(int argc, char *argv[])
{
    int ret;
    char devName[MAX_NAME_LEN];

    printf("Procon Converter start.\n");

    /*
    初期値
    マウス操作は画面に対して、横がX、縦がYとする
    Proconのジャイロとは座標軸が異なるので注意すること
    Proconの座標は平置きして上から見たときに
    https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/imu_sensor_notes.md
    のLeft Jyo-Con図と合わせてある
    */
    XSensitivity = X_SENSITIVITY;
    YSensitivity = Y_SENSITIVITY;
    YFollowing = Y_FOLLOWING;
    Processing = 1;
    fKeyboard = -1;
    fMouse = -1;
    fProcon = -1;
    fGadget = -1;
    YTotal = 0;
    Straight = AXIS_MAX_INPUT;
    StraightHalf = (int)((float)Straight * AXIS_HALF_INPUT_FACTOR);
    Diagonal = (int)(0.7071f * (float)AXIS_MAX_INPUT); //0.7071 is cos 45
    DiagonalHalf = (int)((float)Diagonal * AXIS_HALF_INPUT_FACTOR);

    pthread_mutex_init(&MouseMtx, NULL);

    ret = InputDevNameGet(DEV_KEYBOARD, KEYBOARD_NAME, devName);
    if (ret == -1)
    {
        printf("Keybord is not found.\n");
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
        printf("Mouse is not found.\n");
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

    ret = ProconHidrawNameGet(devName);
    if (ret == -1)
    {
        printf("Procon is not found.\n");
        Processing = 0;
        goto EXIT;
    }

    fProcon = open(devName, O_RDWR);
    if (fProcon == -1)
    {
        printf("Procon open error %d.\n", errno);
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
    while (Processing)
    {
        sleep(3);
    }

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

    if (fProcon != -1)
    {
        close(fProcon);
    }

    pthread_mutex_destroy(&MouseMtx);

    printf("Procon Converter exit.\n");
    return 0;
}

