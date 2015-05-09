/*
 * @f ccnl-ext-hmac.c
 * @b HMAC-256 signing support
 *
 * Copyright (C) 2015 <christian.tschudin@unibas.ch>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * File history:
 * 2015-05-08 created
 */

#ifdef USE_HMAC256

#include "lib-sha256.c"

// RFC2014 keyval generation
void
ccnl_hmac256_keyval(unsigned char *key, int klen,
                    unsigned char *keyval) // MUST have 64 bytes (BLOCK_LENGTH)
{
    if (klen <= SHA256_BLOCK_LENGTH) {
        memcpy(keyval, key, klen);
    } else {
        SHA256_CTX_t ctx;

        ccnl_SHA256_Init(&ctx);
        ccnl_SHA256_Update(&ctx, key, klen);
        ccnl_SHA256_Final(keyval, &ctx);
        klen = SHA256_DIGEST_LENGTH;
    }
    memset(keyval + klen, 0, SHA256_BLOCK_LENGTH - klen);
}

void
ccnl_hmac256_keyid(unsigned char *key, int klen,
                   unsigned char *keyid) // MUST have 32 bytes (DIGEST_LENGTH)
{
    SHA256_CTX_t ctx;

    ccnl_SHA256_Init(&ctx);
    ccnl_SHA256_Update(&ctx, key, klen);

    if (klen > SHA256_BLOCK_LENGTH) {
        unsigned char md[32];
        ccnl_SHA256_Final(md, &ctx);
        ccnl_SHA256_Init(&ctx);
        ccnl_SHA256_Update(&ctx, md, sizeof(md));
    }

    ccnl_SHA256_Final(keyid, &ctx);
}

// internal
void
ccnl_hmac256_keysetup(SHA256_CTX_t *ctx, unsigned char *keyval, int kvlen,
                      unsigned char pad)
{
    unsigned char buf[64];
    int i;

    if (kvlen > sizeof(buf))
        kvlen = sizeof(buf);
    for (i = 0; i < kvlen; i++, keyval++)
        buf[i] = *keyval ^ pad;
    while (i < sizeof(buf))
        buf[i++] = 0 ^ pad;

    ccnl_SHA256_Init(ctx);
    ccnl_SHA256_Update(ctx, buf, sizeof(buf));
}

// RFC2014 signature generation
void
ccnl_hmac256_sign(unsigned char *keyval, int kvlen,
                  unsigned char *data, int dlen,
                  unsigned char *md, int *mlen)
{
    unsigned char tmp[SHA256_DIGEST_LENGTH];
    SHA256_CTX_t ctx;

    ccnl_hmac256_keysetup(&ctx, keyval, kvlen, 0x36); // inner hash
    ccnl_SHA256_Update(&ctx, data, dlen);
    ccnl_SHA256_Final(tmp, &ctx);

    ccnl_hmac256_keysetup(&ctx, keyval, kvlen, 0x5c); // outer hash
    ccnl_SHA256_Update(&ctx, tmp, sizeof(tmp));
    ccnl_SHA256_Final(tmp, &ctx);

    if (*mlen > SHA256_DIGEST_LENGTH)
        *mlen = SHA256_DIGEST_LENGTH;
    memcpy(md, tmp, *mlen);
}

#ifdef NEEDS_PACKET_CRAFTING

#ifdef USE_SUITE_CCNTLV

// write Content packet *before* buf[offs], adjust offs and return bytes used
int
ccnl_ccntlv_prependSignedContentWithHdr(struct ccnl_prefix_s *name,
                                        unsigned char *payload, int paylen,
                                        unsigned int *lastchunknum, 
                                        int *contentpos,
                                        unsigned char *keyval, // 64B
                                        unsigned char *keydigest, // 32B
                                        int *offset, unsigned char *buf)
{
    int dummy, len, mdoffset, oldoffset;
    unsigned char hoplimit = 255; // setting to max (conten obj has no hoplimit)

    if (*offset < (8 + paylen + 4+32 + 3*4+32))
        return -1;

    oldoffset = *offset;
    *offset -= 32; // reserve space for the digest
    mdoffset = *offset;
    ccnl_ccntlv_prependTL(CCNX_TLV_TL_ValidationPayload, 32, offset, buf);
    *offset -= 32;
    memcpy(buf + *offset, keydigest, 32);
    ccnl_ccntlv_prependTL(CCNX_VALIDALGO_KEYID, 32, offset, buf);
    ccnl_ccntlv_prependTL(CCNX_VALIDALGO_HMAC_SHA256, 4+32, offset, buf);
    ccnl_ccntlv_prependTL(CCNX_TLV_TL_ValidationAlgo, 4+4+32, offset, buf);

    len = oldoffset - *offset;
    len += ccnl_ccntlv_prependContent(name, payload, paylen, lastchunknum,
                                      offset, contentpos, buf);
    if (len >= ((1 << 16) - 8))
        return -1;
    dummy = 32;
    ccnl_hmac256_sign(keyval, 64, buf + *offset, mdoffset - *offset,
                      buf + mdoffset, &dummy);
    ccnl_ccntlv_prependFixedHdr(CCNX_TLV_V1, CCNX_PT_Data,
                                len, hoplimit, offset, buf);
    return oldoffset - *offset;
}

#endif // USE_SUITE_CCNTLV

#endif // NEEDS_PACKET_CRAFTING

#endif // USE_HMAC256
