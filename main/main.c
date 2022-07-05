#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <sys/param.h>
#include "esp_netif.h"
#include "addr_from_stdin.h"
#include "protocol_examples_common.h"
#include "lwip/sockets.h"

//#define ADC_RAW 			//WORKS
#define ADC_DMA 			//WORKS WITH | WITHOUT WIFI
//#define WIFI_SOFT_AP 		//WORKS WITH WIFI
#define WIFI_STA 			//WORKS WITH DMA
//#define TCP_CLIENT_ON 	//WORKS WITHOUT WIFI

#ifdef TCP_CLIENT_ON

#define CONFIG_EXAMPLE_IPV4 "MY_TCP_SERVER_IP"
#define PORT 9000

#ifdef CONFIG_EXAMPLE_IPV4
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4
#elif defined(CONFIG_EXAMPLE_IPV6)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#else
#define HOST_IP_ADDR "192.168.0.106"
#endif

static const char * TCP_TAG = "tcp_example";
static const char * payload = "Message from ESP32";

static void tcp_client_task(void * pvParameters) {
  char rx_buffer[128];
  char host_ip[] = HOST_IP_ADDR;
  int addr_family = 0;
  int ip_protocol = 0;

  while (1) {
#ifdef CONFIG_EXAMPLE_IPV4
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(host_ip);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_IPV6)
    struct sockaddr_in6 dest_addr = {
      0
    };
    inet6_aton(host_ip, & dest_addr.sin6_addr);
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(PORT);
    dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
    addr_family = AF_INET6;
    ip_protocol = IPPROTO_IPV6;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
    struct sockaddr_storage dest_addr = {
      0
    };
    ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_STREAM, & ip_protocol, & addr_family, & dest_addr));
#endif
    int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sock < 0) {
      ESP_LOGE(TCP_TAG, "Unable to create a socket: errno:%d", errno);
      break;
    }
    ESP_LOGI(TCP_TAG, "Socket created, connecting to %s:%d", host_ip, PORT);
    int err = connect(sock, (struct sockaddr * ) & dest_addr, sizeof(struct sockaddr_in6));
    if (err != 0) {
      ESP_LOGE(TCP_TAG, "Socket unable to connect: errno %d", errno);
      break;
    }
    ESP_LOGI(TCP_TAG, "Successfully connected!");

    while (1) {
      int err = send(sock, payload, strlen(payload), 0);
      if (err < 0) {
        ESP_LOGE(TCP_TAG, "Error occured during sending: errno %d", errno);
        break;
      }
      int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
      if (len < 0) {
        ESP_LOGE(TCP_TAG, "recv failed: errno %d", errno);
        break;
      } else {
        rx_buffer[len] = 0;
        ESP_LOGI(TCP_TAG, "Received %d bytes from %s:", len, host_ip);
        ESP_LOGI(TCP_TAG, "%s", rx_buffer);
      }
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    if (sock != -1) {
      ESP_LOGE(TCP_TAG, "Shutting down socket and restarting...");
      shutdown(sock, 0);
      close(sock);
    }
  }
  vTaskDelete(NULL);
}

#endif

#ifdef WIFI_SOFT_AP
	#define EXAMPLE_ESP_WIFI_SSID "ESP_AP"
	#define EXAMPLE_ESP_WIFI_PASS "123456789"
	#define EXAMPLE_ESP_WIFI_CHANNEL 1
	#define EXAMPLE_MAX_STA_CONN 3
#endif

#ifdef WIFI_STA
	#define EXAMPLE_ESP_WIFI_SSID "MY_SSID"
	#define EXAMPLE_ESP_WIFI_PASS "myPASSWORD"
	#define EXAMPLE_ESP_MAXIMUM_RETRY 5
#endif

