#include "smartled.h"

SmartLED* led = NULL;

bool splitValueString(char* src, char* option, char* value)
{
    char* part = strtok (src, ":");
    if (part != 0)
    {
        strcpy(option, part);
        part = strtok (NULL, ":");
        if (part != 0)
            strcpy(value, part);
        else
            return false;
    } else 
    {
        return false;
    }
    return true;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * buffer, size_t len)
{
    char incoming[len+2];
    memset(incoming, 0, len+2);
    memcpy(incoming, (char*)buffer, len);
    char optionName[24];
    char optionValue[24];
    IPAddress ip = led->remoteIP(num);
    switch(type) 
    {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED: 
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], buffer);
            led->sendTXT(num, "Connected");
            led->sendCurrentValues(num);
            break;
        case WStype_TEXT:
            switch (buffer[0])
            {
                case '#': led->selectMode((const char *) &incoming[1]);
                          Serial.print("New mode selected :");
                          Serial.println(led->mode());
                          led->sendTXT(num, "selectMode done");
                          break;
                case '$': memset(optionName, 0, sizeof(optionName));
                          memset(optionValue, 0, sizeof(optionValue));
                          if (!splitValueString((char *) &incoming[1], optionName, optionValue)) 
                              return;
                          led->setOption(optionName, optionValue);
                          Serial.printf("[$] Option %s done\n", optionName, optionValue);
                          led->sendTXT(num, "setOption done");
                          break;
                case '@': memset(optionName, 0, sizeof(optionName));
                          memset(optionValue, 0, sizeof(optionValue));
                          if (!splitValueString((char *) &incoming[1], optionName, optionValue)) 
                              return;
                          led->setOption(optionName, optionValue);
                          Serial.printf("[@] Option %s done\n", optionName, optionValue);
                          led->sendTXT(num, "setOption done");
                          break;
                case '?': led->dump();
                          break;
                default:
                          Serial.println("Unknown operation");
                          break;
            }
            break;
    }
}

SmartLED::SmartLED(uint8_t pCount, uint8_t pPin, neoPixelType colorScheme, bool ue)
{
    defaultSpeed = 100;
    modifier = 0;
    zeroSpeed = 0;
    pixelCount = pCount;
    strip = new Adafruit_NeoPixel(pCount, pPin, colorScheme);
    strip->begin();
    readyLeds = (RGBColor*) malloc(sizeof (RGBColor) * pCount);
    modSettings.leds = (RGBColor*) malloc(sizeof (RGBColor) * pCount);
    fLeds = (RGBFloat*) malloc(sizeof (RGBFloat) * pCount);
    useEEPROM = ue;
    setDefaultValues();
    nextUpdate = millis() + refreshRate;
    needToUpdate = false;
    microsOverflow = false;
    led = this;
    
    webSocket = new WebSocketsServer(81);
    webSocket->begin();
    webSocket->onEvent(webSocketEvent);
    
    strip->show();
};

SmartLED::~SmartLED() 
{
    delete webSocket;
    delete strip;
    free(readyLeds);
    free(modSettings.leds);
    free(fLeds);
};

void SmartLED::selectModeByID(ModeID mID)
{
    settings.mode = (ModeID) mID;
    effect = modes[settings.mode].effect;
    settings.specialMode = (mID < MICycle) ? MIOff : mID;
    (this->*effect)(true);
    lastSaved = millis();
    needToSave = true;
    modSettings.effectPaused = false;
    modifier = 0;
}

void SmartLED::selectMode(const char* modeName)
{
    int i;
    for (i = 0; i < MIMAX; i++)
    {
        if (strcmp(modeName, modes[i].modeName) == 0)
            break;
    }
    if (i == MIMAX)
        i = MIOff;
    selectModeByID((ModeID) i);
}

ModeID SmartLED::mode()
{
    return settings.mode;
}

uint32_t SmartLED::calculateStep(uint8_t speed, uint16_t stepBase)
{
    needToUpdate = true;
    uint32_t maxStep = ((stepBase == 0) ? modes[settings.mode].stepBase : stepBase) * 100 + 1000;
    uint32_t result = micros() + (maxStep - (speed * modes[settings.mode].stepBase));
    if (micros() > result)
    {
        microsOverflow = true;
    }
    return result;
}

void SmartLED::process()
{
    webSocket->loop();
    uint32_t currentMicros = micros();
    if (microsOverflow)
    {
        if (currentMicros < nextStep)
        {
            microsOverflow = false;
        }
    }
    /// модификатор можно выполнять только если указатель не нулевой, и уже пришло время для следующего шага
    if ((modifier) && (currentMicros > nextStep) && (!microsOverflow))
    {
        (this->*modifier)();
        currentMicros = micros();
    }
    if ((!modSettings.effectPaused) && (millis() > settings.cycle.nextChange) && (!microsOverflow))
    {
        switch (settings.specialMode)
        {
            case MICycle: makeCycle(false);
                break;
            case MIShedule: makeShedule(false);
                break;
        }
        currentMicros = micros();
    }
    /// эффект можно выполнять только в том случае, если скорость эффекта не нулевая, 
    /// пришло время для следующего шага и при этом эффект временно не заблокирован модификатором
    if ((*effectSpeed != 0) && (currentMicros > nextStep) && (!modSettings.effectPaused) && (!microsOverflow))
    {
        (this->*effect)(false);
        nextStep = calculateStep(abs(*effectSpeed));
    }
    if (needToUpdate)
    {
        strip->show();
        needToUpdate = false;
    }
    autosave();
}

IPAddress SmartLED::remoteIP(uint8_t num)
{
    return webSocket->remoteIP(num);
}

void SmartLED::sendTXT(uint8_t num, const char* txt)
{
    webSocket->sendTXT(num, txt);
}

void SmartLED::sendCurrentValues(uint8_t num)
{
    sendSection(num, MIWaves);
    sendSection(num, MIRainbow);
    sendSection(num, MILines);
    sendSection(num, MISnake);
    sendSection(num, MISnowflake);
    sendSection(num, MIStroboscope);
    sendSection(num, MIPulse);
    sendSection(num, MICycle);
}

