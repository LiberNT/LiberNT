#ifndef _SECURITY_H
#define _SECURITY_H

#include <sspi.h>

#if defined(SECURITY_WIN32) || defined(SECURITY_KERNEL)
#include <secext.h>
#endif

#define MICROSOFT_KERBEROS_NAME MICROSOFT_KERBEROS_NAME_W
#ifdef UNICODE
#define NEGOSSP_NAME NEGOSSP_NAME_W
#else
#if (!(defined(UNICODE)))
#define NEGOSSP_NAME NEGOSSP_NAME_A
#else
#define NEGOSSP_NAME NEGOSSP_NAME_W
#endif
#endif
#define NEGOSSP_NAME_A "Negotiate"
#define NEGOSSP_NAME_W L"Negotiate"
#define NTLMSP_NAME L"NTLM"
#define NTLMSP_NAME_A "NTLM"

#endif /* _SECURITY_H */
