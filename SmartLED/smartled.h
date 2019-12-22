#ifndef SMARTLED_H
#define SMARTLED_H

#include <Adafruit_NeoPixel.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>

class SmartLED;

/** перечисление доступных режимов
 */
typedef enum 
{
    MIOff           = 0,                ///< лента отключена
    MIWaves         = 1,                ///< волны
    MIRainbow,                          ///< радуга
    MILines,                            ///< линии
    MISnowflake,                        ///< снежинки
    MIStroboscope,                      ///< стробоскоп
//    MIIntersects,                       ///< пересечения
    MISnake,                            ///< змейка
    MIPulse,                            ///< пульс
    MICycle,                            ///< спец.режим - автоматическое переключение режимов
    MIShedule,                          ///< спец.режим - планировщик режимов
    MIMAX                               ///< максимальный доступный режим
} ModeID;

/// объявление типа указателя на метод-эффект
typedef void (SmartLED::* EffectPtr)(bool);
/// объявление типа указателя на метод-модификатор
typedef void (SmartLED::* ModifierPtr)();

/** основные параметры каждого режима
 */
typedef struct 
{
  ModeID modeID;                        ///< идентификатор режима
  const char* modeName;                 ///< название режима
  uint16_t stepBase;                    ///< базовое значение для расчета шага задержки
  EffectPtr effect;                     ///< указатель на соответствующий метод-эффект
} LightMode;

/** три беззнаковых целых для описания одного цвета
 */
typedef struct 
{
  uint8_t r, g, b;
} RGBColor;

/** три знаковых целых, каждое применительно к соответствующему цвету
 */
typedef struct 
{
  int16_t r, g, b;
} RGBValue;

/** структура из 3 массива цветов, каждый состоит из 2 элементов. 
 * 1 элемент - это текущее значение цвета, 
 * 2 элемент - это значение для изменения цвета в соответствии с тек.эффектом
 */
typedef struct 
{
  float r[2], g[2], b[2];
} RGBFloat;

struct ShedulerElement;

/** описание элемента планирования
 */
typedef struct ShedulerElement
{
  ModeID mode;                          ///< идентификатор запланированного режима
  uint16_t length;                      ///< длительность работы режима, сек.
  void* parameters;                     ///< указатель на параметры запланированного режима
  ShedulerElement* next;                ///< указатель на следующий запланированный элемент
};

/** параметры планировщика
 */
typedef struct 
{
  uint8_t count;                        ///< количество запланированных элементов
  uint8_t current;                      ///< текущий (работающий) элемент
  ShedulerElement* head;                ///< указатель на голову списка запланированных элементов
} StripSheduler;

/** параметры смены режимов
 */
typedef struct 
{
    uint32_t nextChange;                ///< 
    uint32_t period;
    uint8_t current;
    uint8_t fading;
    bool isRandom;
    bool needToFade;
} StripCycle;

/** параметры эффекта волн
 */
typedef struct 
{
  RGBColor colorMin;                    ///< минимальные значения волн каждого цвета
  RGBColor colorMax;                    ///< максимальные значения волн каждого цвета
  RGBColor count;                       ///< количество волн каждого цвета
  RGBValue speed;                       ///< скорость движения волн каждого цвета
} StripWaves;

/** параметры эффекта радуги
 */
typedef struct 
{
  RGBColor color[10];                   ///< цвет каждой ключевой точки
  uint8_t count;                        ///< количество используемых ключевых точек, от 2 до 10
  int8_t speed;                         ///< скорость движения радуги
  bool reverse;                         ///< разрешить случайное изменение направления движения радуги
} StripRainbow;

/** параметры эффекта линий
 */
typedef struct 
{
  RGBColor color[10];                   ///< цвет каждой линии для последовательного переключения
  uint8_t count;                        ///< количество используемых цветов
  int8_t speed;                         ///< скорость движения линии
  bool reverse;                         ///< разрешить случайное изменение направления движения линий
  bool multiColor;                      ///< разрешить использовать случайный цвет линий
} StripLines;

/** параметры эффекта снежинок
 */
typedef struct 
{
  RGBColor color;                       ///< цвет снежинок
  bool multiColor;                      ///< разрешить использовать случайный цвет снежинок
  uint8_t flakeSize;                    ///< радиус снежинок, пикс.
  uint8_t count;                        ///< частота появления снежинок
  uint8_t fading;                       ///< скорость затухания снежинок
} StripSnowflake;

/** параметры эффекта стробоскопа
 */
