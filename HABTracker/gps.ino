// This is copied from David Akerman's FlexTrack - https://github.com/daveake/FlexTrack
// Annoyingly, I couldn't get FlexTrack to work, so for now just borrow its GPS code

/* ========================================================================== */
/*   gps.ino                                                                  */
/*                                                                            */
/*   Serial and i2c code for ublox on AVR                                     */
/*                                                                            */
/*                                                                            */
/*                                                                            */
/* ========================================================================== */

// Include i2c library, if using it

#ifdef GPS_I2C
//  #include <Wire.h>
  #include <I2C.h>
#endif

// Globals
byte RequiredFlightMode=0;
byte GlonassMode=0;
byte RequiredPowerMode=-1;
byte LastCommand1=0;
byte LastCommand2=0;
byte HaveHadALock=0;

char Hex(char Character)
{
  char HexTable[] = "0123456789ABCDEF";
	
  return HexTable[Character];
}

void FixUBXChecksum(unsigned char *Message, int Length)
{ 
  int i;
  unsigned char CK_A, CK_B;
  
  CK_A = 0;
  CK_B = 0;

  for (i=2; i<(Length-2); i++)
  {
    CK_A = CK_A + Message[i];
    CK_B = CK_B + CK_A;
  }
  
  Message[Length-2] = CK_A;
  Message[Length-1] = CK_B;
}

void SendUBX(unsigned char *Message, int Length)
{
  LastCommand1 = Message[2];
  LastCommand2 = Message[3];
  
#ifdef GPS_I2C  
  I2c.write(0x42, 0, Message, Length);
#else
  int i;
  
  for (i=0; i<Length; i++)
  {
    Serial.write(Message[i]);
  }
#endif
}


