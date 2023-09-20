#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "freertos/queue.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/gpio.h"

#define WIFI_SSID "Nome da sua rede"
#define WIFI_PASSWORD "Senha da sua rede"
#define MQTT_TOKEN "Token do seu Broker MQTT"
#define MQTT_ID_CLIENT "Controle_MQTT"

#define LED 2
#define BUTTON GPIO_NUM_13
#define BUTO GPIO_NUM_12
#define PORTA GPIO_NUM_25

#define TAG "MQTT"
#define TAG2 "[SENSOR]"

TaskHandle_t DelTaskAl;
SemaphoreHandle_t porta_aberta;
SemaphoreHandle_t porta_fechada;
QueueHandle_t button_queue;

// Declarando o handler do client global
static esp_mqtt_client_handle_t client = NULL;

void vtask_seg(void *);
void vtask_Alarme(void *);
void vtask_senha(void *pvParameters)
{
    char msg[50];
    char button_sequence[8] = {0};
    int button_index = 0;
    int buttons_pressed = 0;
    printf("COLOQUE A SENHA!\n");

    while (1)
    {
        char button_value = '\0';

        if (!gpio_get_level(BUTTON))
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            button_value = '1';
        }
        else if (!gpio_get_level(BUTO))
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            button_value = '2';
        }

        if (button_value != '\0')
        {
            xQueueSend(button_queue, &button_value, portMAX_DELAY);
            button_sequence[button_index] = button_value;
            printf("Botão %c Pressionado!\n", button_value);

            button_index = (button_index + 1) % 4;
            buttons_pressed++;

            if (buttons_pressed == 4)
            {
                if (button_sequence[0] == '1' && button_sequence[1] == '2' && button_sequence[2] == '2' && button_sequence[3] == '1')
                {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    sprintf(msg, "[{\"variable\": \"senha\",\"value\": \"correta\"}]"); // Cria uma mensagem para publicar no broker
                    esp_mqtt_client_publish(client, "tago/data/post", msg, 0, 1, 0);
                    vTaskDelete(DelTaskAl);
                    printf("[ALARME DESTIVADO]\n");
                    vTaskDelete(NULL);
                }
                else
                {
                    printf("SENHA INCORRETA\n");
                }

                buttons_pressed = 0;
                memset(button_sequence, 0, sizeof(button_sequence));
                button_index = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "tago/data/post", 0);
        ESP_LOGI(TAG, "Envio do subscribe com sucesso, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "publish enviado com sucesso, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        // Parse o JSON
        cJSON *root = cJSON_Parse(event->data);

        if (root == NULL)
        {
            printf("Erro ao analisar JSON.\n");
            return 1;
        }

        // Verifica se o JSON é uma matriz
        if (cJSON_IsArray(root))
        {
            // Obtém o primeiro elemento da matriz
            cJSON *element = cJSON_GetArrayItem(root, 0);

            // Verifica se o elemento é um objeto JSON
            if (cJSON_IsObject(element))
            {
                // Obtém os valores das chaves "variable" e "value"
                cJSON *variable = cJSON_GetObjectItem(element, "variable");
                cJSON *value = cJSON_GetObjectItem(element, "value");

                // Verifica se as chaves existem e são do tipo string e número
                if (cJSON_IsString(variable) && cJSON_IsNumber(value))
                {
                    printf("Chave 'variable': %s\n", variable->valuestring);
                    printf("Chave 'value': %d\n", value->valueint);
                }
                else if (cJSON_IsString(variable) && cJSON_IsString(value))
                {
                    printf("Chave 'variable': %s\n", variable->valuestring);
                    printf("Chave 'value': %s\n", value->valuestring);

                    if (strcmp(variable->valuestring, "porta") == 0 && strcmp(value->valuestring, "aberta") == 0)
                    {

                        xSemaphoreGive(porta_aberta);
                        xTaskCreate(vtask_senha, "Task Senha", 1024 * 2, NULL, 3, NULL);
                        xTaskCreate(vtask_Alarme, "Task Alarme", 1024 * 2, NULL, 1, &DelTaskAl);
                    }
                    else if (strcmp(variable->valuestring, "porta") == 0 && strcmp(value->valuestring, "fechada") == 0)
                    {
                        xSemaphoreGive(porta_fechada);
                    }
                }
                else
                {
                    printf("Chaves inválidas no objeto JSON.\n");
                }
            }
            else
            {
                printf("Elemento não é um objeto JSON.\n");
            }
        }
        else
        {
            printf("JSON não é uma matriz.\n");
        }

        // Libera a memória alocada pela cJSON
        cJSON_Delete(root);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    }
    return ESP_OK;
}
// Configurando o driver do WiFi
void init_hw(void)
{
    gpio_config_t conf_BUTTON = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .pin_bit_mask = 1 << BUTTON};

    gpio_config(&conf_BUTTON);

    gpio_config_t conf_BUTO = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .pin_bit_mask = 1 << BUTO};

    gpio_config(&conf_BUTO);

    gpio_set_direction(PORTA, GPIO_MODE_OUTPUT);
    gpio_set_level(PORTA, 0);
}
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:

        ESP_LOGI(TAG, "Conectando ao roteador");
        break;

    case WIFI_EVENT_STA_DISCONNECTED:

        ESP_LOGE(TAG, "Falha ao conectar ao Ponto de Acesso");
        break;

    case IP_EVENT_STA_GOT_IP:

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
        break;
    }
}

