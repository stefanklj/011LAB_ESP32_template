/*Info:
 Ovo je templejt koji u sebi sadrzi:
 GPIO;
 EXTINT,
 WIFI konekciju,
 Telnet,
 Tajmer0 za merenje vremena,
 delay u mikrosekundama(posto sve ispod 20ms ne radi dobro),i
 OTA upload firmvera, komanda za upload je: make ota ESP32_IP=192.168.0.29 (ip adresu proveriti)
 koji menja particije u koje upisuje firmver kako ne bi zaustavio rad trenutnog firmvera.

 ---------------------------------------------------------------------------------------

 U make menuconfig moze se podestiti WIFI mreza i njena sifra, ulazni pin ext interrupta i izlazni pin.
 Posle kucanja make, dodati i -j5 kako bi se koristilo vise jezgara za kompajliranje.
 Neki include fajlovi se ne nalaze u folderu projekta vec u IDF-PATH, bar ja tako mislim, treba proveriti.
 Prioriteti taskova, sto je veci broj, veci je i prioritet.
 28.2.2018.

 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_types.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "rom/ets_sys.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sys/param.h"
#include "esp_log.h"
#include "ota_server.h"
#include "msgdef.h"  //ovde ima typedef neki uklopiti to lepo

#define ESP_INTR_FLAG_DEFAULT 0
#define TIMER_DIVIDER         8
#define TEST_WITHOUT_RELOAD   0
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD
#define TELNET_PORT 23

#define triger_pin_sensor1 13
#define echo_pin_sensor1 9
#define triger_pin_sensor2 14
#define echo_pin_sensor2 12

typedef struct {
	int type;  // the type of timer's event
	int timer_group;
	int timer_idx;
	uint64_t timer_counter_value;
} timer_event_t;

timer_event_t timer_event;
static SemaphoreHandle_t xTelnetSemaphore;
static QueueHandle_t xTelnetQueue;
static EventGroupHandle_t wifi_event_group;
static volatile bool Telnet_Initialized = false;
static volatile bool Telnet_Connnected = false;
static const char *TAG = "Telnet";
const int CONNECTED_BIT = BIT0;
uint64_t echo1_time;
uint64_t echo2_time;
bool triger1_state = false;
bool triger2_state = false;
bool received_echo1_flag = true;
bool received_echo2_flag = true;
bool Print_flag = false;
int broj_socketa = -1;

static esp_err_t event_handler(void *ctx, system_event_t *event) {

	switch (event->event_id) {
	case SYSTEM_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		/* This is a workaround as ESP32 WiFi libs don't currently
		 auto-reassociate. */
		Telnet_Connnected = false;
		esp_wifi_connect();
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		break;
	default:
		break;
	}
	return ESP_OK;
}

