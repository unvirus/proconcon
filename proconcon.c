/*
スプラトゥーン3 マウスコンバーター for RaspberryPi 4b
2022/08/28 ぬこいばらき（unvirus）

how to build
gcc proconcon.c -o proconcon.out -l pthread -lm  -O3 -Wall 
 
Version history 
ver.0.01 2022/08/27 First release 
ver.0.02 2022/08/28 マウスズレ調整
ver.0.03 2022/09/03 デバイスの選択を自動化、ソース整理
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
#define Y_SENSITIVITY           (22.0f)     //マウス操作、上下感度
#define Y_FOLLOWING             (1.50f)     //マウス操作、上下追従補正

//スティック入力値
#define AXIS_CENTER             (1920)
#define AXIS_MAX_INPUT          (1920)
#define AXIS_HALF_INPUT_FACTOR  (0.833f)

#define MOUSE_READ_COUNTER      (3)     //指定した回数に達した場合マウス操作がされていない事を示す
#define Y_ANGLE_DEFAULT         (-640)  //Y角度、コントローラー平置き、0度
#define Y_ANGLE_UPPPER_LIMIT    (3000 + Y_ANGLE_DEFAULT)    //Y上限
#define Y_ANGLE_LOWER_LIMIT     (-1500 + Y_ANGLE_DEFAULT)   //Y下限

#define MAX_NAME_LEN            (256)
#define MAX_PACKET_LEN          (64)
#define MAX_BUFFER_LEN          (512)
#define BITMAP_HEADER_LEN       (54)

#define BANNER_WHDTH            (320)
#define BANNER_HEIGHT           (120)

#define GADGET_NAME             "/dev/hidg0"
#define BANNER_NAME             "./banner.bmp"  //320＊120の256色ビットマップ画像を指定可能、白黒のみ使うこと

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
#define KEYBORD_NAME        "Topre_Corporation_Realforce_108"
#define MOUSE_NAME          "Logitech_G403_Prodigy_Gaming_Mouse"

#define PROCON_NAME         "Nintendo Co., Ltd. Pro Controller"     //Procon名は固定

typedef struct {
    short Y_Angle;                //約4200から約-4200、平置き時約-668
    short X_Angle;                //約4200から約-4200、平置き時約-28
    short Z_Angle;                //解析情報をあさるとZ角度だが何かが変、平置き時約4075
    short X_Accel;                //約16000から約-16000
    short Y_Accel;                //約16000から約-16000
    short Z_Accel;                //約16000から約-16000
} ProConGyroData;

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
    unsigned char Reserved;         //unknown value
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
int fBanner;
int thKeyboardCreated;
int thMouseCreated;
int thOutputReportCreated;
int thInputReportCreated;
int MouseReadCnt;
int YTotal;
int Slow;
int BannerOn;
int BannerLen;
int BannerLoopCnt;
int BannerX;
int BannerOffs;
int BannerProtDir;
int GoStraight;
int GoStraightHalf;
int GoDiagonally;
int GoDiagonallyHalf;
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
ProconData BannerPad;
unsigned char KeyMap[KEY_WIMAX];
unsigned char BitmapData1[64 * 1024];
unsigned char BitmapData2[64 * 1024];

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

int ToInt(unsigned char *pBuf)
{
    int ret;

    ret = pBuf[3];
    ret <<= 8;
    ret |= pBuf[2];
    ret <<= 8;
    ret |= pBuf[1];
    ret <<= 8;
    ret |= pBuf[0];

    return ret;
}

short ToShort(unsigned char *pBuf)
{
    int ret;

    ret = pBuf[1];
    ret <<= 8;
    ret |= pBuf[0];

    return ret;
}

int BannerTemplateOpen(void)
{
    int ret;
    int top;
    int len;
    int i;
    int j;
    int src_offs;
    int dst_offs;
    unsigned char bmHeader[BITMAP_HEADER_LEN];

    if (fBanner != -1)
    {
        close(fBanner);
        fBanner = -1;
    }

    fBanner = open(BANNER_NAME, O_RDONLY);
    if (fBanner == -1)
    {
        printf("fBanner open error %d.\n", errno);
        return 0;
    }

    ret = read(fBanner, bmHeader, sizeof(bmHeader));
    if (ret != sizeof(bmHeader))
    {
        printf("fBanner read error.\n");
        goto ERROR_EXIT;
    }

    //BMヘッダ確認
    if (bmHeader[0] != 'B')
    {
        printf("Not bitmap file.\n");
        goto ERROR_EXIT;
    }

    if (bmHeader[1] != 'M')
    {
        printf("Not bitmap file.\n");
        goto ERROR_EXIT;
    }

    //データ開始位置
    top = ToInt(&bmHeader[10]);
    printf("data top=%d.\n", top);

    ret = ToInt(&bmHeader[18]);
    if (ret != BANNER_WHDTH)
    {
        printf("illigal bmp width=%d.\n", ret);
        goto ERROR_EXIT;
    }

    ret = ToInt(&bmHeader[22]);
    if (ret != BANNER_HEIGHT)
    {
        printf("illigal bmp height=%d.\n", ret);
        goto ERROR_EXIT;
    }

    //8Bitカラー
    ret = (int)ToShort(&bmHeader[28]);
    if (ret != 8)
    {
        printf("illigal bmp color=%d.\n", ret);
        goto ERROR_EXIT;
    }

    //ファイルオフセットを進める
    len = top - BITMAP_HEADER_LEN;
    ret = read(fBanner, BitmapData1, len);
    if (ret != len)
    {
        printf("fBanner read error.\n");
        goto ERROR_EXIT;
    }

    /*
    カラーパレット分読み飛ばし
    Windows標準インデックスカラービットマップ(256色）は     
    パレット0が黒、パレット255が白なのでパレットは見ない
    */
    BannerLen = BANNER_WHDTH * BANNER_HEIGHT;
    ret = read(fBanner, BitmapData1, BannerLen);
    if (ret != BannerLen)
    {
        printf("fBanner read error.\n");
        goto ERROR_EXIT;
    }

    close(fBanner);

    //BMPは左下から開始なので逆にする
    src_offs = 0;
    dst_offs = BANNER_WHDTH * (BANNER_HEIGHT - 1);
    for (i = 0; i < BANNER_HEIGHT; i++)
    {
        memcpy(&BitmapData2[dst_offs], &BitmapData1[src_offs], BANNER_WHDTH);
        src_offs += BANNER_WHDTH;
        dst_offs -= BANNER_WHDTH;
    }

    //ドット打ち順に並べ替える
    for (i = 0; i < BANNER_HEIGHT; i++)
    {
        if (i & 1)
        {
            //奇数ラインの時はラインを逆転する
            //一時退避
            src_offs = BANNER_WHDTH * i;
            memcpy(&BitmapData1[0], &BitmapData2[src_offs], BANNER_WHDTH);

            dst_offs = src_offs + BANNER_WHDTH - 1;
            for (j = 0; j < BANNER_WHDTH; j++)
            {
                BitmapData2[dst_offs] = BitmapData1[j];
                dst_offs--;
            }
        }
    }

    return 1;

