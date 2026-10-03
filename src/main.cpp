#include <Arduino.h>
#include <M5Unified.h>
#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <cstring>
#include "ir_pipeline.h"
#include "storage.h"
#include "tvbgone.h"
#include "macros.h"
#include "hold_policy.h"
#include "backup.h"
#include "text_editor.h"

namespace {
constexpr gpio_num_t kRxPin = GPIO_NUM_42;
constexpr gpio_num_t kTxPin = GPIO_NUM_46;
constexpr uint32_t kResolutionHz = 1000000;
constexpr uint32_t kSonyCarrierHz = 40000;
constexpr uint32_t kRawCarrierHz = 38000;
constexpr uint32_t kRawCarrierOptionsHz[] = {36000, 38000, 40000};
constexpr size_t kCapacity = 256;
constexpr uint32_t kFrameGapNs = 12000000;
constexpr uint32_t kWaitTimeoutMs = 15000;
constexpr bool kVerboseRawLog = false;

// Static BSS, never PSRAM. RX and TX have separate buffers so the ISR cannot
// overwrite the frame while rmt_transmit is reading it.
rmt_symbol_word_t rxSymbols[kCapacity];
rmt_symbol_word_t txSymbols[kCapacity];
ir::Run analysisRuns[kCapacity * 2]; // Internal RAM; never used for RAW replay.
rmt_symbol_word_t tvSymbols[512]; // Independent flash database, shared RMT TX.
store::Signal activeSignal;
store::Signal slotScratch;
rmt_channel_handle_t rxChannel = nullptr;
rmt_channel_handle_t txChannel = nullptr;
rmt_encoder_handle_t encoder = nullptr;
volatile bool rxDone = false;
volatile size_t rxCount = 0;
size_t frameCount = 0;
uint32_t frameDurationUs = 0;
uint32_t waitingSince = 0;
bool rxArmed = false;
bool hardwareReady = false;
ir::DecodeResult decoded;
size_t rawCarrierIndex = 1; // 38 kHz on every new Learn.
bool rawBPending = false;
uint32_t rawBPressedAt = 0;
bool capturedAPending = false;
uint32_t capturedAPressedAt = 0;
uint8_t selectedRemote = 0, selectedButton = 0;
uint8_t selectedMacro = 0, selectedStep = 0, editRemote = 0, editButton = 0, editDelay = 0;
constexpr uint16_t kDelays[] = {0,250,500,1000,2000};
macros::Macro activeMacro;
editor::State nameEditor;
enum class RenameTarget { Remote, Button, Macro } renameTarget;
bool renameAPending = false, renameBPending = false;
uint32_t renameAAt = 0, renameBAt = 0;
backup::Importer importer;
uint32_t importLastAt = 0;
bool macroRunning = false;
uint8_t macroStep = 0;
uint32_t macroNextAt = 0;
bool savedAPending = false, holdActive = false;
uint32_t holdNextUs = 0;
uint32_t holdAdditional = 0;
hold::Policy holdPolicy;
tvbgone::Region tvRegion = tvbgone::Region::NorthAmerica;
size_t tvPosition = 0;
uint32_t tvNextAt = 0;
bool tvRunning = false;
const char *saveMessage = "";
constexpr const char *kHomeItems[] = {"Learn", "Remotes", "Macros", "TV-B-Gone", "Settings", "About"};
constexpr const char *kSavedItems[] = {"Test", "Rename", "Delete", "Back"};

enum class Page { Home, About, Waiting, Captured, Remotes, Buttons, SavedButton,
                  SaveRemote, SaveButton, Overwrite, SaveResult, Settings,
                  EraseConfirm, TvMenu, TvSending, Error, RemoteActions,
                  MacroList, MacroMenu, MacroEdit, MacroStep, MacroRunning,
                  Rename, Import };
Page page = Page::Home;
Page resultBack = Page::Captured;
Page renameBack = Page::Home;
int selection = 0;

bool check(esp_err_t result, const char *operation) {
  if (result == ESP_OK) return true;
  Serial.printf("[ERROR] %s: %s (0x%x)\n", operation, esp_err_to_name(result), result);
  return false;
}

void heading(const char *title) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(4, 5);
  M5.Display.println(title);
  M5.Display.drawFastHLine(3, 25, 233, TFT_DARKGREY);
  M5.Display.setCursor(4, 31);
}

