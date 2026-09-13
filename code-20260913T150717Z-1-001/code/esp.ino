#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char *ssid = "Medical_Device_WiFi";
const char *password = "12345678";

ESP8266WebServer server(80);

// Store schedule data
int numDoses = 0;
// Arrays to store data for up to 3 doses
int startTimes[3], endTimes[3], remindTimes[3], pulses[3], pills[3];

void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>body{font-family: Arial; margin: 20px;} input{margin: 5px 0;}</style>"
                "<script>"
                "function generateForm() {"
                "  var doses = document.getElementById('doses').value;"
                "  var formHtml = '';"
                "  for(var i=1; i<=doses; i++) {"
                "    formHtml += '<h3>Dose ' + i + '</h3>';"
                "    formHtml += 'Start Time (HH:MM): <input type=\"time\" name=\"start'+i+'\" required><br>';"
                "    formHtml += 'End Time (HH:MM): <input type=\"time\" name=\"end'+i+'\" required><br>';"
                "    formHtml += 'Reminder Time (HH:MM): <input type=\"time\" name=\"remind'+i+'\" required><br>';"
                "    formHtml += 'Solution Pulses: <input type=\"number\" name=\"pulse'+i+'\" required><br>';"
                "    formHtml += 'Number of Tablets: <input type=\"number\" name=\"pills'+i+'\" required><br>';"
                "  }"
                "  formHtml += '<br><input type=\"submit\" value=\"Save Schedule\">';"
                "  document.getElementById('doseForms').innerHTML = formHtml;"
                "}"
                "</script></head><body>"
                "<h2>Medical Dispenser Setup</h2>"
                "<label>How many doses today? (1-3): </label>"
                "<input type='number' id='doses' min='1' max='3' onchange='generateForm()'>"
                "<form action='/save' method='POST' id='doseForms'></form>"
                "</body></html>";
  server.send(200, "text/html", html);
}

int timeToMinutes(String timeStr) {
  // Converts "HH:MM" to minutes from midnight
  int colonIndex = timeStr.indexOf(':');
  int h = timeStr.substring(0, colonIndex).toInt();
  int m = timeStr.substring(colonIndex + 1).toInt();
  return (h * 60) + m;
}

void handleSave() {
  numDoses = 0;
  // Loop through potential arguments to count submitted doses
  for (int i = 1; i <= 3; i++) {
    if (server.hasArg("start" + String(i))) {
      numDoses++;
      startTimes[i-1] = timeToMinutes(server.arg("start" + String(i)));
      endTimes[i-1] = timeToMinutes(server.arg("end" + String(i)));
      remindTimes[i-1] = timeToMinutes(server.arg("remind" + String(i)));
      pulses[i-1] = server.arg("pulse" + String(i)).toInt();
      pills[i-1] = server.arg("pills" + String(i)).toInt();
    }
  }

  // Basic Anti-Crash Validation: Check if start times overlap
  bool overlap = false;
  if (numDoses > 1 && startTimes[1] <= endTimes[0]) overlap = true;
  if (numDoses > 2 && startTimes[2] <= endTimes[1]) overlap = true;

  if (overlap) {
    server.send(200, "text/html", "<h2>Error: Times overlap!</h2><a href='/'>Go Back</a>");
    return;
  }

  // Format data string to send to Arduino
  // Format: <NumDoses,Start1,End1,Remind1,Pulse1,Pills1,Start2...>
  String dataString = "<" + String(numDoses);
  for (int i = 0; i < numDoses; i++) {
    dataString += "," + String(startTimes[i]) + "," + String(endTimes[i]) + "," + 
                  String(remindTimes[i]) + "," + String(pulses[i]) + "," + String(pills[i]);
  }
  dataString += ">";
  
  Serial.println(dataString); // Send to Arduino via Hardware TX (connected to Arduino A1)

  server.send(200, "text/html", "<h2>Schedule Saved!</h2><p>Data sent to device.</p><a href='/'>Back</a>");
}

void setup() {
  Serial.begin(9600);
  WiFi.softAP(ssid, password);
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();
}

void loop() {
  server.handleClient();
}