typedef struct 
{
  RGBColor color;                       ///< цвет появляющихся пикселей
  bool multiColor;                      ///< разрешить использовать случайный цвет пикселей
  uint8_t count;                        ///< частота появления пикселей
} StripStroboscope;

/** параметры эффекта змейки
 */
typedef struct 
{
  RGBColor color;                       ///< цвет змеек
  uint8_t count;                        ///< количество змеек на ленте
  int8_t speed;                         ///< скорость движения змеек
  bool multiColor;                      ///< разрешить использовать случайный цвет змеек
  bool reverse;                         ///< разрешить случайное изменение направления движения змеек
} StripSnake;

/** параметры ключевых точек для эффекта пересечений
 */
typedef struct 
{
  uint8_t pos;                          ///< текущая позиция точки
  RGBColor color;                       ///< цвет точки
  int8_t speed;                         ///< скорость движения точки
} IPoint;

/** TODO параметры эффекта пересечений
 */
typedef struct 
{
  uint8_t count;                        ///< количество движущихся точек
  IPoint point[10];                     ///< параметры каждой точки
} StripIntersects;

/** параметры эффекта пульсации
 */
typedef struct 
{
  RGBColor colorMin;                    ///< минимальный цвет ленты
  RGBColor colorMax;                    ///< максимальный цвет ленты
  int8_t speed;                         ///< скорость изменения цвета
} StripPulse;

/** параметры работы модификаторов
 */
typedef struct
{
    uint8_t discretization;             ///< дискретизация (или количество шагов)
    uint8_t currentDiscret;             ///< текущий дискрет (или шаг)
    int8_t modifierSpeed;               ///< скорость текущего модификатора (может работать параллельно с эффектом, а может временно отключать эффект)
    bool modifierVisible;               ///< отображать действие модификатора, в противном случае он будет выполнен без пошаговой перерисовки и максимально быстро
    bool effectPaused;                  ///< приостановить эффект во время работы модификатора
    RGBColor *leds;                     ///< массив для временной работы с лентой
} ModifierParameters;

typedef struct 
{
    ModeID mode;                        ///< текущий режим
    ModeID specialMode;                 ///< специальный режим (проверяется после основного цикла)
    
    StripWaves waves;                   ///< параметры волн
    StripRainbow rainbow;               ///< параметры радуги
    StripLines lines;                   ///< параметры линий
    StripSnowflake snowflake;           ///< параметры снежинок
    StripStroboscope stroboscope;       ///< параметры стробоскопа
    StripIntersects intersects;         ///< параметры пересечений
    StripSnake snake;                   ///< параметры змейки
    StripPulse pulse;                   ///< параметры пульса
    StripCycle cycle;                   ///< параметры автосмены режимов
    
    uint32_t effectCreating;            ///< счетчик для создания нового элемента эффекта 
    int8_t direct;                      ///< направление движения эффекта
    int16_t position;                   ///< текущая позиция
    int8_t step;                        ///< шаг
    RGBColor currentColor;              ///< текущий цвет элемента

    uint16_t headerSize;                ///< размер заголовка
    uint16_t shedulerSize;              ///< размер области памяти с запланированными эффектами
} Configuration;

/** класс для работы с лентой
 */
class SmartLED
{
public:
    /**
     * Конструктор класса
     * @param pCount количество пикселей в ленте
     * @param pPin номер пина, на котором висит лента
     * @param colorScheme цветовая схема библиотеки NeoPixel
     * @param ue TODO признак необходимости хранения параметров в EEPROM 
     */
    SmartLED(uint8_t pCount, uint8_t pPin, neoPixelType colorScheme = NEO_RGB, bool ue = true);
    /**
     * Деструктор класса
     */
    ~SmartLED();
    /**
     * Выбор нового режима по индексу
     * @param mID индекс нового режима работы ленты
     */
    void selectModeByID(ModeID mID);
    /**
     * Выбор нового режима работы по имени
     * @param modeName имя нового режима
     */
    void selectMode(const char* modeName);
    /**
     * Установить новое значение параметра. Значения можно задавать только параметрам, имеющимся в текущем режиме
     * @param option имя параметра в текстовом виде
     * @param strVal новое значение параметра в текстовом виде (для цветовых значений числа разделяются точкой с запятой)
     */
    void setOption(char* option, char* strVal);
    /**
     * Получить активный режим работы
     * @return режим работы
     */
    ModeID mode();
    /**
     * обновить данные в соответствии с заданным режимом и (или) модификатором, отправить их в ленту и при необходимости обновить ее
     */
    void process();
    /**
     * Получить адрес клиента
     * @param num номер клиента
     * @return IP-адрес
     */
    IPAddress remoteIP(uint8_t num);
    /**
     * Отправить простой текст клиенту
     * @param num номер клиента
     * @param txt отправляемый текст
     */
    void sendTXT(uint8_t num, const char* txt);
    /**
     * Отправить текущие значения клиенту
     * @param num номер клиента
     */
    void sendCurrentValues(uint8_t num);
    void sendSection(uint8_t num, ModeID sectionID);
    void sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, int32_t value);
    void sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, bool value);
    void sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, RGBColor value);
    void sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, RGBValue value);
    void sendValue(uint8_t num, const char *sectionTxt, const char* optionTxt, RGBFloat value);
    /**
     * дамп определенной области памяти
     * @param begin начало области памяти
     * @param length длина блока памяти
     */
    void memoryDump(uint8_t* begin, uint8_t length);
    /**
     * Сделать дамп памяти
     */
    void dump();
    EffectPtr effect;                   ///< указатель на текущий метод-эффект
    ModifierPtr modifier;               ///< указатель на текущий метод-модификатор
   
