 /* COMPAÑÍA: IOT ROBOTIX
 * NOMBRE: CÓDIGO DE EJEMPO PARA EVAL BOARD DEVKIT V2.0
 * DESARROLLADO POR: DIEGO ALAN PARDO
 * SITIO: https://www.iot-robotix.mx/
 * DESCRIPCIÓN: Este código de ejemplo permite hacer uso de los periféricos existentes sobre la tarjeta DEVKIT V2.0:
 *  - LED de usuario
 *  - Botón de usuario
 *  - Radio SIGFOX SubGHz
 *  - Sensor de temperatura analógico
 *  - LED Indicadores de actividad RX/TX
 * CARACTERÍSTICAS DE LA TARJETA:
 *  - Alimentación por puerto USB
 *  - Alimentación externa de 7-12 VDC mediante Jack / Terminales de tornillo
 *  - Compatibilidad para programarse como ARDUINO UNO (bootloader ARDUINO UNO Atmega328P)
 *  - Pinout de ARDUINO UNO disponible a través de housings
 *  - Reguladores de tensión 3V3 y 5V0 incorporados
 */

/* @ LIBRERÍAS USADAS
 * 
 */
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* @ DECLARACIÓN DE VARIABLES GLOBALES
 * 
 */
volatile uint16_t SFX_Temperatura = 0;
volatile uint16_t SFX_Voltajes    = 0;
bool Espera_downlink  = false;
bool Flag_Trigger_downlink = false;   
bool Flag_Esperando_Downlink = false; // BANDETA QUE SE MANTIENE ACTIVA MIENTRAS SE ESPERA UN DOWNLINK
char RespuestaSigfox[100];
char buffer_reply[255];
uint8_t indx = 0;
volatile unsigned int   timerSigfox     = 0;
volatile uint32_t       MiliSegundosSF  = 0;
volatile uint32_t       MilisAnterior   = 0;
volatile uint32_t       T_ms_Entre_Publicaciones = 0;
volatile unsigned long  x = 0;
volatile uint8_t        Registro_8b_estados  = 0;
/*float temperatura = 0;*/

/* @ CONSTANTES PARA PINOUT DE MCU
 * 
 */
const uint8_t   SDW_SF       = 3;         // PIN SHUTDOWN DEL MÓDULO SFX
const uint8_t   TX_SFX       = 5;         // PIN TX DEL SOFTWARE SERIAL PARA WISOL
const uint8_t   RX_SFX       = 4;         // PIN RX DEL SOFTWARE SERIAL PARA WISOL
const uint8_t   USER_BUTTON  = 2;         // ENTRADA PARA EL BOTÓN DE USUARIO
const uint8_t   LED_USUARIO  =  LED_BUILTIN;
/*const int       VTEMP_PIN    = A5;*/

/* @ MACROS DE CONTROL PARA EL COMPILADOR
 * - AL COMENTAR ESTE #DEFINE SE MANTIENE EL WISOL EN CONFIGURACIÓN PARA ZONA 2 = 902.2 MHZ
 * - AL DEJAR SIN COMENTAR EL #DEFINE SE CONFIGURA EL WISOL PARA ZONA 4 = 920.8 MHZ
 */ 
// #define ZONA4          // INDICA AL PREPROCESADOR QUE DEBE COMPILAR O NO LA CONFIGRUACIÓN PARA ZONA 4 DE SIGFOX

/* @ OBJETO SERIAL POR SOFTWARE
 * SE REQUIERE PARA COMUNICACIÓN CON EL MÓDULO WISOL Y DEBE USARSE A 9600BPS, YA QUE ES LA VELOCIDAD POR DEFECTO DEL WISOL
 */
  SoftwareSerial UART_SFX(RX_SFX, TX_SFX); // SFW UART PARA DEBUG - RX, TX

/* @ SETUP
 * CONFIGURACIÓN DE PINOUT DE MCU E INICIALIZACIÓN
 */