void render() {
  switch (page) {
    case Page::Home:
      heading("IR Learner");
      for (int i = 0; i < 6; ++i)
        M5.Display.printf("%c %s\n", selection == i ? '>' : ' ',
                          kHomeItems[i]);
      M5.Display.print("B: next  A: enter");
      break;
    case Page::About:
      heading("About");
      M5.Display.print("StickS3 IR learner\nRX 42 / TX 46\nSony 40 / RAW 36-40 kHz\n\nA/B: back");
      break;
    case Page::Waiting:
      heading("Learn");
      M5.Display.println("Waiting for IR...");
      M5.Display.println("Aim from 30+ cm");
      M5.Display.println("A/B: cancel");
      break;
    case Page::Captured:
      heading("Captured");
      M5.Display.printf("Symbols: %u\nDuration: %lu ms\n", static_cast<unsigned>(frameCount),
                        static_cast<unsigned long>(frameDurationUs / 1000));
      if (decoded.valid) {
        M5.Display.printf("Protocol: %s\nCmd:%lu Addr:%lu  %u kHz\nA: Test  B: Again\nHold A: Save  Hold B: Back",
                          decoded.protocol == ir::Protocol::SonySIRC12 ? "Sony SIRC12" : "NEC",
                          static_cast<unsigned long>(decoded.command),
                          static_cast<unsigned long>(decoded.address),
                          decoded.protocol == ir::Protocol::SonySIRC12 ? 40u : 38u);
      } else {
        M5.Display.printf("Protocol: RAW\nCarrier: %lu kHz\nA: Test  B: Carrier\nHold A: Save  Hold B: Again",
                          static_cast<unsigned long>(kRawCarrierOptionsHz[rawCarrierIndex] / 1000));
      }
      break;
    case Page::Remotes:
    case Page::SaveRemote:
      heading(page == Page::Remotes ? "Remotes" : "Save / Remote");
      for (uint8_t i = 0; i < store::kRemotes; ++i) {
        store::Remote remote;
        store::loadRemote(i, remote);
        M5.Display.printf("%c %s\n", selection == i ? '>' : ' ', remote.name);
      }
      M5.Display.printf("%c Back\nB: next  A: enter", selection == 4 ? '>' : ' ');
      break;
    case Page::Buttons:
    case Page::SaveButton:
      heading(page == Page::Buttons ? "Remote buttons" : "Save / Button");
      for (uint8_t i = 0; i < store::kButtons; ++i) {
        const bool occupied = store::loadSignal(selectedRemote, i, slotScratch);
        M5.Display.printf("%c %s %s\n", selection == i ? '>' : ' ',
                          occupied ? slotScratch.name : "[empty]",
                          occupied ? "" : "");
      }
      M5.Display.printf("%c Back\nB: next  A: enter", selection == 4 ? '>' : ' ');
      break;
    case Page::SavedButton:
      heading("Saved button");
      M5.Display.printf("%s\n%s  %lu kHz\n", activeSignal.name,
                        !activeSignal.decoded.valid ? "RAW" :
                          activeSignal.decoded.protocol == ir::Protocol::SonySIRC12 ? "Sony SIRC12" : "NEC",
                        static_cast<unsigned long>(activeSignal.carrierHz / 1000));
      for (int i = 0; i < 4; ++i)
        M5.Display.printf("%c %s\n", selection == i ? '>' : ' ',
                          kSavedItems[i]);
      M5.Display.print("B: next A: enter/Hold");
      break;
    case Page::RemoteActions: {
      store::Remote remote;
      store::loadRemote(selectedRemote,remote);
      heading(remote.name);
      constexpr const char *items[]={"Buttons","Rename","Back"};
      for (int i=0;i<3;++i) M5.Display.printf("%c %s\n",selection==i?'>':' ',items[i]);
      M5.Display.print("B: next  A: enter");
      break;
    }
    case Page::MacroList:
      heading("Macros");
      for (uint8_t i=0;i<macros::kSlots;++i) {
        macros::Macro m;macros::load(i,m);
        M5.Display.printf("%c %s\n",selection==i?'>':' ',m.name);
      }
      M5.Display.printf("%c Back\nB: next A: enter",selection==4?'>':' ');
      break;
    case Page::MacroMenu: {
      heading(activeMacro.name);
      M5.Display.printf("%u steps\n",activeMacro.stepCount);
      constexpr const char *items[]={"Run","Edit","Rename","Delete","Back"};
      for (int i=0;i<5;++i) M5.Display.printf("%c %s\n",selection==i?'>':' ',items[i]);
      break;
    }
    case Page::MacroEdit:
      heading("Macro steps");
      for (int i=selection>2?selection-2:0;
           i<=activeMacro.stepCount+(activeMacro.stepCount<macros::kMaxSteps?1:0) &&
           i<(selection>2?selection-2:0)+5;++i) {
        if (i==activeMacro.stepCount && activeMacro.stepCount<macros::kMaxSteps)
          M5.Display.printf("%c Add step\n",selection==i?'>':' ');
        else if (i==activeMacro.stepCount+(activeMacro.stepCount<macros::kMaxSteps?1:0))
          M5.Display.printf("%c Back\n",selection==i?'>':' ');
        else M5.Display.printf("%c %u: R%u B%u +%u\n",selection==i?'>':' ',i+1,
          activeMacro.steps[i].remote+1,activeMacro.steps[i].button+1,
          activeMacro.steps[i].delayAfterMs);
      }
      break;
    case Page::MacroStep:
      heading("Edit step");
      M5.Display.printf("%c Remote %u\n%c Button %u\n%c Delay %u ms\n%c Save\n%c Delete/Back\nB: next A: change",
        selection==0?'>':' ',editRemote+1,selection==1?'>':' ',editButton+1,
        selection==2?'>':' ',kDelays[editDelay],selection==3?'>':' ',selection==4?'>':' ');
      break;
    case Page::MacroRunning:
      heading("Macro running");
      M5.Display.printf("%s\nStep %u / %u\n\nB: Cancel",activeMacro.name,
        unsigned(macroStep+1),unsigned(activeMacro.stepCount));
      break;
    case Page::Rename:
      heading("Rename");
      M5.Display.printf("%s%c\nCharacter: %c\nA: add B: next char\nHold A: save\nHold B: cancel",nameEditor.text,'_',nameEditor.current());
      break;
    case Page::Import:
      heading("Import Backup");
      M5.Display.printf("Paste Serial backup\n115200 baud\nImported %u Errors %u\nB: cancel",importer.imported(),importer.errors());
      break;
    case Page::Overwrite:
    case Page::EraseConfirm:
      heading(page == Page::Overwrite ? "Overwrite button?" : "Erase ALL learned?");
      M5.Display.println(page == Page::EraseConfirm ? "Only IRcito NVS data" : "Old signal will be replaced");
      M5.Display.printf("%c Yes\n%c No\nB: next  A: enter", selection == 0 ? '>' : ' ',
                        selection == 1 ? '>' : ' ');
      break;
    case Page::SaveResult:
      heading("Storage");
      M5.Display.printf("%s\n\nA/B: back", saveMessage);
      break;
    case Page::Settings:
      heading("Settings");
      M5.Display.printf("%c Export Backup\n%c Import Backup\n%c Erase all learned\n%c Back\nB: next A: enter",
                        selection == 0 ? '>' : ' ', selection == 1 ? '>' : ' ',
                        selection == 2 ? '>' : ' ', selection == 3 ? '>' : ' ');
      break;
    case Page::TvMenu:
      heading("TV-B-Gone");
      M5.Display.printf("%c Start\n%c Region: %s\n%c Back\n\nB: next  A: enter",
                        selection == 0 ? '>' : ' ', selection == 1 ? '>' : ' ',
                        tvbgone::name(tvRegion), selection == 2 ? '>' : ' ');
      break;
    case Page::TvSending:
      heading("TV-B-Gone");
      M5.Display.printf("%s\nSending %u/%u\n\nB: Cancel",
                        tvbgone::name(tvRegion), unsigned(tvPosition + 1),
                        unsigned(tvbgone::count(tvRegion)));
      break;
    case Page::Error:
      heading("IR hardware error");
      M5.Display.println("See USB serial log");
      M5.Display.println("A/B: retry");
      break;
  }
}

