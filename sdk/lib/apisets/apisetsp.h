#ifndef APISETSP_H
#define APISETSP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "apisets.h"

typedef struct _ROS_APISET
{
    const UNICODE_STRING Name;
    const UNICODE_STRING Target;
    DWORD dwOsVersions;
} ROS_APISET;

extern const ROS_APISET g_Apisets[];
extern const LONG g_ApisetsCount;
extern const ROS_APISET g_ApisetsNoHost[];
extern const LONG g_ApisetsNoHostCount;

#ifdef __cplusplus
}
#endif

#endif // APISETSP_H