#ifdef WIFI_SOFT_AP
	static const char * WIFI_AP_TAG = "WIFI SOFT AP";
	static void wifi_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
	  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t * event = (wifi_event_ap_staconnected_t * ) event_data;
		ESP_LOGI(WIFI_AP_TAG, "station"
		  MACSTR "join, AID=%d", MAC2STR(event -> mac), event -> aid);
	  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t * event = (wifi_event_ap_stadisconnected_t * ) event_data;
		ESP_LOGI(WIFI_AP_TAG, "station"
		  MACSTR "leave, AID=%d", MAC2STR(event -> mac), event -> aid);
	  }
}
void wifi_init_softap(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init( & cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, & wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
    .ap = {
      .ssid = EXAMPLE_ESP_WIFI_SSID,
      .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
      .channel = EXAMPLE_ESP_WIFI_CHANNEL,
      .password = EXAMPLE_ESP_WIFI_PASS,
      .max_connection = EXAMPLE_MAX_STA_CONN,
      .authmode = WIFI_AUTH_WPA_WPA2_PSK
    },
  };
  if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, & wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(WIFI_AP_TAG, "wifi init_softap finished. SSID:%s password:%s channel:%d", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);

}
#endif

#ifdef WIFI_STA
	static EventGroupHandle_t s_wifi_event_group;
	#define WIFI_CONNECTED_BIT BIT0
	#define WIFI_FAIL_BIT BIT1
	static const char * WIFI_TAG = "WIFI STATION";
	static int s_retry_num = 0;
	static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
	  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
		  esp_wifi_connect();
		  s_retry_num++;
		  ESP_LOGI(WIFI_TAG, "retrying to connect to the AP");
		} else {
		  xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(WIFI_TAG, "connecting to the AP failed");
	  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t * event = (ip_event_got_ip_t * ) event_data;
		ESP_LOGI(WIFI_TAG, "got ip:"
		  IPSTR, IP2STR( & event -> ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	  }
}
void wifi_init_sta(void) {
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init( & cfg));
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, & event_handler, NULL, & instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, & event_handler, NULL, & instance_got_ip));

  wifi_config_t wifi_config = {
    .sta = {
      .ssid = EXAMPLE_ESP_WIFI_SSID,
      .password = EXAMPLE_ESP_WIFI_PASS
    },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, & wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(WIFI_TAG, "wifi_init_sta finished");
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(WIFI_TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(WIFI_TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
  } else {
    ESP_LOGE(WIFI_TAG, "UNEXPECTED EVENT");
  }
}
#endif

#ifdef ADC_DMA
	#define TIMES 				128 //256
	#define DMA_BUFFER_SIZE 	128
	#define GET_UNIT(x)			((x >> 3) & 0x1)
	#define ADC_RESULT_BYTE 	2
	#define ADC_CONV_LIMIT_EN 	1 //For ESP32, this should always be set to 1
	#define ADC_CONV_LIMIT_NUM 	250
	#define ADC_CONV_MODE 		ADC_CONV_SINGLE_UNIT_1 //ESP32 only supports ADC1 DMA mode
	#define ADC_SAMPLING_FREQ 	10 * 1000
	#define ADC_OUTPUT_TYPE ADC_DIGI_OUTPUT_FORMAT_TYPE1

	static uint16_t adc1_chan_mask = BIT(7);
	static uint16_t adc2_chan_mask = 0;
	static adc_channel_t channel[1] = {
	  ADC1_CHANNEL_0
	}; //channel attached to GPIO NUM 36
	static const char * ADC_DMA_TAG = "ADC DMA";
	static void continuous_adc_init(uint16_t adc1_chan_mask, uint16_t adc2_chan_mask, adc_channel_t * channel, uint8_t channel_num) {
	  adc_digi_init_config_t adc_dma_config = {
		.max_store_buf_size = DMA_BUFFER_SIZE, //256, //better response time due to faster buffer dumping //previously 1024bytes were allocated
		.conv_num_each_intr = TIMES,
		.adc1_chan_mask = adc1_chan_mask,
		.adc2_chan_mask = adc2_chan_mask,
	  };
	  ESP_ERROR_CHECK(adc_digi_initialize( & adc_dma_config));

	  adc_digi_configuration_t dig_cfg = {
		.conv_limit_en = ADC_CONV_LIMIT_EN,
		.conv_limit_num = ADC_CONV_LIMIT_NUM,
		.sample_freq_hz = ADC_SAMPLING_FREQ,
		.conv_mode = ADC_CONV_MODE,
		.format = ADC_OUTPUT_TYPE,
	  };

	  adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {
		0
	  };
	  dig_cfg.pattern_num = channel_num;
	  for (int i = 0; i < channel_num; i++) {
		uint8_t unit = GET_UNIT(channel[i]);
		uint8_t ch = channel[i] & 0x7;
		adc_pattern[i].atten = ADC_ATTEN_DB_11;
		adc_pattern[i].channel = ch;
		adc_pattern[i].unit = unit;
		adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

		ESP_LOGI(ADC_DMA_TAG, "adc_pattern[%d].atten is :%x", i, adc_pattern[i].atten);
		ESP_LOGI(ADC_DMA_TAG, "adc_pattern[%d].channel is :%x", i, adc_pattern[i].channel);
		ESP_LOGI(ADC_DMA_TAG, "adc_pattern[%d].unit is :%x", i, adc_pattern[i].unit);
	  }
	  dig_cfg.adc_pattern = adc_pattern;
	  ESP_ERROR_CHECK(adc_digi_controller_configure( & dig_cfg));
	}
#endif

#ifdef ADC_RAW
	void getRawADC() {
		uint16_t adc1RAW = adc1_get_raw(ADC_CHANNEL_0);
		printf("ADC1 GPIO36 VAL: %d\n", adc1RAW);
		return;
	}
#endif

void app_main(void) {

#ifdef TCP_CLIENT_ON
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  //ESP_ERROR_CHECK(example_connect());
  xTaskCreate(tcp_client_task, "tcp_client", 2048, NULL, 5, NULL);
#endif

#ifdef WIFI_STA
  esp_err_t wifi_ret = nvs_flash_init();
  if (wifi_ret == ESP_ERR_NVS_NO_FREE_PAGES || wifi_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    wifi_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(wifi_ret);
  ESP_LOGI(WIFI_TAG, "ESP_WIFI_MODE_STA");
  wifi_init_sta();
#endif

#ifdef WIFI_SOFT_AP
  esp_err_t wifi_ap_ret = nvs_flash_init();
  if (wifi_ap_ret == ESP_ERR_NVS_NO_FREE_PAGES || wifi_ap_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    wifi_ap_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(wifi_ap_ret);
  ESP_LOGI(WIFI_AP_TAG, "ESP_WIFI_MODE_AP");
  wifi_init_softap();
#endif

#ifdef ADC_DMA
  esp_err_t ret;
  uint32_t ret_num = 0;
  uint8_t result[TIMES] = {
    0
  };
  memset(result, 0xcc, TIMES);
  continuous_adc_init(adc1_chan_mask, adc2_chan_mask, channel, sizeof(channel) / sizeof(adc_channel_t));
  adc_digi_start();
  while (1) {
    ret = adc_digi_read_bytes(result, TIMES, & ret_num, ADC_MAX_DELAY);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
      if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(ADC_DMA_TAG, "INVALID STATE");
      }
      ESP_LOGI("TASK:", "ret is %x, ret_num is %d", ret, ret_num);
      for (int i = 0; i < ret_num; i += ADC_RESULT_BYTE) {
        adc_digi_output_data_t * p = (adc_digi_output_data_t * ) & result[i];
        #if CONFIG_IDF_TARGET_ESP32
        ESP_LOGI(ADC_DMA_TAG, "Unit: %d, Channel: %d, Value: %x", 1, p -> type1.channel, p -> type1.data);
        #endif
      }
      vTaskDelay(1);
    } else if (ret == ESP_ERR_TIMEOUT) {
      ESP_LOGW(ADC_DMA_TAG, "No data, increase timeout or reduce conv_num_each_intr");
      vTaskDelay(1000);
    }
  }

  adc_digi_stop();
  ret = adc_digi_deinitialize();
  assert(ret == ESP_OK);
  #endif

#ifdef ADC_RAW
  while (1) {
    getRawADC();
    vTaskDelay(1);
  };
#endif
}