void SmartLED::sendSection(uint8_t num, ModeID sectionID)
{
    char optName[16];
    switch (sectionID)
    {
        case MIWaves:
            sendValue(num, modes[sectionID].modeName, "colorMin", settings.waves.colorMin);
            sendValue(num, modes[sectionID].modeName, "colorMax", settings.waves.colorMax);
            sendValue(num, modes[sectionID].modeName, "count", settings.waves.count);
            sendValue(num, modes[sectionID].modeName, "speed", settings.waves.speed);
            break;
        case MIRainbow:
            memset(optName, 0, 16);
            sendValue(num, modes[sectionID].modeName, "count", settings.rainbow.count);
            sendValue(num, modes[sectionID].modeName, "reverse", settings.rainbow.reverse);
            sendValue(num, modes[sectionID].modeName, "speed", settings.rainbow.speed);
            for (int i = 0; i < 10; i++)
            {
                sprintf(optName, "color%d", i);
                sendValue(num, modes[sectionID].modeName, optName, settings.rainbow.color[i]);
            }
            break;
        case MILines:
            memset(optName, 0, 16);
            sendValue(num, modes[sectionID].modeName, "count", settings.lines.count);
            sendValue(num, modes[sectionID].modeName, "reverse", settings.lines.reverse);
            sendValue(num, modes[sectionID].modeName, "multiColor", settings.lines.multiColor);
            sendValue(num, modes[sectionID].modeName, "speed", settings.lines.speed);
            for (int i = 0; i < 10; i++)
            {
                sprintf(optName, "color%d", i);
                sendValue(num, modes[sectionID].modeName, optName, settings.lines.color[i]);
            }
            break;
        case MISnowflake:
            sendValue(num, modes[sectionID].modeName, "count", settings.snowflake.count);
            sendValue(num, modes[sectionID].modeName, "color", settings.snowflake.color);
            sendValue(num, modes[sectionID].modeName, "flakeSize", settings.snowflake.flakeSize);
            sendValue(num, modes[sectionID].modeName, "multiColor", settings.snowflake.multiColor);
            sendValue(num, modes[sectionID].modeName, "fading", settings.snowflake.fading);
            break;
        case MIStroboscope:
            sendValue(num, modes[sectionID].modeName, "count", settings.stroboscope.count);
            sendValue(num, modes[sectionID].modeName, "color", settings.stroboscope.color);
            sendValue(num, modes[sectionID].modeName, "multiColor", settings.stroboscope.multiColor);
            break;
        case MISnake:
            sendValue(num, modes[sectionID].modeName, "count", settings.snake.count);
            sendValue(num, modes[sectionID].modeName, "color", settings.snake.color);
            sendValue(num, modes[sectionID].modeName, "multiColor", settings.snake.multiColor);
            sendValue(num, modes[sectionID].modeName, "reverse", settings.snake.reverse);
            sendValue(num, modes[sectionID].modeName, "speed", settings.snake.speed);
            break;
        case MIPulse:
            sendValue(num, modes[sectionID].modeName, "colorMin", settings.pulse.colorMin);
            sendValue(num, modes[sectionID].modeName, "colorMax", settings.pulse.colorMax);
            sendValue(num, modes[sectionID].modeName, "speed", settings.pulse.speed);
            break;
        case MICycle:

            break;
        case MIShedule:

            break;
        default:
            break;
    }
//    sendTXT(num, "done!");
}

void SmartLED::sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, int32_t value)
{
    char sendStr[80];
    memset(sendStr, 0, sizeof(sendStr));
    sprintf(sendStr, "%s:%s:%d", sectionTxt, optionTxt, value);
    sendTXT(num, sendStr);
}

void SmartLED::sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, bool value)
{
    char sendStr[80];
    memset(sendStr, 0, sizeof(sendStr));
    sprintf(sendStr, "%s:%s:%s", sectionTxt, optionTxt, value ? "true" : "false");
    sendTXT(num, sendStr);
}

void SmartLED::sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, RGBColor value)
{
    char sendStr[80];
    memset(sendStr, 0, sizeof(sendStr));
    sprintf(sendStr, "%s:%s:%d;%d;%d", sectionTxt, optionTxt, value.r, value.g, value.b);
    sendTXT(num, sendStr);
}

void SmartLED::sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, RGBValue value)
{
    char sendStr[80];
    memset(sendStr, 0, sizeof(sendStr));
    sprintf(sendStr, "%s:%s:%d;%d;%d", sectionTxt, optionTxt, value.r, value.g, value.b);
    sendTXT(num, sendStr);
}

void SmartLED::sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, RGBFloat value)
{
    char sendStr[80];
    memset(sendStr, 0, sizeof(sendStr));
    sprintf(sendStr, "%s:%s:%d;%d;%d", sectionTxt, optionTxt, (int32_t)value.r, (int32_t)value.g, (int32_t)value.b);
    sendTXT(num, sendStr);
}

void SmartLED::ledsToZero()
{
    memset(readyLeds, 0, sizeof (RGBColor) * pixelCount);
}

void SmartLED::autosave()
{
    if (!useEEPROM)
        return;
    if ((needToSave) && ((lastSaved + 60000) < millis()))
        needToSave = false;
    else
        return;

    settings.headerSize = sizeof (settings);
    settings.shedulerSize = sizeof (sheduler);
    uint8_t eeprom_buffer[settings.headerSize + settings.shedulerSize];
    EEPROM.begin(sizeof (eeprom_buffer));

    memcpy(eeprom_buffer, &settings, settings.headerSize);
    memcpy(eeprom_buffer + settings.headerSize, &sheduler, settings.shedulerSize);

    for (int i = 0; i < sizeof (eeprom_buffer); i++)
    {
        EEPROM.write(i, eeprom_buffer[i]);
    }
    EEPROM.commit();
    lastSaved = millis();
}

bool SmartLED::loadSettings()
{
    if (!useEEPROM)
        return false;
    uint8_t eeprom_buffer[sizeof (settings) + sizeof (sheduler)];
    EEPROM.begin(sizeof (eeprom_buffer));
    for (int i = 0; i < sizeof (eeprom_buffer); i++)
    {
        eeprom_buffer[i] = EEPROM.read(i);
    }
    memcpy(&settings, eeprom_buffer, sizeof (settings));
    memcpy(&sheduler, &eeprom_buffer[sizeof (settings)], sizeof (sheduler));
    if ((settings.headerSize != sizeof (settings)) || (settings.shedulerSize != sizeof (sheduler)))
        return false;
    if (settings.mode >= MIMAX)
        settings.mode = MIOff;
    if (settings.specialMode > MIOff)
        settings.mode = settings.specialMode;
    lastSaved = millis();
    selectModeByID(settings.mode);
    needToSave = false;
    return true;
};

void SmartLED::setDefaultValues()
{
    if (loadSettings())
        return;
    
    settings.waves.count = RGBColor({1, 1, 1});

    settings.rainbow.count = 2;
    settings.rainbow.speed = 1;
    settings.rainbow.reverse = false;

    settings.lines.count = 2;
    settings.lines.speed = 1;
    settings.lines.reverse = false;
    settings.lines.multiColor = false;

    settings.snowflake.multiColor = false;
    settings.snowflake.flakeSize = 1;
    settings.snowflake.count = 10;
    settings.snowflake.fading = 90;

    settings.stroboscope.multiColor = false;
    settings.stroboscope.count = 1;

    settings.snake.count = 1;
    settings.snake.speed = 1;
    settings.snake.multiColor = false;
    settings.snake.reverse = false;

    settings.pulse.speed = 1;

    settings.cycle.period = 60;
    settings.cycle.nextChange = millis() + (settings.cycle.period * 1000);
    settings.cycle.current = MIRainbow;

    settings.effectCreating = 0;
    settings.direct = -1;
    
    selectModeByID(MIOff);
}

