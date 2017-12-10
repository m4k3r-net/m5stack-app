/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

#define NO_DYSPLAY 0
#define DISPLAY_BAR 1
#define DISPLAY_MARK 2

const char *networkName = CONFIG_SSID;
const char *networkPswd = CONFIG_SSID_PASSWORD;

//IP address to send UDP data to:
// either use the ip address of the server or
// a network broadcast address
const char *udpAddress = CONFIG_UDP_SERVER_ADDRESS;
const int udpPort = CONFIG_UDP_PORT;

//Are we currently connected?
boolean connected = false;

//The udp library class
WiFiUDP udp;

struct __attribute__((packed)) rcpkt {
    uint32_t version;
    uint64_t timestamp_us;
    uint16_t sequence;
    uint16_t pwms[8];
};

//wifi event handler
static void WiFiEvent(WiFiEvent_t event){
    switch(event) {
      case SYSTEM_EVENT_STA_GOT_IP:
          //When connected set 
          Serial.print("WiFi connected! IP address: ");
          Serial.println(WiFi.localIP());

          // Start device display with ID of sensor
          M5.Lcd.fillRect(20, 0, 300, 16, BLACK);
          M5.Lcd.setTextColor(GREEN ,BLACK);
          M5.Lcd.setCursor(20,0);
          M5.Lcd.print("WiFi connected! IP address: ");
          M5.Lcd.print(WiFi.localIP());

          //initializes the UDP state
          //This initializes the transfer buffer
          udp.begin(WiFi.localIP(),udpPort);
          connected = true;
          break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
	  Serial.println("WiFi lost connection");

          // Start device display with ID of sensor
          M5.Lcd.fillRect(20, 0, 300, 16, BLACK);
          M5.Lcd.setTextColor(RED ,BLACK);
          M5.Lcd.setCursor(20,0);
          M5.Lcd.print("WiFi lost connection");

         connected = false;
          break;
      default:
          break;
    }
}

static void connectToWiFi(const char * ssid, const char * pwd)
{
    Serial.println("Connecting to WiFi network: " + String(ssid));

    // delete old config
    WiFi.disconnect(true);
    //register event handler
    WiFi.onEvent(WiFiEvent);
  
    //Initiate connection
    WiFi.begin(ssid, pwd);

    Serial.println("Waiting for WIFI connection...");
}

// The setup routine runs once when M5Stack starts up
void setup(){

    // Initialize the M5Stack object
    M5.begin();

#if CONFIG_THROTTLE_BUTTON
    //M5.Speaker.mute();
#else
    // ??? analogRead makes some click noise on M5 speaker even when
    // it's muted. Looks that the speaker output pin(25) is effected by
    // electorical noise. Zero DAC output to pin 25 reduces that click
    // sound, though there is still small residual noise.
    M5.Speaker.write(0);
#endif

    // LCD display
    M5.Lcd.setTextColor(GREEN ,BLACK);
    M5.Lcd.setCursor(20,0);
    M5.Lcd.printf("M5 propo");

#if (CONFIG_DISPLAY_TYPE == DISPLAY_BAR)
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(40,28);
    M5.Lcd.printf("CH1: ROLL");
    M5.Lcd.setCursor(30,130);
    M5.Lcd.printf("CH2: PITCH");
    M5.Lcd.setCursor(40,212);
    M5.Lcd.printf("CH4: YAW");
    M5.Lcd.setCursor(196,130);
    M5.Lcd.printf("CH3: THROTTLE");
#endif

    //Connect to the WiFi network
    connectToWiFi(networkName, networkPswd);
}

extern "C" {
    xQueueHandle att_queue = NULL;
    void imu_task(void *arg);
}

static uint16_t pwm_sat(uint16_t pwm)
{
    if (pwm > 1900)
        return 1900;
    else if (pwm < 1100)
        return 1100;
    return pwm;
}

#if CONFIG_THROTTLE_BUTTON
const float throttle_hover = (CONFIG_THROTTLE_HOVER - 1100) / 800.0;
#endif
float throttle = 0.0; // % of throttle
uint16_t rcpkt_count;
int32_t prev_cx, prev_cy, prev_mx, prev_my;
int count;

#if (CONFIG_DISPLAY_TYPE == DISPLAY_BAR)
const uint8_t xo = 28;
const uint8_t yo = 60;
const uint8_t bw = 160;

static inline float curve(float x)
{
#if 1
    float v;
    if (x < 0)
        v = - x*x;
    else
        v = x*x;
    x = v;
#endif
    x += 0.5;
    if (x < 0) return 0.0;
    else if (x > 1) return 1.0;
    return x;
}
#endif

