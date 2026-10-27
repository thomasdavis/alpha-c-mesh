/* The table: names, ck messages, an injected fake, and loading on a machine
 * without the driver (this one: the error names libcuda.so.1). */
#include "mesh/check.h"
#include "mesh/cuda_driver.h"

#include <stdlib.h>
#include <string.h>

static CUresult fake_name(CUresult r, const char **name) {
    *name = r == 100 ? "CUDA_ERROR_NO_DEVICE" : NULL;
    return r == 100 ? 0 : 1;
}

int main(void) {
    CHECK_S(mesh_cu_result_name(0), "CUDA_SUCCESS");
    CHECK_S(mesh_cu_result_name(CUDA_ERROR_NOT_READY), "CUDA_ERROR_NOT_READY");
    CHECK_S(mesh_cu_result_name(2), "CUDA_ERROR_OUT_OF_MEMORY");
    CHECK(mesh_cu_result_name(12345) == NULL);
    char *d = mesh_cu_result_debug(12345);
    CHECK_S(d, "cudaError_enum(12345)");
    free(d);
    char *err = NULL;
    CHECK(mesh_cu_ck(0, "cuInit", &err) && err == NULL);
    CHECK(!mesh_cu_ck_debug(2, "cuMemAlloc", &err));
    CHECK_S(err, "cuMemAlloc: CUDA_ERROR_OUT_OF_MEMORY");
    mesh_cuda_fns fake = {0};
    fake.cuGetErrorName = fake_name;
    mesh_cuda_set(&fake);
    CHECK(mesh_cuda() == &fake);
    CHECK(!mesh_cu_ck(100, "cuInit", &err));
    CHECK_S(err, "cuInit: CUDA_ERROR_NO_DEVICE");
    CHECK(!mesh_cu_ck(2, "x", &err));
    CHECK_S(err, "x: CUDA_ERROR_OUT_OF_MEMORY");
    mesh_cuda_set(NULL);
    /* No GPU here: loading fails cleanly (on a GPU machine it succeeds). */
    if (!mesh_cuda_load(&err)) CHECK(strstr(err, "libcuda.so.1") != NULL);
    else CHECK(mesh_cuda() != NULL && mesh_cuda()->cuInit != NULL);
    free(err);
    return mesh_check_done();
}