bool onReceive(rmt_channel_handle_t, const rmt_rx_done_event_data_t *data, void *) {
  rxCount = data->num_symbols;
  rxDone = true;
  return false; // The main loop polls; no higher priority task to wake.
}

bool initIr() {
  if (hardwareReady) return true;
  // M5Stack: the speaker amplifier interferes with built-in IR RX.
  M5.Speaker.end();
  Serial.println("[POWER] Speaker amplifier disabled for IR RX");
  M5.Power.setExtOutput(true, m5::ext_none);
  Serial.println("[POWER] EXT_5V output requested for integrated IR");
  delay(100);
  if (!M5.Power.getExtOutput()) {
    Serial.println("[ERROR] EXT_5V did not turn on; integrated IR is unpowered");
    return false;
  }

  // Receiver is active-low. Pull-up keeps its idle level defined.
  if (!check(gpio_set_pull_mode(kRxPin, GPIO_PULLUP_ONLY), "GPIO42 pull-up")) return false;
  rmt_rx_channel_config_t rxConfig = {};
  rxConfig.gpio_num = kRxPin;
  rxConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  rxConfig.resolution_hz = kResolutionHz;
  rxConfig.mem_block_symbols = 128;
  if (!check(rmt_new_rx_channel(&rxConfig, &rxChannel), "rmt_new_rx_channel")) return false;
  rmt_rx_event_callbacks_t callbacks = {};
  callbacks.on_recv_done = onReceive;
  if (!check(rmt_rx_register_event_callbacks(rxChannel, &callbacks, nullptr), "rmt_rx_register_event_callbacks")) return false;
  if (!check(rmt_enable(rxChannel), "rmt_enable RX")) return false;
  if (!check(gpio_set_pull_mode(kRxPin, GPIO_PULLUP_ONLY), "GPIO42 pull-up after RMT init")) return false;

  rmt_tx_channel_config_t txConfig = {};
  txConfig.gpio_num = kTxPin;
  txConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  txConfig.resolution_hz = kResolutionHz;
  txConfig.mem_block_symbols = 128;
  txConfig.trans_queue_depth = 1;
  if (!check(rmt_new_tx_channel(&txConfig, &txChannel), "rmt_new_tx_channel")) return false;
  rmt_carrier_config_t carrier = {};
  carrier.frequency_hz = kRawCarrierHz;
  carrier.duty_cycle = 0.33f;
  if (!check(rmt_apply_carrier(txChannel, &carrier), "rmt_apply_carrier")) return false;
  rmt_copy_encoder_config_t encoderConfig = {};
  if (!check(rmt_new_copy_encoder(&encoderConfig, &encoder), "rmt_new_copy_encoder")) return false;
  if (!check(rmt_enable(txChannel), "rmt_enable TX")) return false;
  hardwareReady = true;
  Serial.println("[IR] Ready: RX GPIO42, TX GPIO46, RMT 1 MHz, Sony 40000 / RAW default 38000 Hz 33%");
  return true;
}

bool armRx() {
  if (!hardwareReady || !rxChannel) return false;
  rxDone = false;
  rxCount = 0;
  rmt_receive_config_t config = {};
  config.signal_range_min_ns = 1000;
  config.signal_range_max_ns = kFrameGapNs;
  if (!check(rmt_receive(rxChannel, rxSymbols, sizeof(rxSymbols), &config), "rmt_receive")) return false;
  rxArmed = true;
  waitingSince = millis();
  Serial.printf("[RX] Armed: buffer=%u symbols, frame gap=%lu us\n",
                static_cast<unsigned>(kCapacity), static_cast<unsigned long>(kFrameGapNs / 1000));
  return true;
}

void learn() {
  decoded = {};
  rawCarrierIndex = 1;
  rawBPending = false;
  if (!initIr() || !armRx()) {
    page = Page::Error;
  } else {
    page = Page::Waiting;
  }
  render();
}

