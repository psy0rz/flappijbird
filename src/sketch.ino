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


//display
static const int DATA_PIN = 20;
static const int CLK_PIN  = 5;
static const int CS_PIN   = 21;
static const int INTENSITY = 5;

//button and sound
static const int LOW_PIN = 11;             // ground pin for the button ;-)
static const int BUTTON_PIN = 9;           // choose the input pin for the pushbutton 
static const int SOUND_PIN = 8;            // pieze element for sound effects

//highscores
static const int RESET_SCORE_PIN = 10;      //pull this to ground during reset, to clear highscore 
static const int SCORE_ADDRESS = 1023;      //eeprom address to store score in
static const int MAX_RECORDING = 1000;        //maxnumber of button pushes to record 

//screen size
static const int Y_MAX = 1000; ;//not actual screen size..we downscale this later because arduino is bad at floats
static const int Y_MIN = -100; //negative so the player doesnt instantly die on the bottom
static const int X_MAX = 7;
static const int X_MIN = 0;

//bird and tube settings
static const int BIRD_JUMP_SPEED = 60; //strongness of the jump. 
static const int TUBES = 3; //max nr of tubes active at the same time
static const int BIRD_GRAVITY=-7; 

LedControl lc=LedControl(DATA_PIN, CLK_PIN, CS_PIN, 1);

char msg[100]; //global message buffer for textscroller

//status of a tube
struct tube_status
{
  int y;
  int x;
  int gap;
};

//are we in playback-mode? 
bool recording_play=false;

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
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LOW_PIN, OUTPUT);
    digitalWrite(LOW_PIN, LOW);

    //reset score?
    pinMode(RESET_SCORE_PIN, INPUT_PULLUP);
    if (!digitalRead(RESET_SCORE_PIN))
    {
      EEPROM.write(SCORE_ADDRESS,0);
    }
}

//reboot arduino
void(* reboot) (void) = 0;

//called when player is finshed
void finished(int score, byte recording[])
{
  noTone(SOUND_PIN); //interference with scroll
  sprintf(msg,"    %d  ", score);
  scrolltext(lc, msg, 50, BUTTON_PIN);

  //got highscore?
  if (score>EEPROM.read(SCORE_ADDRESS) || EEPROM.read(SCORE_ADDRESS)==255)
  {
    //store highscore
    EEPROM.write(SCORE_ADDRESS, score);
    //store recording
    eeprom_update_block(recording, 0, MAX_RECORDING);
    rickroll();
    scrolltext(lc, "   w00t!  ", 25, BUTTON_PIN);
  }
  else
  {
    ;
  }
  reboot();
}


void sound(int freq, int duration)
{
  if (!recording_play)
    tone(SOUND_PIN, freq, duration);
}


void loop()
{


  //bird physics
  int bird_y=Y_MAX/2;
  int bird_x=3;
  int bird_speed=BIRD_JUMP_SPEED; //game starts with jump
  byte bird_bits=0;

  //settings for new tubes (static for now)
  int tube_min=100; //min and max tube y-offsets for randomizer
  int tube_max=800;
  int tube_gap=300; //gap size
  int tube_countdown_min=10; //min and max time for tube creation randomizer 
  int tube_countdown_max=100;

  //tube dynamics
  int tube_shift_delay=10; //frames between each left shift (static for now)
  tube_status tubes[TUBES];  //the list of tubes (look at tube_status struct for more info)
  int tube_shift_countdown=tube_shift_delay; //count down before next leftshit
  int tube_countdown=10; //cycles before creating next tube
  byte tube_bits_at_bird=0;

  unsigned long start_time=millis();
  int frame_time=25;

  bool button_state=true;

  int score=0;
  byte recording[MAX_RECORDING]; //record button frame-timing for replay
  byte recording_press_nr=0;
  byte recording_press_time=0;
  memset(recording, 255, MAX_RECORDING);

  //init tubes  
  for (int tube_nr=0; tube_nr<TUBES; tube_nr++)
  {
    tubes[tube_nr].x=X_MIN-1; //disables it
  }

  //show highscore
  sprintf(msg,"   highscore %d - flappIJbird ", EEPROM.read(SCORE_ADDRESS));
  if (scrolltext(lc, msg, 25, BUTTON_PIN))
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
    bird_speed=bird_speed+BIRD_GRAVITY;

    //not in playback mode?
    if (!recording_play)
    {
      //button changed?
      if (!recording_play && digitalRead(BUTTON_PIN) != button_state)
      {
        button_state=digitalRead(BUTTON_PIN);

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
    }
    //we're in playback mode 
    else 
    {
      //is it time to simulate the next buttonpress?
      if (recording_press_time>= recording[recording_press_nr])
      {
        bird_speed=BIRD_JUMP_SPEED;
        recording_press_time=0;
        recording_press_nr++;
      }

      //abort playback?
      if (!digitalRead(BUTTON_PIN))
        reboot();
    }

    //change y postion of bird according to current speed
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
        lc.setRow(0, bird_x, tube_bits_at_bird|bird_bits);
        delay(50);
      }
      finished(score, recording);
    }

    //clip to ceiling
    if (bird_y>Y_MAX)
      bird_y=Y_MAX;

    //downscale height to 8 pixels :P
    //( LSB is top pixel )
    bird_bits=(B10000000 >> ((bird_y*7) / Y_MAX));

    //increase framenumber since last button press 
    recording_press_time++;

    //////////////////////////////// tubes
    tube_shift_countdown--;
    tube_countdown--;

    //is it time to shift the tubes to left?
    if (tube_shift_countdown<=0)
    {
      tube_bits_at_bird=0;
      tube_shift_countdown=tube_shift_delay;

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
 

    /////////////////////////// tube collision detection
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

    //wait for next frame
    while( (millis()-start_time) < frame_time){
      ;
    }
  }
}