RGBColor SmartLED::parseColorValue(char* valueString)
{
    uint8_t clr[3];
    uint8_t i = 0;
    char* part = strtok (valueString, ";");
    while (part != 0)
    {
        clr[i] = (uint8_t) atol(part);
        part = strtok (NULL, ";");
        i++;
    }
    if (i != 3) 
        return RGBColor({0, 0, 0});
    return RGBColor({clr[0], clr[1], clr[2]});
}

RGBValue SmartLED::parseSignedValue(char* valueString)
{
    int16_t clr[3];
    uint8_t i = 0;
    char* part = strtok (valueString, ";");
    while (part != 0)
    {
        clr[i] = (int16_t) atol(part);
        part = strtok (NULL, ";");
        i++;
    }
    if (i != 3) 
        return RGBValue({0, 0, 0});
    return RGBValue({clr[0], clr[1], clr[2]});
}

int32_t SmartLED::parseSingleValue(const char* valueString)
{
    return (int32_t) atol(valueString);
}

void SmartLED::generateLine(int idx)
{
    if (settings.lines.reverse)
    {
        settings.position = (random(100) % 2 == 0) ? -1 : pixelCount;
    } else
    {
        settings.position = (settings.lines.speed < 0) ? pixelCount : -1;
    }
    settings.step = (settings.position < 0) ? 1 : -1;
    if (settings.lines.multiColor)
    {
        settings.currentColor.r = random(0, 255);
        settings.currentColor.g = random(0, 255);
        settings.currentColor.b = random(0, 255);
    } else
        settings.currentColor = settings.lines.color[idx];
}

void SmartLED::addSnowflake()
{
    if (settings.snowflake.flakeSize >= (pixelCount / 2))
        settings.snowflake.flakeSize = pixelCount / 2 - 1;
    uint16_t pos = random(settings.snowflake.flakeSize, pixelCount - settings.snowflake.flakeSize);
    RGBFloat tmpColor;
    if (settings.snowflake.multiColor)
    {
        tmpColor.r[0] = random(10, 255);
        tmpColor.g[0] = random(10, 255);
        tmpColor.b[0] = random(10, 255);
    } else
    {
        tmpColor.r[0] = settings.snowflake.color.r;
        tmpColor.g[0] = settings.snowflake.color.g;
        tmpColor.b[0] = settings.snowflake.color.b;
    }
    fLeds[pos] = tmpColor;
    uint8_t rr, gg, bb;
    for (int i = 1; i <= settings.snowflake.flakeSize; i++)
    {
        rr = tmpColor.r[0] = (tmpColor.r[0] > 32) ? tmpColor.r[0] / 2. : tmpColor.r[0] * 0.7;
        gg = tmpColor.g[0] = (tmpColor.g[0] > 32) ? tmpColor.g[0] / 2. : tmpColor.g[0] * 0.7;
        bb = tmpColor.b[0] = (tmpColor.b[0] > 32) ? tmpColor.b[0] / 2. : tmpColor.b[0] * 0.7;
        fLeds[pos - i].r[0] = readyLeds[pos - i].r | rr;
        fLeds[pos - i].g[0] = readyLeds[pos - i].g | gg;
        fLeds[pos - i].b[0] = readyLeds[pos - i].b | bb;
        fLeds[pos + i].r[0] = readyLeds[pos + i].r | rr;
        fLeds[pos + i].g[0] = readyLeds[pos + i].g | gg;
        fLeds[pos + i].b[0] = readyLeds[pos + i].b | bb;
    }
    float cMax;
    for (int i = (pos - settings.snowflake.flakeSize); i <= (pos + settings.snowflake.flakeSize); i++)
    {
        cMax = (100 - settings.snowflake.fading)*4 + 50;
        fLeds[i].r[1] = fLeds[i].r[0] / cMax;
        fLeds[i].g[1] = fLeds[i].g[0] / cMax;
        fLeds[i].b[1] = fLeds[i].b[0] / cMax;
    }
}

void SmartLED::addStroboscope()
{
    uint16_t pos = random(pixelCount);
    if (settings.stroboscope.multiColor)
    {
        readyLeds[pos].r = random(1, 10) * 25;
        readyLeds[pos].g = random(1, 10) * 25;
        readyLeds[pos].b = random(1, 10) * 25;
    } else
    {
        readyLeds[pos] = settings.stroboscope.color;
        readyLeds[pos].r = (settings.stroboscope.color.r / 25) * 25;
        readyLeds[pos].g = (settings.stroboscope.color.g / 25) * 25;
        readyLeds[pos].b = (settings.stroboscope.color.b / 25) * 25;
    }
}

void SmartLED::memoryDump(uint8_t* begin, uint8_t length)
{
    for (int i = 0; i < length; i++)
    {
        Serial.printf("%u\t", begin[i]);
        if (i % 10 == 9) Serial.printf("\n");
    }
}

void SmartLED::dump()
{
    Serial.printf("Current mode is %s, special mode is %s, effect speed is %d\n", modes[settings.mode].modeName, modes[settings.specialMode].modeName, *effectSpeed);
    
    Serial.printf("StripWaves\n");
    memoryDump((uint8_t*)&settings.waves, sizeof(StripWaves));
    
    Serial.printf("\nStripRainbow\n");
    memoryDump((uint8_t*)&settings.rainbow, sizeof(StripRainbow));
    
    Serial.printf("\nStripLines\n");
    memoryDump((uint8_t*)&settings.lines, sizeof(StripLines));

    Serial.printf("\nStripSnowflake\n");
    memoryDump((uint8_t*)&settings.snowflake, sizeof(StripSnowflake));

    Serial.printf("\nStripStroboscope\n");
    memoryDump((uint8_t*)&settings.stroboscope, sizeof(StripStroboscope));

    Serial.printf("\nStripIntersects\n");
    memoryDump((uint8_t*)&settings.intersects, sizeof(StripIntersects));

    Serial.printf("\nStripSnake\n");
    memoryDump((uint8_t*)&settings.snake, sizeof(StripSnake));

    Serial.printf("\nStripPulse\n");
    memoryDump((uint8_t*)&settings.pulse, sizeof(StripPulse));

    Serial.printf("\nStripCycle\n");
    memoryDump((uint8_t*)&settings.cycle, sizeof(StripCycle));

    Serial.printf("\nTail of settings\n");
    memoryDump((uint8_t*)&settings.effectCreating, 15);

    Serial.printf("\nmodSettings\n");
    memoryDump((uint8_t*)&modSettings, sizeof(ModifierParameters));

    Serial.printf("\nother internal data\n");
    memoryDump((uint8_t*)&modSettings, 22);
    Serial.printf("\n\n");
}

