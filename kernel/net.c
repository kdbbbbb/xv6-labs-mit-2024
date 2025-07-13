#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

#define MAX_PORTS 1024
#define UDP_RECV_QUEUE 16


struct udp_packet {
  uint32 src_ip;
  uint16 sport;
  uint16 len;
  char *data;
};

struct udp_queue {
  struct spinlock lock;
  int bound;
  int head;
  int tail;
  int size;
  struct udp_packet packets[UDP_RECV_QUEUE];
};

static struct udp_queue udp_ports[MAX_PORTS];

void
netinit(void)
{
  initlock(&netlock, "netlock");
  for(int i = 0; i < MAX_PORTS; i++) {
    struct udp_queue *q = &udp_ports[i];
    initlock(&q->lock, "udpport");
    q->bound = 0;
    q->head = q->tail = q->size = 0;
  }
}



//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
uint64
sys_bind(void)
{
  int port;
  argint(0, &port);
  if(port < 0 || port >= MAX_PORTS)
    return -1;

  struct udp_queue *q = &udp_ports[port];
  acquire(&q->lock);
  if(q->bound){
    release(&q->lock);
    return -1;
  }
  q->bound = 1;
  q->head = q->tail = q->size = 0;
  release(&q->lock);
  return 0;
}


//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.

// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  int dport, maxlen;
  uint64 srcva, sportva, bufva;
  struct proc *p = myproc();

  argint(0, &dport);
  argaddr(1, &srcva);
  argaddr(2, &sportva);
  argaddr(3, &bufva);
  argint(4, &maxlen);

  if(dport < 0 || dport >= MAX_PORTS)
    return -1;

  struct udp_queue *q = &udp_ports[dport];
  acquire(&q->lock);
  while(q->bound && q->size == 0) {
    sleep(q, &q->lock);
  }

  if(!q->bound || q->size == 0){
    release(&q->lock);
    return -1;
  }

  struct udp_packet *pkt = &q->packets[q->head];
  int len = pkt->len < maxlen ? pkt->len : maxlen;
  if (copyout(p->pagetable, srcva, (char *)&pkt->src_ip, sizeof(pkt->src_ip)) < 0 ||
      copyout(p->pagetable, sportva, (char *)&pkt->sport, sizeof(pkt->sport)) < 0 ||
      copyout(p->pagetable, bufva, pkt->data, len) < 0) {
    release(&q->lock);
    return -1;
  }

  kfree(pkt->data);
  q->head = (q->head + 1) % UDP_RECV_QUEUE;
  q->size--;

  release(&q->lock);
  return len;
}
// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport, dst, dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  if (sport < 0 || dst < 0 || dport < 0 || len < 0)
    return -1;
  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
 

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45;
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = 0;
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));
  udp->sum = 0;  // 关闭UDP校验和

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  int ret = e1000_transmit(buf, total);
  if(ret < 0){
    kfree(buf);
    printf("send: e1000_transmit failed\n");
    return -1;
  }

  // 这里假设 e1000_transmit 不会释放 buf，调用 kfree 释放内存
  kfree(buf);

  return 0;
}


void
ip_rx(char *buf, int len)
{
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct eth *eth = (struct eth *)buf;
  struct ip *ip = (struct ip *)(eth + 1);
  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf);
    return;
  }

  int ip_len = ntohs(ip->ip_len);
  struct udp *udp = (struct udp *)(ip + 1);
  int dport = ntohs(udp->dport);
  int sport = ntohs(udp->sport);
  uint32 src_ip = ntohl(ip->ip_src);
  //char *payload = (char *)(udp + 1);
  int data_len = ip_len - sizeof(struct ip) - sizeof(struct udp);

  if(dport < 0 || dport >= MAX_PORTS){
    kfree(buf);
    return;
  }

  struct udp_queue *q = &udp_ports[dport];
  acquire(&q->lock);
  if(!q->bound || q->size >= UDP_RECV_QUEUE){
    release(&q->lock);
    kfree(buf);
    return;
  }

  struct udp_packet *pkt = &q->packets[q->tail];
  pkt->src_ip = src_ip;
  pkt->sport = sport;
  pkt->len = data_len;
  pkt->data = buf;

  q->tail = (q->tail + 1) % UDP_RECV_QUEUE;
  q->size++;
  wakeup(q);
  release(&q->lock);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
uint64
sys_unbind(void)
{
  // 这里可以简单实现，释放端口绑定状态
  int port;
  argint(0, &port);
  if(port < 0 || port >= MAX_PORTS)
    return -1;

  struct udp_queue *q = &udp_ports[port];
  acquire(&q->lock);
  if(!q->bound){
    release(&q->lock);
    return -1;
  }
  q->bound = 0;
  q->head = q->tail = q->size = 0;
  release(&q->lock);
  wakeup(q);  // 唤醒等待recv的进程
  return 0;
}
