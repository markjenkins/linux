#ifndef _NET_AH_H
#define _NET_AH_H

#include <net/xfrm.h>

struct ah_data
{
	u8			*key;
	int			key_len;
	u8			*work_icv;
	int			icv_full_len;
	int			icv_trunc_len;

	void			(*icv)(struct ah_data*,
	                               struct sk_buff *skb, u8 *icv);

	struct crypto_tfm	*tfm;
};

extern void skb_ah_walk(const struct sk_buff *skb,
                        struct crypto_tfm *tfm, icv_update_fn_t icv_update);

static inline void
ah_hmac_digest(struct ah_data *ahp, struct sk_buff *skb, u8 *auth_data)
{
	struct crypto_tfm *tfm = ahp->tfm;

	memset(auth_data, 0, ahp->icv_trunc_len);
	crypto_hmac_init(tfm, ahp->key, &ahp->key_len);
	skb_ah_walk(skb, tfm, crypto_hmac_update);
	crypto_hmac_final(tfm, ahp->key, &ahp->key_len, ahp->work_icv);
	memcpy(auth_data, ahp->work_icv, ahp->icv_trunc_len);
}

#endif
