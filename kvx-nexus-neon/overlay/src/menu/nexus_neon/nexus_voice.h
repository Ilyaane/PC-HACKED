#pragma once
#include <Arduino.h>
#include <stdint.h>
// Native Nexus voice on KVXputer: use its existing I2S mic/audio engines.
// No secondary M5Cardputer instance, no Morse code, no PSRAM requirement.
namespace NexusVoice {
bool storage();
bool hasDraft();
String draftRecipient();
bool record(const String &recipient);
bool playDraft();
bool removeDraft();
bool send(const String &recipient);
bool receive(uint32_t id);
bool playReceived(uint32_t id);
bool busy();
bool poll(); // true when an asynchronous transfer completed
bool completedDownload();
uint32_t lastDownloadId();
String status();
void cancel(); // best-effort cancellation on module exit
}