void handleRx() {
  if (!rxDone || page != Page::Waiting) return;
  rxArmed = false;
  const size_t count = rxCount;
  rxDone = false;
  Serial.printf("[RX] Complete: %u symbols\n", static_cast<unsigned>(count));
  if (count < 4 || count >= kCapacity) {
    Serial.printf("[RX] Rejected short/overflow frame: %u\n", static_cast<unsigned>(count));
    if (!armRx()) { page = Page::Error; render(); }
    return;
  }
  frameCount = count;
  frameDurationUs = 0;
  for (size_t i = 0; i < frameCount; ++i) {
    const auto &rx = rxSymbols[i];
    frameDurationUs += rx.duration0 + rx.duration1;
    // The demodulated receiver is active-low; TX carrier is active-high.
    txSymbols[i] = rx;
    txSymbols[i].level0 = !rx.level0;
    txSymbols[i].level1 = !rx.level1;
    if (kVerboseRawLog || i < 8) {
      Serial.printf("[RAW] %03u: RX %u:%u us %u:%u us -> TX %u:%u\n",
                    static_cast<unsigned>(i), rx.level0, rx.duration0, rx.level1, rx.duration1,
                    txSymbols[i].level0, txSymbols[i].level1);
    }
  }
  if (!kVerboseRawLog && frameCount > 8)
    Serial.printf("[RAW] %u additional symbols omitted (set kVerboseRawLog for all)\n",
                  static_cast<unsigned>(frameCount - 8));
  Serial.printf("[RX] Accepted: %u symbols, %lu us; carrier not measurable by IR RX\n",
                static_cast<unsigned>(frameCount), static_cast<unsigned long>(frameDurationUs));
  size_t runCount = 0;
  if (ir::normalize(rxSymbols, frameCount, analysisRuns, kCapacity * 2, runCount)) {
    Serial.printf("[NORMALIZE] generated %u runs\n", static_cast<unsigned>(runCount));
    ir::Merge merges[8] = {};
    const size_t merged = ir::deglitch(analysisRuns, runCount, merges, 8);
    for (size_t i = 0; i < merged && i < 8; ++i) {
      const auto &m = merges[i];
      Serial.printf("[DEGLITCH] merged level=%u %lu/%lu/%lu -> %lu us\n",
                    m.level, static_cast<unsigned long>(m.firstUs),
                    static_cast<unsigned long>(m.glitchUs),
                    static_cast<unsigned long>(m.lastUs),
                    static_cast<unsigned long>(m.totalUs));
    }
    Serial.printf("[DEGLITCH] %u merges; %u runs for decode%s\n",
                  static_cast<unsigned>(merged), static_cast<unsigned>(runCount),
                  merged > 8 ? " (merge details capped at 8)" : "");
    Serial.println("[DECODE] trying Sony SIRC12");
    Serial.println("[DECODE] trying NEC");
    decoded = ir::decode(analysisRuns, runCount);
  } else {
    Serial.println("[NORMALIZE] failed; using RAW fallback");
  }
  if (decoded.valid) {
    Serial.printf("[DECODE] protocol=%s\n",
                  decoded.protocol == ir::Protocol::SonySIRC12 ? "Sony SIRC12" : "NEC");
    Serial.printf("[DECODE] address=%lu command=%lu bits=%u\n",
                  static_cast<unsigned long>(decoded.address),
                  static_cast<unsigned long>(decoded.command), decoded.bits);
  } else {
    Serial.println("[DECODE] no protocol matched; using RAW fallback");
    Serial.printf("[RAW] Carrier selected: %lu Hz\n",
                  static_cast<unsigned long>(kRawCarrierOptionsHz[rawCarrierIndex]));
  }
  // Capture metadata for persistence; the RX and prepared RAW TX arrays above
  // remain untouched. A saved signal follows this same replay path.
  activeSignal.id = 0;
  activeSignal.name[0] = 0;
  activeSignal.decoded = decoded;
  activeSignal.rawCount = frameCount;
  memcpy(activeSignal.raw, rxSymbols, frameCount * sizeof(rmt_symbol_word_t));
  activeSignal.carrierHz = decoded.valid ?
      (decoded.protocol == ir::Protocol::SonySIRC12 ? kSonyCarrierHz : kRawCarrierHz) :
      kRawCarrierOptionsHz[rawCarrierIndex];
  activeSignal.repeatCount = decoded.valid && decoded.protocol == ir::Protocol::SonySIRC12 ? 3 : 1;
  activeSignal.repeatPeriodUs = activeSignal.repeatCount == 3 ? 45000 : 0;
  page = Page::Captured;
  render();
}

bool setTxCarrier(uint32_t hz) {
  rmt_carrier_config_t carrier = {};
  carrier.frequency_hz = hz;
  carrier.duty_cycle = 0.33f;
  if (!check(rmt_apply_carrier(txChannel, &carrier), "rmt_apply_carrier TX")) return false;
  Serial.printf("[TX] Carrier set to %lu Hz\n", static_cast<unsigned long>(hz));
  return true;
}

