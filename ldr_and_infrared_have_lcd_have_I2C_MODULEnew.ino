#include <Wire.h> 

#include "DFRobot_RGBLCD1602.h"
DFRobot_RGBLCD1602 lcd(/*lcdCols*/20,/*lcdRows*/4); 
int ldr=A0;
int relay=2;
int ir=3;
int led=1;
int threshold=500;
void setup(){
  pinMode(relay,OUTPUT);
  Serial.begin(9600);
  lcd.init();
  
  lcd.setCursor(0,0);
  lcd.print("HOME SYSTEM");
  delay(1000);
  }
  void loop(){
    int ldrvalue=analogRead(ldr);
    int irvalue=digitalRead(ir);
      lcd.setCursor(0,1);
  lcd.print("                                            ");
    if(ldrvalue>threshold){
      digitalWrite(relay,LOW);
        lcd.setCursor(0,0);
  lcd.print(" relay is off: ");

    lcd.print(ldrvalue);
    }
    else{
       digitalWrite(relay,HIGH);
        lcd.setCursor(0,0);
  lcd.print(" relay is on: ");
 
    lcd.print(ldrvalue);
      }
      if(irvalue==HIGH){
         digitalWrite(led,HIGH);
        lcd.setCursor(0,1);
  lcd.print("  IR IS DETECT:");

    lcd.print(irvalue);
        }
        else{
              digitalWrite(led,LOW);
        lcd.setCursor(0,1);
  lcd.print("  IR IS NOT DETECT:");
   
    lcd.print(irvalue);
          }
          delay(2000);
    }
  
