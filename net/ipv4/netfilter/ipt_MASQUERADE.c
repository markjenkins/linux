/* Masquerade.  Simple mapping which alters range to a local IP address
   (depending on route). */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables MASQUERADE target module");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Lock protects masq region inside conntrack */
static DECLARE_RWLOCK(masq_lock);

/* FIXME: Multiple targets. --RR */
static int
masquerade_check(const char *tablename,
		 const struct ipt_entry *e,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
	const struct ip_nat_multi_range *mr = targinfo;

	if (strcmp(tablename, "nat") != 0) {
		DEBUGP("masquerade_check: bad table `%s'.\n", tablename);
		return 0;
	}
	if (targinfosize != IPT_ALIGN(sizeof(*mr))) {
		DEBUGP("masquerade_check: size %u != %u.\n",
		       targinfosize, sizeof(*mr));
		return 0;
	}
	if (hook_mask & ~(1 << NF_IP_POST_ROUTING)) {
		DEBUGP("masquerade_check: bad hooks %x.\n", hook_mask);
		return 0;
	}
	if (mr->range[0].flags & IP_NAT_RANGE_MAP_IPS) {
		DEBUGP("masquerade_check: bad MAP_IPS.\n");
		return 0;
	}
	if (mr->rangesize != 1) {
		DEBUGP("masquerade_check: bad rangesize %u.\n", mr->rangesize);
		return 0;
	}
	return 1;
}

static unsigned int
masquerade_target(struct sk_buff **pskb,
		  const struct net_device *in,
		  const struct net_device *out,
		  unsigned int hooknum,
		  const void *targinfo,
		  void *userinfo)
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	const struct ip_nat_multi_range *mr;
	struct ip_nat_multi_range newrange;
	struct rtable *rt;
	u_int32_t newsrc;

	IP_NF_ASSERT(hooknum == NF_IP_POST_ROUTING);

	/* FIXME: For the moment, don't do local packets, breaks
	   testsuite for 2.3.49 --RR */
	if ((*pskb)->sk)
		return NF_ACCEPT;

	ct = ip_conntrack_get(*pskb, &ctinfo);
	IP_NF_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED
	                    || ctinfo == IP_CT_RELATED + IP_CT_IS_REPLY));

	mr = targinfo;
	rt = (struct rtable *)(*pskb)->dst;
	newsrc = inet_select_addr(out, rt->rt_gateway, RT_SCOPE_UNIVERSE);
	if (!newsrc) {
		printk("MASQUERADE: %s ate my IP address\n", out->name);
		return NF_DROP;
	}

	WRITE_LOCK(&masq_lock);
	ct->nat.masq_index = out->ifindex;
	WRITE_UNLOCK(&masq_lock);

	/* Transfer from original range. */
	newrange = ((struct ip_nat_multi_range)
		{ 1, { { mr->range[0].flags | IP_NAT_RANGE_MAP_IPS,
			 newsrc, newsrc,
			 mr->range[0].min, mr->range[0].max } } });

	/* Hand modified range to generic setup. */
	return ip_nat_setup_info(ct, &newrange, hooknum);
}

static inline int
device_cmp(const struct ip_conntrack *i, void *_ina)
{
	int ret = 0;
	struct in_ifaddr *ina = _ina;

	READ_LOCK(&masq_lock);
	/* If it's masquerading out this interface with a different address,
	   or we don't know the new address of this interface. */
	if (i->nat.masq_index == ina->ifa_dev->dev->ifindex
	    && i->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip != ina->ifa_address)
		ret = 1;
	READ_UNLOCK(&masq_lock);

	return ret;
}

static inline int
connect_unassure(const struct ip_conntrack *i, void *_ina)
{
	struct in_ifaddr *ina = _ina;

	/* We reset the ASSURED bit on all connections, so they will
	 * get reaped under memory pressure. */
	if (i->nat.masq_index == ina->ifa_dev->dev->ifindex)
		clear_bit(IPS_ASSURED_BIT, (unsigned long *)&i->status);
	return 0;
}

static int masq_inet_event(struct notifier_block *this,
			   unsigned long event,
			   void *ptr)
{
	/* For some configurations, interfaces often come back with
	 * the same address.  If not, clean up old conntrack
	 * entries. */
	if (event == NETDEV_UP)
		ip_ct_selective_cleanup(device_cmp, ptr);
	else if (event == NETDEV_DOWN)
		ip_ct_selective_cleanup(connect_unassure, ptr);

	return NOTIFY_DONE;
}

static struct notifier_block masq_inet_notifier = {
	.notifier_call	= masq_inet_event,
};

static struct ipt_target masquerade = {
	.name		= "MASQUERADE",
	.target		= masquerade_target,
	.checkentry	= masquerade_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;

	ret = ipt_register_target(&masquerade);

	if (ret == 0)
		/* Register IP address change reports */
		register_inetaddr_notifier(&masq_inet_notifier);

	return ret;
}

static void __exit fini(void)
{
	ipt_unregister_target(&masquerade);
	unregister_inetaddr_notifier(&masq_inet_notifier);	
}

module_init(init);
module_exit(fini);