void SmartLED::makeOff(bool isDefault)
{
    if (isDefault)
    {
        ledsToZero();
        for (int i = 0; i < pixelCount; i++)
            strip->setPixelColor(i, 0);
        effectSpeed = &zeroSpeed;
        modSettings.modifierSpeed = zeroSpeed;
        needToUpdate = true;
        modifier = 0;
    }
}

void SmartLED::makeWaves(bool isDefault)
{
    if (isDefault)
    {
        uint32_t beginTime = micros();
        float rLength = ((float) pixelCount / (float) settings.waves.count.r) / 2.;
        float gLength = ((float) pixelCount / (float) settings.waves.count.g) / 2.;
        float bLength = ((float) pixelCount / (float) settings.waves.count.b) / 2.;
        float rAmp = settings.waves.colorMax.r - settings.waves.colorMin.r;
        float gAmp = settings.waves.colorMax.g - settings.waves.colorMin.g;
        float bAmp = settings.waves.colorMax.b - settings.waves.colorMin.b;
        int rPeriod, gPeriod, bPeriod;
        float rCurrent, gCurrent, bCurrent;
        float rDiscrets = 101 - abs(settings.waves.speed.r);
        float gDiscrets = 101 - abs(settings.waves.speed.g);
        float bDiscrets = 101 - abs(settings.waves.speed.b);
        for (int i = 0; i < pixelCount; i++)
        {
            rCurrent = rAmp * ((float) i / rLength);
            rPeriod = floor(i / rLength);
            fLeds[i].r[0] = (rPeriod % 2 == 0) ? settings.waves.colorMin.r + rCurrent - rPeriod * rAmp : settings.waves.colorMin.r - rCurrent + (rPeriod - 1) * rAmp + 2 * rAmp;
            fLeds[i].r[1] = fLeds[i].r[0] / rDiscrets;
            gCurrent = gAmp * ((float) i / gLength);
            gPeriod = floor(i / gLength);
            fLeds[i].g[0] = (gPeriod % 2 == 0) ? settings.waves.colorMin.g + gCurrent - gPeriod * gAmp : settings.waves.colorMin.g - gCurrent + (gPeriod - 1) * gAmp + 2 * gAmp;
            fLeds[i].g[1] = fLeds[i].g[0] / gDiscrets;
            bCurrent = bAmp * ((float) i / bLength);
            bPeriod = floor(i / bLength);
            fLeds[i].b[0] = (bPeriod % 2 == 0) ? settings.waves.colorMin.b + bCurrent - bPeriod * bAmp : settings.waves.colorMin.b - bCurrent + (bPeriod - 1) * bAmp + 2 * bAmp;
            fLeds[i].b[1] = fLeds[i].b[0] / bDiscrets;
            strip->setPixelColor(i, strip->Color((uint8_t) fLeds[i].r[0], (uint8_t) fLeds[i].g[0], (uint8_t) fLeds[i].b[0]));
        }
        moving[0] = moving[1] = moving[2] = 0;
        effectSpeed = &defaultSpeed;
        nextStep = calculateStep(abs(*effectSpeed));
        modifier = 0;
        return;
    }
    float *prevColor, *prevColorOffset;
    float *color, *colorOffset;
    if (settings.waves.speed.r != 0)                            // нужно двигать этот цвет вправо или влево
    {
        if (settings.waves.speed.r > 0)                         // смещение вправо?
        {
            moving[0]++;                                        // увеличить счетчик
        } else if (settings.waves.speed.r < 0)                  // иначе уменьшить
        {
            moving[0]--;
        }
        int8_t delta = (settings.waves.speed.r > 0) ? -1 : 1;   // посчитаем смещение относительно текущего индекса
        if (settings.waves.speed.r < 0)                         // и начнем "смещать"
            for (int i = 0; i < pixelCount; i++)
            {
                color = &(fLeds[i].r[0]);
                prevColor = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].r[0]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].r[0]) : &(fLeds[i + delta].r[0]);
                prevColorOffset = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].r[1]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].r[1]) : &(fLeds[i + delta].r[1]);
                colorOffset = &(fLeds[i].r[1]);
                *color = *color - *colorOffset + *prevColorOffset;
                if (*color < 0) *color = 0.01;
            } else
            for (int i = (pixelCount - 1); i >= 0; i--)
            {
                color = &(fLeds[i].r[0]);
                prevColor = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].r[0]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].r[0]) : &(fLeds[i + delta].r[0]);
                prevColorOffset = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].r[1]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].r[1]) : &(fLeds[i + delta].r[1]);
                colorOffset = &(fLeds[i].r[1]);
                *color = *color - *colorOffset + *prevColorOffset;
                if (*color < 0) *color = 0.01;
            }
        if (abs(moving[0]) == (101 - abs(settings.waves.speed.r))) // счетчик достиг значения скорости?
        {
            moving[0] = 0;                                      // обнулить
            float discrets = 101 - abs(settings.waves.speed.r); // и снова вычислить кол-во дискретов
            for (int i = 0; i < pixelCount; i++)                // рассчитать смещение каждого индекса по дискретизации
            {
                fLeds[i].r[1] = fLeds[i].r[0] / discrets;       // и запомнить его во втором элементе массива этого цвета
            }
        }
    }
    if (settings.waves.speed.g != 0)                            // нужно двигать этот цвет вправо или влево
    {
        if (settings.waves.speed.g > 0)                         // смещение вправо?
        {
            moving[1]++;                                        // увеличить счетчик
        } else if (settings.waves.speed.g < 0)                  // иначе уменьшить
        {
            moving[1]--;
        }
        int8_t delta = (settings.waves.speed.g > 0) ? -1 : 1;   // посчитаем смещение относительно текущего индекса
        if (settings.waves.speed.g < 0)                         // и начнем "смещать"
            for (int i = 0; i < pixelCount; i++)
            {
                color = &(fLeds[i].g[0]);
                prevColor = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].g[0]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].g[0]) : &(fLeds[i + delta].g[0]);
                prevColorOffset = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].g[1]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].g[1]) : &(fLeds[i + delta].g[1]);
                colorOffset = &(fLeds[i].g[1]);
                *color = *color - *colorOffset + *prevColorOffset;
                if (*color < 0) *color = 0.01;
            } else
            for (int i = (pixelCount - 1); i >= 0; i--)
            {
                color = &(fLeds[i].g[0]);
                prevColor = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].g[0]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].g[0]) : &(fLeds[i + delta].g[0]);
                prevColorOffset = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].g[1]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].g[1]) : &(fLeds[i + delta].g[1]);
                colorOffset = &(fLeds[i].g[1]);
                *color = *color - *colorOffset + *prevColorOffset;
                if (*color < 0) *color = 0.01;
            }
        if (abs(moving[1]) == (101 - abs(settings.waves.speed.g))) // счетчик достиг значения скорости?
        {
            moving[1] = 0;                                      // обнулить
            float discrets = 101 - abs(settings.waves.speed.g); // и снова вычислить кол-во дискретов
            for (int i = 0; i < pixelCount; i++)                // рассчитать смещение каждого индекса по дискретизации
            {
                fLeds[i].g[1] = fLeds[i].g[0] / discrets;       // и запомнить его во втором элементе массива этого цвета
            }
        }
    }
    if (settings.waves.speed.b != 0)                            // нужно двигать этот цвет вправо или влево
    {
        if (settings.waves.speed.b > 0)                         // смещение вправо?
        {
            moving[2]++;                                        // увеличить счетчик
        } else if (settings.waves.speed.b < 0)                  // иначе уменьшить
        {
            moving[2]--;
        }
        int8_t delta = (settings.waves.speed.b > 0) ? -1 : 1;   // посчитаем смещение относительно текущего индекса
        if (settings.waves.speed.b < 0)                         // и начнем "смещать"
            for (int i = 0; i < pixelCount; i++)
            {
                color = &(fLeds[i].b[0]);
                prevColor = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].b[0]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].b[0]) : &(fLeds[i + delta].b[0]);
                prevColorOffset = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].b[1]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].b[1]) : &(fLeds[i + delta].b[1]);
                colorOffset = &(fLeds[i].b[1]);
                *color = *color - *colorOffset + *prevColorOffset;
                if (*color < 0) *color = 0.01;
            } else
            for (int i = (pixelCount - 1); i >= 0; i--)
            {
                color = &(fLeds[i].b[0]);
                prevColor = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].b[0]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].b[0]) : &(fLeds[i + delta].b[0]);
                prevColorOffset = ((i == 0)&&(delta < 0)) ? &(fLeds[pixelCount - 1].b[1]) : ((i == (pixelCount - 1))&&(delta > 0)) ? &(fLeds[0].b[1]) : &(fLeds[i + delta].b[1]);
                colorOffset = &(fLeds[i].b[1]);
                *color = *color - *colorOffset + *prevColorOffset;
                if (*color < 0) *color = 0.01;
            }
        if (abs(moving[2]) == (101 - abs(settings.waves.speed.b))) // счетчик достиг значения скорости?
        {
            moving[2] = 0;                                      // обнулить
            float discrets = 101 - abs(settings.waves.speed.b); // и снова вычислить кол-во дискретов
            for (int i = 0; i < pixelCount; i++)                // рассчитать смещение каждого индекса по дискретизации
            {
                fLeds[i].b[1] = fLeds[i].b[0] / discrets;       // и запомнить его во втором элементе массива этого цвета
            }
        }
    }
    for (int i = 0; i < pixelCount; i++)
    {
        readyLeds[i].r = (uint8_t) fLeds[i].r[0];
        readyLeds[i].g = (uint8_t) fLeds[i].g[0];
        readyLeds[i].b = (uint8_t) fLeds[i].b[0];
        strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));
    }
}

