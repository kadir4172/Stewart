/* <Controlling code for Arduino Controlled Rotary Stewart Platform>
    Copyright (C) <2014>  <Tomas Korgo>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.*/

#include <Servo.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

//defines speed of repetitive maneuvers 1-15
#define MANEUVER_SPEED 10
//define of characters used for control of serial communication ['0'-'8']
#define SETBACKOFF 48
#define SETBACKON 49
#define SETPOSITIONS 50
#define PRINTPOS 51
#define STOPPRINTPOS 52
#define SWITCHIRDA 53
#define SETPOSITIONSINMS 54
#define SWITCHIRDAOFF 55
#define GEPOSITION 56
//defines of LCD pin numbers, most probably they dont have to be changed except of I2C_ADDR which value is neccessary and have to be changed.
#define I2C_ADDR 0x27
#define LCD 0
#define IMU 0
#define BACKLIGHT_PIN 3
#define En 2
#define Rw 1
#define Rs 0
#define D4 4
#define D5 5
#define D6 6
#define D7 7
//MIN and MAX PWM pulse sizes, they can be found in servo documentation
#define MAX 2200
#define MIN 800

//Positions of servos mounted in opposite direction
#define INV1 1
#define INV2 3
#define INV3 5

//constants for computation of positions of connection points
#define pi  3.14159
#define deg2rad 180/pi
#define deg30 pi/6

#define IMU_CALIBRATION_ON_POWERUP 0
//variables used for proper show of positions on LCD
char shown=0, showPos=0, useIrda=0;
unsigned long time;


#if (( MANEUVER_SPEED > 15 ) || ( MANEUVER_SPEED < 1 ))
  #error "MANEUVER_SPEED must be between 1-15"
#endif


//variable to store connected LCD
LiquidCrystal_I2C lcd(I2C_ADDR, En, Rw, Rs, D4,D5,D6,D7);

//Array of servo objects
Servo servo[6];
//Zero positions of servos, in this positions their arms are perfectly horizontal, in us
static int zero[6]={1475,1470,1490,1480,1460,1490};
//In this array is stored requested position for platform - x,y,z,rot(x),rot(y),rot(z)
static float arr[6]={0,0.0,0, radians(0),radians(0),radians(0)};
//Actual degree of rotation of all servo arms, they start at 0 - horizontal, used to reduce
//complexity of calculating new degree of rotation
static float theta_a[6]={0.0,0.0,0.0, 0.0,0.0,0.0};
//Array of current servo positions in us
static int servo_pos[6];
//rotation of servo arms in respect to axis x
const float beta[] = {pi/2,-pi/2,-pi/6, 5*pi/6,-5*pi/6,pi/6},
//maximum servo positions, 0 is horizontal position
      servo_min=radians(-80),servo_max=radians(80),
//servo_mult - multiplier used for conversion radians->servo pulse in us
//L1-effective length of servo arm, L2 - length of base and platform connecting arm
//z_home - height of platform above base, 0 is height of servo arms
servo_mult=400/(pi/4),L1 = 0.79,L2 = 4.66, z_home = 4.05;
//RD distance from center of platform to attachment points (arm attachment point)
//RD distance from center of base to center of servo rotation points (servo axis)
//theta_p-angle between two servo axis points, theta_r - between platform attachment points
//theta_angle-helper variable
//p[][]=x y values for servo rotation points
//re[]{}=x y z values of platform attachment points positions
//equations used for p and re will affect postion of X axis, they can be changed to achieve
//specific X axis position
const float RD = 2.42,PD =2.99,theta_p = radians(37.5),
theta_angle=(pi/3-theta_p)/2, theta_r = radians(8),
      p[2][6]={
          {
            -PD*cos(deg30-theta_angle),-PD*cos(deg30-theta_angle),
            PD*sin(theta_angle),PD*cos(deg30+theta_angle),
            PD*cos(deg30+theta_angle),PD*sin(theta_angle)
         },
         {
            -PD*sin(deg30-theta_angle),PD*sin(deg30-theta_angle),
            PD*cos(theta_angle),PD*sin(deg30+theta_angle),
            -PD*sin(deg30+theta_angle),-PD*cos(theta_angle)
         }
      },
      re[3][6] = {
          {
              -RD*sin(deg30+theta_r/2),-RD*sin(deg30+theta_r/2),
              -RD*sin(deg30-theta_r/2),RD*cos(theta_r/2),
              RD*cos(theta_r/2),-RD*sin(deg30-theta_r/2),
          },{
              -RD*cos(deg30+theta_r/2),RD*cos(deg30+theta_r/2),
              RD*cos(deg30-theta_r/2),RD*sin(theta_r/2),
              -RD*sin(theta_r/2),-RD*cos(deg30-theta_r/2),
          },{
              0,0,0,0,0,0
          }
};
//arrays used for servo rotation calculation
//H[]-center position of platform can be moved with respect to base, this is
//translation vector representing this move
static float M[3][3], rxp[3][6], T[3], H[3] = {0,0,z_home};


