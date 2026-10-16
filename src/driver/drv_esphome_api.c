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

// Funzioni di interfaccia richieste dal linker (inattive in modalita' standalone)
void ESPHome_API_Send_HelloResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_ConnectResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_PingResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_DeviceInfoResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_ListEntitiesDoneResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_DisconnectResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_ScannerStateResponse(int client_sock) { (void)client_sock; }
void ESPHome_API_Send_ConnectionsFreeResponse(int client_sock) { (void)client_sock; }

void ESPHome_API_Process_Packet(int client_sock, uint32_t type, uint8_t* payload, uint32_t size)
{
	(void)client_sock;
	(void)type;
	(void)payload;
	(void)size;
}

// Gestione scansione BLE: Prossimita', Soglia Dinamica, Rilascio Rapido e LED Attivita'
bool ESPHome_API_Hook_ScanResult(int client_sock, const uint8_t* mac, int rssi, uint8_t addr_type, const uint8_t* data, int data_len)
{
	uint8_t my_uuid[16] = {
		0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x78, 0x90,
		0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF
	};

	static int is_present = 0;
	static int timeout_counter = 0;
	static int led_state = 0;
	static int blink_div = 0;

	int found = 0;
	int i;
	int soglia_rssi = -85;
	int max_timeout = 80; // Default rilascio rapido (~2-4 secondi)
	int val_ch11;
	int val_ch16;
	int mac_target_1;
	int mac_target_2;
	int mac_target_3;
	int mac_target_4;
	int current_mac;

	(void)client_sock;
	(void)addr_type;

	if (data != NULL) {
		// Lettura dei 4 slot MAC Address dai Canali 12, 13, 14 e 15
		mac_target_1 = CHANNEL_Get(12);
		mac_target_2 = CHANNEL_Get(13);
		mac_target_3 = CHANNEL_Get(14);
		mac_target_4 = CHANNEL_Get(15);

		// 1. Verifica corrispondenza ultimi 3 byte del MAC Address
		if (mac != NULL) {
			current_mac = (mac[3] << 16) | (mac[4] << 8) | mac[5];

			if ((mac_target_1 != 0 && current_mac == mac_target_1) ||
				(mac_target_2 != 0 && current_mac == mac_target_2) ||
				(mac_target_3 != 0 && current_mac == mac_target_3) ||
				(mac_target_4 != 0 && current_mac == mac_target_4)) {
				found = 1;
			}
		}

		// 2. Verifica corrispondenza UUID nel pacchetto (per smartphone)
		if (found == 0 && data_len >= 16) {
			for (i = 0; i <= data_len - 16; i++) {
				if (memcmp(&data[i], my_uuid, 16) == 0) {
					found = 1;
					break;
				}
			}
		}

		// Lettura soglia RSSI dal Canale 11
		val_ch11 = CHANNEL_Get(11);
		if (val_ch11 != 0) {
			soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;
		}

		// Lettura durata di rilascio dal Canale 16
		val_ch16 = CHANNEL_Get(16);
		if (val_ch16 > 0) {
			max_timeout = val_ch16;
		}

		// 3. Controllo Uscite: Canale 0 (Rele') e Canale 10 (LED)
		if (found == 1 && rssi > soglia_rssi) {
			timeout_counter = max_timeout;

			if (is_present == 0) {
				is_present = 1;
				CHANNEL_Set(0, 1, 0); // Eccita il rele'
			}

			// Forza il LED fisso acceso mentre il rele' e' attivo
			if (led_state != 1) {
				led_state = 1;
				CHANNEL_Set(10, 1, 0);
			}
		} else {
			if (is_present == 1) {
				// Dispositivo assente o fuori portata: conteggio alla rovescia per il rilascio
				if (timeout_counter > 0) {
					timeout_counter--;
				} else {
					is_present = 0;
					led_state = 0;
					CHANNEL_Set(0, 0, 0);  // Rilascia il rele'
					CHANNEL_Set(10, 0, 0); // Spegne il LED
				}
			} else {
				// Rele' a riposo: lampeggio del LED sul traffico Bluetooth generico
				blink_div++;
				if (blink_div >= 2) {
					blink_div = 0;
					led_state = !led_state;
					CHANNEL_Set(10, led_state, 0);
				}
			}
		}
	}

	return false;
}

#endif // ENABLE_DRIVER_ESPHOME_API
