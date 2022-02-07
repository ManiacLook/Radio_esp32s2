#include <WiFi.h>
#include <SPI.h>
#include <RDA5807.h>
#include <U8g2lib.h>
#include <Ticker.h>


int oldsec = -1; 
int buttonFlag = 0;


/************************** fm radio begin **************************/
#define ESP32_I2C_SDA 5       // I2C数据线
#define ESP32_I2C_SCL 4       // I2C时钟线
#define MAX_DELAY_RDS 40      // 40ms - polling method
#define MAX_STATIONS 10       // 电台数量上限

uint8_t stations = 0;         // 选中的电台index
long rds_elapsed = millis();
uint8_t fmvol = 6;            // 音量
RDA5807 rx;                   // 收音机对象
uint16_t fmstation[MAX_STATIONS] = {0}; //电台列表


// 初始化FM收音机
void fmInit() 
{
    Wire.begin(ESP32_I2C_SDA, ESP32_I2C_SCL);  // 设置I2C从机
    rx.setup();                                // 启动FM收音机
    rx.setVolume(fmvol);                       // 设置音频音量
}


// 找到后台寻找信号最好的频段
uint16_t findMaxSing(uint16_t freq, uint16_t rssi) 
{
    while (rx.isStereo()) 
    {
        rx.setFrequencyUp();  // 增加频率
        delay(100);
        // 接收的立体声信号强度足够大
        if (rx.isStereo() && rx.getRssi() > rssi) 
        {
            freq = rx.getFrequency();
            rssi = rx.getRssi();
        }
    }
    return freq;
}


// 寻找信号值较大的电台频率
void searchFM() 
{
    uint16_t i = 0;
    uint8_t num = 0;
    rx.setFrequency(8700);  // 从8700MHZ开始扫描
    while (i < 210) 
    {
        delay(100);
        if (rx.isStereo() && rx.getRssi() > 20) 
        {
            fmstation[num ++] = findMaxSing(rx.getFrequency(), rx.getRssi());
            Serial.print("Good Frequency: ");
            Serial.println(rx.getFrequency());
            if (num >= MAX_STATIONS)    return;  // 搜索的电台数量达到上限
        }
        rx.setFrequencyUp();
        i ++;
        Serial.print("Search FM");
        Serial.print(rx.getFrequency());
        Serial.print(":");
        Serial.println(rx.getRssi() );
    }
    // 输出收到的所有电台
    for (i = 0; i < MAX_STATIONS; i ++) 
    {
        Serial.print(fmstation[i]);
        Serial.println("");
    }
    // Serial.println("End Search");
}


// 换台(改频率)
void changeStation() 
{
    while (1) 
    {
        if (stations == MAX_STATIONS - 1) 
        {
            stations = 0;
            rx.setFrequency(fmstation[stations]);
            return;
        }
        if (fmstation[stations + 1] != 0) 
        {
            rx.setFrequency(fmstation[stations + 1]);
            stations ++;
            return;
        }
        stations ++;
    }
}


// 设置FM频率
void SetFMFrequency() 
{
    rx.setFrequency(fmstation[stations]);
}
/************************** fm radio end **************************/


/************************** web radio begin **************************/
#define WEBSERVERIP "192.168.137.221"
#define WEBSERVERPORT 8080

String IPAddress;
uint16_t num = 0;
uint8_t netbuf[3][1024];      // 网络数据缓冲区
uint16_t writep = 0;          // 写入数量
uint16_t readp = 0;           // 读取数量
WiFiClient client;            // 声明一个客户端对象，用于与服务器进行连接
bool connstat = false;        // 连接状态
bool iswaitecho = false;      // 是否等待服务器回应
Ticker flipper;               // 时间中断
uint16_t m_offset = 0;


// 定时中断，DAC_OUT输出缓冲区信号(播放声音)
void onTimer(void) 
{
  if (readp <= writep)  dacWrite(17, netbuf[readp % 3][m_offset++]);  // 播放一次声音(dac输出)
  if (m_offset >= 1024) 
  {
    m_offset = 0;
    readp ++;  // 读取完成一个缓冲区
  }
}


// 连接IP端口
bool connNetMusic() 
{
    uint8_t i = 0;
    while (i < 5)  // 最多连接5次
    {
        if (client.connect(WEBSERVERIP, WEBSERVERPORT)) 
        {
            connstat = true;
            Serial.println("连接成功");
            return true;
        } 
        else 
        {
            Serial.println("访问失败");
            client.stop();  // 关闭客户端
        }
        i ++;
        delay(100);
    }
    return false;
}


