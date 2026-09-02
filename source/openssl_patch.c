#include "openssl_patch.h"

#include <string.h>
#include <kubridge.h>
#include <so_util/so_util.h>

extern so_module so_mod;

/*
 * Mirrors OpenSSL 1.1.x's struct evp_cipher_ctx_st (crypto/evp/evp_local.h,
 * unchanged across the 1.1.0/1.1.1 series). The game's .so was built
 * against a header where this struct was still fully public, and directly
 * accesses these fields; upstream OpenSSL later made EVP_CIPHER_CTX opaque,
 * which vitasdk's current openssl-1.1.1 package headers reflect. The .so's
 * compiled code and on-device data layout haven't changed, so we mirror the
 * classic layout locally rather than rely on the (now-opaque) system header.
 */
typedef struct legacy_evp_cipher_ctx_st {
    const void *cipher;
    void *engine;
    int encrypt;
    int buf_len;
    unsigned char oiv[16];
    unsigned char iv[16];
    unsigned char buf[32];
    int num;
    void *app_data;
    int key_len;
    unsigned long flags;
    void *cipher_data;
    int final_used;
    int block_mask;
    unsigned char final_block[32];
} LEGACY_EVP_CIPHER_CTX;

// Must stay hooked: the .so's own CRYPTO_atomic_add was built for Android
// and falls back to the Linux kernel user helper __kuser_cmpxchg at
// 0xffff0fc0, which does not exist on the Vita -> crash_trace.txt showed
// pc=0xffff0fc0 the moment curl_global_init's ENGINE setup took a
// reference. Real atomics via GCC builtins (ldrex/strex) instead.
int legacy_CRYPTO_atomic_add(int *val, int amount, int *ret) {
    *ret = __sync_add_and_fetch(val, amount);
    return 1;
}

void *legacy_CRYPTO_zalloc(size_t num, const char *file, int line) {
    void *ret;

    ret = CRYPTO_malloc(num, file, line);
    if (ret != NULL)
        memset(ret, 0, num);

    return ret;
}

void legacy_EVP_CIPHER_CTX_set_num(void *ctx, int num) {
    ((LEGACY_EVP_CIPHER_CTX *)ctx)->num = num;
}

void *legacy_EVP_CIPHER_CTX_get_cipher_data(const void *ctx) {
    return ((const LEGACY_EVP_CIPHER_CTX *)ctx)->cipher_data;
}

int legacy_EVP_CIPHER_CTX_num(const void *ctx) {
    return ((const LEGACY_EVP_CIPHER_CTX *)ctx)->num;
}

unsigned char *legacy_EVP_CIPHER_CTX_iv_noconst(const void *ctx) {
    return (unsigned char *)((const LEGACY_EVP_CIPHER_CTX *)ctx)->iv;
}

unsigned char *legacy_EVP_CIPHER_CTX_buf_noconst(const void *ctx) {
    return (unsigned char *)((const LEGACY_EVP_CIPHER_CTX *)ctx)->buf;
}

void patch_openssl(void) {
    // Only pure memory helpers are redirected. Everything else in the .so's
    // statically-linked OpenSSL is left alone: redirecting registries and
    // state-carrying entry points (EVP_add_*, CRYPTO_*_ex_data,
    // CRYPTO_get_ex_new_index, ERR_load_*, EVP_Digest*/Encrypt*, ...) into
    // vitasdk's *separate* libcrypto instance split OpenSSL's global state
    // across two libraries. Observed fallout, via curl's own error queue:
    //  - EVP_add_cipher/digest registered into the Vita-side OBJ_NAME table
    //    -> the .so's lookups found nothing -> "library has no ciphers".
    //  - CRYPTO_get_ex_new_index went to vitasdk's ex_data registry ->
    //    SSL_get_ex_data_X509_STORE_CTX_idx() < 0 -> SSL_CTX_new() failed
    //    with "x509 verification setup problems" (ssl_lib.c:2359)
    //    -> curl "SSL: couldn't create a context" on every HTTPS request.
    hook_addr(so_symbol(&so_mod, "CRYPTO_free"), (uintptr_t)&CRYPTO_free);
    hook_addr(so_symbol(&so_mod, "CRYPTO_zalloc"), (uintptr_t)&legacy_CRYPTO_zalloc);
    hook_addr(so_symbol(&so_mod, "CRYPTO_malloc"), (uintptr_t)&CRYPTO_malloc);
    hook_addr(so_symbol(&so_mod, "CRYPTO_realloc"), (uintptr_t)&CRYPTO_realloc);
    hook_addr(so_symbol(&so_mod, "OPENSSL_cleanse"), (uintptr_t)&OPENSSL_cleanse);
    hook_addr(so_symbol(&so_mod, "CRYPTO_memcmp"), (uintptr_t)&CRYPTO_memcmp);
    hook_addr(so_symbol(&so_mod, "CRYPTO_atomic_add"), (uintptr_t)&legacy_CRYPTO_atomic_add);
}