ERROR_EXIT:
    close(fBanner);
    fBanner = -1;

    return 0;
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
            if (KeyMap[KEY_9])
            {
                if (BannerOn)
                {
                    BannerOn = 0;
                }
                else
                {
                    ret = BannerTemplateOpen();
                    if (ret == 1)
                    {
                        BannerOn = 1;
                    }
                }
                printf("BannerOn=%d\n", BannerOn);
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
            printf("ProCon OutputReport write error %d.\n", errno);
            Processing = 0;
            continue;
        }
    }

    printf("OutputReportThread exit.\n");
    return NULL;
}

void BannerProt(ProconData *pPad)
{
    if (BannerOn == 0)
    {
        BannerProtDir = 0;
        BannerX = 0;
        BannerOffs = 0;
        return;
    }

    if (BannerOffs >= BannerLen)
    {
        //最後までドット打ちした
        BannerOn = 0;
        BannerProtDir = 0;
        BannerX = 0;
        BannerOffs = 0;
        return;
    }

    BannerLoopCnt++;

    if (BannerLoopCnt == 1)
    {
        memset(&BannerPad, 0, sizeof(BannerPad));
    }

    if (BannerLoopCnt == 3)
    {
        if (BitmapData2[BannerOffs] == 0xFF)
        {
            BannerPad.B = 1;
        }
        else
        {
            BannerPad.A = 1;
        }

        BannerOffs++;
    }

    if (BannerLoopCnt == 5)
    {
        memset(&BannerPad, 0, sizeof(BannerPad));
    }

    if (BannerLoopCnt == 7)
    {
        BannerX++;

        if (BannerX < BANNER_WHDTH)
        {
            if (BannerProtDir)
            {
                BannerPad.A = 0;
                BannerPad.B = 0;
                BannerPad.Left = 1;
            }
            else
            {
                BannerPad.A = 0;
                BannerPad.B = 0;
                BannerPad.Right = 1;
            }
        }
        else
        {
            BannerPad.Down = 1;
            BannerPad.Left = 0;
            BannerPad.Right = 0;

            BannerProtDir++;
            BannerProtDir &= 1;

            BannerX = 0;
        }
    }

    if (BannerLoopCnt == 8)
    {
        BannerLoopCnt = 0;
    }

    pPad->A = BannerPad.A;
    pPad->B = BannerPad.B;
    pPad->Down = BannerPad.Down;
    pPad->Left = BannerPad.Left;
    pPad->Right = BannerPad.Right;
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
            YValSet(pAxis, AXIS_CENTER + GoStraightHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER + GoStraight);
        }
    }
    else if (Dir == 0x0c)
    {
        //右上
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + GoDiagonallyHalf);
            YValSet(pAxis, AXIS_CENTER + GoDiagonallyHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER + GoDiagonally);
            YValSet(pAxis, AXIS_CENTER + GoDiagonally);
        }
    }
    else if (Dir == 0x04)
    {
        //右
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + GoStraightHalf);
            YValSet(pAxis, AXIS_CENTER);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER + GoStraight);
            YValSet(pAxis, AXIS_CENTER);
        }

    }
    else if (Dir == 0x06)
    {
        //右下
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER + GoDiagonallyHalf);
            YValSet(pAxis, AXIS_CENTER - GoDiagonallyHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER + GoDiagonally);
            YValSet(pAxis, AXIS_CENTER - GoDiagonally);
        }
    }
    else if (Dir == 0x02)
    {
        //下
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER - GoStraightHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER);
            YValSet(pAxis, AXIS_CENTER - GoStraight);
        }
    }
    else if (Dir == 0x03)
    {
        //左下
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - GoDiagonallyHalf);
            YValSet(pAxis, AXIS_CENTER - GoDiagonallyHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER - GoDiagonally);
            YValSet(pAxis, AXIS_CENTER - GoDiagonally);
        }
    }
    else if (Dir == 0x01)
    {
        //左
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - GoStraightHalf);
            YValSet(pAxis, AXIS_CENTER);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER - GoStraight);
            YValSet(pAxis, AXIS_CENTER);
        }
    }
    else if (Dir == 0x09)
    {
        //左上
        if (Slow)
        {
            XValSet(pAxis, AXIS_CENTER - GoDiagonallyHalf);
            YValSet(pAxis, AXIS_CENTER + GoDiagonallyHalf);
        }
        else
        {
            XValSet(pAxis, AXIS_CENTER - GoDiagonally);
            YValSet(pAxis, AXIS_CENTER + GoDiagonally);
        }
    }
    else
    {
        //入力無し
        XValSet(pAxis, AXIS_CENTER);
        YValSet(pAxis, AXIS_CENTER);
    }
}