void SmartLED::makeRainbow(bool isDefault)
{
    if (isDefault)
    {
        ledsToZero();
        int sectionLength = pixelCount / (settings.rainbow.count);
        RGBFloat currentPos;
        currentPos.r[0] = settings.rainbow.color[0].r;
        currentPos.g[0] = settings.rainbow.color[0].g;
        currentPos.b[0] = settings.rainbow.color[0].b;
        uint8_t nextPos = 0;
        for (int i = 0; i < pixelCount; i++)
        {
            if ((i % sectionLength == 0) && ((pixelCount - i + 1) > sectionLength))
            {
                currentPos.r[0] = settings.rainbow.color[nextPos].r;
                currentPos.g[0] = settings.rainbow.color[nextPos].g;
                currentPos.b[0] = settings.rainbow.color[nextPos].b;
                nextPos++;
                if (nextPos >= settings.rainbow.count)
                {
                    nextPos = 0;
                    sectionLength = pixelCount - i + 1;
                }
                currentPos.r[1] = (settings.rainbow.color[nextPos].r - currentPos.r[0]) / sectionLength;
                currentPos.g[1] = (settings.rainbow.color[nextPos].g - currentPos.g[0]) / sectionLength;
                currentPos.b[1] = (settings.rainbow.color[nextPos].b - currentPos.b[0]) / sectionLength;
            } else
            {
                currentPos.r[0] += currentPos.r[1];
                currentPos.g[0] += currentPos.g[1];
                currentPos.b[0] += currentPos.b[1];
            }
            readyLeds[i].r = (currentPos.r[0] > 0) ? currentPos.r[0] : 0;
            readyLeds[i].g = (currentPos.g[0] > 0) ? currentPos.g[0] : 0;
            readyLeds[i].b = (currentPos.b[0] > 0) ? currentPos.b[0] : 0;
        }
        effectSpeed = &settings.rainbow.speed;
        settings.direct = (settings.rainbow.speed < 0) ? -1 : 1;
        nextStep = calculateStep(abs(*effectSpeed));
        modSettings.effectPaused = false;
        modifier = 0;
        return;
    }
    modSettings.modifierVisible = true;
    modSettings.effectPaused = true;
    modSettings.currentDiscret = 0;
    if (settings.rainbow.reverse && (random(100) < 2))
    {
        settings.rainbow.speed *= -1;
        settings.direct = (settings.rainbow.speed < 0) ? -1 : 1;
    }
    modifier = &SmartLED::modifierMoving;
    modSettings.discretization = 4;
    modSettings.modifierSpeed = *effectSpeed;

    nextStep = calculateStep(abs(*effectSpeed));
}

void SmartLED::makeLines(bool isDefault)
{
    if (isDefault)
    {
        ledsToZero();
        settings.effectCreating = 0;
        generateLine(settings.effectCreating);
        effectSpeed = &settings.lines.speed;
        nextStep = calculateStep(abs(*effectSpeed));
        modifier = 0;
        return;
    }
    settings.position += settings.step;
    strip->setPixelColor(settings.position, strip->Color(settings.currentColor.r, settings.currentColor.g, settings.currentColor.b));
    readyLeds[settings.position] = settings.currentColor;
    if (((settings.position + settings.step) < 0) || ((settings.position + settings.step) > (pixelCount - 1)))
    {
        if (settings.lines.reverse)
        {
            settings.position = (random(100) % 2 == 0) ? -1 : pixelCount;
            settings.step = (settings.position < 0) ? 1 : -1;
        } else
        {
            if (settings.position == 0)
                settings.position = pixelCount;
            else
                settings.position = -1;
        }
        settings.effectCreating++;
        if (settings.effectCreating >= settings.lines.count)
            settings.effectCreating = 0;
        if (settings.lines.multiColor)
        {
            settings.currentColor.r = random(0, 255);
            settings.currentColor.g = random(settings.currentColor.g / 2, 255) - settings.currentColor.g / 2;
            settings.currentColor.b = random(settings.currentColor.g / 2, 255) - settings.currentColor.g / 2;
        } else
            settings.currentColor = settings.lines.color[settings.effectCreating];
    }
}

