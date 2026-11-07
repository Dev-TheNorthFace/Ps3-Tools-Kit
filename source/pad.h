#ifndef PAD_H
#define PAD_H

enum {
    B_UP, B_DOWN, B_LEFT, B_RIGHT, B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE,
    B_START, B_SELECT, B_L1, B_R1, B_L2, B_R2, B_L3, B_R3, B_COUNT
};

typedef struct {
    int connected;
    int held[B_COUNT];      
    int pressed[B_COUNT];   
    int repeat[B_COUNT];    
    int lx, ly, rx, ry;     
    int pl2, pr2;           
    int sx, sy, sz;         
    int has_pressure, has_sensor;
    int last_button;        
} PadState;

extern PadState pad;
extern const char *button_names[B_COUNT];

#define PAD_PORTS 7
typedef struct {
    int connected;
    unsigned buttons;          
    int lx, ly, rx, ry;        
    int pre[12];               
    int sx, sy, sz, sg;        
    int has_pressure, has_sensor, has_vibrate;
    unsigned long long frames; 
    int len;                   
} PadRaw;
extern PadRaw g_pads[PAD_PORTS];
int pad_rumble(int port, int small_on, int large_speed);   

void pad_init(void);
void pad_update(void);
void pad_end(void);

#endif