void DisableNMEAProtocol(unsigned char Protocol)
{
  unsigned char Disable[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
  
  Disable[7] = Protocol;
  
  FixUBXChecksum(Disable, sizeof(Disable));
  
  SendUBX(Disable, sizeof(Disable));
  
  Serial.print("Disable NMEA "); Serial.println(Protocol);
}

void SetFlightMode(byte NewMode)
{
  // Send navigation configuration command
  unsigned char setNav[] = {0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC};

  setNav[8] = NewMode;

  FixUBXChecksum(setNav, sizeof(setNav));
  
  SendUBX(setNav, sizeof(setNav));
}

    
#ifdef POWERSAVING
void SetGNSSMode(void)
 {
  // Sets CFG-GNSS to disable everything other than GPS GNSS
  // solution. Failure to do this means GPS power saving 
  // doesn't work. Not needed for MAX7, needed for MAX8's
  
  uint8_t setGNSS[] = {
    0xB5, 0x62, 0x06, 0x3E, 0x2C, 0x00, 0x00, 0x00,
    0x20, 0x05, 0x00, 0x08, 0x10, 0x00, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x03, 0x08, 0x10, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x06, 0x08, 0x0E, 0x00, 0x00, 0x00,
    0x01, 0x01, 0xFC, 0x11};
    SendUBX(setGNSS, sizeof(setGNSS));
} 
#endif




#ifdef POWERSAVING
void SetPowerMode(byte SavePower)
{
  uint8_t setPSM[] = {0xB5, 0x62, 0x06, 0x11, 0x02, 0x00, 0x08, 0x01, 0x22, 0x92 };
  
  setPSM[7] = SavePower ? 1 : 0;
  
  FixUBXChecksum(setPSM, sizeof(setPSM));
  
  SendUBX(setPSM, sizeof(setPSM));
}
#endif

void ProcessUBX_ACK(unsigned char *Buffer, int Length)
{
  if ((LastCommand1 == 0x06) && (LastCommand2 == 0x24))
  {
    GPS.FlightMode = RequiredFlightMode;
  }
  else if ((LastCommand1 == 0x06) && (LastCommand2 == 0x3E))
  {
    GlonassMode = 1;
  }
  else if ((LastCommand1 == 0x06) && (LastCommand2 == 0x11))
  {
    GPS.PowerMode = RequiredPowerMode;
  }
//  else
//  {
//    Serial.print("ACK ("); Serial.print(LastCommand1); Serial.print(","); Serial.print(LastCommand2); Serial.println(")");
//  }
  LastCommand1 = 0;
  LastCommand2 = 0;
}

void ProcessUBX_NAV_SOL(unsigned char *Buffer, int Length)
{
  if (Buffer[17] & 1)
  {
    GPS.Lock = Buffer[16];    /// Type of fix: 0=none, 1=deadreckoning; 2=2D, 3=3D, 4=GPS+deadreckoning, 5=time only, 6+ reserved
    if (GPS.Lock >= 3)
    {
      HaveHadALock = 1;
    }
  }
  else
  {
    GPS.Lock = 0;
  }

  GPS.Satellites = Buffer[53];
  
//  Serial.print("Lock="); Serial.print(GPS.Lock);Serial.print(", Sats=");Serial.println(GPS.Satellites);
}

void ProcessUBX_NAV_POSLLH(unsigned char *Buffer, int Length)
{
  struct TUBlox
  {
    unsigned long Time;
    long Longitude;
    long Latitude;
    long HeightEllipsoid;
    long HeightSeaLevel;
    unsigned long HAccuracy;
    unsigned long VAccuracy;
  } *UBlox;
    
  UBlox = (struct TUBlox*)(Buffer+6);

  GPS.SecondsInDay = (UBlox->Time / 1000) % 86400;      // Time of day in seconds = Time in week in ms / 1000, mod 86400
  GPS.Hours = GPS.SecondsInDay / 3600;
  GPS.Minutes = (GPS.SecondsInDay / 60) % 60;
  GPS.Seconds = GPS.SecondsInDay % 60;  
  if (HaveHadALock)
  {
    GPS.Longitude = (float)(UBlox->Longitude) / 10000000;
    GPS.Latitude = (float)(UBlox->Latitude) / 10000000;
    GPS.Altitude = UBlox->HeightSeaLevel / 1000;
  }

//  Serial.print("GPS: ");
//  Serial.print(GPS.Hours); Serial.print(":"); Serial.print(GPS.Minutes); Serial.print(":"); Serial.print(GPS.Seconds);Serial.print(" - ");
//  Serial.print(GPS.Latitude, 6); Serial.print(',');Serial.print(GPS.Longitude, 6);Serial.print(',');Serial.print(GPS.Altitude);Serial.print(',');
//  Serial.println(GPS.Satellites);
}


void ProcessUBX(unsigned char *Buffer, int Length)
{
//  Serial.print("UBX "); Serial.print(Buffer[2], HEX);
//  Serial.print(" "); Serial.println(Buffer[3], HEX);
  
  if ((Buffer[2] == 0x05) && (Buffer[3] == 0x01))
  {
    ProcessUBX_ACK(Buffer, Length);
  }
  else if ((Buffer[2] == 1) && (Buffer[3] == 6))
  {
    ProcessUBX_NAV_SOL(Buffer, Length);
  }
  else if ((Buffer[2] == 1) && (Buffer[3] == 2))
  {
    ProcessUBX_NAV_POSLLH(Buffer, Length);
  }
}

void ProcessNMEA(unsigned char *Buffer, int Count)
{
    if (strncmp((char *)Buffer+3, "GGA", 3) == 0)
    {
      DisableNMEAProtocol(0);      
    }
    else if (strncmp((char *)Buffer+3, "RMC", 3) == 0)
    {
      DisableNMEAProtocol(4);
    }
    else if (strncmp((char *)Buffer+3, "GSV", 3) == 0)
    {
      DisableNMEAProtocol(3);
    }
    else if (strncmp((char *)Buffer+3, "GLL", 3) == 0)
    {
      DisableNMEAProtocol(1);
    }
    else if (strncmp((char *)Buffer+3, "GSA", 3) == 0)
    {
      DisableNMEAProtocol(2);
    }
    else if (strncmp((char *)Buffer+3, "VTG", 3) == 0)
    {
      DisableNMEAProtocol(5);
    }
}


void SetupGPS(void)
{

  Serial.println(F("SetupGPS"));
  // Switch GPS on, if we have control of that
#ifdef GPS_ON
  pinMode(GPS_ON, OUTPUT);
  digitalWrite(GPS_ON, 1);
#endif

  // Init I2C library if we're using it  
#ifdef GPS_I2C
  // Init i2c library
  // Wire.begin();
  I2c.begin();
#endif
}

int GPSAvailable(void)
{
  int FD, FE, Bytes;
  
#ifdef GPS_I2C
  // return Wire.available();
  // return I2c.available();
  I2c.read(0x42, 0xFD, 2);    // Set up read from registers FD/FE - number of bytes available
  FD = I2c.receive();
  FE = I2c.receive();
  
  Bytes = FD * 256 + FE;

//  if (Bytes > 0)
//  {
//    Serial.println(""); Serial.println(Bytes);
//  }

  if (Bytes > 32)
  {
    Bytes = 32;
  }
  
  if (Bytes > 0)
  {
    I2c.read(0x42, 0xFF, Bytes);    // request 32 bytes from slave device 0x42 (Ublox default)
  }

  return Bytes;
#else  
  return Serial.available();
#endif
}

char ReadGPS(void)
{
#ifdef GPS_I2C
  // return Wire.read();        
  return I2c.receive();        
#else
  return Serial.read();
#endif
}

void PollGPSTime(void)
{
  uint8_t request[] = {0xB5, 0x62, 0x01, 0x21, 0x00, 0x00, 0x22, 0x67};
  SendUBX(request, sizeof(request));
}

void PollGPSLock(void)
{
  uint8_t request[] = {0xB5, 0x62, 0x01, 0x06, 0x00, 0x00, 0x07, 0x16};
  SendUBX(request, sizeof(request));
}

void PollGPSPosition(void)
{
  uint8_t request[] = {0xB5, 0x62, 0x01, 0x02, 0x00, 0x00, 0x03, 0x0A};
  SendUBX(request, sizeof(request));
}
  
void CheckGPS(void)
{
  static unsigned long PollTime=0;
  static unsigned char Line[80];
  static int Length=0;
  static int UBXLength=0;
  static unsigned int PollMode=0;
  unsigned char Character, Bytes, i;
  
  if (millis() >= PollTime)
  {
    if (PollTime == 0)
    {
      PollTime = millis();
    }
    else
    {
      PollTime += 200;
    }
    
    Length = 0;
    
    switch (PollMode)
    {
      case 0:
        // Poll for lock/sats
        PollGPSLock();
      break;
        
      case 1:
        // Poll for position
        PollGPSPosition();
      break;
      
      case 2:

        // Flight/ped mode
        
        // TODO: Ant - I do not know what this is for, why not always use FlightMode?
        //    for now I'm going to change it to always want flight mode.
      
//        RequiredFlightMode = (GPS.Altitude > 1000) ? 6 : 3;    // 6 is airborne <1g mode; 3=Pedestrian mode
        RequiredFlightMode = 6;    
        if (RequiredFlightMode != GPS.FlightMode)
        {
          SetFlightMode(RequiredFlightMode);
          Serial.println("Setting flight mode\n");
        }
      break;
      
      case 3:
        // Power saving
        #ifdef POWERSAVING
          if (!GlonassMode)
          {
            Serial.println("*** SetGNSSMode() ***");
            SetGNSSMode();
          }
          else
          {           
            // All the following need to be true for us to try power saving mode
            RequiredPowerMode = (GPS.Lock==3) && (GPS.Satellites>=5);
            
            if (RequiredPowerMode != GPS.PowerMode)
            {
              Serial.print("*** SetPowerMode("); Serial.print(RequiredPowerMode); Serial.println(") ***");
              SetPowerMode(RequiredPowerMode);
            }
          }
        #endif
      break;
    }
    
    if (++PollMode >= 5)
    {
      PollMode = 0;
    }
    // delay(100);
  }

  do
  {
    Bytes = GPSAvailable();
  
    for (i=0; i<Bytes; i++)
    { 
      Character = ReadGPS();
    
      if ((Character == 0xB5) && (Length <= 4))
      {
        Line[0] = Character;
        Length = 1;
      }
      else if ((Character == 0x62) && (Length <= 4))
      {
        if (Length == 0)
        {
          Serial.print("MISSED B5 ");
          Line[0] = 0xB5;
        }
        Line[1] = Character;
        Length = 2;
      }
      else if ((Character == '$') && (Length == 0))
      {
        Line[0] = Character;
        Length = 1;
      }
      else if (Length >= (sizeof(Line)-2))
      {
        Length = 0;
      }
      else if (Length > 0)
      {
        if (Line[0] == 0xB5)
        {
          // UBX
          Line[Length++] = Character;
          if (Length == 6)
          {
            // Got UBX Length
            UBXLength = (int)Line[4] + (int)(Line[5]) * 256;
          }
          else if ((Length > 6) && (Length >= (UBXLength+8)))
          {
            ProcessUBX(Line, Length);
            
            LastCommand1 = Line[2];
            LastCommand2 = Line[3];
            
            Length = 0;
          }
        }
        else if (Line[0] == '$')
        {
          if (Character == '$')
          {
            Length = 1;
          }
          else if (Character != '\r')
          {
            Line[Length++] = Character;
            if (Character == '\n')
            {
              Line[Length] = '\0';
              ProcessNMEA(Line, Length);
              Length = 0;
            }
          }
        }
      }
    }
  } while (Bytes > 0);
}

