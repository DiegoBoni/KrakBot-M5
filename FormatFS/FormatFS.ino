#include <LittleFS.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Formateando LittleFS...");
    LittleFS.begin(false);
    LittleFS.format();
    LittleFS.end();
    Serial.println("OK — ya podes flashear KRAKBOT");
}

void loop() {}