void replay(const store::Signal &signal, bool sonySingle = false) {
  if (!hardwareReady && !initIr()) { page = Page::Error; render(); return; }
  if (!hardwareReady || !txChannel || !encoder ||
      (!signal.decoded.valid && signal.rawCount == 0) || signal.rawCount > kCapacity) {
    Serial.println("[TX] Invalid state; replay cancelled");
    return;
  }
  rmt_transmit_config_t config = {};
  config.loop_count = 0;
  config.flags.eot_level = 0;
  if (!signal.decoded.valid) {
    const uint32_t carrierHz = signal.carrierHz;
    if (!setTxCarrier(carrierHz)) return;
    Serial.printf("[TX] RAW replay at %lu Hz\n", static_cast<unsigned long>(carrierHz));
    if (!check(rmt_transmit(txChannel, encoder, txSymbols,
                            signal.rawCount * sizeof(rmt_symbol_word_t), &config), "rmt_transmit RAW")) return;
    if (check(rmt_tx_wait_all_done(txChannel, 1000), "rmt_tx_wait_all_done RAW")) {
      Serial.println("[TX] RAW replay complete");
    }
    return;
  }

  if (signal.decoded.protocol == ir::Protocol::NEC) {
    if (!setTxCarrier(kRawCarrierHz)) return;
    // Standard NEC: address, its complement, command, its complement; LSB first.
    const uint8_t addr = static_cast<uint8_t>(signal.decoded.address);
    const uint8_t cmd = static_cast<uint8_t>(signal.decoded.command);
    const uint32_t payload = uint32_t(addr) | (uint32_t(uint8_t(~addr)) << 8) |
                             (uint32_t(cmd) << 16) | (uint32_t(uint8_t(~cmd)) << 24);
    rmt_symbol_word_t nec[34] = {};
    nec[0].level0 = 1; nec[0].duration0 = 9000;
    nec[0].level1 = 0; nec[0].duration1 = 4500;
    for (size_t bit = 0; bit < 32; ++bit) {
      nec[bit + 1].level0 = 1;
      nec[bit + 1].duration0 = 560;
      nec[bit + 1].level1 = 0;
      nec[bit + 1].duration1 = (payload & (uint32_t(1) << bit)) ? 1690 : 560;
    }
    nec[33].level0 = 1; nec[33].duration0 = 560;
    nec[33].level1 = 0; nec[33].duration1 = 560;
    Serial.printf("[TX] NEC address=%lu command=%lu at 38000 Hz\n",
                  static_cast<unsigned long>(signal.decoded.address),
                  static_cast<unsigned long>(signal.decoded.command));
    if (!check(rmt_transmit(txChannel, encoder, nec, sizeof(nec), &config), "rmt_transmit NEC")) return;
    if (check(rmt_tx_wait_all_done(txChannel, 1000), "rmt_tx_wait_all_done NEC"))
      Serial.println("[TX] NEC replay complete");
    return;
  }

  if (!setTxCarrier(kSonyCarrierHz)) return;
  // Sony SIRC12: 7 command bits, then 5 address bits, LSB first.
  const uint16_t payload = static_cast<uint16_t>(signal.decoded.command | (signal.decoded.address << 7));
  rmt_symbol_word_t sony[13] = {};
  sony[0].level0 = 1;
  sony[0].duration0 = 2400;
  sony[0].level1 = 0;
  sony[0].duration1 = 600;
  for (size_t bit = 0; bit < 12; ++bit) {
    sony[bit + 1].level0 = 1;
    sony[bit + 1].duration0 = (payload & (1u << bit)) ? 1200 : 600;
    sony[bit + 1].level1 = 0;
    sony[bit + 1].duration1 = 600;
  }
  Serial.printf("[TX] Sony SIRC12 command=%lu address=%lu\n",
                static_cast<unsigned long>(signal.decoded.command),
                static_cast<unsigned long>(signal.decoded.address));
  const unsigned frames = sonySingle ? 1 : 3;
  for (unsigned frame = 0; frame < frames; ++frame) {
    Serial.printf("[TX] Sony frame %u/%u\n", frame + 1, frames);
    const uint32_t frameStartUs = micros();
    if (!check(rmt_transmit(txChannel, encoder, sony, sizeof(sony), &config), "rmt_transmit")) return;
    if (!check(rmt_tx_wait_all_done(txChannel, 1000), "rmt_tx_wait_all_done")) return;
    if (frame + 1 < frames) {
      const uint32_t elapsedUs = micros() - frameStartUs;
      if (elapsedUs < 45000) delayMicroseconds(45000 - elapsedUs);
      else Serial.printf("[TX] Sony frame overran 45 ms period: %lu us\n", static_cast<unsigned long>(elapsedUs));
    }
  }
  Serial.println("[TX] Sony SIRC12 replay complete");
}

bool sendNecRepeat() {
  if (!setTxCarrier(kRawCarrierHz)) return false;
  rmt_symbol_word_t repeat[2] = {};
  repeat[0].level0=1; repeat[0].duration0=9000;
  repeat[0].level1=0; repeat[0].duration1=2250;
  repeat[1].level0=1; repeat[1].duration0=560;
  repeat[1].level1=0; repeat[1].duration1=560;
  rmt_transmit_config_t config = {};
  config.flags.eot_level=0;
  return check(rmt_transmit(txChannel,encoder,repeat,sizeof(repeat),&config),"rmt_transmit NEC repeat") &&
         check(rmt_tx_wait_all_done(txChannel,1000),"rmt_tx_wait_all_done NEC repeat");
}

void serviceHold() {
  if (page!=Page::SavedButton || selection!=0 || !savedAPending) return;
  if (M5.BtnA.wasReleased()) {
    if (holdActive) Serial.println("[HOLD] stop");
    savedAPending=holdActive=false;return;
  }
  if (!M5.BtnA.isPressed()) return;
  if (holdPolicy.mode==hold::Mode::None) {
    savedAPending=false;
    Serial.println("[HOLD] repeat unavailable for this signal");return;
  }
  if (int32_t(micros()-holdNextUs)<0) return;
  if (!holdActive) {
    holdActive=true;holdAdditional=0;
    Serial.printf("[HOLD] start protocol=%s\n",
      holdPolicy.mode==hold::Mode::SonyFrame?"Sony SIRC12":
      holdPolicy.mode==hold::Mode::NecRepeat?"NEC":"RAW");
  }
  const uint32_t frameStart=micros();
  bool okay=true;
  if (holdPolicy.mode==hold::Mode::SonyFrame) replay(activeSignal,true);
  else if (holdPolicy.mode==hold::Mode::NecRepeat) okay=sendNecRepeat();
  else replay(activeSignal);
  if (!okay) {savedAPending=holdActive=false;return;}
  Serial.printf("[HOLD] repeat %u\n",++holdAdditional);
  if (holdPolicy.maxAdditional && holdAdditional>=holdPolicy.maxAdditional) {
    savedAPending=holdActive=false;Serial.println("[HOLD] configured RAW repeats complete");return;
  }
  holdNextUs=frameStart+holdPolicy.periodUs;
  if (int32_t(micros()-holdNextUs)>=0)
    holdNextUs=micros()+holdPolicy.periodUs;
}

void prepareSavedTx(const store::Signal &signal) {
  for (size_t i = 0; i < signal.rawCount; ++i) {
    const auto &rx = signal.raw[i];
    txSymbols[i] = rx;
    txSymbols[i].level0 = !rx.level0;
    txSymbols[i].level1 = !rx.level1;
  }
}

bool sendTvCode(const tvbgone::Code &code) {
  if (!hardwareReady && !initIr()) return false;
  const size_t symbols = tvbgone::encode(code, tvSymbols, 512);
  if (!symbols) { Serial.println("[TVBGONE] invalid/oversized code"); return false; }
  // Non-modulated entries in the original public database require a steady
  // HIGH for mark, not a carrier. The learner's setTxCarrier() is unchanged.
  if (code.hz) {
    if (!setTxCarrier(code.hz)) return false;
  } else if (!check(rmt_apply_carrier(txChannel, nullptr), "TV-B-Gone disable carrier")) return false;
  rmt_transmit_config_t config = {};
  config.loop_count = 0;
  config.flags.eot_level = 0;
  return check(rmt_transmit(txChannel, encoder, tvSymbols,
                           symbols * sizeof(rmt_symbol_word_t), &config), "rmt_transmit TV-B-Gone") &&
         check(rmt_tx_wait_all_done(txChannel, 10000), "rmt_tx_wait_all_done TV-B-Gone");
}

