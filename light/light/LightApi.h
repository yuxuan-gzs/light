// LightApi.h - network bridge module attached to the light main program.
// Exported as C symbols from light.exe (see LightApi.def); same-process
// plugin DLLs (e.g. YXGuiPlugin) can call them via
// GetModuleHandle(NULL) + GetProcAddress.
#pragma once
#include "winwrap.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the network module. Returns 0 on success.
int LightApi_Init(void);

// Shutdown the network module.
void LightApi_Shutdown(void);

// Synchronous HTTP request.
//   method:    "GET" / "POST" / "PUT" / "DELETE" ...
//   url:       full http:// or https:// address
//   headers:   extra request headers, lines separated by \r\n, may be NULL
//   body:      request body, may be NULL
//   timeoutMs: timeout in milliseconds, <= 0 means default 30000
//   outStatus: receives HTTP status code; set to 0 when the request fails
//   outBody:   receives UTF-8 response body (NUL terminated); may be NULL
//              on failure. Caller MUST release with LightApi_Free.
// Returns 0 when a response was received (status available), non-zero on failure.
int LightApi_HttpRequest(const char* method,
                         const char* url,
                         const char* headers,
                         const char* body,
                         int         timeoutMs,
                         int*        outStatus,
                         char**      outBody);

// Chunk callback used by LightApi_HttpRequestStream (SSE streaming basis).
// Called for every chunk of the response body as it arrives.
typedef void (*LightApiChunkFn)(const char* data, size_t len, void* user);

// Synchronous HTTP request with per-chunk streaming callback.
// Same semantics as LightApi_HttpRequest, but response body is delivered
// incrementally via chunkFn (may be NULL for plain full-body requests).
int LightApi_HttpRequestStream(const char* method,
                               const char* url,
                               const char* headers,
                               const char* body,
                               int         timeoutMs,
                               int*        outStatus,
                               LightApiChunkFn chunkFn,
                               void*           user);

// Release a buffer allocated by this module (e.g. outBody).
void LightApi_Free(void* ptr);

#ifdef __cplusplus
}
#endif