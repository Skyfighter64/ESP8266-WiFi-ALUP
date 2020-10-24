#include <FastLED.h>
#include <ESP8266WiFi.h>








/*
 * TODOS:
 * Make sure to remove wifi credentials before pushing
 * 
 * Test if FrameError byte gets sent when a frame error occurs
 * Create test programs to test transmission of ints, strings, etc.
 * 
 */




/*
 *  protocol constants 
 *  
 *  Those values should not be changed
 *
 */

#define PROTOCOL_VERSION "0.1 (internal)"

#define CONNECTION_REQUEST_BYTE 255
#define CONNECTION_ACKNOWLEDGEMENT_BYTE 254
#define CONFIGURATION_START_BYTE 253
#define CONFIGURATION_ACKNOWLEDGEMENT_BYTE 252
#define CONFIGURATION_ERROR_BYTE 251
#define FRAME_ACKNOWLEDGEMENT_BYTE 250
#define FRAME_ERROR_BYTE 249

#define SUBCOMMAND_OFFSET 8



/*
 * Toggle for debug mode
 * 
 * Comment to deactivate debug LEDs, uncomment to activate
 * 
 * The debug mode outputs messages over the serial connection
 * to help for following the protocol flow of this device and debug the program
 */
 
#define debug



/*
 *  Configuration
 * 
 *  The folowing values are for configuring this sketch.
 *  Set them depending on your usecase. For more information
 *  on each value, see the documentation on GitHub
 * 
 */


/*
 *  Set the Pin order which should be used for the digital pin numbers. 
 *  Uncomment a setting to set it. If nothing is uncommented, the FastLED library will select
 *  one automatically based on your board definitions. 
 *  
 *  For more, see: https://github.com/FastLED/FastLED/wiki/ESP8266-notes
 */
 
//#define FASTLED_ESP8266_RAW_PIN_ORDER
//#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
//#define FASTLED_ESP8266_D1_PIN_ORDER

/*
 * the values for the connected LED Strip
 * Change them depending on your configuration
 */
#define NUM_LEDS 10
#define DATA_PIN 5
#define CLOCK_PIN 1


/*
 * the delay for each frame in ms
 * This delay depends on the type of LEDs and its maximum refresh rate
 * 
 * WS28121b:
 *  This type of LEDs have a maximum refresh rate of 400hz which translates to at least 2.5ms needed delay between each frame
 *  Therefore, the recommended value for this type is 3
 *  
 *  
 *  TOD: improvement idea: measure execution time for each frame and subtract it from the delay
 */
#define FRAME_DELAY 5

/*
 * the baud rate for serial communication (only used for debugging)
 * make sure to set the same value to both devices
 */
#define BAUD 115200


/*
 *  The SSID and password of the Wifi network to which the ESP8266 should connect
 * 
 */
#define STASSID "test"
#define STAPSK  "12345678"


/*
 * the port number at which the server will listen for connections
 */
#define SERVER_PORT 1201

/*
 * the device name 
 * set it to anything you like
 */
#define DEVICE_NAME "ESP8266ALUP"

/*
 * extra configuration values
 * Not required for normal use, unless stated otherwise in the configuration guide
 */
#define EXTRA_VALUES ""




/*
 * enumerator with all connection states
 */
enum ConnectionState
{
  Connecting,
  Connected,
  Disconnected
};

//the current connection state
ConnectionState state;


// Define the array of leds
CRGB leds[NUM_LEDS];


//open a TCP server at the given port
WiFiServer server(SERVER_PORT);

//the client currently connected to the Server
WiFiClient client;

//the time of the last frame applied since the start of the program
//used to calculate the remaining frame delay
unsigned long last_frame_time = 0;



void setup() {

  //set up the LEDs
  SetupLEDs();
#ifdef debug
  //setup for debugging
  //open the serial connection for debugging
  Serial.begin(BAUD);


 Serial.println("Starting...");
#endif


  //connect to the WiFi network
  ConnectWifi();
  
  //start the TCP server 
  server.begin();

}