Adafruit_BNO055 bno = Adafruit_BNO055(55);
void setup(){
//LCD inicialisation and turning on the backlight
#if LCD
   lcd.begin(16,2);
   lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
   lcd.setBacklight(HIGH);
   lcd.home();
#endif

//attachment of servos to PWM digital pins of arduino
   servo[0].attach(3, MIN, MAX);
   servo[1].attach(5, MIN, MAX);
   servo[2].attach(6, MIN, MAX);
   servo[3].attach(9, MIN, MAX);
   servo[4].attach(10, MIN, MAX);
   servo[5].attach(11, MIN, MAX);
//begin of serial communication
   Serial.begin(9600);
//putting into base position
   setPos(arr);



#if IMU
     /* Initialise the sensor */
  if(!bno.begin())
  {
    /* There was a problem detecting the BNO055 ... check your connections */
    Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
    while(1);
  }
  
  delay(1000);
    
  bno.setExtCrystalUse(true); 
  bno.setMode(Adafruit_BNO055::OPERATION_MODE_NDOF);

  
#if IMU_CALIBRATION_ON_POWERUP   
  while(!bno.isFullyCalibrated()){
    uint8_t system, gyro, accel, mag;
    bno.getCalibration(&system, &gyro, &accel, &mag);
    Serial.print("Gyro: ");
    Serial.print(gyro, 4);
    Serial.print("\tAcc: ");
    Serial.print(accel, 4);
    Serial.print("\tmag: ");
    Serial.print(mag, 4);
    Serial.print("\tsys: ");
    Serial.print(system, 4);
    Serial.println("");      
  }  
    adafruit_bno055_offsets_t offsets_type;
    bno.getSensorOffsets(offsets_type);
    Serial.print("Accel_x_offset: ");
    Serial.println(offsets_type.accel_offset_x);
    Serial.print("Accel_y_offset: ");
    Serial.println(offsets_type.accel_offset_y);
    Serial.print("Accel_z_offset: ");
    Serial.println(offsets_type.accel_offset_z);
    Serial.print("Mag_x_offset: ");
    Serial.println(offsets_type.mag_offset_x);
    Serial.print("Mag_y_offset: ");
    Serial.println(offsets_type.mag_offset_y);
    Serial.print("Mag_z_offset: ");
    Serial.println(offsets_type.mag_offset_z);
    Serial.print("Gyro_x_offset: ");
    Serial.println(offsets_type.gyro_offset_x);
    Serial.print("Gyro_y_offset: ");
    Serial.println(offsets_type.gyro_offset_y);
    Serial.print("Gyro_z_offset: ");
    Serial.println(offsets_type.gyro_offset_z);
    Serial.print("Accel_radius_offset: ");
    Serial.println(offsets_type.accel_radius);
    Serial.print("Mag_radius_offset: ");
    Serial.println(offsets_type.mag_radius);

    bno.setSensorOffsets(offsets_type);
    bno.setMode(Adafruit_BNO055::OPERATION_MODE_NDOF);
#else
    
    adafruit_bno055_offsets_t offsets_type;
    offsets_type.accel_offset_x = 65516;
    offsets_type.accel_offset_y = 12;
    offsets_type.accel_offset_z = 2;

    offsets_type.mag_offset_x   = 65023;
    offsets_type.mag_offset_y   = 625;
    offsets_type.mag_offset_z   = 64742;

    offsets_type.gyro_offset_x  = 0;
    offsets_type.gyro_offset_y  = 65535;
    offsets_type.gyro_offset_z  = 1;

    offsets_type.accel_radius   = 1000;
    offsets_type.mag_radius     = 793;

    bno.setSensorOffsets(offsets_type);
    
    bno.setMode(Adafruit_BNO055::OPERATION_MODE_NDOF);
#endif

  
  bno.setMode(Adafruit_BNO055::OPERATION_MODE_NDOF);
  delay(100);
#endif
}

