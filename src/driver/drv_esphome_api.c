#include "../new_common.h"
#include "../logging/logging.h"
#include "drv_esphome_api.h"
#include "../hal/hal_wifi.h"
#include <lwip/sockets.h>
#include "../new_cfg.h"
#include "../hal/hal_bt_proxy.h"

#if ENABLE_DRIVER_ESPHOME_API

extern void CHANNEL_Set(int ch, int val, int iFlags);
extern int CHANNEL_Get(int ch);

void ESPHome_API_Send_HelloResponse(int s) { (void)s; }
void ESPHome_API_Send_ConnectResponse(int s) { (void)s; }
void ESPHome_API_Send_PingResponse(int s) { (void)s; }
void ESPHome_API_Send_DeviceInfoResponse(int s) { (void)s; }
void ESPHome_API_Send_ListEntitiesDoneResponse(int s) { (void)s; }
void ESPHome_API_Send_DisconnectResponse(int s) { (void)s; }
void ESPHome_API_Send_ScannerStateResponse(int s) { (void)s; }
void ESPHome_API_Send_ConnectionsFreeResponse(int s) { (void)s; }
void ESPHome_API_Process_Packet(int s, uint32_t t, uint8_t* p, uint32_t sz) {
	(void)s; (void)t; (void)p; (void)sz;
}

bool ESPHome_API_Hook_ScanResult(int client_sock, const uint8_t* mac, int rssi, uint8_t addr_type, const uint8_t* data, int data_len)
{
	uint8_t my_uuid[16] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x78,0x90,0x12,0x34,0x56,0x78,0x90,0xAB,0xCD,0xEF};
	
	static int boot_delay_counter = 0;
	static int is_pulsing = 0;
	static int pulse_timer = 0;
	static int cooldown_timer = 0;
	static int led_state = 0;
	static int blink_div = 0;
	static int debug_log_div = 0;

	int found = 0;
	int i, val_ch11, val_ch17, val_ch18;
	int mac_dritto = 0, mac_invertito = 0;
	int soglia_rssi = -85;
	int pulse_duration = 250;    // ~10 secondi reali
	int cooldown_duration = 1500; // ~60 secondi di cooldown

	(void)client_sock;
	(void)addr_type;

	// Guardia Wi-Fi iniziale (20s di boot)
	if (boot_delay_counter < 200) {
		boot_delay_counter++;
		return false;
	}

	val_ch11 = CHANNEL_Get(11);
	if (val_ch11 == 0) return false;
	soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

	val_ch17 = CHANNEL_Get(17);
	if (val_ch17 > 0) pulse_duration = val_ch17 * 25;

	val_ch18 = CHANNEL_Get(18);
	if (val_ch18 > 0) cooldown_duration = val_ch18 * 25;

	if (cooldown_timer > 0) {
		cooldown_timer--;
	}

	if (is_pulsing == 1) {
		pulse_timer--;
		if (pulse_timer <= 0) {
			is_pulsing = 0;
			CHANNEL_Set(0, 0, 0);   // Spegne il relè
			CHANNEL_Set(10, 0, 0);  // Spegne il LED
		}
		return false;
	}

	if (cooldown_timer > 0) {
		return false;
	}

	// Lampeggio LED in standby (stile scheda di rete)
	blink_div++;
	if (blink_div >= 6) {
		blink_div = 0;
		led_state = !led_state;
		CHANNEL_Set(10, led_state, 2); // Flag 2 = Silenzioso
	}

	// 1. Verifica riscontro MAC Address
	if (mac != NULL) {
		mac_dritto = (mac[3] << 16) | (mac[4] << 8) | mac[5];
		mac_invertito = (mac[2] << 16) | (mac[1] << 8) | mac[0];

		// Log di debug ogni tot pacchetti per vedere il valore calcolato in decimale
		debug_log_div++;
		if (debug_log_div >= 30) {
			debug_log_div = 0;
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE Check: MAC %02X:%02X:%02X:%02X:%02X:%02X | Dritto: %d | Invertito: %d", 
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac_dritto, mac_invertito);
		}

		if ((CHANNEL_Get(12) != 0 && (mac_dritto == CHANNEL_Get(12) || mac_invertito == CHANNEL_Get(12))) ||
			(CHANNEL_Get(13) != 0 && (mac_dritto == CHANNEL_Get(13) || mac_invertito == CHANNEL_Get(13))) ||
			(CHANNEL_Get(14) != 0 && (mac_dritto == CHANNEL_Get(14) || mac_invertito == CHANNEL_Get(14))) ||
			(CHANNEL_Get(15) != 0 && (mac_dritto == CHANNEL_Get(15) || mac_invertito == CHANNEL_Get(15)))) {
			found = 1;
		}
	}

	// 2. Verifica riscontro Advertiser UUID (se il MAC non ha dato match)
	if (found == 0 && data != NULL && data_len >= 16) {
		for (i = 0; i <= data_len - 16; i++) {
			if (memcmp(&data[i], my_uuid, 16) == 0) {
				found = 1;
				break;
			}
		}
	}

	// Se trovato e con segnale superiore alla soglia RSSI: attiva l'impulso relè!
	if (found == 1 && rssi > soglia_rssi) {
		is_pulsing = 1;
		pulse_timer = pulse_duration;
		cooldown_timer = cooldown_duration + pulse_duration;

		CHANNEL_Set(0, 1, 0);  // Relè ON (impulso)
		CHANNEL_Set(10, 1, 0); // LED Fisso
		ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! Relè attivato per RSSI %d", rssi);
	}

	return false;
}

#endif
