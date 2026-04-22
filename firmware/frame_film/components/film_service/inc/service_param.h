#ifndef __SERVICE_PARAM_H__
#define __SERVICE_PARAM_H__


/*********************************************************************
 * INCLUDES
 */


/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */


/*********************************************************************
* TYPEDEFS
*/
typedef struct
{

} ServiceFilm_Def_t;

#pragma pack(4)
typedef struct
{
    uint8_t factory_flag;
    ServiceFilm_Def_t film;
} ServiceParam_Def_t; /*服务参数*/
#pragma pack()

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * GLOBAL VARIABLES
 */
extern ServiceParam_Def_t g_service_param;

/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */
extern void service_param_init(void);
extern void service_param_save(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_PARAM_H__ */