static void initialise_wifi(void) {
	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK (esp_wifi_set_storage(WIFI_STORAGE_RAM));wifi_config_t
	wifi_config = { .sta = { .ssid = EXAMPLE_WIFI_SSID, .password =
	EXAMPLE_WIFI_PASS, }, };
	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...",
			wifi_config.sta.ssid);
	ESP_ERROR_CHECK (esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK
	(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK (esp_wifi_start());}

static void err_handler(void) {
	while (1) {
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
}

static void ota_server_task(void * param) {
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true,
			portMAX_DELAY);
	ota_server_start();
	vTaskDelete (NULL);
}

static esp_err_t get_telnet_msg(telnet_msg_t *pMsg) {
	if (!pMsg)
		return ESP_ERR_INVALID_ARG;

	if (xSemaphoreTake(xTelnetSemaphore, (TickType_t) 10) == pdTRUE) {
		/* We were able to obtain the semaphore and can now access the
		 shared resource. */
		if (xQueueReceive(xTelnetQueue, pMsg, (TickType_t) 10) != pdPASS) {
			/* We have finished accessing the shared resource.  Release the
			 semaphore. */
			xSemaphoreGive(xTelnetSemaphore);
			return ESP_ERR_INVALID_RESPONSE;
		}
		/* We have finished accessing the shared resource.  Release the
		 semaphore. */
		xSemaphoreGive(xTelnetSemaphore);
	}

	return ESP_OK;
}

static void telnet_task(void *pvParameters) {
	char buffer[5 * 1024] = { 0 };
	esp_err_t err = 0;

	(void) pvParameters;

	// Init
	xTelnetQueue = xQueueCreate(TELNET_MAX_Q_ELEMENTS, TELNET_MSG_SIZE);
	if (xTelnetQueue == NULL) {
		ESP_LOGE(TAG, "Failed to create telnet msg queue. Error 0x%x", err);
		err_handler();
	}

	xTelnetSemaphore = xSemaphoreCreateMutex();

	if (xTelnetSemaphore == NULL) {
		ESP_LOGE(TAG, "Failed to create telnet queue mutex.");
		err_handler();
	}

	Telnet_Initialized = true;

	// loop
	while (1) {
		/* Wait for the callback to set the CONNECTED_BIT in the
		 event group.
		 */
		xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true,
				portMAX_DELAY);

		struct in_addr iaddr = { 0 };
		tcpip_adapter_ip_info_t ip_info = { 0 };

		err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to get IP address info. Error 0x%x", err);
			err_handler();
		}

		inet_addr_from_ipaddr(&iaddr, &ip_info.ip);
		ESP_LOGI(TAG, "IP address %s", inet_ntoa(iaddr.s_addr));

		// init
		int sock = -1;

		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) {
			ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
			err_handler();
		}

		int value = 1;
		err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
		if (err < 0) {
			ESP_LOGE(TAG, "Failed to set SO_REUSEADDR. Error %d", errno);
			close(sock);
			continue;
		}

		struct sockaddr_in server;
		// Prepare the sockaddr_in structure
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = INADDR_ANY;
		server.sin_port = htons( TELNET_PORT);

		// Bind
		err = bind(sock, (struct sockaddr *) &server, sizeof(server));
		if (err < 0) {
			ESP_LOGE(TAG, "Failed to bind. Error %d", err);
			close(sock);
			continue;
		}

		// Listen for 1 connection
		err = listen(sock, 1);
		if (err < 0) {
			ESP_LOGE(TAG, "Failed to listen. Error %d", err);
			close(sock);
			continue;
		}

		ESP_LOGI(TAG, "Waiting for client on port %d", TELNET_PORT);
		//accept connection from an incoming client
		struct sockaddr_in client;
		int c_len = 0;
		int client_sock = accept(sock, (struct sockaddr *) &client,
				(socklen_t *) &c_len);
		broj_socketa = client_sock;

		if (client_sock < 0) {
			ESP_LOGE(TAG, "Failed to accept. Error %d", err);
			close(sock);
			continue;
		}
		ESP_LOGI(TAG, "Connection accepted.");
		Telnet_Connnected = true;

		err = lwip_fcntl_r(client_sock, F_SETFL, O_NONBLOCK);
		if (err < 0) {
			ESP_LOGE(TAG, "Failed to set socket to non blocking. Error %d",
					err);
		}

		// Receive message from client
		int read_size = 0;
		while (1) {
			read_size = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

			if (read_size > 0) {
				ESP_LOGI(TAG, "%d bytes received", read_size);
				// Process message

				// Send response to client
				write(client_sock, buffer, strlen(buffer));
			} else if (read_size != 0) {
				telnet_msg_t msg = { 0 };
				if (get_telnet_msg(&msg) == ESP_OK) {
					write(client_sock, msg.pSenderTag, strlen(msg.pSenderTag));
					write(client_sock, msg.pData, msg.length);

					if (msg.isMalloced)
						free(msg.pData);
				}
			}

			if (read_size == 0) {
				ESP_LOGI(TAG, "Client disconnected. Goodbye!");
				Telnet_Connnected = false;
				close(client_sock);
				close(sock);
				break;
			} else if (read_size == -1) {
				//ESP_LOGE(TAG, "Failed to receive. Error %d", err);
			}
		}

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

static void example_tg0_timer_init(int timer_idx, bool auto_reload) {
	/* Select and initialize basic parameters of the timer */
	timer_config_t config;
	config.divider = TIMER_DIVIDER;
	config.counter_dir = TIMER_COUNT_UP;
	config.counter_en = TIMER_PAUSE;
	config.alarm_en = TIMER_ALARM_DIS;
	config.intr_type = TIMER_INTR_LEVEL;
	config.auto_reload = auto_reload;
	timer_init(TIMER_GROUP_0, timer_idx, &config);

	/* Timer's counter will initially start from value below.
	 Also, if auto_reload is set, this value will be automatically reload on alarm */
	timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000000);

	timer_start(TIMER_GROUP_0, timer_idx);
}

