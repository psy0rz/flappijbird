#include "LedControlMS.h"
#include "scroller.h"
#include <EEPROM.h>
#include <avr/eeprom.h>
#include "rickroll.h"
/*** 

flappIJbird for Ijduino. (C)2015 edwin@datux.nl

first created at the https://nurdspace.nl Nurd-inn.

ijduino: http://ijhack.nl/project/ijduino

github: https://github.com/psy0rz/stuff/tree/master/flappijbird

Released under GPL. 

***/


static const int DATA_PIN = 20;
static const int CLK_PIN  = 5;
static const int CS_PIN   = 21;

static const int lowPin = 11;             /* ground pin for the buton ;-) */
static const int buttonPin = 9;           /* choose the input pin for the pushbutton */
static const int soundPin = 8;             

static const int resetScorePin = 10;      /* pull this to ground to reset score */
static const int scoreAddress = 1023;

static const int INTENSITY = 5;


LedControl lc=LedControl(DATA_PIN, CLK_PIN, CS_PIN, 1);

void setup()
{
    randomSeed(0); //change this for a different level

    /*
       The MAX72XX is in power-saving mode on startup,
       we have to do a wakeup call
     */
    lc.shutdown(0,false);
    /* Set the brightness to a medium values */
    lc.setIntensity(0,INTENSITY);
    /* and clear the display */
    lc.clearDisplay(0);
    /* setup pins */
    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(lowPin, OUTPUT);
    digitalWrite(lowPin, LOW);

    //reset score?
    pinMode(resetScorePin, INPUT_PULLUP);
    if (!digitalRead(resetScorePin))
    {
      EEPROM.write(scoreAddress,0);
    }
}

//screen size
#define Y_MAX 1000 //not actual screen size..we downscale this later because arduino is bad at floats
#define Y_MIN -100 //negative so the player doesnt instantly die on the bottom
#define X_MAX 7
#define X_MIN 0

#define BIRD_JUMP_SPEED 60

#define TUBES 3 //max nr of tubes active at the same time

#define MAX_RECORDING 500 //number of button pushes

char msg[100];

struct tube_status
{
  int y;
  int x;
  int gap;
};

void(* reboot) (void) = 0;

//called when player is finshed
void finished(int score, byte recording[])
{
  noTone(soundPin); //interference with scroll
  sprintf(msg,"    %d  ", score);
  scrolltext(lc, msg, 50);

  //got highscore?
  if (score>EEPROM.read(scoreAddress) || EEPROM.read(scoreAddress)==255)
  {
    //store highscore
    EEPROM.write(scoreAddress, score);
    //store recording
    eeprom_update_block(recording, 0, MAX_RECORDING);
    rickroll();
  }
  else
  {
    ;
  }
  reboot();
}

bool recording_play=false;

void sound(int freq, int duration)
{
  if (!recording_play)
    tone(soundPin, freq, duration);
}