void SmartLED::makeSnowflake(bool isDefault)
{
    if (isDefault)
    {
        for (int i = 0; i < pixelCount; i++)
            memset(&fLeds[i], 0, sizeof (RGBFloat));
        //        ledsToZero();
        settings.effectCreating = 0;
        effectSpeed = &defaultSpeed;
        modifier = 0;
        nextStep = calculateStep(abs(*effectSpeed));
        return;
    }
    if (++settings.effectCreating > ((100 - settings.snowflake.count)*6))
    {
        addSnowflake();
        settings.effectCreating = 0;
    }
    for (int i = 0; i < pixelCount; i++)
    {
        if (fLeds[i].r[0] > 1) fLeds[i].r[0] -= fLeds[i].r[1];
        if (fLeds[i].g[0] > 1) fLeds[i].g[0] -= fLeds[i].g[1];
        if (fLeds[i].b[0] > 1) fLeds[i].b[0] -= fLeds[i].b[1];
        readyLeds[i].r = (fLeds[i].r[0] > 1) ? fLeds[i].r[0] : 0;
        readyLeds[i].g = (fLeds[i].g[0] > 1) ? fLeds[i].g[0] : 0;
        readyLeds[i].b = (fLeds[i].b[0] > 1) ? fLeds[i].b[0] : 0;
        strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));
    }
}

void SmartLED::makeStroboscope(bool isDefault)
{
    if (isDefault)
    {
        ledsToZero();
        settings.effectCreating = 0;
        effectSpeed = &defaultSpeed;
        nextStep = calculateStep(abs(*effectSpeed));
        modifier = 0;
        return;
    }
    if (++settings.effectCreating > ((600) / settings.stroboscope.count))
    {
        addStroboscope();
        settings.effectCreating = 0;
    }
    for (int i = 0; i < pixelCount; i++)
    {
        if (readyLeds[i].r > 0) readyLeds[i].r -= 25;
        if (readyLeds[i].g > 0) readyLeds[i].g -= 25;
        if (readyLeds[i].b > 0) readyLeds[i].b -= 25;
        strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));
    }
}

void SmartLED::makeIntersects(bool isDefault)
{
    delayMicroseconds(10000);
}

void SmartLED::makeSnake(bool isDefault)
{
    if (isDefault)
    {
        int snakeLength = pixelCount / settings.snake.count;
        RGBColor tmp;
        for (int i = 0; i < pixelCount; i++)
        {
            if ((i % snakeLength) == 0)             // начало новой змейки
            {
                if (settings.snake.multiColor)
                {
                    tmp.r = random(10, 255);
                    tmp.g = random(10, 255);
                    tmp.b = random(10, 255);
                } else
                    tmp = settings.snake.color;
                readyLeds[i] = tmp;
            } else
            {
                tmp.r = (tmp.r > 32) ? tmp.r >> 1 : tmp.r * 0.7;
                tmp.g = (tmp.g > 32) ? tmp.g >> 1 : tmp.g * 0.7;
                tmp.b = (tmp.b > 32) ? tmp.b >> 1 : tmp.b * 0.7;
                readyLeds[i].r = tmp.r;
                readyLeds[i].g = tmp.g;
                readyLeds[i].b = tmp.b;
            }
            strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));
        }
        effectSpeed = &settings.snake.speed;
        nextStep = calculateStep(abs(*effectSpeed));
        modSettings.modifierVisible = false;
        modSettings.currentDiscret = 0;
        modSettings.discretization = 4;
        modSettings.modifierSpeed = *effectSpeed;
        modifier = 0;
        if (settings.direct > 0)
            modifierInvert();
        return;
    }
    if (settings.snake.reverse && (random(*effectSpeed * 100) < 10))
    {
        settings.direct *= -1;
        modifier = &SmartLED::modifierInvert;
        modSettings.modifierVisible = true;
        modSettings.effectPaused = true;
        modSettings.modifierSpeed = *effectSpeed;
        modSettings.currentDiscret = 0;
        modSettings.discretization = 4;
    } else
    {
        modifier = &SmartLED::modifierMoving;
        modSettings.modifierVisible = true;
        modSettings.effectPaused = true;
        modSettings.currentDiscret = 0;
        modSettings.discretization = 4;
        modSettings.modifierSpeed = *effectSpeed;
    }
}

void SmartLED::makePulse(bool isDefault)
{
    if (isDefault)
    {
        effectSpeed = &settings.pulse.speed;
        nextStep = calculateStep(abs(*effectSpeed));
        settings.position = 0;
        settings.step = 1;
        modifier = 0;
        return;
    }
    settings.position += settings.step;
    if ((settings.position <= 0) || (settings.position >= 255))
        settings.step *= -1;
    float cMulti = (float) (settings.position) / 255.;
    
    settings.currentColor.r = settings.pulse.colorMin.r + (settings.pulse.colorMax.r - settings.pulse.colorMin.r) * cMulti;
    settings.currentColor.g = settings.pulse.colorMin.g + (settings.pulse.colorMax.g - settings.pulse.colorMin.g) * cMulti;
    settings.currentColor.b = settings.pulse.colorMin.b + (settings.pulse.colorMax.b - settings.pulse.colorMin.b) * cMulti;
    uint32_t stripColor = strip->Color(settings.currentColor.r, settings.currentColor.g, settings.currentColor.b);
    for (int i = 0; i < pixelCount; i++)
    {
        readyLeds[i] = settings.currentColor;
        strip->setPixelColor(i, stripColor);
    }
}