// The loop routine runs over and over again forever
void loop() {

    float att[4];
    if (xQueueReceive(att_queue, &att[0], 0) == pdTRUE) {
        if ((count % 10) == 0) {
#if (CONFIG_DISPLAY_TYPE == DISPLAY_BAR)
            uint8_t v;
            // Display roll bar
            v = (uint8_t)(curve(att[0])*bw);
            M5.Lcd.fillRect(xo, 40, v, 16, CYAN);
            M5.Lcd.fillRect(xo+v, 40, bw-v, 16, TFT_DARKGREY);
            // Display pitch bar
            v = (uint8_t)(curve(att[1])*bw);
            M5.Lcd.fillRect(100, yo, 24, bw-v, TFT_DARKGREY);
            M5.Lcd.fillRect(100, yo+bw-v, 24, v, GREEN);
            // Display throttle bar
            v = (uint8_t)(throttle*bw);
            M5.Lcd.fillRect(280, yo, 24, bw-v, TFT_DARKGREY);
            M5.Lcd.fillRect(280, yo+bw-v, 24, v, ORANGE);
            // Display yaw bar. Test only.
            v = (uint8_t)(0.5*bw);
            M5.Lcd.fillRect(xo, 224, v, 16, YELLOW);
            M5.Lcd.fillRect(xo+v, 224, bw-v, 16, TFT_DARKGREY);
#elif (CONFIG_DISPLAY_TYPE == DISPLAY_MARK)
            int32_t cx, cy;
            cx = (int32_t)(240*att[0]);
            cy = (int32_t)(240*att[1]);
            cx = 160 + cx;
            cy = 120 + cy;
            if (cx < 0)
                cx = 0;
            if (cy < 0)
                cy = 0;
#if CONFIG_DISPLAY_COMPASS
            int32_t mx, my;
            mx = (int32_t)(120*att[3]); // East
            my = (int32_t)(120*att[2]); // North
            mx = 160 + mx;
            my = 120 - my;
            if (mx < 0)
                mx = 0;
            if (my < 0)
                my = 0;
            M5.Lcd.fillRect(prev_mx, prev_my, 16, 16, BLACK);
#endif
            M5.Lcd.fillRect(prev_cx, prev_cy, 16, 16, BLACK);
#if CONFIG_DISPLAY_COMPASS
            M5.Lcd.setTextColor(CYAN, BLACK);
            M5.Lcd.setCursor(mx, my);
            M5.Lcd.print("N");
            prev_mx = mx; prev_my = my;
#endif
            M5.Lcd.setTextColor(YELLOW, BLACK);
            M5.Lcd.setCursor(cx, cy);
            M5.Lcd.print("x");
            prev_cx = cx; prev_cy = cy;

            // Display throttle bar
            uint8_t h = (uint8_t)(throttle*240);
            M5.Lcd.fillRect(320-16, 0, 15, 240-h, BLACK);
            M5.Lcd.fillRect(320-16, 240-h, 15, 240, ORANGE);
#endif
        }

        float pwm_thr, pwm_pitch, pwm_roll, pwm_yaw;
        pwm_roll = pwm_sat((int16_t)(att[0] * 800) + 1500+(5));
        pwm_pitch = pwm_sat((int16_t)(att[1] * 800) + 1500+(-10));
        pwm_yaw = 1500+(60);
#if CONFIG_THROTTLE_BUTTON
        if(M5.BtnA.isPressed()) {
            throttle -= 0.001;
            if (throttle < 0)
                throttle = 0;
        }
        if (M5.BtnC.isPressed()) {
            throttle += 0.001;
            if (throttle > 100.0)
                throttle = 100.0;
        }
        if (M5.BtnB.isPressed()) {
            if (throttle < throttle_hover) {
                throttle += 0.01;
                if (throttle > throttle_hover)
                    throttle = throttle_hover;
            } else if (throttle > throttle_hover) {
                throttle -= 0.01;
                if (throttle < throttle_hover)
                    throttle = throttle_hover;
            }
        }
#else
        float adc = analogRead(35) / 4095.0;
        adc = 1 - adc;
        // make dead band <10% and >90%
        if (adc < 0.1)
            adc = 0.1;
        if (adc > 0.9)
            adc = 0.9;
        adc = (adc - 0.1) / 0.8;
        throttle = 0.9*throttle + 0.1*adc;
#endif
        pwm_thr = pwm_sat((uint16_t)(throttle*800) + 1100);

        if (connected) {
            struct rcpkt pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.version = 2;
            pkt.sequence = rcpkt_count++;
            pkt.timestamp_us = ((uint64_t)1000) * millis();
            pkt.pwms[0] = pwm_roll;
            pkt.pwms[1] = pwm_pitch;
            pkt.pwms[2] = pwm_thr;
            pkt.pwms[3] = pwm_yaw;
            for(int i=4; i<8; i++)
                pkt.pwms[i] = 1500;
            udp.beginPacket(udpAddress,udpPort);
            udp.write((const uint8_t *)&pkt, sizeof(pkt));
            udp.endPacket();
        }

        count++;
    }

    M5.update();
    delay(1);
}

// The arduino task
void loopTask(void *pvParameters)
{
    setup();
    for(;;) {
        micros(); //update overflow
        loop();
    }
}

extern "C" void app_main()
{
    initArduino();

    att_queue = xQueueCreate(32, 4*sizeof(float));

    xTaskCreatePinnedToCore(loopTask, "loopTask", 8192, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
    xTaskCreate(imu_task, "imu_task", 2048, NULL, 1, NULL);
}