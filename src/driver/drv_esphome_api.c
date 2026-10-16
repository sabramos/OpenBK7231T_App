#include "../new_common.h"
#include "../logging/logging.h"
#include "drv_esphome_api.h"
#include "../hal/hal_wifi.h"
#include <lwip/sockets.h>
#include "../new_cfg.h"
#include "../hal/hal_bt_proxy.h"
#include "../hal/hal_pins.h"

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
	int pulse_duration = 100;    // Default ~10 secondi (calcolato sui pacchetti o cicli)
	int cooldown_duration = 600; // Default ~60 secondi di attesa

	(void)client_sock;
	(void)addr_type;

	// Guardia Wi-Fi iniziale (finche' Canale 11 e' a 0, ignora tutto)
	val_ch11 = CHANNEL_Get(11);
	if (val_ch11 == 0) return false;
	soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

	// Lettura durata impulso da Canale 17 (configurabile)
	val_ch17 = CHANNEL_Get(17);
	if (val_ch17 > 0) pulse_duration = val_ch17 * 10; // Es. 10 secondi = ~100 cicli di scansione

	// Lettura cooldown da Canale 18 (configurabile)
	val_ch18 = CHANNEL_Get(18);
	if (val_ch18 > 0) cooldown_duration = val_ch18 * 10; // Es. 60 secondi = ~600 cicli

	// Gestione Timer Cooldown e Impulso in background (chiamato a ogni pacchetto BLE)
	if (cooldown_timer > 0) {
		cooldown_timer--;
	}

	if (is_pulsing == 1) {
		pulse_timer--;
		if (pulse_timer <= 0) {
			is_pulsing = 0;
			CHANNEL_Set(0, 0, 0);   // Spegne il relè
			CHANNEL_Set(10, 0, 4);  // Spegne il LED di stato
		}
		return false; // Durante l'impulso o il cooldown non analizza nuovi MAC
	}

	if (cooldown_timer > 0) {
		// In pausa di rispetto: fa solo lampeggiare silenziosamente il LED
		blink_div++;
		if (blink_div >= 10) {
			blink_div = 0;
			led_state = !led_state;
			// Controllo diretto del pin LED (es. Pin 10 o simile tramite hal_pins se configurato, 
			// oppure usiamo CHANNEL_Set con flag silenzioso ma meno frequente)
		}
		return false;
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
			// Dispositivo riconosciuto e vicino: attiva l'impulso relè!
			is_pulsing = 1;
			pulse_timer = pulse_duration;
			cooldown_timer = cooldown_duration + pulse_duration; // Inizia il cooldown

			CHANNEL_Set(0, 1, 0);  // Eccita il relè
			CHANNEL_Set(10, 1, 4); // Accende il LED fisso (senza log MQTT)
		} else {
			// Standby: lampeggio silenzioso del LED senza sporcare i log
			blink_div++;
			if (blink_div >= 12) {
				blink_div = 0;
				led_state = !led_state;
				// Aggiornamento pin LED senza generare log testuali continui
				// (manteniamo unCHANNEL_Set sporadico ogni 12 pacchetti per evitare lo spam a schermo)
				CHANNEL_Set(10, led_state, 2); 
			}
		}
	}
	return false;
}

#endif
