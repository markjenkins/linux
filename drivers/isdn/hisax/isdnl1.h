/* $Id: isdnl1.h,v 2.9.6.3 2001/09/23 22:24:49 kai Exp $
 *
 * Layer 1 defines
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define D_RCVBUFREADY	0
#define D_XMTBUFREADY	1
#define D_L1STATECHANGE	2
#define D_CLEARBUSY	3
#define D_RX_MON0	4
#define D_RX_MON1	5
#define D_TX_MON0	6
#define D_TX_MON1	7
#define E_RCVBUFREADY	8

#define B_RCVBUFREADY   0
#define B_XMTBUFREADY   1
#define B_CMPLREADY     2

#define B_LL_NOCARRIER	8
#define B_LL_CONNECT	9
#define B_LL_OK		10

extern void debugl1(struct IsdnCardState *cs, char *fmt, ...);
extern void DChannel_proc_xmt(struct IsdnCardState *cs);
extern void DChannel_proc_rcv(struct IsdnCardState *cs);
extern void l1_msg(struct IsdnCardState *cs, int pr, void *arg);
extern void l1_msg_b(struct PStack *st, int pr, void *arg);

static inline void
fill_fifo_b(struct BCState *bcs)
{
	bcs->cs->bc_l1_ops->fill_fifo(bcs);
}

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir);
#endif

static inline void
sched_b_event(struct BCState *bcs, int event)
{
	set_bit(event, &bcs->event);
	schedule_work(&bcs->work);
}

static inline void
sched_d_event(struct IsdnCardState *cs, int event)
{
	set_bit(event, &cs->event);
	schedule_work(&cs->work);
}

/* called with the card lock held */
static inline void
xmit_complete_b(struct BCState *bcs)
{
	skb_queue_tail(&bcs->cmpl_queue, bcs->tx_skb);
	sched_b_event(bcs, B_CMPLREADY);
	bcs->tx_skb = NULL;
}

/* called with the card lock held */
static inline void
xmit_complete_d(struct IsdnCardState *cs)
{
	dev_kfree_skb_irq(cs->tx_skb);
	cs->tx_skb = NULL;
}

/* called with the card lock held */
static inline void
xmit_ready_b(struct BCState *bcs)
{
	bcs->tx_skb = skb_dequeue(&bcs->squeue);
	if (bcs->tx_skb) {
		bcs->count = 0;
		set_bit(BC_FLG_BUSY, &bcs->Flag);
		fill_fifo_b(bcs);
	} else {
		clear_bit(BC_FLG_BUSY, &bcs->Flag);
		sched_b_event(bcs, B_XMTBUFREADY);
	}
}

/* called with the card lock held */
static inline void
xmit_ready_d(struct IsdnCardState *cs)
{
	cs->tx_skb = skb_dequeue(&cs->sq);
	if (cs->tx_skb) {
		cs->tx_cnt = 0;
		cs->DC_Send_Data(cs);
	} else {
		sched_d_event(cs, D_XMTBUFREADY);
	}
}