void setup() {
  pinMode(TX_SFX,OUTPUT);
  pinMode(RX_SFX,INPUT);
  pinMode(USER_BUTTON,INPUT);
  pinMode(SDW_SF,OUTPUT);
  pinMode(LED_USUARIO,OUTPUT);
  
  Serial.begin(9600);     // INICIALIZACIÓN DE HDW UART PARA COMUNICACIÓN EXTERNA
  UART_SFX.begin(9600);   // INICIALIZACIÓN DE SFW UART PARA COMUNICACIÓN CON WISOL

  Serial.println(F("PRACTICA 1 - OBTENER ID, PAC Y ENVIAR MENSAJE A SIGFOX"));
  digitalWrite(SDW_SF,0);         // LINEA QUE MANTIENE EL WISOL APAGADO (0-APAGADO, 1-ENCENDIDO)
  digitalWrite(LED_USUARIO,1);    // LINEA QUE ENCIENDE O APAGA EL LED DE USUARIO (1-APAGADO, 0-ENCENDIDO)
  
  Solicita_PAC_ID_SF();
}
/* @ MAIN LOOP
 * - SE LEE EL STATUS DEL BOÓN DE USUARIO Y SI ES PRESIONADO SE TRANSMITE LA TEMPERATURA Y VOLTAJE REGISTRADO POR EL MÓDULO WISOL
 *   A TRAVÉS DE LA RED SIGFOX
 * - EN CASO DE RECIBIRSE UN DOWNLINK, SE OBTIENE EL PAYLOAD, SE MUESTRA POR EL PUERTO USB  Y SE ALMACENA EN UN BUFFER
 */
void loop() {

  if (!digitalRead(USER_BUTTON))
  {
      digitalWrite(LED_USUARIO,0);
      
      encenderSigfox();
      //SendATCommnadSF("AT$T?", " ", 2000);    // SE SOLICITA TEMPERATURA DEL EQUIPO
      //SendATCommnadSF("AT$V?", " ", 2000);    // SE SOLICITAN VOLTAJES EN EL DISPOSITIVO
      Publica_Payload(0);
      if(!Espera_downlink){
        apagarSigfox(); 
      }
      digitalWrite(LED_USUARIO,1);
  }
  
  if ((UART_SFX.available() != 0)&&(Espera_downlink))
  {
      indx = 0;
      while (UART_SFX.available() != 0)
      {
          buffer_reply[indx] = UART_SFX.read();
          indx++;
          delay(5);
      }
      Serial.println(F("\r\n ***** MENSAJE DOWNLINK SFX RECIBIDO *****"));
      Serial.println(buffer_reply);
      Espera_downlink = 0;
      indx = 0;
      apagarSigfox();
  }
  
}
/* @ RESET
 * EN CASO DE ERROR, SE REINICA EL MCU AL LLAMAR A ESTA FUNCIÓN
 */
void (*resetFunc)(void) = 0; //declare reset function at address 0

/* @ ENVIAR COMANDOS AT A WISOL
 * SE ENVÍAN COMANDOS AR AL MÓDULO WISOL Y SE ALMACENA LA RESPUESTA EN UN BUFFER
 * ARGUMENTOS:
 *   - *ATCommand - COMANDO AT A ENVIAR AL MÓDULO
 *   - *Response - RESPUESTA ESPERADA DEL MÓDULO
 *   - MilisTimeout - TIEMPO DE ESPERA MÁXIMO PARA RECIBIR RESPUESTA, EN MILISEGUNDOS
 * LA FUNCIÓN RETORNA UN VALOR DISCRTO (BOOL) EN 1 CUANDO SE RECIBIÓ UNA RESPUESTA CORRECTA Y 0 EN CASO DE ALGUN PROBLEMA
 */
bool SendATCommnadSF(char*ATCommand, char*Response, uint32_t MilisTimeout) {
  bool ok = 0;
  x = 0;
  uint32_t MS_Transcurridos = 0;
  uint8_t i = 0;
  
  memset(RespuestaSigfox, '\0', sizeof(RespuestaSigfox));
  Serial.print(F("\n\rMCU to SFX----->>"));
  Serial.println(ATCommand);
  timerSigfox = 0;
  MiliSegundosSF = 0; 

  while (UART_SFX.available()!= 0){
    UART_SFX.read();
  }
   
  UART_SFX.print(ATCommand);
  UART_SFX.print("\n");

  MilisAnterior = millis();

  while (MS_Transcurridos <= MilisTimeout){
    MS_Transcurridos = millis() - MilisAnterior;
    if (UART_SFX.available()!= 0){
        while (UART_SFX.available()!= 0){
            RespuestaSigfox[i] = UART_SFX.read();
            i++;
            delay(15);
            }
        MS_Transcurridos = 50000;
    }
  }

  if (strstr(RespuestaSigfox, Response) != NULL) {
    ok = 1;
    Serial.print(F("\n\r<<-----SFX to MCU\r\n"));
    Serial.println(RespuestaSigfox);
  }
  else if (strstr(RespuestaSigfox, "parse error") != NULL) {
    Serial.print(F("\n\r<<-----SFX to MCU PARSE ERROR \r\n"));
    Serial.println(RespuestaSigfox);
    ok = 0;
  }
  else if (strstr(RespuestaSigfox, "_SFX") != NULL) {
    Serial.print(F("\n\r<<-----SFX to MCU ERROR \r\n"));
    Serial.println(RespuestaSigfox);
    ok = 0;
  } 
  else;
 
  if (!ok){
    //Serial.print(F("\r\nRespuesta inesperada \r\n"));
    Serial.println(RespuestaSigfox);
    }
  
  return ok;
}
/* @ APAGAR WISOL
 * SE MANTIENE EL WISOL APAGADO
 */
