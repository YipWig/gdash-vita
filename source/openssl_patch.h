/*
 * patch.h
 *
 * Patching OpenSSL functions.
 *
 * Copyright (C) 2024 hatoving
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef OPENSSL_PATCH_H
#define OPENSSL_PATCH_H

#include <openssl/crypto.h>
#include <openssl/evp.h>

/*
 * These re-implement OpenSSL 1.1.x internal ABI entry points that the
 * game's statically-linked (older) OpenSSL calls into directly. Current
 * vitasdk openssl-1.1.1 headers declare newer/opaque-struct signatures for
 * these same names (upstream hardened EVP_CIPHER_CTX after this code was
 * written), so our replacements are named differently here to avoid
 * "conflicting types" errors against <openssl/crypto.h>/<openssl/evp.h>.
 * Only their *addresses* matter: patch_openssl() installs them by hooking
 * the .so's own "CRYPTO_atomic_add" etc. symbol names via so_symbol(), so
 * the local C identifier is irrelevant to the game calling them.
 */
int legacy_CRYPTO_atomic_add(int *val, int amount, int *ret);
void *legacy_CRYPTO_zalloc(size_t num, const char *file, int line);
void legacy_EVP_CIPHER_CTX_set_num(void *ctx, int num);
void *legacy_EVP_CIPHER_CTX_get_cipher_data(const void *ctx);
int legacy_EVP_CIPHER_CTX_num(const void *ctx);
unsigned char *legacy_EVP_CIPHER_CTX_iv_noconst(const void *ctx);
unsigned char *legacy_EVP_CIPHER_CTX_buf_noconst(const void *ctx);

void patch_openssl();

#endif