void SmartLED::makeCycle(bool isDefault)
{
    if (isDefault)
    {
        if ((settings.cycle.current >= (uint8_t) MICycle) || (settings.cycle.current <= (uint8_t) MIOff))
            settings.cycle.current = (uint8_t) MIWaves;
        settings.cycle.nextChange = millis() + (settings.cycle.period * 1000);
        if (settings.cycle.isRandom)
            settings.cycle.current = random(MIOff + 1, MICycle - 1);
        else
        {
            settings.cycle.current++;
            if ((settings.cycle.current >= (uint8_t) MICycle) || (settings.cycle.current <= (uint8_t) MIOff))
                settings.cycle.current = (uint8_t) MIWaves;
        }
        if (settings.cycle.fading > 0)
        {
            settings.cycle.needToFade = true;
        }
        effect = modes[settings.cycle.current].effect;
        settings.mode = (ModeID)settings.cycle.current;
        modSettings.effectPaused = false;
        modifier = 0;
        (this->*effect)(true);
        return;
    }
    if (!settings.cycle.needToFade)
    {
        makeCycle(true);
    }
    else
    {
        modifier = &SmartLED::modifierFading;
        modSettings.effectPaused = true;
        modSettings.modifierVisible = true;
        modSettings.currentDiscret = 0;
        modSettings.modifierSpeed = settings.cycle.fading;
        settings.cycle.needToFade = false;
    }
}

void SmartLED::makeShedule(bool isDefault)
{
    
}

void SmartLED::setOption(char* option, char* strVal)
{
    ModeID controlMode = (settings.specialMode == MIOff) ? settings.mode : settings.specialMode;
    switch (controlMode)
    {
        case MIWaves:
            if (strcmp(option, "colorMax") == 0)
            {
                settings.waves.colorMax = parseColorValue(strVal);
            } else if (strcmp(option, "colorMin") == 0)
            {
                settings.waves.colorMin = parseColorValue(strVal);
            } else if (strcmp(option, "speed") == 0)
            {
                settings.waves.speed = parseSignedValue(strVal);
            } else if (strcmp(option, "count") == 0)
            {
                settings.waves.count = parseColorValue(strVal);
            }
            (this->*effect)(true);
            break;
        case MIRainbow:
            if (strcmp(option, "speed") == 0)
            {
                settings.rainbow.speed = parseSingleValue(strVal);
            } else if (strcmp(option, "count") == 0)
            {
                settings.rainbow.count = parseSingleValue(strVal);
                (this->*effect)(true);
            } else if (strcmp(option, "rainbowRev") == 0)
            {
                settings.rainbow.reverse = parseSingleValue(strVal);
            } else
            {
                for (int i = 0; i < 10; i++)
                    if (option[5] == i + 48)
                    {
                        settings.rainbow.color[i] = parseColorValue(strVal);
                        (this->*effect)(true);
                        break;
                    }
            }
            break;
        case MILines:
            if (strcmp(option, "speed") == 0)
            {
                int oldSpeed = settings.lines.speed;
                settings.lines.speed = parseSingleValue(strVal);
                if (oldSpeed * settings.lines.speed < 0)
                    (this->*effect)(true);
            } else if (strcmp(option, "count") == 0)
            {
                settings.lines.count = parseSingleValue(strVal);
            } else if (strcmp(option, "linesMC") == 0)
            {
                settings.lines.multiColor = parseSingleValue(strVal);
            } else if (strcmp(option, "linesRev") == 0)
            {
                settings.lines.reverse = parseSingleValue(strVal);
            } else
            {
                for (int i = 0; i < 10; i++)
                    if (option[5] == i + 48)
                    {
                        settings.lines.color[i] = parseColorValue(strVal);
                        (this->*effect)(true);
                        break;
                    }
            }
            break;
        case MISnowflake:
            if (strcmp(option, "color") == 0)
            {
                settings.snowflake.color = parseColorValue(strVal);
            } else if (strcmp(option, "flakeSize") == 0)
            {
                settings.snowflake.flakeSize = parseSingleValue(strVal);
            } else if (strcmp(option, "fading") == 0)
            {
                settings.snowflake.fading = parseSingleValue(strVal);
            } else if (strcmp(option, "count") == 0)
            {
                settings.snowflake.count = parseSingleValue(strVal);
            } else if (strcmp(option, "snowflakeMC") == 0)
            {
                settings.snowflake.multiColor = parseSingleValue(strVal);
            }
            break;
        case MIStroboscope:
            if (strcmp(option, "color") == 0)
            {
                settings.stroboscope.color = parseColorValue(strVal);
            } else if (strcmp(option, "count") == 0)
            {
                settings.stroboscope.count = parseSingleValue(strVal);
            } else if (strcmp(option, "stroboscopeMC") == 0)
            {
                settings.stroboscope.multiColor = parseSingleValue(strVal);
            }
            break;
//        case MIIntersects:
//
//        break;
        case MISnake:
            if (strcmp(option, "color") == 0)
            {
                settings.snake.color = parseColorValue(strVal);
                (this->*effect)(true);
            } else if (strcmp(option, "count") == 0)
            {
                settings.snake.count = parseSingleValue(strVal);
                (this->*effect)(true);
            } else if (strcmp(option, "speed") == 0)
            {
                settings.snake.speed = parseSingleValue(strVal);
            } else if (strcmp(option, "snakeMC") == 0)
            {
                settings.snake.multiColor = parseSingleValue(strVal);
                (this->*effect)(true);
            } else if (strcmp(option, "snakeRev") == 0)
            {
                settings.snake.reverse = parseSingleValue(strVal);
            }
            break;
        case MIPulse:
            if (strcmp(option, "colorMax") == 0)
            {
                settings.pulse.colorMax = parseColorValue(strVal);
            } else if (strcmp(option, "colorMin") == 0)
            {
                settings.pulse.colorMin = parseColorValue(strVal);
            } else if (strcmp(option, "speed") == 0)
            {
                settings.pulse.speed = parseSingleValue(strVal);
            }
            break;
        case MICycle:
            if (strcmp(option, "period") == 0)
            {
                settings.cycle.period = parseSingleValue(strVal);
                settings.cycle.nextChange = millis() + (settings.cycle.period * 1000);
            } else if (strcmp(option, "isRandom") == 0)
            {
                settings.cycle.isRandom = parseSingleValue(strVal);
            } else if (strcmp(option, "fading") == 0)
            {
                settings.cycle.fading = parseSingleValue(strVal);
            }
            break;
        case MIShedule:

            break;
        default:
            return;
            break;
    }
    lastSaved = millis();
    needToSave = true;
}

void SmartLED::mirrorToArray()
{
    for (int i = 0; i < pixelCount; i++)
        modSettings.leds[i] = readyLeds[pixelCount - i - 1];
}