// interrupt service routine, called when the button is pressed
void IRAM_ATTR echo_pin_sensor1_handler(void* arg) {

	if (gpio_get_level(echo_pin_sensor1)) {

		timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000000);
	}

	else if (!(gpio_get_level(echo_pin_sensor1))) {
		timer_get_counter_value(timer_event.timer_group, timer_event.timer_idx,
				&echo1_time);
		received_echo1_flag = true;
	}
}

void IRAM_ATTR echo_pin_sensor2_handler(void* arg) {

	if (gpio_get_level(echo_pin_sensor2)) {

		timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000000);
	}

	else if (!(gpio_get_level(echo_pin_sensor2))) {
		timer_get_counter_value(timer_event.timer_group, timer_event.timer_idx,
				&echo2_time);
		received_echo2_flag = true;
	}
}

// task that will react to button clicks
void triger_task() {

	while (1) {

		triger1_state = 1;
		gpio_set_level(triger_pin_sensor1, triger1_state);
		ets_delay_us(10);
		triger1_state = 0;
		gpio_set_level(triger_pin_sensor1, triger1_state);
		received_echo1_flag = false;
		Print_flag = true;

		vTaskDelay(100 / portTICK_RATE_MS);

		triger2_state = 1;
		gpio_set_level(triger_pin_sensor2, triger2_state);
		ets_delay_us(10);
		triger2_state = 0;
		gpio_set_level(triger_pin_sensor2, triger2_state);
		received_echo2_flag = false;

		vTaskDelay(100 / portTICK_RATE_MS);

	}
}

void Print_task() {
	char daljina_niz[100];
	int distance1 = 0;
	int distance2 = 0;

	while (1) {
		vTaskDelay(300 / portTICK_RATE_MS);
		if (Print_flag) {

			distance1 = (int) echo1_time / 580; //konvert u us
			distance2 = (int) echo2_time / 580; //konvert u us
			printf(" L %d cm  |   R %d cm\r\n", distance1, distance2);

			// if (broj_socketa > -1) {
			// 	sprintf(daljina_niz, "Sensor L %d cm  |  Sensor R %d cm\r\n",distance1,distance2);
			// 	write(broj_socketa, daljina_niz, strlen(daljina_niz));//TODO write prepravi u printf_telnet();
			// }

			Print_flag = false;

		}
	}
}

void app_main() {

	ESP_ERROR_CHECK (nvs_flash_init());initialise_wifi();

	example_tg0_timer_init(TIMER_0, TEST_WITHOUT_RELOAD);

// configure button and led pins as GPIO pins
	gpio_pad_select_gpio(triger_pin_sensor1);
	gpio_pad_select_gpio(echo_pin_sensor1);
	gpio_pad_select_gpio(triger_pin_sensor2);
	gpio_pad_select_gpio(echo_pin_sensor2);
// set the correct direction
	gpio_set_direction(triger_pin_sensor1, GPIO_MODE_OUTPUT);
	gpio_set_direction(echo_pin_sensor1, GPIO_MODE_INPUT);
	gpio_set_direction(triger_pin_sensor2, GPIO_MODE_OUTPUT);
	gpio_set_direction(echo_pin_sensor2, GPIO_MODE_INPUT);
// enable interrupt on falling (1->0) edge for button pin
	gpio_set_intr_type(echo_pin_sensor1, GPIO_INTR_ANYEDGE);
	gpio_set_intr_type(echo_pin_sensor2, GPIO_INTR_ANYEDGE);
// install ISR service with default configuration
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
// attach the interrupt service routine
	gpio_isr_handler_add(echo_pin_sensor1, echo_pin_sensor1_handler, NULL);
	gpio_isr_handler_add(echo_pin_sensor2, echo_pin_sensor2_handler, NULL);

	xTaskCreate(&triger_task, "triger_task", 2048, NULL, 5,
			NULL);
	xTaskCreate(&Print_task, "Print_task", 8192, NULL, 10,
			NULL);
	xTaskCreate(&telnet_task, "telnet_task", 8192, NULL, 2, NULL);
	xTaskCreate(&ota_server_task, "ota_server_task", 4096, NULL, 5, NULL);
}