void apagarSigfox() {
  while (!SendATCommnadSF("AT", "OK", 1000));
  digitalWrite(SDW_SF,0); 
  }
/* @ ENCENDER WISOL
 * SE ENCIENDE MÓDULO WISOL, SE ENVÍA UN COMANDO DUMMIE AT Y SE CONFIGURA EN ZONA 2 O 4 SEGÚN SE REQUIERA
 */
void encenderSigfox() {
  uint8_t Cont = 0;
  digitalWrite(SDW_SF,1);
  delay(3000);
  while (!SendATCommnadSF("AT", "OK", 2000) && Cont < 6) {
    Cont++;
    Serial.print(F("\r\nIntento numero:\r\n"));
    Serial.println(Cont);
    if (Cont >= 6);
      resetFunc();
  }
  #ifdef ZONA4
    Serial.print(F("\r\nConfigurando WISOL en zona 4 (Freq. 920.8 MHz)\r\n"));
    SendATCommnadSF("AT$IF=920800000", "OK", 3);
    SendATCommnadSF("AT$DR=922300000", "OK", 3);
  #endif 
  #ifndef ZONA4
    Serial.print(F("\r\nConfigurando WISOL en zona 2 (Freq. 902.2 MHz)\r\n"));
  #endif 
}
/* @ SOLICITAR PAC E ID
 * SE SOLICITA LA INFORMACIÓN DE IDENTIFICACIÓN DEL MÓDULO, TEMEPRATURA INTERNA Y VOLTAJES 
 */
void Solicita_PAC_ID_SF() {
  encenderSigfox();
  Serial.println("PETICION DE ID Y PAC. ");
  SendATCommnadSF("AT$I=10", " ", 2000);  // SE SOLICITA ID DEL MÓDULO
  SendATCommnadSF("AT$I=11", " ", 2000);  // SE SOLICITA PAC DEL DISPOSITIVO
  apagarSigfox(); 
}
/* @ TRANSMISIÓN DE DATOS
 * SE TRANSMITE UN PAYLOAD DE EJEMPLO, CON EL VOLTAJE Y TEMPERATURA DEL WISOL,ARGUMENTOS:
 *  - TIPO: 1 - SE ESPERA DOWNLINK, 0 - NO SE ESPERA DOWNLINK
 */
void Publica_Payload(bool tipo) {
  char Envio[60] = "\0";  
  uint8_t Cont = 0;
  
  Serial.print(F("\r\nEnvio Sigfox\r\n"));
  while (!SendATCommnadSF("AT$RC", "OK", 2000) && Cont < 6) {
    Cont++;
    Serial.print(F("\r\nIntento numero:\r\n"));
    Serial.println(Cont); 
    if (Cont >= 5){
      resetFunc();
      ;
      }
  }
  
  memset( Envio, '\0', sizeof(Envio));

  if (tipo == 1) {
    Flag_Esperando_Downlink = true;
    sprintf(Envio, "AT$SF=FFFFFF");
    SendATCommnadSF(Envio, "OK", 5000); 
    Espera_downlink = true;
    Serial.print(F("\r\n******* En espera de recibir downlink, Flag_Esperando_Downlink = 1 *******\r\n"));
  }
  else {
    sprintf(Envio, "AT$SF=FFFFFF");
    SendATCommnadSF(Envio, "OK",5000); 
    Espera_downlink = false;
    Serial.print(F("\r\n******* No se espera recibir downlink *******\r\n"));
  }
}