static inline void
xmit_data_req_b(struct BCState *bcs, struct sk_buff *skb)
{
	struct IsdnCardState *cs = bcs->cs;
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (bcs->tx_skb) {
		skb_queue_tail(&bcs->squeue, skb);
	} else {
		set_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->tx_skb = skb;
		bcs->count = 0;
		fill_fifo_b(bcs);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}

static inline void
xmit_data_req_d(struct IsdnCardState *cs, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (cs->debug & DEB_DLOG_HEX)
		LogFrame(cs, skb->data, skb->len);
	if (cs->debug & DEB_DLOG_VERBOSE)
		dlogframe(cs, skb, 0);
	if (cs->tx_skb) {
		skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
	} else {
		cs->tx_skb = skb;
		cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
		cs->DC_Send_Data(cs);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}

static inline void
xmit_pull_ind_b(struct BCState *bcs, struct sk_buff *skb)
{
	struct IsdnCardState *cs = bcs->cs;
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (bcs->tx_skb) {
		WARN_ON(1);
	} else {
		set_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->tx_skb = skb;
		bcs->count = 0;
		fill_fifo_b(bcs);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}

static inline void
xmit_pull_ind_d(struct IsdnCardState *cs, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (cs->tx_skb) {
		WARN_ON(1);
	} else {
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 0);
#ifdef L2FRAME_DEBUG
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
		cs->tx_skb = skb;
		cs->tx_cnt = 0;
		cs->DC_Send_Data(cs);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}

/* If busy, the PH_PULL | CONFIRM scheduling is handled under
 * the card lock by xmit_ready_b() above, so no race */
static inline void
xmit_pull_req_b(struct PStack *st)
{
	struct BCState *bcs = st->l1.bcs;
	struct IsdnCardState *cs = bcs->cs;
	unsigned long flags;
	int busy = 0;

	spin_lock_irqsave(&cs->lock, flags);
	if (bcs->tx_skb) {
		set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		busy = 1;
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	if (!busy)
		L1L2(st, PH_PULL | CONFIRM, NULL);
}

/* If busy, the PH_PULL | CONFIRM scheduling is handled under
 * the card lock by xmit_ready_d() above, so no race */
static inline void
xmit_pull_req_d(struct PStack *st)
{
	struct IsdnCardState *cs = st->l1.hardware;
	unsigned long flags;
	int busy = 0;

#ifdef L2FRAME_DEBUG
	if (cs->debug & L1_DEB_LAPD)
		debugl1(cs, "-> PH_REQUEST_PULL");
#endif
	spin_lock_irqsave(&cs->lock, flags);
	if (cs->tx_skb) {
		set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		busy = 1;
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	if (!busy)
		L1L2(st, PH_PULL | CONFIRM, NULL);
}

/* called with the card lock held */
static inline void
xmit_restart_b(struct BCState *bcs)
{
#ifdef ERROR_STATISTIC
	bcs->err_tx++;
#endif
	if (!bcs->tx_skb) {
		WARN_ON(1);
		return;
	}
	skb_push(bcs->tx_skb, bcs->count);
	bcs->tx_cnt += bcs->count;
	bcs->count = 0;
}

/* called with the card lock held */
static inline void
xmit_restart_d(struct IsdnCardState *cs)
{
#ifdef ERROR_STATISTIC
	cs->err_tx++;
#endif
	if (!cs->tx_skb) {
		WARN_ON(1);
		return;
	}
	skb_push(cs->tx_skb, cs->tx_cnt);
	cs->tx_cnt = 0;
}

/* Useful for HSCX/ISAC work-alike's */
/* ---------------------------------------------------------------------- */

/* XPR - transmit pool ready */
/* called with the card lock held */
static inline void
xmit_xpr_b(struct BCState *bcs)
{
	/* current frame? */
	if (bcs->tx_skb) {
		/* last frame not done yet? */
		if (bcs->tx_skb->len) {
			fill_fifo_b(bcs);
			return;
		}
		xmit_complete_b(bcs);
		bcs->count = 0;
	}
	xmit_ready_b(bcs);
}

/* XPR - transmit pool ready */
/* called with the card lock held */
static inline void
xmit_xpr_d(struct IsdnCardState *cs)
{
	if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
		del_timer(&cs->dbusytimer);
	if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
		sched_d_event(cs, D_CLEARBUSY);
	/* current frame? */
	if (cs->tx_skb) {
		/* last frame not done yet? */
		if (cs->tx_skb->len) {
			cs->DC_Send_Data(cs);
			return;
		}
		xmit_complete_d(cs);
		cs->tx_cnt = 0;
	}
	xmit_ready_d(cs);
}

/* XDU - transmit data underrun */
/* called with the card lock held */
static inline void
xmit_xdu_b(struct BCState *bcs, void (*reset_xmit)(struct BCState *bcs))
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_WARN)
		debugl1(cs, "HSCX %c EXIR XDU", 'A' + bcs->channel);

	if (bcs->mode == L1_MODE_TRANS) {
		fill_fifo_b(bcs);
	} else {
		xmit_restart_b(bcs);
		reset_xmit(bcs);
	}
}

/* XDU - transmit data underrun */
/* called with the card lock held */
static inline void
xmit_xdu_d(struct IsdnCardState *cs, void (*reset_xmit)(struct IsdnCardState *cs))
{
	printk(KERN_WARNING "HiSax: D XDU\n");
	if (cs->debug & L1_DEB_WARN)
		debugl1(cs, "D XDU");

	if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
		del_timer(&cs->dbusytimer);
	if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
		sched_d_event(cs, D_CLEARBUSY);

	xmit_restart_d(cs);
	if (reset_xmit)
		reset_xmit(cs);
}

static inline unsigned char *
xmit_fill_fifo_b(struct BCState *bcs, int fifo_size, int *count, int *more)
{
	struct IsdnCardState *cs = bcs->cs;
	unsigned char *p;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, __FUNCTION__);

	if (!bcs->tx_skb || bcs->tx_skb->len <= 0) {
		WARN_ON(1);
		return NULL;
	}

	*more = (bcs->mode == L1_MODE_TRANS);
	if (bcs->tx_skb->len > fifo_size) {
		*more = 1;
		*count = fifo_size;
	} else {
		*count = bcs->tx_skb->len;
	}
	p = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, *count);
	bcs->tx_cnt -= *count;
	bcs->count += *count;

	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "%s %c cnt %d", __FUNCTION__,
			     bcs->hw.hscx.hscx ? 'B' : 'A', *count);
		QuickHex(t, p, *count);
		debugl1(cs, bcs->blog);
	}
	return p;
}

static inline unsigned char *
xmit_fill_fifo_d(struct IsdnCardState *cs, int fifo_size, int *count, int *more)
{
	unsigned char *p;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, __FUNCTION__);

	if (!cs->tx_skb || cs->tx_skb->len <= 0) {
		WARN_ON(1);
		return NULL;
	}

	*more = 0;
	if (cs->tx_skb->len > fifo_size) {
		*more = 1;
		*count = fifo_size;
	} else {
		*count = cs->tx_skb->len;
	}

	p = cs->tx_skb->data;
	skb_pull(cs->tx_skb, *count);
	cs->tx_cnt += *count;

	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "%s cnt %d", __FUNCTION__, *count);
		QuickHex(t, p, *count);
		debugl1(cs, cs->dlog);
	}
	return p;
}
