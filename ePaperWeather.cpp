#include "ePaperWeather.h"
#include "epd.h"
#include <cstdio>
#include <cstdlib>

char ePaperWeather::_backgroundBmp[9] = "BACK.BMP";
char ePaperWeather::_loBatBmp[11] = "LOBAT.BMP";
char ePaperWeather::_hiBatBmp[11] = "HIBAT.BMP";
char ePaperWeather::_bigPrefix = 'B';
char ePaperWeather::_lilPrefix = 'S';
int ePaperWeather::_bigWidths[10] = {157, 92, 150, 141, 157, 151, 150, 157, 141, 150};
int ePaperWeather::_lilWidths[10] = {87, 53, 77, 74, 82, 80, 80, 82, 74, 79};

enum { TEMP, HI_TEMP, DEW_POINT };

int ePaperWeather::_batPosition[2] = {524, 452};
int ePaperWeather::_bigHeight = 217;
int ePaperWeather::_lilHeight = 110;

// return the x position of the left edge of the drawing box for 
// one of the temperatures displayed on the ePaper
// numbers are variable-width, so compute full width then subtract
// half of it from the center line of the drawing box.
// The center lines are:
//     main temp: 300
//     hi temp:   450
//     dew point: 150 
int ePaperWeather::_computeLeftEdge(bool isNegative, int hundreds, int tens, int ones, int type)
{
    int leftEdge = 0;
    int width = 0;
    
    if (isNegative) width += 30;
    if (hundreds > 0) width += (type == TEMP) ? _bigWidths[hundreds] : _lilWidths[hundreds];
    if (tens > 0 || hundreds > 0) width += (type == TEMP) ? _bigWidths[tens] : _lilWidths[tens];
    width += (type == TEMP) ? _bigWidths[ones] : _lilWidths[ones];
    
    if (type == TEMP) leftEdge = (int) 300 - (width / 2);
    if (type == DEW_POINT) leftEdge = (int) 150 - (width / 2);
    if (type == HI_TEMP) leftEdge = (int) 450 - (width /2);
    
    return leftEdge;    
}


// take a double and return a rounded integer between -199 and 199
int ePaperWeather::_roundTemp(double Temp)
{
    Temp = Temp + 0.5 - (Temp < 0);
    int t = (int)Temp;
    if (t < -199) t = -199;
    if (t >  199) t =  199;
    return t;
}

// display a temperature of a type TEMP (big, top center), HI_TEMP (small, lower right), 
// or DEW_POINT (small, lower left).
void ePaperWeather::_displayTemp(bool isNegative, int huns, int tens, int ones, int type)
{
    // where to draw each thing
    int ts[3]; ts[TEMP] = 138; ts[HI_TEMP] = 563; ts[DEW_POINT] = 563; // top edges of drawing areas
    int ls[3]; ls[TEMP] = 0;   ls[HI_TEMP] = 304; ls[DEW_POINT] = 0;   // left edges
    int rs[3]; rs[TEMP] = 500; rs[HI_TEMP] = 600; rs[DEW_POINT] = 290; // right edges
    char prefix = (type == TEMP) ? _bigPrefix : _lilPrefix;
 
    // blank out the old value
    epd_set_color(WHITE, BLACK); // white rectangles to erase old data
    epd_fill_rect(ls[type], ts[type], rs[type], (ts[type] + ((type == TEMP) ? _bigHeight : _lilHeight)));
    epd_set_color(BLACK, WHITE); // back to black on white
   
    int x = _computeLeftEdge(isNegative, huns, tens, ones, type);
    int y = ts[type];

    char filename[7] = "";
    
    // is it negative? draw a 20x15 rectangle to the left
    // Only dew point can be negative in my neighborhood :-)
    if (isNegative) 
    {
        epd_fill_rect(x, (y + 55), (x + 20), (y + 72));
        x = x + 30;
    }
    
    if (huns > 0) // display a 1
    {
        sprintf(filename, "%c1.BMP", prefix);
        epd_disp_bitmap(filename, x, y);
        x = x + ((type == TEMP) ? _bigWidths[1] : _lilWidths[1]);
    }
    
    if (tens > 0 || huns > 0) // display a tens digit if the number is 2 or 3 digits long
    {
        sprintf(filename, "%c%d.BMP", prefix, tens);
        epd_disp_bitmap(filename, x, y);
        x = x + ((type == TEMP) ? _bigWidths[tens] : _lilWidths[tens]);
    }
    
    sprintf(filename, "%c%d.BMP", prefix, ones); // always display a ones digit
    epd_disp_bitmap(filename, x, y);
    
}

// break a temp into digits and call a better _displayTemp
void ePaperWeather::_displayTemp(double temp, int type)
{
     // separate out the digits to be displayed one at a time
    bool isNegative = (temp < 0);
    int huns = (int)(abs(temp) >= 100);
    int tens = (int)(abs(temp - (huns * 100)) / 10);
    int ones = (int)(abs(temp) % 10);

    _displayTemp (isNegative, huns, tens, ones, type);
}

void ePaperWeather::_blankBat()
{
    epd_set_color(WHITE, BLACK); // white rectangles to erase old data
    epd_fill_rect(_batPosition[0], _batPosition[1], _batPosition[0] + 48, _batPosition[1] + 24);
    epd_set_color(BLACK, WHITE); // back to black on white
}
    
void ePaperWeather::_updateBat(bool lo, bool hi)
{
    _blankBat();

    if (lo) epd_disp_bitmap(_loBatBmp, _batPosition[0], _batPosition[1]);
    if (hi) epd_disp_bitmap(_hiBatBmp, _batPosition[0], _batPosition[1]);
}

void ePaperWeather::UpdateDisplay(double Temp, double HiTemp, double DewPoint, bool loBatt, bool hiBatt)
{
    epd_wakeup();
            
    epd_set_memory(MEM_TF);     // flash memory (MEM_NAND is onboard, MEM_TF is SD Card)
    epd_screen_rotation(3);     // sideways
    epd_set_color(BLACK, WHITE);// black on white
    
    epd_clear();
    epd_disp_bitmap(_backgroundBmp, 0, 0);
    _displayTemp(Temp, TEMP);
    _displayTemp(HiTemp, HI_TEMP);
    _displayTemp(DewPoint, DEW_POINT);
    _updateBat(loBatt, hiBatt);
    epd_update();
    
    epd_enter_stopmode();
}

ePaperWeather::ePaperWeather() { }

ePaperWeather::ePaperWeather(double Temp, double HiTemp, double DewPoint, bool loBatt, bool hiBatt)
{
    this->UpdateDisplay(Temp, HiTemp, DewPoint, loBatt, hiBatt);
}