void sendNextTvCode() {
  if (!tvRunning || page != Page::TvSending || int32_t(millis() - tvNextAt) < 0) return;
  if (tvPosition >= tvbgone::count(tvRegion)) {
    Serial.println("[TVBGONE] complete");
    tvRunning = false; page = Page::TvMenu; selection = 0; render(); return;
  }
  const tvbgone::Code *entry = tvbgone::code(tvRegion, tvPosition);
  if (!entry) { tvRunning = false; page = Page::Error; render(); return; }
  render();
  Serial.printf("[TVBGONE] code %u/%u carrier=%lu Hz\n", unsigned(tvPosition + 1),
                unsigned(tvbgone::count(tvRegion)), static_cast<unsigned long>(entry->hz));
  if (!sendTvCode(*entry)) { tvRunning = false; page = Page::Error; render(); return; }
  ++tvPosition;
  tvNextAt = millis() + 205; // The upstream inter-code pause, cancelable below.
}

void saveSelectedSlot() {
  saveMessage = store::saveSignal(selectedRemote, selectedButton, activeSignal)
                    ? "Saved. Reboot to verify." : "Save failed; see Serial.";
  resultBack = Page::Captured;
  page = Page::SaveResult;
  render();
}

void startRename(RenameTarget target, Page back) {
  renameTarget=target;renameBack=back;
  nameEditor.start(""); // New name is composed from scratch; cancellation leaves NVS intact.
  renameAPending=renameBPending=false;
  page=Page::Rename;render();
}

void commitRename() {
  if (!store::validName(nameEditor.text)) {Serial.println("[NAME] empty/invalid name");return;}
  bool ok=false;
  if (renameTarget==RenameTarget::Remote) {
    store::Remote remote;
    store::loadRemote(selectedRemote,remote);
    std::memcpy(remote.name,nameEditor.text,sizeof(remote.name));
    ok=store::saveRemote(selectedRemote,remote);
  } else if (renameTarget==RenameTarget::Button) {
    store::Signal previous;
    if (store::loadSignal(selectedRemote,selectedButton,previous)) {
      std::memcpy(previous.name,nameEditor.text,sizeof(previous.name));
      ok=store::saveSignal(selectedRemote,selectedButton,previous);
      if (ok) activeSignal=previous;
    }
  } else {
    macros::Macro candidate=activeMacro;
    std::memcpy(candidate.name,nameEditor.text,sizeof(candidate.name));
    ok=macros::save(selectedMacro,candidate);
    if (ok) activeMacro=candidate;
  }
  Serial.printf("[NAME] %s\n",ok?"saved":"save failed; previous name retained");
  if (ok) {page=renameBack;selection=0;render();}
}

void serviceRename() {
  if (M5.BtnA.wasPressed()) {renameAAt=millis();renameAPending=true;}
  if (M5.BtnB.wasPressed()) {renameBAt=millis();renameBPending=true;}
  if (renameAPending && M5.BtnA.isPressed() && millis()-renameAAt>=800) {
    renameAPending=false;commitRename();
  } else if (renameAPending && M5.BtnA.wasReleased()) {
    renameAPending=false;nameEditor.accept();render();
  }
  if (page!=Page::Rename) return;
  if (renameBPending && M5.BtnB.isPressed() && millis()-renameBAt>=800) {
    renameBPending=false;page=renameBack;selection=0;render();
  } else if (renameBPending && M5.BtnB.wasReleased()) {
    renameBPending=false;nameEditor.next();render();
  }
}

void startMacro() {
  if (!activeMacro.stepCount) {Serial.println("[MACRO] empty");return;}
  macroStep=0;macroNextAt=millis();macroRunning=true;
  page=Page::MacroRunning;Serial.printf("[MACRO] start %s\n",activeMacro.name);render();
}
void serviceMacro() {
  if (M5.BtnB.wasPressed()) {
    Serial.println("[MACRO] cancelled");macroRunning=false;
    page=Page::MacroMenu;selection=0;render();return;
  }
  if (!macroRunning || int32_t(millis()-macroNextAt)<0) return;
  if (macroStep>=activeMacro.stepCount) {
    Serial.println("[MACRO] complete");macroRunning=false;
    page=Page::MacroMenu;selection=0;render();return;
  }
  const auto &step=activeMacro.steps[macroStep];
  if (!store::loadSignal(step.remote,step.button,activeSignal)) {
    Serial.printf("[MACRO] missing Remote %u / Button %u\n",step.remote+1,step.button+1);
    macroRunning=false;saveMessage="Missing macro button; see Serial";
    resultBack=Page::MacroMenu;page=Page::SaveResult;render();return;
  }
  prepareSavedTx(activeSignal);
  Serial.printf("[MACRO] step %u/%u R%u B%u delay=%u ms\n",macroStep+1,
    activeMacro.stepCount,step.remote+1,step.button+1,step.delayAfterMs);
  replay(activeSignal); // Same known-good replay path as SavedButton/Test.
  macroNextAt=millis()+step.delayAfterMs;
  ++macroStep;render();
}

void serialLine(const char *line, void *) {Serial.println(line);}
void exportBackup() {
  Serial.println("[EXPORT] BEGIN");
  const bool okay=backup::exportAll(serialLine,nullptr);
  Serial.println("[EXPORT] END");
  saveMessage=okay?"Exported on USB Serial":"Export failed; see Serial";
  resultBack=Page::Settings;page=Page::SaveResult;render();
}