//function calculating needed servo rotation value
float getAlpha(int *i){
   static int n;
   static float th=0;
   static float q[3], dl[3], dl2;
   double min=servo_min;
   double max=servo_max;
   n=0;
   th=theta_a[*i];
   while(n<20){
    //calculation of position of base attachment point (point on servo arm where is leg connected)
      q[0] = L1*cos(th)*cos(beta[*i]) + p[0][*i];
      q[1] = L1*cos(th)*sin(beta[*i]) + p[1][*i];
      q[2] = L1*sin(th);
    //calculation of distance between according platform attachment point and base attachment point
      dl[0] = rxp[0][*i] - q[0];
      dl[1] = rxp[1][*i] - q[1];
      dl[2] = rxp[2][*i] - q[2];
      dl2 = sqrt(dl[0]*dl[0] + dl[1]*dl[1] + dl[2]*dl[2]);
    //if this distance is the same as leg length, value of theta_a is corrent, we return it
      if(abs(L2-dl2)<0.01){
         return th;
      }
    //if not, we split the searched space in half, then try next value
      if(dl2<L2){
         max=th;
      }else{
         min=th;
      }
      n+=1;
      if(max==servo_min || min==servo_max){
         return th;
      }
      th = min+(max-min)/2;
   }
   return th;
}

//function calculating rotation matrix
void getmatrix(float pe[])
{
   float psi=pe[5];
   float theta=pe[4];
   float phi=pe[3];
   M[0][0] = cos(psi)*cos(theta);
   M[1][0] = -sin(psi)*cos(phi)+cos(psi)*sin(theta)*sin(phi);
   M[2][0] = sin(psi)*sin(phi)+cos(psi)*cos(phi)*sin(theta);

   M[0][1] = sin(psi)*cos(theta);
   M[1][1] = cos(psi)*cos(phi)+sin(psi)*sin(theta)*sin(phi);
   M[2][1] = -cos(psi)*sin(phi)+sin(psi)*sin(theta)*cos(phi);

   M[0][2] = -sin(theta);
   M[1][2] = cos(theta)*sin(phi);
   M[2][2] = cos(theta)*cos(phi);
}
//calculates wanted position of platform attachment poins using calculated rotation matrix
//and translation vector
void getrxp(float pe[])
{
   for(int i=0;i<6;i++){
      rxp[0][i] = T[0]+M[0][0]*(re[0][i])+M[0][1]*(re[1][i])+M[0][2]*(re[2][i]);
      rxp[1][i] = T[1]+M[1][0]*(re[0][i])+M[1][1]*(re[1][i])+M[1][2]*(re[2][i]);
      rxp[2][i] = T[2]+M[2][0]*(re[0][i])+M[2][1]*(re[1][i])+M[2][2]*(re[2][i]);
   }
}
//function calculating translation vector - desired move vector + home translation vector
void getT(float pe[])
{
   T[0] = pe[0]+H[0];
   T[1] = pe[1]+H[1];
   T[2] = pe[2]+H[2];
}

unsigned char setPos(float pe[]){
    unsigned char errorcount;
    errorcount=0;
    for(int i = 0; i < 6; i++)
    {
        getT(pe);
        getmatrix(pe);
        getrxp(pe);
        theta_a[i]=getAlpha(&i);
        if(i==INV1||i==INV2||i==INV3){
            servo_pos[i] = constrain(zero[i] - (theta_a[i])*servo_mult, MIN,MAX);
        }
        else{
            servo_pos[i] = constrain(zero[i] + (theta_a[i])*servo_mult, MIN,MAX);
        }
    }

    for(int i = 0; i < 6; i++)
    {
        if(theta_a[i]==servo_min||theta_a[i]==servo_max||servo_pos[i]==MIN||servo_pos[i]==MAX){
            errorcount++;
        }
        servo[i].writeMicroseconds(servo_pos[i]);
    }
    return errorcount;    
}

//functions used for displaying actual platform position on 16x2 LCD display
#if LCD
void showRot(){
   lcd.setCursor(0,0);
   lcd.print("Rot");
   lcd.setCursor(12,0);
   lcd.print((int)(arr[3]*deg2rad));
   lcd.setCursor(3,1);
   lcd.print((int)(arr[4]*deg2rad));
   lcd.setCursor(11,1);
   lcd.print((int)(arr[5]*deg2rad));
}
void showComm(){
   if(shown==0){
      shown=1;
      lcd.setCursor(3,0);
      lcd.print("ation x: ");
      lcd.setCursor(0,1);
      lcd.print("y: ");
      lcd.setCursor(8,1);
      lcd.print("z: ");
   }
}
void clearNr(){
   lcd.setCursor(12,0);
   lcd.print("    ");
   lcd.setCursor(3,1);
   lcd.print("     ");
   lcd.setCursor(11,1);
   lcd.print("     ");

}
void showLoc(){
   lcd.setCursor(0,0);
   lcd.print("Loc");
   lcd.setCursor(12,0);
   lcd.print((int)(arr[0]*25.4));
   lcd.setCursor(3,1);
   lcd.print((int)(arr[1]*25.4));
   lcd.setCursor(11,1);
   lcd.print((int)(arr[2]*25.4));
}
#endif