private:
    const uint32_t refreshRate = 20;    ///< частота перерисовки ленты, не менее мс
    const LightMode modes[10] = {
        { MIOff, "off", 1000, &SmartLED::makeOff },
        { MIWaves, "waves", 1000, &SmartLED::makeWaves },
        { MIRainbow, "rainbow", 1000, &SmartLED::makeRainbow },
        { MILines, "lines", 1000, &SmartLED::makeLines },
        { MISnowflake, "snowflake", 20, &SmartLED::makeSnowflake },
        { MIStroboscope, "stroboscope", 1000, &SmartLED::makeStroboscope },
    //    { MIIntersects, "intersects", 1000, &makeIntersects },
        { MISnake, "snake", 1000, &SmartLED::makeSnake },
        { MIPulse, "pulse", 1000, &SmartLED::makePulse },
        { MICycle, "cycle", 1000, &SmartLED::makeCycle },
        { MIShedule, "shedule", 1000, &SmartLED::makeShedule }
    };
    Adafruit_NeoPixel *strip;           ///< указатель на объект ленты
    WebSocketsServer *webSocket;        ///< указатель на вебсокет
    ModifierParameters modSettings;     ///< параметры модификатора
    uint32_t lastSaved;                 ///< время последнего сохранения настроек, мс
    uint32_t nextUpdate;                ///< время следующего обновления ленты, мс
    uint32_t nextStep;                  ///< время выполнения следующего шага, мс
    uint16_t pixelCount;                ///< количество диодов в ленте
    int8_t defaultSpeed;                ///< скорость по умолчанию для тех эффектов, в которых напрямую управлять скоростью нельзя (например, волны)
    int8_t zeroSpeed;                   ///< скорость для выключенного состояния (0)
    int8_t *effectSpeed;                ///< скорость текущего эффекта
    uint8_t pixelPin;                   ///< пин, к которому подключена лента
    bool needToSave;                    ///< признак необходимости сохранения настроек
    bool microsOverflow;
    bool needToUpdate;                  ///< признак необходимости обновления ленты
    bool useEEPROM;                     ///< TODO признак использования EEPROM
    RGBColor *readyLeds;                ///< массив с целочисленными данными, готовыми для отправки в ленту (FIXME - от него нужно избавиться, основным должен быть след.массив)
    RGBFloat *fLeds;                    ///< массив с дробными данными (для корректного расчета некоторых эффектов и модификаторов)
    Configuration settings;             ///< рабочие настройки
    StripSheduler sheduler;             ///< настройки планировщика
    
    int8_t moving[3];                   ///< массив переменных для расчета движения волн

    /**
     * Рассчитать время следующей активации эффекта или модификатора
     * @param speed скорость выполнения эффекта или модификатора (обычно число от 1 до 100)
     * @param stepBase база для расчета времени, задается если нужно перекрыть предустановленные параметры
     * @return рассчетное время в микросекундах
     */
    uint32_t calculateStep(uint8_t speed, uint16_t stepBase = 0);
    /**
     * Разобрать строку на три беззнаковых целых числа, и поместить их в структуру типа RGBColor
     * @param valueString строка вида "123;45;67"
     * @return структура типа RGBColor
     */
    RGBColor parseColorValue(char* valueString);
    /**
     * Разобрать строку на три знаковых целых числа, и поместить их в структуру типа RGBValue
     * @param valueString строка вида "123;-45;6789"
     * @return структура типа RGBValue
     */
    RGBValue parseSignedValue(char* valueString);
    /**
     * Преобразовать строку в число
     * @param valueString строка
     * @return число
     */
    int32_t parseSingleValue(const char* valueString);
    /**
     * Обнулить массив данных для ленты
     */
    void ledsToZero();
    /**
     * Автоматическое сохранение параметров, если пользователь менял режимы или параметры, и они не сохранены.
     * Выполняется через минуту после последних изменений
     */
    void autosave();
    /**
     * Загрузить параметры из EEPROM, выполняется при инициализации класса
     * @return true, если загрузка выполнена успешно, и false, если загруженные данные некорректны
     */
    bool loadSettings();
    /**
     * Установить значения по умолчанию. Выполняется при инициализации класса, если не удалось загрузить настройки из EEPROM
     */
    void setDefaultValues();
    /**
     * Заполнить параметры для отображения новой линии
     * @param idx индекс линии от 0 до 9
     */
    void generateLine(int idx);
    /**
     * Добавить новую снежинку
     */
    void addSnowflake();
    /**
     * Добавить пиксель в стробоскоп
     */
    void addStroboscope();
    /**
     * Метод эффекта отключения ленты. Данные обнуляются, лента гаснет.
     * @param isDefault true, если метод запущен первый раз
     */
    void makeOff(bool isDefault);
    /**
     * Метод эффекта волн. Движение волны каждого цвета рассчитывается независимо 
     * от других, затем для каждого пикселя формируется необходимый цвет
     * @param isDefault true, если метод запущен первый раз
     */
    void makeWaves(bool isDefault);
    /**
     * Метод эффекта радуги. Цвета пикселей рассчитываются исходя из количества и цвета 
     * ключевых точек; формируются плавные цветовые переходы между ними
     * @param isDefault true, если метод запущен первый раз
     */
    void makeRainbow(bool isDefault);
    /**
     * Метод эффекта линий. Линия начинает заполнять всю ленту одним цветом справа 
     * налево или наоборот
     * @param isDefault true, если метод запущен первый раз
     */
    void makeLines(bool isDefault);
    /**
     * Метод эффекта снежинок. Каждая снежинка появляется в произвольном месте и 
     * угасает с заданной скоростью
     * @param isDefault true, если метод запущен первый раз
     */
    void makeSnowflake(bool isDefault);
    /**
     * Метод эффекта стробоскопа. На ленте в произвольных местах вспыхивают и выстро
     * гаснут яркие пиксели
     * @param isDefault true, если метод запущен первый раз
     */
    void makeStroboscope(bool isDefault);
    /**
     * Метод эффекта пересечений. пока не реализован
     * @param isDefault true, если метод запущен первый раз
     */
    void makeIntersects(bool isDefault);
    /**
     * Метод эффекта змейки. На ленте появляется несколько исчезающих световых
     * полосок, движущихся слева направо или наоборот
     * @param isDefault true, если метод запущен первый раз
     */
    void makeSnake(bool isDefault);
    /**
     * Метод эффекта пульса. Цвет всей ленты плавно изменяется от минимального 
     * до максимального и обратно с заданной скоростью
     * @param isDefault true, если метод запущен первый раз
     */
    void makePulse(bool isDefault);
    /**
     * Метод эффекта автосмены режимов. С заданной периодичностью меняет текущий
     * режим работы ленты
     * @param isDefault true, если метод запущен первый раз
     */
    void makeCycle(bool isDefault);
    /**
     * Метод эффекта планирования. пока не реализован
     * @param isDefault true, если метод запущен первый раз
     */
    void makeShedule(bool isDefault);
    
    /**
     * Зеркально отразить текущий массив данных ленты и скопировать его во 
     * вспомогательный массив modSettings.leds
     */
    void mirrorToArray();
    
    /// Разные преобразования ленты - modifier<Имя модификатора>
    /**
     * Метод-модификатор, инвертирующий ленту
     */
    void modifierInvert();
    /**
     * Смещение ленты влево с заданным числом шагом на один пиксель
     */
    void moveLeft();
    /**
     * Смещение ленты вправо с заданным числом шагом на один пиксель
     */
    void moveRight();
    /**
     * Метод-модификатор, сдвигающий ленту вправо или влево с заданной дискретизацией
     */
    void modifierMoving();
    /**
     * Метод-модификатор, который плавно затемняет всю ленту
     */
    void modifierFading();
    
};

#endif /* SMARTLED_H */