void loop()
{


  //bird physics
  int bird_y=Y_MAX/2;
  int bird_x=3;
  int bird_speed=BIRD_JUMP_SPEED; //game starts with jump
  int bird_gravity=-7;
  byte bird_bits=0;

  //tube
  int tube_min=100;
  int tube_max=800;
  int tube_gap=300;

  int tube_shift_delay=250; //milliseconds between each left shift
  tube_status tubes[TUBES]; 
  unsigned long tube_time=millis();
  int tube_countdown=10; //cycles before creating next tube
  int tube_countdown_min=10;
  int tube_countdown_max=100;
  byte tube_bits_at_bird=0;

  int score=0;

  unsigned long start_time=millis();
  int frame_time=25;

  bool button_state=true;

  byte recording[MAX_RECORDING]; //record button frame-timing for replay
  byte recording_press_nr=0;
  byte recording_press_time=0;
  memset(recording, 255, MAX_RECORDING);

//  byte recording_frames=0; //number of frames since last press

  //init tubes  
  for (int tube_nr=0; tube_nr<TUBES; tube_nr++)
  {
    tubes[tube_nr].x=X_MIN-1; //disables it
  }

  //show highscore
  sprintf(msg,"   highscore %d - flappIJbird ", EEPROM.read(scoreAddress));
  if (scrolltext(lc, msg, 25, buttonPin))
  {
    //scroller was NOT aborted, so start playback
    recording_play=true;
    eeprom_read_block(recording, 0, MAX_RECORDING);
  }

  //start signal
  for(int i=0; i<10; i++)
  {
    sound(1000+(i*100), 100);
    delay(50);
  }

  //main gameloop
  while(1)
  {
    start_time=millis();

    //////////////////////////////// bird physics and control

    //gravity, keep accelerating downwards
    bird_speed=bird_speed+bird_gravity;

    //button changed?
    if (!recording_play && digitalRead(buttonPin) != button_state)
    {
      button_state=digitalRead(buttonPin);

      //its pressed, so jump! 
      if (!button_state)
      {
          bird_speed=BIRD_JUMP_SPEED;

          //move to next recording position (for later game-replay)
          if (recording_press_nr<MAX_RECORDING)
            recording[recording_press_nr]=recording_press_time;
          recording_press_time=0;
          recording_press_nr++;
      }
    }

    //we're in playback, simulate a buttonpress?
    if (recording_play)
    {
      if (recording_press_time>= recording[recording_press_nr])
      {
        bird_speed=BIRD_JUMP_SPEED;
        recording_press_time=0;
        recording_press_nr++;
      }

      //abort playback?
      if (!digitalRead(buttonPin))
        reboot();
    }

    //change y postion of bird
    bird_y=bird_y+bird_speed;

    //crashed on bottom?
    if (bird_y<Y_MIN)
    {
      //blink and make sound
      for (int i=0; i<10; i++)
      {
        sound( 2000- (i*200), 200);
        lc.setRow(0, bird_x, tube_bits_at_bird);
        delay(50);
        lc.setRow(0, bird_x, bird_bits);
        delay(50);
      }
      finished(score, recording);
    }

    if (bird_y>Y_MAX)
      bird_y=Y_MAX;

    //downscale birdheight to 8 pixels :P
    //( LSB is top pixel )
    bird_bits=(B10000000 >> ((bird_y*7) / Y_MAX));

    //////////////////////////////// tubes

    tube_countdown--;

    //is it time to shift the tubes to left?
    if (millis()-tube_time > tube_shift_delay)
    {
      tube_bits_at_bird=0;

      tube_time=millis();
      //traverse all the tubes
      for (int tube_nr=0; tube_nr<TUBES; tube_nr++)
      {
        //is the tube active? (inactive tubes are outside the left of the screen)
        if (tubes[tube_nr].x>=X_MIN)
        {
          //remove from old location
          lc.setRow(0, tubes[tube_nr].x, 0);
          tubes[tube_nr].x--;

          //draw on new location, and do collision detection
          if (tubes[tube_nr].x>=X_MIN)
          {
            //determine tube-bits (some bitwise magic instead of forloops)
            byte tube_bits=(1 <<((tubes[tube_nr].gap*7/Y_MAX)+1))-1; //determine gap-pixels  00000111    
            tube_bits=tube_bits << (tubes[tube_nr].y*7/Y_MAX); //shift gap in place          00011100     
            tube_bits=~tube_bits; //invert it to get an actual gap                           11100011

            //are we on the birdplace?
            if (tubes[tube_nr].x==bird_x)
            {
              //store it to blend with the bird and do collision detetion later
              tube_bits_at_bird=tube_bits;
            }
            else
            {
              //draw the tube
              lc.setRow(0, tubes[tube_nr].x, tube_bits);
            }

            //is the tube past the bird?
            if (tubes[tube_nr].x==bird_x-1)
            {
              score++; //scored a point! \o/
              sound( 1000+(score*100), 100);
            }
          }
        }
        //tube is inactive, do we need a new tube?
        else
        {
          if (tube_countdown<0)
          {
            tubes[tube_nr].x=X_MAX+1;
            tubes[tube_nr].y=random(tube_min, tube_max);
            tubes[tube_nr].gap=tube_gap;
            tube_countdown=random(tube_countdown_min, tube_countdown_max);
          }
        }
      }
    }

    //draw bird (and tube)
    lc.setRow(0, bird_x,  bird_bits|tube_bits_at_bird);
 
    //is the tube at the bird? 
    if (tube_bits_at_bird)
    {
      //collision?
      if ( (tube_bits_at_bird|bird_bits) == tube_bits_at_bird)
      {
        //blink and make sound
        for (int i=0; i<10; i++)
        {
          sound( 1000, 100);
          lc.setRow(0, bird_x, bird_bits);
          delay(50);
          sound( 500, 100);
          lc.setRow(0, bird_x, bird_bits|tube_bits_at_bird);
          delay(50);
        }
        finished(score, recording);
      }
    }


    //record framenumber since last press
    recording_press_time++;


    //wait for next frame
    while( (millis()-start_time) < frame_time){
      ;
    }
  }
}
