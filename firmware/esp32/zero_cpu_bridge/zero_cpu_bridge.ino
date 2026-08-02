// Zero-CPU ESP32-S3 hardware bridge
// Protocol: ZEROCPU/1, 115200 baud

#include <Arduino.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {

constexpr unsigned long kBaudRate = 115200;
constexpr int kGpioOutputPin = 2;
constexpr int kGpioInputPin = 4;
constexpr int kPwmOutputPin = 5;
constexpr int kAdcInputPin = 1;

constexpr unsigned long kGpioOutputOffset = 0;
constexpr unsigned long kGpioInputOffset = 8;
constexpr unsigned long kPwmOutputOffset = 16;
constexpr unsigned long kAdcInputOffset = 24;
constexpr unsigned long kStatusOffset = 32;
constexpr unsigned long kCommandOffset = 40;

String inputLine;
long long gpioOutputValue = 0;
long long pwmOutputValue = 0;
long long commandValue = 0;

void sendPong() { Serial.println("ZEROCPU/1 PONG"); }
void sendOk() { Serial.println("ZEROCPU/1 OK"); }
void sendValue(long long value) {
  Serial.print("ZEROCPU/1 VALUE ");
  Serial.println(value);
}
void sendError(const String& message) {
  Serial.print("ZEROCPU/1 ERROR ");
  Serial.println(message);
}

bool parseUnsigned(const String& text, unsigned long& result) {
  if (text.isEmpty()) return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long value = strtoul(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0') return false;
  result = value;
  return true;
}

bool parseSigned(const String& text, long long& result) {
  if (text.isEmpty()) return false;
  errno = 0;
  char* end = nullptr;
  const long long value = strtoll(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0') return false;
  result = value;
  return true;
}

bool splitOnce(const String& text, String& first, String& remainder) {
  const int separator = text.indexOf(' ');
  if (separator < 0) return false;
  first = text.substring(0, separator);
  remainder = text.substring(separator + 1);
  remainder.trim();
  return true;
}

bool readRegister(unsigned long offset, long long& value) {
  switch (offset) {
    case kGpioOutputOffset: value = gpioOutputValue; return true;
    case kGpioInputOffset: value = digitalRead(kGpioInputPin); return true;
    case kPwmOutputOffset: value = pwmOutputValue; return true;
    case kAdcInputOffset: value = analogRead(kAdcInputPin); return true;
    case kStatusOffset: value = 1; return true;
    case kCommandOffset: value = commandValue; return true;
    default: return false;
  }
}

bool writeRegister(unsigned long offset, long long value, String& error) {
  switch (offset) {
    case kGpioOutputOffset:
      gpioOutputValue = value;
      digitalWrite(kGpioOutputPin, value == 0 ? LOW : HIGH);
      return true;
    case kGpioInputOffset:
      error = "GPIO input register is read-only";
      return false;
    case kPwmOutputOffset:
      if (value < 0 || value > 255) {
        error = "PWM value must be in range 0..255";
        return false;
      }
      pwmOutputValue = value;
      analogWrite(kPwmOutputPin, static_cast<int>(value));
      return true;
    case kAdcInputOffset:
      error = "ADC input register is read-only";
      return false;
    case kStatusOffset:
      error = "status register is read-only";
      return false;
    case kCommandOffset:
      commandValue = value;
      if (value == 1) {
        gpioOutputValue = 0;
        pwmOutputValue = 0;
        digitalWrite(kGpioOutputPin, LOW);
        analogWrite(kPwmOutputPin, 0);
      }
      return true;
    default:
      error = "unknown hardware register offset";
      return false;
  }
}

void handleRead(const String& arguments) {
  unsigned long offset = 0;
  if (!parseUnsigned(arguments, offset)) {
    sendError("READ requires one decimal offset");
    return;
  }
  long long value = 0;
  if (!readRegister(offset, value)) {
    sendError("unknown hardware register offset");
    return;
  }
  sendValue(value);
}

void handleWrite(const String& arguments) {
  String offsetText;
  String valueText;
  if (!splitOnce(arguments, offsetText, valueText) ||
      valueText.isEmpty() || valueText.indexOf(' ') >= 0) {
    sendError("WRITE requires decimal offset and value");
    return;
  }
  unsigned long offset = 0;
  long long value = 0;
  if (!parseUnsigned(offsetText, offset) || !parseSigned(valueText, value)) {
    sendError("WRITE contains an invalid number");
    return;
  }
  String error;
  if (!writeRegister(offset, value, error)) {
    sendError(error);
    return;
  }
  sendOk();
}

void handleLine(String line) {
  line.trim();
  if (line.isEmpty()) return;
  if (line == "ZEROCPU/1 PING") {
    sendPong();
    return;
  }
  constexpr const char* kReadPrefix = "ZEROCPU/1 READ ";
  constexpr const char* kWritePrefix = "ZEROCPU/1 WRITE ";
  if (line.startsWith(kReadPrefix)) {
    handleRead(line.substring(strlen(kReadPrefix)));
    return;
  }
  if (line.startsWith(kWritePrefix)) {
    handleWrite(line.substring(strlen(kWritePrefix)));
    return;
  }
  sendError("unknown request");
}

void receiveSerialLines() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\n') {
      handleLine(inputLine);
      inputLine = "";
    } else if (character != '\r') {
      if (inputLine.length() >= 255) {
        inputLine = "";
        sendError("request is too long");
      } else {
        inputLine += character;
      }
    }
  }
}

} // namespace

void setup() {
  pinMode(kGpioOutputPin, OUTPUT);
  pinMode(kGpioInputPin, INPUT_PULLUP);
  pinMode(kPwmOutputPin, OUTPUT);
  pinMode(kAdcInputPin, INPUT);
  digitalWrite(kGpioOutputPin, LOW);
  analogWrite(kPwmOutputPin, 0);
  Serial.begin(kBaudRate);
}

void loop() {
  receiveSerialLines();
  delay(1);
}