void ProconInput(ProconData *pPad)
{
    unsigned char dir;
    ProConGyroData gyro;

    //key
    if (KeyMap[KEY_1])
    {
        //視点リセット
        pPad->Y = 1;
        YTotal = Y_ANGLE_DEFAULT;
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
        //スーパージャンプ
        pPad->A = 1;
    }

    if (KeyMap[KEY_R] == 1)
    {
        //マップ
        pPad->X = 1;
    }

    if (KeyMap[KEY_F] == 1)
    {
        //ナイス
        pPad->Up = 1;
    }

    if (KeyMap[KEY_V] == 1)
    {
        //カモン
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
        YTotal = Y_ANGLE_DEFAULT;
    }

    //左スティック
    dir = KeyMap[KEY_W] << 3;
    dir |= KeyMap[KEY_D] << 2;
    dir |= KeyMap[KEY_S] << 1;
    dir |= KeyMap[KEY_A];
    StickInput(pPad->L_Axis, dir);

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

    //mouse
    pthread_mutex_lock(&MouseMtx);
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
    //printf("Y raw=%d, mouse=%d\n", test.Y_Angle, gyro.Y_Angle);

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

    if (MouseMap.R)
    {
        //サブ
        pPad->R = 1;
    }

    if (MouseMap.L)
    {
        //メイン
        pPad->ZR = 1;
    }

    if (MouseMap.Side)
    {
        //イカ
        pPad->ZL = 1;
    }

    if (MouseMap.Extra)
    {
        //アサリ
        pPad->L = 1;
    }

    if (MouseMap.Wheel)
    {
        //スペシャル、マウスホイールを動かす
        pPad->StickR = 1;
        MouseMap.Wheel = 0;
    }

    pthread_mutex_unlock(&MouseMtx);
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
                BannerProt((ProconData *)buf);
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

    found = 0;
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
    ProConのジャイロとは座標軸が異なるので注意すること
    ProConの座標は平置きして上から見たときに
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
    fBanner = -1;
    YTotal = Y_ANGLE_DEFAULT;
    GoStraight = AXIS_MAX_INPUT;
    GoStraightHalf = (int)((float)GoStraight * AXIS_HALF_INPUT_FACTOR);
    GoDiagonally = (int)(0.7071f * (float)AXIS_MAX_INPUT); //0.7071 is cos 45
    GoDiagonallyHalf = (int)((float)GoDiagonally * AXIS_HALF_INPUT_FACTOR);

    pthread_mutex_init(&MouseMtx, NULL);

    ret = InputDevNameGet(DEV_KEYBOARD, KEYBORD_NAME, devName);
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

    printf("ProCon Converter exit.\n");
    return 0;
}

