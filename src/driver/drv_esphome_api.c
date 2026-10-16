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
	
	static int is_pulsing = 0;
	static int pulse_timer = 0;
	static int cooldown_timer = 0;
	static int led_state = 0;
	static int blink_div = 0;

	int found = 0;
	int i, val_ch11, val_ch17, val_ch18, current_mac;
	int soglia_rssi = -85;
	int pulse_duration = 250;    // ~10 secondi reali
	int cooldown_duration = 1500; // ~60 secondi di cooldown

	(void)client_sock;
	(void)addr_type;

	// GUARDIA WI-FI FONDAMENTALE: Finché il Canale 11 è a 0 (durante i primi 12s di boot),
	// il codice C si ferma e lascia l'antenna libera di collegarsi al Wi-Fi.
	val_ch11 = CHANNEL_Get(11);
	if (val_ch11 == 0) return false;
	soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

	// Lettura durata impulso da Canale 17 (configurabile)
	val_ch17 = CHANNEL_Get(17);
	if (val_ch17 > 0) pulse_duration = val_ch17 * 25;

	// Lettura cooldown da Canale 18 (configurabile)
	val_ch18 = CHANNEL_Get(18);
	if (val_ch18 > 0) cooldown_duration = val_ch18 * 25;

	// Gestione Timer in background
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
		return false; // Durante il cooldown non fa nulla
	}

	if (data != NULL) {
		if (mac != NULL) {
			current_mac = (mac[3] << 16) | (mac[4] << 8) | mac[5];
			if ((CHANNEL_Get(12) != 0 && current_mac == CHANNEL_Get(12)) ||
				(CHANNEL_Get(13) != 0 && current_mac == CHANNEL_Get(13)) ||
				(CHANNEL_Get(14) != 0 && current_mac == CHANNEL_Get(14)) ||
				(CHANNEL_Get(15) != 0 && current_mac == CHANNEL_Get(15))) {
				found = 1;
			}
		}

		if (found == 0 && data_len >= 16) {
			for (i = 0; i <= data_len - 16; i++) {
				if (memcmp(&data[i], my_uuid, 16) == 0) {
					found = 1;
					break;
				}
			}
		}

		if (found == 1 && rssi > soglia_rssi) {
			// Dispositivo riconosciuto: attiva l'impulso relè a pulsante
			is_pulsing = 1;
			pulse_timer = pulse_duration;
			cooldown_timer = cooldown_duration + pulse_duration;

			CHANNEL_Set(0, 1, 0);  // Eccita il relè
			CHANNEL_Set(10, 1, 0); // Accende il LED fisso
		} else {
			// Standby: Lampeggio silenzioso del LED (usando flag 2 per non intasare MQTT)
			blink_div++;
			if (blink_div >= 15) {
				blink_div = 0;
				led_state = !led_state;
				CHANNEL_Set(10, led_state, 2); // Flag 2 = silenzioso
			}
		}
	}
	return false;
}

#endif