void playMusic() 
{
        digitalWrite(41, HIGH);
        digitalWrite(42, HIGH);

        if (connstat == true)
        {
            if (iswaitecho == false && (writep - readp) < 2) 
            {
                client.write('n');  // 申请一个缓冲片
                iswaitecho = true;
            }
            if (writep % 120 == 0)   displayMessage();  // 降低刷新屏幕频率
            if (client.available())  // 如果有数据可读取
            {
                num = client.read(netbuf[writep % 3], 1024);
                if (writep == 0 && readp == 0) 
                {
                    flipper.attach(0, onTimer);  // 开启定时中断 就是每秒中断20000次
                }
                writep ++;
                iswaitecho = false;
            }
        }
        else
        {
            displayMessage();
        }
}
/************************** web radio end **************************/



uint8_t countStation() 
{
  uint8_t i;
  for (i = 0; i < MAX_STATIONS; i++) 
  {
      if (fmstation[i] == 0) return i;
  }
  return i;
}


/************************** output control begin **************************/
// 功能控制
int curr_sour = 0;    // 选择的功能, 0为显示日期, 1为FM 收音机, 2为Web 收音机
#define button1 1
#define button2 2
#define button3 3
#define button4 6


void FunctionUp() 
{
    if (buttonFlag == 1)
    {
        // 如果是按键1, 修改功能
        curr_sour = (curr_sour + 1) % 3;
    }
    if (curr_sour == 0) 
    {
        // Date 功能, 没有特别的操作, 需要关闭网络收音机的客户端
        Serial.println("info: Datetime Page");
        digitalWrite(41, LOW);
        digitalWrite(42, LOW);
        client.write('q');
        client.stop();  // 关闭客户端
        flipper.detach();  // 关闭中断
        connstat = false;
    }
    else if (curr_sour == 1 ) 
    {
        // FM收音机, 只需要打开收音机的开关即可
        Serial.println("info: FM Radio Page");
        digitalWrite(41, HIGH); 
        digitalWrite(42, LOW);
        if (buttonFlag == 2) 
        {
            changeStation();
        }
        else if (buttonFlag == 3)
        {
            fmvol --;
            if (fmvol < 1)  fmvol = 1;
            rx.setVolume(fmvol);
        }
        else if (buttonFlag == 4)
        {
            fmvol ++;
            if (fmvol > 15) fmvol = 15;
            rx.setVolume(fmvol);
        }
    }
    else if (curr_sour == 2)
    {
        // net radio 网络收音机, 需要启动收音机的开关
        Serial.println("info: Net Radio Page");
        digitalWrite(41, HIGH);
        digitalWrite(42, HIGH);
        connNetMusic();
    }
}
/************************** output control end **************************/


/************************** monitor begin **************************/
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 36, /* data=*/ 35, /* cs=*/ 46, /* dc=*/ 33, /* reset=*/ 34);

void mointorInit() 
{
    u8g2.begin();
}


// 欢迎画面, 显示WELCOME
void displayWelcome()
{
  u8g2.firstPage();  // 绘图，当前页码为0
  u8g2.clearBuffer();  // 清除内存中数据缓冲区(清屏)
  u8g2.setFontDirection(0);  // 字体从左到右
  u8g2.setFont(u8g2_font_ncenB14_tr);  // 使用大字体
  u8g2.setCursor(10, 30);  // 设置绘制光标位置
  u8g2.print("WELCOME");  // 绘制内容
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(30, 60);
  u8g2.print("Please wait...");
  u8g2.sendBuffer();  // 绘制缓冲区内容
}