void serviceImport() {
  if (M5.BtnB.wasPressed()) {
    Serial.printf("[IMPORT] cancelled; committed=%u errors=%u\n",importer.imported(),importer.errors());
    page=Page::Settings;selection=0;render();return;
  }
  while (Serial.available()) {
    importLastAt=millis();
    if (!importer.feed(char(Serial.read())))
      Serial.printf("[IMPORT] rejected record; errors=%u\n",importer.errors());
    if (importer.complete()) {
      Serial.printf("[IMPORT] complete records=%u errors=%u\n",importer.imported(),importer.errors());
      saveMessage=importer.errors()?"Imported with errors; see Serial":"Import complete";
      resultBack=Page::Settings;page=Page::SaveResult;render();return;
    }
  }
  if (millis()-importLastAt>30000) {
    Serial.printf("[IMPORT] timeout; partial records=%u errors=%u\n",importer.imported(),importer.errors());
    importLastAt=millis();
  }
}
} // namespace

void setup() {
  auto config = M5.config();
  config.output_power = true;
  config.internal_spk = false;
  M5.begin(config);
  M5.Speaker.end();
  Serial.begin(115200);
  delay(250);
  Serial.println("[BOOT] IR Learner StickS3 v1.5 utilities");
  M5.Display.setRotation(3);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Serial.printf("[BOOT] PSRAM %s; size=%u bytes; free internal=%u bytes\n",
                psramFound() ? "detected" : "NOT detected (using internal RAM)",
                static_cast<unsigned>(ESP.getPsramSize()),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  Serial.println("[BOOT] Display initialized, speaker disabled");
  store::begin();
  macros::begin();
  render();
}

void loop() {
  M5.update();
  handleRx();
  serviceHold();
  if (page == Page::Waiting && rxArmed && millis() - waitingSince > kWaitTimeoutMs) {
    Serial.printf("[RX] Timeout after %lu ms; still waiting for signal\n", static_cast<unsigned long>(kWaitTimeoutMs));
    waitingSince = millis();
  }
  if (page == Page::Home) {
    if (M5.BtnB.wasPressed()) { selection = (selection + 1) % 6; render(); }
    if (M5.BtnA.wasPressed()) {
      if (selection == 0) learn();
      else {
        page = selection == 1 ? Page::Remotes : selection == 2 ? Page::MacroList :
               selection == 3 ? Page::TvMenu : selection == 4 ? Page::Settings : Page::About;
        selection = 0;
        render();
      }
    }
  } else if (page == Page::Captured) {
    if (M5.BtnA.wasPressed()) { capturedAPressedAt = millis(); capturedAPending = true; }
    if (capturedAPending && M5.BtnA.isPressed() && millis() - capturedAPressedAt >= 800) {
      capturedAPending = false;
      page = Page::SaveRemote; selection = 0; render();
    } else if (capturedAPending && M5.BtnA.wasReleased()) {
      capturedAPending = false;
      replay(activeSignal);
    }
    if (decoded.valid) {
      if (M5.BtnB.wasPressed()) { rawBPressedAt = millis(); rawBPending = true; }
      if (rawBPending && M5.BtnB.isPressed() && millis() - rawBPressedAt >= 800) {
        rawBPending = false; page = Page::Home; selection = 0; render();
      } else if (rawBPending && M5.BtnB.wasReleased()) {
        rawBPending = false; learn();
      }
    } else {
      // Decide on release: a long press must not also cycle the carrier.
      if (M5.BtnB.wasPressed()) {
        rawBPressedAt = millis();
        rawBPending = true;
      }
      if (rawBPending && M5.BtnB.isPressed() && millis() - rawBPressedAt >= 800) {
        rawBPending = false;
        learn();
      } else if (rawBPending && M5.BtnB.wasReleased()) {
        rawBPending = false;
        rawCarrierIndex = (rawCarrierIndex + 1) % 3;
        activeSignal.carrierHz = kRawCarrierOptionsHz[rawCarrierIndex];
        Serial.printf("[RAW] Carrier selected: %lu Hz\n",
                      static_cast<unsigned long>(kRawCarrierOptionsHz[rawCarrierIndex]));
        render();
      }
    }
  } else if (page == Page::Waiting) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      // RMT RX is one-shot. Do not reuse its buffer until any pending receive ends.
      if (rxArmed) check(rmt_disable(rxChannel), "rmt_disable RX cancel");
      rxArmed = false;
      hardwareReady = false;
      // Return through reset to guarantee a clean channel if the user cancels.
      ESP.restart();
    }
  } else if (page == Page::TvSending) {
    if (M5.BtnB.wasPressed()) {
      Serial.println("[TVBGONE] cancelled");
      tvRunning = false; page = Page::TvMenu; selection = 0; render();
    } else sendNextTvCode();
  } else if (page == Page::MacroRunning) {
    serviceMacro();
  } else if (page == Page::Rename) {
    serviceRename();
  } else if (page == Page::Import) {
    serviceImport();
  } else if (page == Page::About || page == Page::Error) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      page = Page::Home; selection = 0; render();
    }
  } else if (page == Page::SaveResult) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      page = resultBack; selection = 0; render();
    }
  } else {
    const int options = page == Page::Overwrite || page == Page::EraseConfirm ? 2 :
                        page == Page::RemoteActions || page == Page::TvMenu ? 3 :
                        page == Page::Settings || page == Page::SavedButton ? 4 :
                        page == Page::MacroEdit ? activeMacro.stepCount+1+
                                               (activeMacro.stepCount<macros::kMaxSteps?1:0) : 5;
    if (M5.BtnB.wasPressed()) { selection = (selection + 1) % options; render(); }
    if (M5.BtnA.wasPressed()) {
      switch (page) {
        case Page::Remotes:
        case Page::SaveRemote:
          if (selection == 4) {
            page = page == Page::Remotes ? Page::Home : Page::Captured;
            selection = 0;
          } else {
            selectedRemote = selection;
            page = page == Page::Remotes ? Page::RemoteActions : Page::SaveButton;
            selection = 0;
          }
          break;
        case Page::RemoteActions:
          if (selection==0) {page=Page::Buttons;selection=0;}
          else if (selection==1) startRename(RenameTarget::Remote,Page::RemoteActions);
          else {page=Page::Remotes;selection=0;}
          break;
        case Page::Buttons:
          if (selection == 4) { page = Page::RemoteActions; selection = 0; }
          else {
            selectedButton = selection;
            if (store::loadSignal(selectedRemote, selectedButton, activeSignal)) {
              prepareSavedTx(activeSignal);
              page = Page::SavedButton;
              selection = 0;
            } else Serial.println("[STORE] empty/invalid button");
          }
          break;
        case Page::SaveButton:
          if (selection == 4) { page = Page::SaveRemote; selection = 0; }
          else {
            selectedButton = selection;
            if (store::loadSignal(selectedRemote, selectedButton, slotScratch)) {
              page = Page::Overwrite; selection = 1; // Default No.
            } else saveSelectedSlot();
          }
          break;
        case Page::Overwrite:
          if (selection == 0) saveSelectedSlot();
          else { page = Page::SaveButton; selection = 0; }
          break;
        case Page::SavedButton:
          if (selection == 0) {
            // Initial known-good press remains unchanged; hold is armed after it.
            holdPolicy=hold::forSignal(activeSignal);
            const uint32_t firstStartUs=micros();
            replay(activeSignal);
            savedAPending=true;holdActive=false;
            // Sony's normal Test sent three frames at 0/45/90 ms. The next
            // held frame starts at ~135 ms; NEC repeat begins at ~110 ms.
            holdNextUs=firstStartUs+hold::firstRepeatOffsetUs(holdPolicy);
          } else if (selection == 1) startRename(RenameTarget::Button,Page::SavedButton);
          else if (selection == 2) {
            if (store::deleteSignal(selectedRemote, selectedButton)) {
              page = Page::Buttons; selection = 0;
            }
          } else { page = Page::Buttons; selection = 0; }
          break;
        case Page::Settings:
          if (selection==0) exportBackup();
          else if (selection==1) {
            importer.reset();
            while (Serial.available()) Serial.read(); // Flush prior serial log/commands.
            importLastAt=millis();page=Page::Import;selection=0;
            Serial.println("[IMPORT] BEGIN; send IRCITO-BACKUP|1 through END as lines");
          } else if (selection==2) {page=Page::EraseConfirm;selection=1;}
          else {page=Page::Home;selection=0;}
          break;
        case Page::EraseConfirm:
          if (selection == 0) {
            saveMessage = store::eraseAll() ? "Learned data erased." : "Erase failed; see Serial.";
            resultBack = Page::Settings; page = Page::SaveResult; selection = 0;
          } else { page = Page::Settings; selection = 0; }
          break;
        case Page::TvMenu:
          if (selection == 0) {
            if (initIr()) {
              tvPosition = 0; tvNextAt = millis(); tvRunning = true;
              page = Page::TvSending;
              Serial.printf("[TVBGONE] start region=%s codes=%u\n", tvbgone::name(tvRegion),
                            unsigned(tvbgone::count(tvRegion)));
            } else page = Page::Error;
          } else if (selection == 1) {
            tvRegion = tvRegion == tvbgone::Region::NorthAmerica ?
                       tvbgone::Region::Europe : tvbgone::Region::NorthAmerica;
          } else { page = Page::Home; selection = 0; }
          break;
        case Page::MacroList:
          if (selection==4) {page=Page::Home;selection=0;}
          else {
            selectedMacro=uint8_t(selection);
            macros::load(selectedMacro,activeMacro);
            page=Page::MacroMenu;selection=0;
          }
          break;
        case Page::MacroMenu:
          if (selection==0) startMacro();
          else if (selection==1) {page=Page::MacroEdit;selection=0;}
          else if (selection==2) startRename(RenameTarget::Macro,Page::MacroMenu);
          else if (selection==3) {
            if (macros::remove(selectedMacro)) {
              macros::defaults(selectedMacro,activeMacro);
              page=Page::MacroList;selection=0;
            }
          } else {page=Page::MacroList;selection=0;}
          break;
        case Page::MacroEdit:
          if (selection==activeMacro.stepCount+(activeMacro.stepCount<macros::kMaxSteps?1:0))
            {page=Page::MacroMenu;selection=0;}
          else {
            selectedStep=uint8_t(selection);
            if (selectedStep<activeMacro.stepCount) {
              const auto &s=activeMacro.steps[selectedStep];
              editRemote=s.remote;editButton=s.button;editDelay=0;
              while (editDelay<4 && kDelays[editDelay]!=s.delayAfterMs) ++editDelay;
            } else {editRemote=editButton=editDelay=0;}
            page=Page::MacroStep;selection=0;
          }
          break;
        case Page::MacroStep:
          if (selection==0) editRemote=(editRemote+1)%store::kRemotes;
          else if (selection==1) editButton=(editButton+1)%store::kButtons;
          else if (selection==2) editDelay=(editDelay+1)%5;
          else if (selection==3) {
            if (!store::loadSignal(editRemote,editButton,slotScratch)) {
              Serial.printf("[MACRO] choose an existing Remote %u / Button %u\n",
                editRemote+1,editButton+1);
              break;
            }
            macros::Macro candidate=activeMacro;
            candidate.steps[selectedStep]={editRemote,editButton,kDelays[editDelay]};
            if (selectedStep==candidate.stepCount) ++candidate.stepCount;
            if (macros::save(selectedMacro,candidate)) {
              activeMacro=candidate;page=Page::MacroEdit;selection=0;
            } else Serial.println("[MACRO] step save failed; original retained");
          } else {
            if (selectedStep<activeMacro.stepCount) {
              macros::Macro candidate=activeMacro;
              for (uint8_t i=selectedStep+1;i<candidate.stepCount;++i)
                candidate.steps[i-1]=candidate.steps[i];
              --candidate.stepCount;
              if (macros::save(selectedMacro,candidate)) activeMacro=candidate;
              else Serial.println("[MACRO] delete step failed; original retained");
            }
            page=Page::MacroEdit;selection=0;
          }
          break;
        default: break;
      }
      render();
    }
  }
  delay(10);
}