void loop() 
{
  state = Disconnected;
  /*
   * wait until a client connects to the TCP Server 
   */
  do
  {
    client = server.available();
  }
  while(!client);

  /*
   *  A client connected to the TCP Server
   */
    #ifdef debug
    Serial.print("Client connected: ");
    Serial.println(client.remoteIP());
    #endif
    
    /*
     * enable keepalive packages for the TCP connection
     */
    client.keepAlive(5,1,1);

    /*
     * Send connection requests to the TCP Client (this function blocks until the connection request gets accepted)
     */
    RequestConnection();
    
    state = Connecting;
     
    /*
     * The TCP Client accepted the connection request
     * 
     * Establish an ALUP connection
     */
    int result = Connect();
    
    if(result == 0)
    {
      /*
       * The ALUP connection was established successfully
       */
      #ifdef debug  
      Serial.println("Client connected successfully");
      #endif  
  
      state = Connected;
  
    }
    else if(result == -1)
    {
      /*
       * the connection could not be established (most likely because of a configuration error)
       */
      #ifdef debug  
      Serial.println("Could not connect to client");
      #endif  
    }

    
    /*
     * Receive data frames while the client is connected
     */
    while(state == Connected)
    {
      #ifdef debug        
      Serial.println("Started reading frame...");
      //measure the time it takes to receive the frame
      unsigned long startTime = micros();
      #endif 
      
      byte commandByte;

      /*
       * read in the data frame
       */
      if(ReceiveFrame(&commandByte) == -1)
      {
        /*
         * An error occured while receiving the frame body
         * 
         * Send a Frame error back and abort receiving the frame
         */
        SendFrameErrorByte();

        
        #ifdef debug  
        Serial.println("an error occured while receiving the frame body");
        #endif  
      }
      else
      {
        /*
         * The frame was received successfully
         * 
         * Apply it to the LEDs and execute the suncommand
         */      
       
        #ifdef debug  
        Serial.print("Recieved a frame in:");
        Serial.print(((micros() - startTime)/1000));
        Serial.println("ms\n"); 
        #endif 
        
        //execute the command received in the frame header if needed
        ExecuteCommand(commandByte);
        //show the frame on the LEDs
        ShowFrame();
  
        //send a frame Acknowlegement
        SendFrameAcknowledgementByte();

      }

      /*
       * update the connection state of the client to see if he may be disconnected
       */
      UpdateConnectionState();
      
    }
    
  /*
   *  Stop the connection to the TCP Client
   */
    client.stop();
  
    #ifdef debug  
    Serial.println("Client disconnected");
    #endif

}




/**
 * wrapper function for writing in bytes over the hardware connection
 * @param bytes: an array of bytes with a size of numOfBytes which should get sent over the hardware connection; has to be non-null
 * @param numOfBytes: the number of bytes to write
 */
void writeBytes(byte bytes[], int numOfBytes)
{
  client.write(bytes, numOfBytes);
  #ifdef debug
  
  Serial.print("Sent: ");
  for(int i = 0; i < numOfBytes; i++)
  {
     Serial.print(bytes[i]);
     Serial.print(", ");
  }
  Serial.println();
  #endif
}

/**
 * wrapper function for reading bytes over the hardware connection
 * Note: This function blocks until the specified number of bytes is read
 * @param bytes: an array of bytes with a size of numOfBytes in which the read bytes will get stored
 * @param numOfBytes: the number of bytes to read 
 * @return: the number of bytes read, or -1 if the client disconnected while reading
 */
int readBytes(byte bytes[], int numOfBytes)
{
  #ifdef debug
  //measure reading time
  unsigned long startTime = micros();
  #endif

  
  while(client.available() < numOfBytes)
  {
    //wait until the specified amount of bytes is ready for reading

    //check if the client is still connected
    UpdateConnectionState();
    if(state == Disconnected)
    {
      //the client disconnected
      return -1; 
    }
  }
  int result = client.read(bytes, numOfBytes);


  #ifdef debug
  Serial.print("Read time:");
  Serial.print((micros() - startTime)/1000);
  Serial.println("ms");

  Serial.print("Received: ");
  for(int i = 0; i < numOfBytes; i++)
  {
     Serial.print(bytes[i]);
     Serial.print(", ");
  }
  Serial.println();
  #endif

  return result;
}
 