// 每次刷新oled，显示信息
void displayMessage()
{
    struct tm timeinfo;
    char str[64] = {0};
    // 获取时间
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("error: can'get time");
        return;
    }
    if (oldsec == timeinfo.tm_sec) return;
    oldsec = timeinfo.tm_sec;

    // 显示其他信息
    u8g2.setFont(u8g2_font_ncenB08_tr);
    if (curr_sour == 0)  // 静音模式(主界面)
    {
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.clearBuffer();
        u8g2.setFontDirection(0);
        u8g2.setCursor(10, 10);
        u8g2.print(&timeinfo, "%Y/%m/%d %H:%M:%S");
        u8g2.setCursor(10, 26);
        u8g2.print(IPAddress.c_str());  // 显示IP
        // 笑脸
        u8g2.setFont(u8g2_font_unifont_t_symbols);  // 设置字体字集Glyph，
        u8g2.drawGlyph(46, 50, 0x25e0);
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawGlyph(61, 50, 0x25e0);
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawGlyph(53, 60, 0x25e1);
        u8g2.drawRFrame(43, 34, 30, 30, 6);   // 在X3 Y17位置开始显示宽30，高30，圆角r6空心圆角四边形
    }
    else if (curr_sour == 1)  // FM 模式
    {
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.clearBuffer();
        u8g2.setCursor(10, 10);
        u8g2.print(&timeinfo, "%Y/%m/%d %H:%M:%S");
        u8g2.setCursor(10, 26);
        sprintf(str, "FM: %dMHz", fmstation[stations]);
        u8g2.print(str);
        // 笑脸
        u8g2.setFont(u8g2_font_unifont_t_symbols);  // 设置字体字集Glyph，
        u8g2.drawGlyph(46, 50, 0x25e0);
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawGlyph(61, 50, 0x25e0);
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawGlyph(53, 60, 0x25e1);
        u8g2.drawRFrame(43, 34, 30, 30, 6);   // 在X3 Y17位置开始显示宽30，高30，圆角r6空心圆角四边形
    }
    else if (curr_sour == 2)  // 网络收音机模式
    {
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.clearBuffer();
        u8g2.setCursor(10, 10);
        u8g2.print(&timeinfo, "%Y/%m/%d %H:%M:%S");
        if (connstat == true)
        {
            // u8g2.clearBuffer();
            u8g2.setCursor(10, 26);
            u8g2.print("NetRadio");
            u8g2.setCursor(10, 40);
            sprintf(str, "%s:%d", WEBSERVERIP, WEBSERVERPORT);
            u8g2.print(str);        
        }
        else
        {
            // u8g2.clearBuffer();
            u8g2.setCursor(10, 26);
            u8g2.print("NetRadio");
            u8g2.setCursor(10, 40);
            u8g2.print("Connection failed.");
        }
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawUTF8(30, 60, "☃ ☃ ☃");
    }
    u8g2.sendBuffer();
}
/************************** monitor end **************************/


/************************** wifi begin **************************/
const char* SSID = "look";
const char* PASSWD = "123456789";
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;  // 中国是东八区，8 * 60 * 60
const int daylightOffset_sec = 0;


// 连接wifi
String WifiConnecttion()
{
    WiFi.mode(WIFI_STA);  // 可连接其他AP
    WiFi.begin(SSID, PASSWD);  // 开始wifi连接
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(400); 
    }
   return WiFi.localIP().toString();  // 返回IP地址
}
/************************** wifi end **************************/


void setup()
{
    // put your setup code here, to run once:
    pinMode(41, OUTPUT);
    pinMode(42, OUTPUT);
    digitalWrite(41, LOW);
    digitalWrite(42, LOW);
    Serial.begin(115200);

    mointorInit();  // 初始化屏幕
    displayWelcome();  // 显示欢迎界面

    // 连wifi
    Serial.println("connect wifi");
    IPAddress = WifiConnecttion();
    Serial.println(IPAddress.c_str());

    fmInit();  // 初始化fm收音机
    searchFM();  // 搜台
    SetFMFrequency();  // 设置搜到的第一个频率
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // 使用官方configTime方法获取时间

    // 初始化按钮
    pinMode(button1, INPUT_PULLUP); 
    pinMode(button2, INPUT_PULLUP);
    pinMode(button3, INPUT_PULLUP);
    pinMode(button4, INPUT_PULLUP);
}

void loop()
{
    num = 0;
    displayMessage();
    if (digitalRead(button1) == LOW) 
    {
        delay(50);
        if (digitalRead(button1) == LOW)
        {
            buttonFlag = 1;
        }
    }
    if (digitalRead(button2) == LOW)
    {
        delay(50);
        if (digitalRead(button2) == LOW)
        {
            buttonFlag = 2;
        }
    }
    if (digitalRead(button3) == LOW)
    {
        delay(50);
        if (digitalRead(button3) == LOW)
        {
            buttonFlag = 3;
        }
    }
    if (digitalRead(button4) == LOW)
    {
        delay(50);
        if (digitalRead(button4) == LOW)
        {
            buttonFlag = 4;
        }
    }
    if (buttonFlag != 0)
    {
        FunctionUp();
        delay(1000);
    }
    if (curr_sour == 2)
    {
        playMusic();
    }

    buttonFlag = 0;
}
