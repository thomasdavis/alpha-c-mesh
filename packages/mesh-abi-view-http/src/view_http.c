/* @ajax/mesh-abi-view-http: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/view_http.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_HTTP_HV_LOAD_NAME + 16 <= MESH_HTTP_REQ_OFF, "http::HV_LOAD_NAME + 16 <= http::REQ_OFF");
_Static_assert(MESH_HTTP_REQ_OFF + MESH_HTTP_REQ_MAX <= MESH_HTTP_HV_SAMPLES, "http::REQ_OFF + http::REQ_MAX <= http::HV_SAMPLES");
_Static_assert(MESH_HTTP_RESP_OFF + MESH_HTTP_RESP_MAX <= MESH_HTTP_VIEW_BYTES, "http::RESP_OFF + http::RESP_MAX <= http::VIEW_BYTES");