/**
 * wrapper function for reading bytes over the hardware connection
 * Note: This function blocks until the specified number of bytes is read or the given timeOut is reached
 * @param bytes: an array of bytes with a size of numOfBytes in which the read bytes will get stored
 * @param numOfBytes: the number of bytes to read 
 * @param timeOut: the maximum time to wait for the given number of bytes in ms
 * @return: the number of bytes read, or -1 if the timeOut was reached or the client disconnected while reading
 */
int readBytes(byte bytes[], int numOfBytes, unsigned long timeOut)
{
  //get the current time since start in ms
  unsigned long startTime = millis();

  
  //loop until the requested number of bytes are available or the timeout is reached
  while(client.available() < numOfBytes)
  {
    /*
     * wait until the specified amount of bytes is ready for reading
     */
    
    //check if the timeout was reached
    if(millis() - startTime >= timeOut)
    {
      //timeout reached
      return 0;
    }

    //check if the client is still connected
    UpdateConnectionState();
    if(state == Disconnected)
    {
      //the client disconnected
      return -1; 
    }
    
  }
  return client.read(bytes, numOfBytes);
}

/**
 * wrapper function for getting the number of available bytes from the hardware connection
 * @return: the number of available bytes
 */
int availableBytes()
{
  if(client == NULL)
  {
    #ifdef debug  
    Serial.println("Client is NULL");
    #endif 
    return 0;
  }

  return client.available();
}



/**
 * function executing the given command
 * @param id: the id of the command to execute according to the ALUP v. 0.1
 */
void ExecuteCommand(byte id)
{
  //check if the given id corresponds to a protocol command or a subcommand
  if(id < SUBCOMMAND_OFFSET)
  {
    ExecuteProtocolCommand(id);
  }
  else
  {
    ExecuteSubCommand(id - SUBCOMMAND_OFFSET);
  }
}



/**
 * function executing the protocol command with the given ID according to the ALUP v. 0.1
 */
void ExecuteProtocolCommand(byte id)
{
  switch(id)
  {
    case 1:
      //clear command received
      //do nothing as the clear command is handled within ReceiveBody()
      break;
    case 2:
      //Disconnect command received
      state = Disconnected;
      break;
  }
}



/**
 * function executing the given subcommand
 * @param id: the id of the command with the subcommand offset already applied according to the ALUP v. 0.1
 */
void ExecuteSubCommand(byte id)
{
    //Execute a subprogram depending on the given ID
    switch(id)
    {
      case 0:
        ExampleSubProgram();
        break;

      /*
       * add your own subprograms here by adding a new 'case' with an unused ID ranging from 0-247
       *Example subprogram with an ID of 1:
       *
       *case 1:
       *  MySubProgram();
       *  break;
       */
          
    }
}




/**
 * an example subprogram 
 */
void ExampleSubProgram()
{
  #ifdef debug
  Serial.print("Example subprogram executed");
  #endif
}


/**
 * function setting up the leds for use with the FastLED protocol
 */
void SetupLEDs()
{
  /*
   * The setup for the connected LED strip
   * Uncomment/edit one of the following lines for your type of LEDs
   * 
   * You may also change the frame delay, data and clock pin, and number of LEDs at the top of this file depending on your LEDs
   * 
   * If your LEDs show the wrong colors (e.g. Blue instead of Red, etc), change the "RGB" in your uncommented line 
   * to either RGB, RBG, BRG, BGR, GRB or GBR
   */
      // FastLED.addLeds<TM1803, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<TM1804, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<TM1809, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<WS2811, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<WS2812, DATA_PIN, RGB>(leds, NUM_LEDS);
       FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
      //FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
      // FastLED.addLeds<APA104, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<UCS1903, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<UCS1903B, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<GW6205, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<GW6205_400, DATA_PIN, RGB>(leds, NUM_LEDS);
      
      // FastLED.addLeds<WS2801, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<SM16716, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<LPD8806, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<P9813, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<APA102, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<DOTSTAR, RGB>(leds, NUM_LEDS);

      // FastLED.addLeds<WS2801, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<SM16716, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<LPD8806, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<P9813, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<DOTSTAR, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
}





/**
 * function connecting to the wifi network with the specified credentials
 */