/// в функции-модификаторе действия должны выполняться в следующей последовательности:
/// 1. проверить режим "невидимости", в зависимости от этого будет нужно (или не нужно)
///    рассчитывать значение следующего шага, исходя из скорости модификатора
/// 2. на нулевом шаге можно заполнить массив временных значений
/// 3. изменить необходимым образом значения в ленте и (или) в массиве readyLeds[]
/// 4. в конце работы функции необходимо проверить текущий шаг, и если больше шагов не будет -
///    необходимо обнулить указатель на модификатор, чтобы он не вызывался в рабочем цикле,
///    при этом на всякий случай лучше установить modSettings.effectPaused = false;
void SmartLED::modifierInvert()
{
    if (!modSettings.modifierVisible)
    {
        mirrorToArray();
        for (int i = 0; i < pixelCount; i++)
        {
            readyLeds[i] = modSettings.leds[i];
            strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));
        }
        modSettings.effectPaused = false;
        modifier = 0;
    } else
    {
        if (modSettings.currentDiscret == 0)
        {
            mirrorToArray();
        } 
        uint16_t cd;
        if (settings.direct >= 0)
        {
            cd = modSettings.currentDiscret;
        } else
        {
            cd = pixelCount - modSettings.currentDiscret - 1;
        }
        readyLeds[cd] = modSettings.leds[cd];
        strip->setPixelColor(cd, strip->Color(readyLeds[cd].r, readyLeds[cd].g, readyLeds[cd].b));
        modSettings.currentDiscret++;
        if (modSettings.currentDiscret >= pixelCount)
        {
            modSettings.effectPaused = false;
            modifier = 0;
        } else
        {
            nextStep = calculateStep(abs(modSettings.modifierSpeed));
        }
    }
}

void SmartLED::moveLeft()
{
    RGBColor tmp;
    if (modSettings.currentDiscret >= modSettings.discretization)
    {
        tmp = modSettings.leds[0];
        for (int i = 1; i < pixelCount; i++)
        {
            modSettings.leds[i - 1] = modSettings.leds[i];
        }
        modSettings.leds[pixelCount - 1] = tmp;
        memcpy(readyLeds, modSettings.leds, sizeof (RGBColor) * pixelCount);
    } else
    {
        for (int i = 0; i < pixelCount - 1; i++)
        {
            readyLeds[i].r = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].r + modSettings.currentDiscret * modSettings.leds[i + 1].r) / modSettings.discretization;
            readyLeds[i].g = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].g + modSettings.currentDiscret * modSettings.leds[i + 1].g) / modSettings.discretization;
            readyLeds[i].b = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].b + modSettings.currentDiscret * modSettings.leds[i + 1].b) / modSettings.discretization;
        }
        int i = pixelCount - 1;
        readyLeds[i].r = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].r + modSettings.currentDiscret * modSettings.leds[0].r) / modSettings.discretization;
        readyLeds[i].g = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].g + modSettings.currentDiscret * modSettings.leds[0].g) / modSettings.discretization;
        readyLeds[i].b = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].b + modSettings.currentDiscret * modSettings.leds[0].b) / modSettings.discretization;
    }
}

void SmartLED::moveRight()
{
    RGBColor tmp;
    if (modSettings.currentDiscret >= modSettings.discretization)
    {
        tmp = modSettings.leds[pixelCount - 1];
        for (int i = pixelCount - 1; i > 0; i--)
        {
            modSettings.leds[i] = modSettings.leds[i - 1];
        }
        modSettings.leds[0] = tmp;
        memcpy(readyLeds, modSettings.leds, sizeof (RGBColor) * pixelCount);
    } else
    {
        for (int i = pixelCount - 1; i > 0; i--)
        {
            readyLeds[i].r = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].r + modSettings.currentDiscret * modSettings.leds[i - 1].r) / modSettings.discretization;
            readyLeds[i].g = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].g + modSettings.currentDiscret * modSettings.leds[i - 1].g) / modSettings.discretization;
            readyLeds[i].b = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[i].b + modSettings.currentDiscret * modSettings.leds[i - 1].b) / modSettings.discretization;
        }
        int i = pixelCount - 1;
        readyLeds[0].r = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[0].r + modSettings.currentDiscret * modSettings.leds[i].r) / modSettings.discretization;
        readyLeds[0].g = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[0].g + modSettings.currentDiscret * modSettings.leds[i].g) / modSettings.discretization;
        readyLeds[0].b = ((modSettings.discretization - modSettings.currentDiscret) * modSettings.leds[0].b + modSettings.currentDiscret * modSettings.leds[i].b) / modSettings.discretization;
    }
}

void SmartLED::modifierMoving()
{
    if (modSettings.currentDiscret == 0)
    {
        for (int i = 0; i < pixelCount; i++)
            modSettings.leds[i] = readyLeds[i];
    }
    if (settings.direct < 0)
        moveLeft();
    else
        moveRight();
    for (int i = 0; i < pixelCount; i++)
        strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));

    if (modSettings.currentDiscret >= modSettings.discretization)
    {
        modSettings.effectPaused = false;
        modifier = 0;
    } else
    {
        modSettings.currentDiscret++;
        nextStep = calculateStep(abs(modSettings.modifierSpeed));
    }
}

void SmartLED::modifierFading()
{
    if (modSettings.currentDiscret == 0)
    {
        float maxVal;
        modSettings.discretization = 0;
        for (int i = 0; i < pixelCount; i++)
        {
            maxVal = readyLeds[i].r;
            if (maxVal < readyLeds[i].g) maxVal = readyLeds[i].g;
            if (maxVal < readyLeds[i].b) maxVal = readyLeds[i].b;
            fLeds[i].r[0] = readyLeds[i].r;
            fLeds[i].r[1] = (maxVal < 1) ? 0 : fLeds[i].r[0] / maxVal;
            fLeds[i].g[0] = readyLeds[i].g;
            fLeds[i].g[1] = (maxVal < 1) ? 0 : fLeds[i].g[0] / maxVal;
            fLeds[i].b[0] = readyLeds[i].b;
            fLeds[i].b[1] = (maxVal < 1) ? 0 : fLeds[i].b[0] / maxVal;
            if (modSettings.discretization < maxVal)
                modSettings.discretization = maxVal;
        }
        modSettings.effectPaused = true;
    }
    modSettings.currentDiscret++;
    if (modSettings.currentDiscret >= modSettings.discretization)
    {
        modSettings.effectPaused = false;
        modifier = 0;
    } 
    for (int i = 0; i < pixelCount; i++)
    {
        fLeds[i].r[0] -= fLeds[i].r[1];
        fLeds[i].g[0] -= fLeds[i].g[1];
        fLeds[i].b[0] -= fLeds[i].b[1];
        readyLeds[i].r = fLeds[i].r[0] = (fLeds[i].r[0] < 1) ? 0 : fLeds[i].r[0];
        readyLeds[i].g = fLeds[i].g[0] = (fLeds[i].g[0] < 1) ? 0 : fLeds[i].g[0];
        readyLeds[i].b = fLeds[i].b[0] = (fLeds[i].b[0] < 1) ? 0 : fLeds[i].b[0];
        strip->setPixelColor(i, strip->Color(readyLeds[i].r, readyLeds[i].g, readyLeds[i].b));
    }
    nextStep = calculateStep(abs(modSettings.modifierSpeed), 1000);
}