static void wifi_init(void)
{

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id; // Handler para indentificar o contexto do evento
    esp_event_handler_instance_t instance_got_ip; // Handler para indentificar o contexto do evento

    // Registra as instâncias
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    wifi_config_t wifi_config =
        {
            .sta =
                {
                    .ssid = WIFI_SSID,
                    .password = WIFI_PASSWORD,
                },
        };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                   // Configura o driver para moodo Station
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config)); // Passa a configuração de SSID e senha

    ESP_LOGI(TAG, "SSID:[%s] Senha:[%s]", WIFI_SSID, WIFI_SSID);

    ESP_ERROR_CHECK(esp_wifi_start());   // Inicia o wifi
    ESP_ERROR_CHECK(esp_wifi_connect()); // Perfoma a conexão com a rede
    ESP_LOGI(TAG, "Aguardando conectar ...");
}

static void mqtt_app_start(void)
{

    ESP_LOGI(TAG, "Inicializando modulo MQTT...");

    /* Realiza a conexão ao broker MQTT */

    esp_mqtt_client_config_t config_mqtt =
        {
            .broker.address.uri = "mqtt://mqtt.tago.io:1883",  // URI do Tago IO
            .credentials.authentication.password = MQTT_TOKEN, // Token do device
            .credentials.username = "tago",                    // Nome de usário
            .credentials.client_id = MQTT_ID_CLIENT            // Nome do seu device
        };

    // Cria um handler para o client
    client = esp_mqtt_client_init(&config_mqtt);
    // Registra a função de callback para o handler do client
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);

    // Inicializa o client, autenticação no broken
    if (esp_mqtt_client_start(client) != ESP_OK)
    {
        ESP_LOGW(TAG, "MQTT não inicializado. Confira a URI ou as credênciais");
        return;
    }

    ESP_LOGI(TAG, "MQTT inicializado");
}

void init_analog(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
}

void vtask_seg(void *pvParameters)
{
    init_analog();
    int analog_value;
    uint8_t led_status = 0;
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    char msg[50];
    bool btn_is_press = false;
    while (1)
    {
        analog_value = adc1_get_raw(ADC1_CHANNEL_0);
        ESP_LOGI(TAG2, "Valor = %d", analog_value);

        if ((analog_value <= 1000) && !btn_is_press)
        {
            btn_is_press = true;
            printf("Está de Noite, Câmera ativada!\n");
            gpio_set_level(LED, 1);
            led_status = LED;
            gpio_set_level(LED, led_status);
            sprintf(msg, "[{\"variable\": \"cameras\",\"value\": \"ligadas\"}]");
            esp_mqtt_client_publish(client, "tago/data/post", msg, 0, 1, 0);
        }
        else if ((analog_value > 1000) && btn_is_press)
        {
            btn_is_press = false;
            printf("Está de DIA, Câmera Desativada!\n");
            gpio_set_level(LED, 0);
            led_status = LED;
            sprintf(msg, "[{\"variable\": \"cameras\",\"value\": \"desligadas\"}]");
            esp_mqtt_client_publish(client, "tago/data/post", msg, 0, 1, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void vtask_Alarme(void *pvParameters)
{
    printf("[ATIVANDO ALARME]\n");
    char msg[50];
    vTaskDelay(pdMS_TO_TICKS(18000));
    sprintf(msg, "[{\"variable\": \"tempo\",\"value\": \"esgotado\"}]");
    esp_mqtt_client_publish(client, "tago/data/post", msg, 0, 1, 0);
    printf("[ALARME ATIVADO]\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    vTaskSuspend(NULL);
}

void vtask_porta(void *pvParameters)
{
    // uint8_t led_status = 0;
    while (1)
    {
        if (xSemaphoreTake(porta_aberta, portMAX_DELAY) == pdTRUE)
        {
            // led_status = 1;
            gpio_set_level(PORTA, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (xSemaphoreTake(porta_fechada, portMAX_DELAY) == pdTRUE)
        {
            // led_status = 0;
            gpio_set_level(PORTA, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void app_main()
{
    init_hw();

    nvs_flash_init();
    wifi_init();
    char msg[50];

    vTaskDelay(pdMS_TO_TICKS(10000));
    mqtt_app_start();

    porta_aberta = xSemaphoreCreateBinary();
    porta_fechada = xSemaphoreCreateBinary();
    button_queue = xQueueCreate(100, sizeof(char));
    xTaskCreate(vtask_seg, "Task segurança", 1024 * 3, NULL, 2, NULL);
    xTaskCreate(vtask_porta, "Task PORTA", 1024 * 1, NULL, 1, NULL);
}