#ifndef EMS22A_H
#define EMS22A_H

class Ems22a {
  private:
    int PIN_INPUT;
    int PIN_CLOCK;
    int PIN_GND;
    int PIN_DATA;
    int PIN_VCC;
    int PIN_CS;

    uint16_t encoderResolution = 1023;
    uint8_t lapNumber = 0;
    uint8_t maxLap = 3;
    static const int prevPositionN = 3;
    uint16_t prevPosition[prevPositionN];
    uint16_t offset = 0;

    void handleLapChange();
    void changeLap(int i);
  
  public:
    void setUp();
    void setLap(uint8_t i);
    void setMaxLap(uint8_t i);
    void setOffset(uint16_t pos);
    void setEqualTo(float pos);
    int readPosition();
    void step();
    int getOffset();
    int getLap();
    int getPosition();
    int getMaxResolution();
    float getAngleRadians();
    Ems22a();
    Ems22a(int, int, int, int, int, int);
    ~Ems22a();
};

#endif
