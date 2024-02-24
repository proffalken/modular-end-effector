// example_effector source code

#include <ArduinoJson.h>

void setup() {
  // Initialize "debug" serial port
  // The data rate must be much higher than the "link" serial port
  Serial.begin(115200);
  while (!Serial) continue;

  // Initialize the "link" serial port
  // Use a low data rate to reduce the error ratio
  Serial1.begin(9600);


  // Create and send the initialisation document
  JsonDocument initdoc;
  initdoc["effector_type"] = "example_effector";

  JsonObject j_servo = initdoc.add<JsonObject>();
  j_servo["actuator_type"] = "servo";
  j_servo["default_value"] = 90;
  j_servo["minimum_value"] = 0;
  j_servo["maximum_value"] = 180;


  JsonObject j_motor = initdoc.add<JsonObject>();
  j_motor["actuator_type"] = "motor";
  j_motor["default_value"] = 0;
  j_motor["minimum_value"] = 0;
  j_motor["maximum_value"] = 255;


  JsonObject j_relay = initdoc.add<JsonObject>();
  j_relay["actuator_type"] = "relay";
  j_relay["default_value"] = 0;
  j_relay["minimum_value"] = 0;
  j_relay["maximum_value"] = 1;

  // Send the JSON document over the "link" serial port
  serializeJson(initdoc, Serial1);


}

void loop() {
  // Check to see if we have recieved a command, if we have, action it
  if (Serial1.available()) 
  {
    // Read the JSON document from the "link" serial port
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, Serial1);

    if (err == DeserializationError::Ok) 
    {
      Serial.println("Command recieved from host");
      Serial.print("actuator_type = ");
      Serial.println(doc["actuator_type"].as<const char*>());
      Serial.print("port_id = ");
      Serial.println(doc["actuator_port_id"].as<int>());
      Serial.print("value = ");
      Serial.println(doc["value"].as<int>());
    } 
    else 
    {
      // Print error to the "debug" serial port
      Serial.print("deserializeJson() returned ");
      Serial.println(err.c_str());
  
      // Flush all bytes in the "link" serial port buffer
      while (Serial1.available() > 0)
        Serial1.read();
    }
  }
}
