#ifndef EPAPERWEATHER_H
#define EPAPERWEATHER_H


class ePaperWeather
{

private:
    static char _backgroundBmp[9];
    static char _loBatBmp[11];
    static char _hiBatBmp[11];
    static char _bigPrefix;
    static char _lilPrefix;
    static int _bigWidths[10];
    static int _lilWidths[10];

    static int _batPosition[2];
    static int _bigHeight;
    static int _lilHeight;
    
    int _computeLeftEdge(bool isNegative, int hundreds, int tens, int ones, int type);
    int _roundTemp(double Temp);
    void _displayTemp(bool isNegative, int huns, int tens, int ones, int type);
    void _displayTemp(double temp, int type);
    void _blankBat();
    void _updateBat(bool lo, bool hi);


public:
    int Temp;
    int HiTemp;
    int DewPoint;
    bool BatteryLow;
    bool BatteryHigh;
    ePaperWeather();
    ePaperWeather(double Temp, double HiTemp, double DewPoint, bool loBatt, bool hiBatt);
    void UpdateDisplay();
};

#endif