//main control loop
void loop()
{
  static float command = 0;
  static int    channel = 0;
  static float cmd_arr[6]={0,0.0,0, radians(0),radians(0),radians(0)};
  static int cmd_state = 1;
  unsigned char error_count;
  float rate = (float)MANEUVER_SPEED / 10.0; 
  switch (cmd_state){
  case 1:
   command += rate;
   if(command >= 25.0){
    command = 25.0;
    cmd_state += 1;
    }
    cmd_arr[channel] = command;
  break;
  case 2:
   command -= rate;
   if(command <= -25.0){
    command = -25.0;
    cmd_state += 1;
    }
    cmd_arr[channel] = command;
  break;
  case 3:
   command += rate;
   cmd_arr[channel] = command;
   if(command >= 0.0){
    command = 0.0;
    cmd_state = 1;
    cmd_arr[channel] = command;
    if(channel >= 5){
      channel = 0;
      }
    else{
      channel  += 1;  
      }    
    }  
  break;
  default:
  break;
  }

  //unit conversions and saturations

  //x axis between convert from +/-25mm to inches
  cmd_arr[0] = cmd_arr[0] / 25.4; 

  //y axis between convert from +/-25mm to inches
  cmd_arr[1] = cmd_arr[1] / 25.4;

  //limit z axis with -13.0 mm from below.
  if(cmd_arr[2] < -13.0){   
    cmd_arr[2] = -13.0;
    }

  //z axis between convert from +25 -15mm to inches
  cmd_arr[2] = cmd_arr[2] / 25.4;

  //Roll axis +/- 15degree to radians
  cmd_arr[3] = radians((cmd_arr[3] * 15.0) / 25.0);

  //Pitch axis +/- 15degree to radians
  cmd_arr[4] = radians((cmd_arr[4] * 15.0) / 25.0);

  //Yaw axis +/- 25degree to radians
  cmd_arr[5] = radians(cmd_arr[5]);


  Serial.print("arr0: ");
  Serial.print(cmd_arr[0], 4);
  Serial.print("\tarr1: ");
  Serial.print(cmd_arr[1], 4);
  Serial.print("\tarr2: ");
  Serial.print(cmd_arr[2], 4);
  Serial.print("\tarr3: ");
  Serial.print(cmd_arr[3], 4);
  Serial.print("\tarr4: ");
  Serial.print(cmd_arr[4], 4);
  Serial.print("\tarr5: ");
  Serial.print(cmd_arr[5], 4);  
  Serial.println("");

  
  error_count = setPos(cmd_arr);
  
  Serial.print("Error Count: ");
  Serial.print(error_count);
  Serial.println("");
  Serial.print("New servo positions: ");
  for(int j=0 ; j<6; j++){
    Serial.print("Servo ");
    Serial.print(j);
    Serial.print("= ");
    Serial.print(servo_pos[j]);
    Serial.print("\t");            
  }
  
  delay(10);


  

//helping subroutine to print current position
#if LCD
   if(showPos==PRINTPOS){
      static byte act=0;
      showComm();
      if(millis()-time<1500){
         act=0;
      }else if(millis()-time<3000){
         if(act==0){
            act=1;
            clearNr();
            showRot();
         }
      }else{
         time=millis();
         clearNr();
         showLoc();
      }
   }
#endif
#if IMU   
/* Get a new sensor event */ 
  sensors_event_t event; 
  bno.getEvent_Orientation(&event);
  
  
  Serial.print("X: ");
  Serial.print(event.orientation.x, 4);
  Serial.print("\tY: ");
  Serial.print(event.orientation.y, 4);
  Serial.print("\tZ: ");
  Serial.print(event.orientation.z, 4);
  Serial.println("");

  arr[0] = 0;
  arr[1] = 0;
  arr[2] = 0;
  arr[3] = radians(event.orientation.y);
  arr[4] = radians(event.orientation.z);
  arr[5] = 0;
  error_count = setPos(arr);
  Serial.print("Error Count: ");
  Serial.print(error_count);
  Serial.println("");
  Serial.print("New servo positions: ");
  for(int j=0 ; j<6; j++){
    Serial.print("Servo " + j);
    Serial.print("= ");
    Serial.print(servo_pos[j]);
    Serial.print("\t");            
  }
  delay(10);
#endif
}