void ConnectWifi()
{
  #ifdef debug
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(STASSID);
  #endif

  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(STASSID, STAPSK);

  #ifdef debug
  while (WiFi.status() != WL_CONNECTED) {   
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  #endif
}


/**
 * function updating the connection state of the client if he disconnected
 */
void UpdateConnectionState()
{
   if(!client.connected())
   {
      state = Disconnected;
   }
}

/**
 * function starting a connection to the master device so LED data can be transmitted 
 * Note: this function blocks until a response is received or a timeout of 5s is reached
 * @return a number indicating the success of this function
 *          One of:
 *             0: The connection could be established 
 *            -1: An error occured while trying to connect
 */
int Connect()
{ 
  //send the configuration to the master device
  SendConfiguration(EXTRA_VALUES);

  //wait for a configuration acknowldgement byte indicating that the configuration was received and applied 
  //successfully by the master device


  //check if a configuration error byte was received or the function timed out while waiting for a configuration acknowledgement
  if(WaitForOneOf(new byte[2] {CONFIGURATION_ACKNOWLEDGEMENT_BYTE, CONFIGURATION_ERROR_BYTE}, 2, 5000) != 0)
  {
    //the configuration could not be received/applied successfully by the master device (Timeout of 5s reached)
    //or a configuration error happened on the master device
    
    #ifdef debug  
    Serial.println("the configuration could not be received/applied successfully by the master device (Timeout of 5s reached)");
    #endif
    
    return -1;
  }

  //the configuration was applied successfully by the master device and a configuration acknowledgement was received
  
  #ifdef debug  
    Serial.println("Configuration appliend successfully");
    #endif
  
  //send a configuration acknowledgement to tell the master device that this device is now ready to receive data 
  SendConfigurationAcknowledgementByte();   
  return 0;
}






/**
 * function waiting to receive a frame over the serial connection
 * Note: this function blocks until a frame is received
 * @param *commandByte: a pointer used to store the command byte when Receiving the 
 * @return: -1 if the frame body size or the body offset is invalid, else 0
 */
int ReceiveFrame(byte *commandByte)
{
      #ifdef debug  
        Serial.println("Starting to receive a frame...");
        #endif 
  
   long bodyOffset;
  
   //get the frame header 
   long bodySize = ReceiveHeader(commandByte, &bodyOffset);

   #ifdef debug  
        Serial.print("Header received: cmd: ");
        Serial.print(*commandByte);
        Serial.print(" bodySize: ");
        Serial.print(bodySize);
        Serial.print(" bodyOffset: ");
        Serial.println(bodyOffset);
        #endif 

   //validate the body size and body offset to receive and apply the frame body
   if(!(ValidateBodySize(bodySize) && ValidateBodyOffset(bodyOffset, bodySize)))
   {
      //the bodySize is invalid or the body offset is invalid or out of range
      DiscardBytes(bodySize);
      
      #ifdef debug
      Serial.println("Invalid frame header received:");
      Serial.print(" BodySize: ");
      Serial.println(bodySize);
      Serial.print(" BodyOffset: ");
      Serial.println(bodyOffset);
      #endif
      
      return -1;
   }

    #ifdef debug  
    Serial.println("Starting to receive the frame body...");
    #endif 

   //receive the frame body and apply it to the led array
   ReceiveBody(bodySize, bodyOffset, *commandByte);
     
    #ifdef debug  
    Serial.println("Received frame body successfully");
    #endif 
   
   return 0;
}



/**
 * function discarding the given amount of bytes from the incoming serial connection
 * @param numOfBytes: the number of bytes to discard; has to be 0 or higher
 */
void DiscardBytes(long numOfBytes)
{
  //check if there are bytes to remove
  if(numOfBytes > 0)
  {
    //discard the faulty frame body
    byte *buff = (byte*) malloc(numOfBytes);
    
    //check if memory could be allocated
    if(buff == NULL)
    {
      //memory could not be allocated; 
      
      #ifdef debug  
      Serial.println("ERROR: Memory could not be allocated");
      #endif
      
      return;
    }

    //read the bytes in from the TCP client
    readBytes(buff, numOfBytes);
    free(buff);

    #ifdef debug  
    Serial.print("Discarted bytes: ");
    Serial.println(numOfBytes);
    #endif
  }
}




/**
 * function showing the frame by applying it to the actual leds using the FastLED library
 */
void ShowFrame()
{
    /*
     * calculate the effective delay depending on the time since the last frame applied
     */
    unsigned long timeSinceLastFrame = (millis() - last_frame_time);
    unsigned long effectiveDelay = max((long) (FRAME_DELAY - timeSinceLastFrame), (long)0);

    #ifdef debug
    Serial.print("Time since last frame: ");
    Serial.print( timeSinceLastFrame);
    Serial.print(", Effectve delay: ");
    Serial.println( effectiveDelay); 
    #endif

    /*
     * delay the frame acknowledgement
     * This has to be done for some types of LEDs like WS2812b as they have a maximum data rate 
     * To change the delay, edit the constant FRAME_DELAY at the top of the script
     */  
     
    delay(effectiveDelay);
    
    
    FastLED.show();
   
    //set the last frame time to the current time in ms
    last_frame_time = millis();
}



/**
 * function validating the given bodySize according to the ALUP v. 0.1
 * @param bodySize: the BodySize to validate
 * @return true if the given bodySize is valid, else false
 */
bool ValidateBodySize(long bodySize)
{
  //check if the given bodySize is a multiple of 3, less or equal than NUM_LEDS * 3 and greater or equal 0
  return (bodySize <= NUM_LEDS * 3) && (bodySize >= 0) && (bodySize % 3 == 0);
}

/**
 * function validating the given bodyOffset according to the ALUP v. 0.1
 * @param offset: the offset value to validate
 * @param bodySize: the size of the frame body; has to be a multiple of 3 and >= 0
 * @return true if the given bodyOffset is valid, else false
 */
bool ValidateBodyOffset(long offset, long bodySize)
{
  //check if the given bodyOffset is >= 0, less or equal than NUM_LEDS - (bodySize / 3)
  return (offset >= 0) && (offset <= NUM_LEDS - (bodySize / 3));
} 


/**
 * function waiting to receive a frame header over the serial connection
 * Note: this function blocks until a frame header is received
 * @param *commandByte: a pointer used to store the command byte when receiving the header
 * @param *offset: a pointer used to store the led offset when receiving the header
 * @return the bodyLength received with the header
 */
long ReceiveHeader(byte *commandByte, long *offset)
{
        //receive the header bytes over the serial connection
        byte headerBytes[9];
        int result = readBytes(headerBytes, 9);

        //check if the client disconnected while reading the data
        if(result == -1)
        {
          /*
           * The client disconnected
           * Close the connection
           */

           //set the protocol command to the "Disconnect" command value
           *commandByte = 2;
           *offset = 0;
           return 0;
        }

        //set the command byte according to the ALUP v. 0.1 (internal)
        *commandByte = headerBytes[8];

        //build the bodyLength from 4 received bytes according to the ALUP v. 0.1 (internal)
        byte bodySizeBytes[] = {headerBytes[0],headerBytes[1], headerBytes[2], headerBytes[3]};

        //build the offset from 4 received bytes according to the ALUP v. 0.1 (internal)
        byte offsetBytes[] = {headerBytes[4],headerBytes[5], headerBytes[6], headerBytes[7]};
        *offset = BytesToLong(offsetBytes);
        
        return BytesToLong(bodySizeBytes);       

}



/**
 * function receiving the frame body with the given size over the serial connection and applying it to the "leds" array
 * @param bodySize: the size of the frame body in bytes; has to be >= 0 and <= NUM_LEDS * 3
 * @param bodyOffset: the offset for the LEDs used when applying the frame body to the LEDs; has to be  >= 0 and <= NUM_LEDS - (bodySize/3)
 * @param commandByte: the command byte received with the frame header; this is needed for the "clear" command
 */
void ReceiveBody(long bodySize, long bodyOffset, byte commandByte)
{
   //check if a clear command was received
  if(commandByte == 1)
  {
   //clear command received
   //clear the current LED array
   FastLED.clear();
  } 
  
  //check if the body size is not 0
  if(bodySize == 0)
  {
    
    //the body size is 0; skip reading it in
    return;
  }
  
  //allocate a buffer to read in the data
  byte *buff = (byte*) malloc(sizeof(byte) * bodySize);

  //check if memory could be allocated
  if(buff == NULL)
  {
    //memory could not be allocated; 

   #ifdef debug  
    Serial.println("ERROR: Memory could not be allocated");
    #endif
    
    return;
  }

 
  //read the bytes from the serial port into the buffer
  int result = readBytes(buff, bodySize);


  //check if the client disconnected while reading the data
  if(result != -1)
  {
    /*
     * The Client did not disconnect while receiving the bytes
     * 
     * Apply the received values
     */

    //loop through all received bytes and apply them to the "leds" array
    for(int i = 0; i < bodySize / 3; i++)
    {
      //apply the buffered data to the LEDs according to the ALUP v. 0.1 (internal)
      leds[i + bodyOffset] = CRGB(buff[i*3], buff[i*3 + 1], buff[i*3 + 2]);
    }
  }
  
  //free the memory used by the buffer
  free(buff);
}





/**
 * function sending connection requests over the serial connection until a connection acknowledgement was received
 * Note: this funciton blocks until a response was received
 */
void RequestConnection()
{
  while(true)
  {
    
    //Send a connection request
    writeBytes(new byte[1] {CONNECTION_REQUEST_BYTE}, 1);

    //wait for a connection acknowledgement
    if(WaitForByte(CONNECTION_ACKNOWLEDGEMENT_BYTE, 250))
    {
      //connection acknowledgement received
      return;
    }

    #ifdef debug  
    Serial.println("Requesting connection... Waiting for response");
    #endif
    
  }
}



/**
 * function sending the configuration over the serial connection
 * @param extraValues: A string of additional configuration values
 */
void SendConfiguration(String extraValues)
{
  SendConfigurationStartByte();
  //check if extraValues is null
  if(extraValues == NULL)
  {
    //extraValues is null
    //make it an empty string
    extraValues = "";
  }
  
  //send the configuration values
  SendString(PROTOCOL_VERSION);
  SendString(DEVICE_NAME);
  SendLong(NUM_LEDS);
  SendLong(DATA_PIN);
  SendLong(CLOCK_PIN);
  SendString(extraValues);
  
}



/**
 * function letting the given led blink the given times with the given delay between each blink
 *@para: a valid pin number which has to be initialized as OUTPUT
 *@param times: how many times the LED should blink
 *@param msDelay: the delay between each blink in ms
 */
void Blink(int led, int times, int msDelay)
{
   for(int i = 0; i < times; i++)
   {
      digitalWrite(led, HIGH);
      delay(msDelay);
      digitalWrite(led, LOW);
      delay(msDelay);
   }
}



/**
 * function waiting for the specified byte to be received
 * Note: this function blocks until the byte is received or the timeout was exceeded
 * @param b: the byte to listen for
 * @param timeOut: the timeout in ms; has to be > 0
 * @return: true if the byte was received within the timeout, else false
 */
bool WaitForByte(byte b, unsigned long timeOut)
{
  return WaitForOneOf(new byte[1] {b}, 1, timeOut) == 0;
}



/**
 * function waiting for one of the given bytes to be received over the serial connection
 * Note: this function blocks until one of the given bytes was received or the timeout was exceeded
 * @param bytes: an array of bytes
 * @param bytesLength:  the length of the bytes array
 * @param timeOut: the timeout in ms; has to be > 0
 * @retrun: the index of the received byte, or -1 if the timeout was exceeded
 */ 
int WaitForOneOf(byte* bytes, int bytesLength, unsigned long timeOut)
{
   unsigned long startTime = millis();

   while((millis() - startTime) < timeOut)
   {
      //read one byte from the serial buffer
      byte buff[1];

      //calculate the time left until timeOut
      long timeLeft = max((timeOut - millis()) - startTime, (unsigned long)0);
      
      if(readBytes(buff, 1, timeLeft) == -1)
      {
        //no byte was received within the timeOut
        #ifdef debug
        Serial.println("ReadBytes timed out");
        #endif
        return -1;
      }

      //check if the received byte was one of the given bytes
      for(int i = 0; i < bytesLength; i++)
      {
        if(buff[0] == bytes[i])
        {
          return i;
        }
      }
   }

   //the specified byte was not received within the time limit
   return -1;
}



/**
 * function sending a configuration start byte over the serial connection
 */
void SendConfigurationStartByte()
{
  byte txBytes[] = {CONFIGURATION_START_BYTE}; 
  writeBytes(txBytes, 1);
}



/**
 * function sending a configuration acknowledgement byte over the serial connection
 */
void SendConfigurationAcknowledgementByte()
{
  byte txBytes[] = {CONFIGURATION_ACKNOWLEDGEMENT_BYTE}; 
  writeBytes(txBytes, 1);
}



/**
 * function sending a frame acknowledgement byte over the serial connection
 */
void SendFrameAcknowledgementByte()
{
  byte txBytes[] = {FRAME_ACKNOWLEDGEMENT_BYTE}; 
  writeBytes(txBytes, 1);
}



/**
 * function sending a frame error byte over the serial connection
 */
void SendFrameErrorByte()
{
  byte txBytes[] = {FRAME_ERROR_BYTE}; 
  writeBytes(txBytes, 1);
}



/**
 * function sending text over the hardware connection together with a null byte as terminator
 * @param text: the text to send
 */
void SendString(String text)
{ 
    int buffSize = text.length() + 1;
    byte* buff = (byte*) malloc(buffSize);
    
    StringToBytes(text, buff, buffSize);
    writeBytes(buff, buffSize);
    
    free(buff);
}

/**
 * function converting the given text to UTF-8 bytes including a null terminator
 * @param text: the text to convert
 * @param outBytes: the converted text including a null terminator; has to have a size of text.length() + 1
 * @param outBytesLength: the length of the outBytes array; has to be text.length() + 1
 */
void StringToBytes(String text, byte outBytes[], int outBytesLength)
{
  text.getBytes(outBytes, outBytesLength);
}



/**
 * function sending the given unsigned 32-bit number over the serial connection
 * @param number: the number to send over the serial connection
 */
void SendLong(long number)
{
  //create a 32bit buffer
  byte outBytes[4];

  //fill the byte representation of the number into the buffer
  LongToBytes(number, outBytes);

  //send the bytes over the serial connection
  writeBytes(outBytes, 4);
}



/**
 *function converting 4 bytes to an unsigned long (32 bit integer)
 *@param bytes: an array of 4 bytes which should be converted
 *@return: the unsigned long converted from the given bytes
 */
unsigned long BytesToUnsignedLong(byte bytes[])
{
  unsigned long number = 0;

  //shift each byte to its correspondig position and add it to the number
  number += (unsigned long) bytes[0] << 24;
  number += (unsigned long) bytes[1] << 16;
  number += (unsigned long) bytes[2] << 8;
  number += (unsigned long) bytes[3];
 
  return number;
}



/**
 *function converting 4 bytes to a long (32 bit integer)
 *@param bytes: an array of 4 bytes which should be converted
 *@return: the long converted from the given bytes
 */
long BytesToLong(byte bytes[])
{
  long number = 0;

  //shift each byte to its correspondig position and add it to the number
  number += (long) bytes[0] << 24;
  number += (long) bytes[1] << 16;
  number += (long) bytes[2] << 8;
  number += (long) bytes[3];
 
  return number;
}




/*
 *function converting 2 bytes to an unsigned int (16 bit number)
 *@param bytes: an array of 2 bytes which should be converted
 *@return: the unsigned int converted from the given bytes 
 */
unsigned int BytesToInt(byte bytes[])
{
  unsigned int number = 0;

  //shift each byte to its correspondig position and add it to the number
  number +=  bytes[0] << 8;
  number +=  bytes[1];
  
  return number;
}



/**
 * function converting a long (32bit integer) to an array of 4 bytes
 * @param number: the number which should be converted
 * @param outBytes: a pointer to the charArray where the result will be stored; has to have a size of 4
 * @return; an integer representing the length of the outBytes array
 */
int LongToBytes(long number, byte * outBytes)
{
  
  outBytes[0] = (number >> 24) & 0xFF;
  outBytes[1] = (number >> 16) & 0xFF;
  outBytes[2] = (number >> 8) & 0xFF;
  outBytes[3] = number & 0xFF;
  
  return 4;
}



/**
 * function converting an unsigned int (16bit integer) to an array of 2 chars
 * @param number: the number which should be converted
 * @param outBytes: a pointer to the charArray where the result will be stored; has to have a size of 2
 * @return; an integer representing the length of the outBytes array
 */
int IntToBytes(unsigned int number, unsigned char * outBytes)
{
  
  outBytes[0] = (number >> 8) & 0xFF;
  outBytes[1] = number & 0xFF;
  
  return 2;
}
