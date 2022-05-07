#include "OV2640.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "CRtspSession.h"
#include "wifikeys.h"

WebServer webServer(80);
WiFiServer rtspServer(8554);

CStreamer *streamer;
OV2640 cam;

void handle_jpg_stream(void) {
    WiFiClient client = webServer.client();
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    webServer.sendContent(response);
    while (1) {
        cam.run();
        if (!client.connected()) {
            break;
        }
        response = "--frame\r\n";
        response += "Content-Type: image/jpeg\r\n\r\n";
        webServer.sendContent(response);
        client.write((char *)cam.getfb(), cam.getSize());
        webServer.sendContent("\r\n");
        if (!client.connected()) {
            break;
        }
    }
}

void handle_jpg(void) {
    WiFiClient client = webServer.client();
    cam.run();
    if (!client.connected()) {
        return;
    }
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-disposition: inline; filename=capture.jpg\r\n";
    response += "Content-type: image/jpeg\r\n\r\n";
    webServer.sendContent(response);
    client.write((char *)cam.getfb(), cam.getSize());
}

void handle_not_found(void)
{
    String message = "Server is running!\n\n";
    message += "URI: ";
    message += webServer.uri();
    message += "\nMethod: ";
    message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += webServer.args();
    message += "\n";
    webServer.send(200, "text/plain", message);
}

void handle_status(void) {
    String message = "\nURI: ";
    message += webServer.uri();
    message += "\nMETHOD: ";
    message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += webServer.args();
    message += "\n";
    webServer.send(200, "text/plain", message);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ;
    }
    IPAddress ip;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(F("."));
    }
    ip = WiFi.localIP();
    // lets print some status output on serial console
    Serial.println();
    Serial.println(F("Connected to WIFI."));
    Serial.println();
    Serial.println(ip);
    // reconfigure some attributes of aithinker esp32 cam
    esp32cam_aithinker_config.frame_size = FRAMESIZE_SXGA;
    esp32cam_aithinker_config.jpeg_quality = 28;
    esp32cam_aithinker_config.fb_count = 5;
    // initialize camera
    cam.init(esp32cam_aithinker_config);
    // lets setup webserver routes
    webServer.on("/", HTTP_GET, handle_jpg_stream);
    webServer.on("/jpg", HTTP_GET, handle_jpg);
    webServer.on("/status", HTTP_GET, handle_status);
    webServer.onNotFound(handle_not_found);
    webServer.begin();
    // start rtsp server
    rtspServer.begin();
    streamer = new OV2640Streamer(cam);
}

void loop() {
    webServer.handleClient();
    uint32_t msecPerFrame = 100;
    static uint32_t lastimage = millis();
    // If we have an active client connection, just service that until gone
    streamer->handleRequests(0); // we don't use a timeout here,
    // instead we send only if we have new enough frames
    uint32_t now = millis();
    if(streamer->anySessions()) {
        if(now > lastimage + msecPerFrame || now < lastimage) { // handle clock rollover
            streamer->streamImage(now);
            lastimage = now;
            // check if we are overrunning our max frame rate
            now = millis();
            if(now > lastimage + msecPerFrame) {
                printf("warning exceeding max frame rate of %d ms\n", now - lastimage);
            }
        }
    }
    // rtsp client / server
    WiFiClient rtspClient = rtspServer.accept();
    if(rtspClient) {
        Serial.print("rtsp client: ");
        Serial.print(rtspClient.remoteIP());
        Serial.println();
        streamer->addSession(rtspClient);
    }
}
