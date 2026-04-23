/***********************************************************
 *
 * MIT License
 *
 * Copyright (c) 2025 kiritro
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * 
 * FileName : /film_service/src/service_param.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/7
 * Description: 服务参数初始化
 * ChangeLog: Change Notes
 *
***********************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "nvs_flash.h"

#include "sys_log.h"
#include "service_param.h"

/*********************************************************************
 * MACROS
 */
#define SERVICE_FACTORY_DEFAULT_FLAG                            (0x22)

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static void nvs_init(void);
static void nvs_param_save(void);
static void service_param_set_default(void);

/*********************************************************************
 * GLOBAL VARIABLES
 */
ServiceParam_Def_t g_service_param = {0};

/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */


/**
 * [service_param_init 初始化服务参数]
 */
void service_param_init(void)
{
    nvs_init();
}

/**
 * [service_param_save 保存服务参数]
 */
void service_param_save(void)
{
    nvs_param_save();
}


/**
 * [service_param_set_default 设置服务参数默认值]
 */
static void service_param_set_default(void)
{
    // 设置服务参数默认值
    g_service_param.factory_flag = SERVICE_FACTORY_DEFAULT_FLAG;

    // 参数还原
    g_service_param.film.load_complete = 0;
    g_service_param.film.play_mode = 0;
    g_service_param.film.current_file_id = 0;
}

/**
 * [nvs_init 初始化nvs]
 */
static void nvs_init(void)
{
    esp_err_t err;
    nvs_handle_t my_nvs_handle;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        SYS_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    SYS_ERROR_CHECK(err);

    err = nvs_open(SYS_M_NVS_NAMESPACE, NVS_READWRITE, &my_nvs_handle);
    SYS_ERROR_CHECK(err);

    int retry = 6;
    while(retry--)
    {
        size_t required_size = sizeof(g_service_param);
        err = nvs_get_blob(my_nvs_handle, SYS_M_NVS_KEY_NAME, &g_service_param, &required_size);

        if(err == ESP_ERR_NVS_NOT_FOUND || g_service_param.factory_flag != SERVICE_FACTORY_DEFAULT_FLAG) //FACTORY RESET
        {
            service_param_set_default();

            err = nvs_set_blob(my_nvs_handle, SYS_M_NVS_KEY_NAME, &g_service_param, sizeof(g_service_param));
            SYS_ERROR_CHECK(err);
            err = nvs_commit(my_nvs_handle);
            SYS_ERROR_CHECK(err);
        }
        else
        {
            SYS_ERROR_CHECK(err);
            if(err == ESP_OK)
            {
                break;
            }
        }
        if(retry == 1)
        {
            SYS_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
    }

    nvs_close(my_nvs_handle);
}

/**
 * [nvs_param_save 保存nvs参数]
 */
static void nvs_param_save(void)
{
    esp_err_t err;
    nvs_handle_t my_nvs_handle;

    err = nvs_open(SYS_M_NVS_NAMESPACE, NVS_READWRITE, &my_nvs_handle);
    SYS_ERROR_CHECK(err);

    if(err == ESP_OK)
    {
        err = nvs_set_blob(my_nvs_handle, SYS_M_NVS_KEY_NAME, &g_service_param, sizeof(g_service_param));
        SYS_ERROR_CHECK(err);

        if(err == ESP_OK)
        {
            err = nvs_commit(my_nvs_handle);
            SYS_ERROR_CHECK(err);
        }
    }

    nvs_close(my_nvs_handle);
}
