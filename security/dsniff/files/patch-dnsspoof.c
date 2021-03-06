--- ./dnsspoof.c.orig	2001-03-15 09:33:03.000000000 +0100
+++ ./dnsspoof.c	2014-07-22 13:20:14.000000000 +0200
@@ -38,7 +38,7 @@
 
 pcap_t		*pcap_pd = NULL;
 int		 pcap_off = -1;
-int		 lnet_sock = -1;
+libnet_t	*l;
 u_long		 lnet_ip = -1;
 
 static void
@@ -90,19 +90,18 @@
 dns_init(char *dev, char *filename)
 {
 	FILE *f;
-	struct libnet_link_int *llif;
+	libnet_t *l;
+	char libnet_ebuf[LIBNET_ERRBUF_SIZE];
 	struct dnsent *de;
 	char *ip, *name, buf[1024];
 
-	if ((llif = libnet_open_link_interface(dev, buf)) == NULL)
-		errx(1, "%s", buf);
+	if ((l = libnet_init(LIBNET_LINK, dev, libnet_ebuf)) == NULL)
+		errx(1, "%s", libnet_ebuf);
 	
-	if ((lnet_ip = libnet_get_ipaddr(llif, dev, buf)) == -1)
-		errx(1, "%s", buf);
+	if ((lnet_ip = libnet_get_ipaddr4(l)) == -1)
+		errx(1, "%s", libnet_geterror(l));
 
-	lnet_ip = htonl(lnet_ip);
-	
-	libnet_close_link_interface(llif);
+	libnet_destroy(l);
 
 	SLIST_INIT(&dns_entries);
 	
@@ -180,7 +179,7 @@
 static void
 dns_spoof(u_char *u, const struct pcap_pkthdr *pkthdr, const u_char *pkt)
 {
-	struct libnet_ip_hdr *ip;
+	struct libnet_ipv4_hdr *ip;
 	struct libnet_udp_hdr *udp;
 	HEADER *dns;
 	char name[MAXHOSTNAMELEN];
@@ -189,7 +188,7 @@
 	in_addr_t dst;
 	u_short type, class;
 
-	ip = (struct libnet_ip_hdr *)(pkt + pcap_off);
+	ip = (struct libnet_ipv4_hdr *)(pkt + pcap_off);
 	udp = (struct libnet_udp_hdr *)(pkt + pcap_off + (ip->ip_hl * 4));
 	dns = (HEADER *)(udp + 1);
 	p = (u_char *)(dns + 1);
@@ -212,7 +211,7 @@
 	if (class != C_IN)
 		return;
 
-	p = buf + IP_H + UDP_H + dnslen;
+	p = buf + dnslen;
 	
 	if (type == T_A) {
 		if ((dst = dns_lookup_a(name)) == -1)
@@ -234,38 +233,38 @@
 		anslen += 12;
 	}
 	else return;
-	
-	libnet_build_ip(UDP_H + dnslen + anslen, 0, libnet_get_prand(PRu16),
-			0, 64, IPPROTO_UDP, ip->ip_dst.s_addr,
-			ip->ip_src.s_addr, NULL, 0, buf);
-	
-	libnet_build_udp(ntohs(udp->uh_dport), ntohs(udp->uh_sport),
-			 NULL, dnslen + anslen, buf + IP_H);
 
-	memcpy(buf + IP_H + UDP_H, (u_char *)dns, dnslen);
+	memcpy(buf, (u_char *)dns, dnslen);
 
-	dns = (HEADER *)(buf + IP_H + UDP_H);
+	dns = (HEADER *)buf;
 	dns->qr = dns->ra = 1;
 	if (type == T_PTR) dns->aa = 1;
 	dns->ancount = htons(1);
 
 	dnslen += anslen;
+
+	libnet_clear_packet(l);
+	libnet_build_udp(ntohs(udp->uh_dport), ntohs(udp->uh_sport),
+			 LIBNET_UDP_H + dnslen, 0,
+			 (u_int8_t *)buf, dnslen, l, 0);
+
+	libnet_build_ipv4(LIBNET_IPV4_H + LIBNET_UDP_H + dnslen, 0,
+			  libnet_get_prand(LIBNET_PRu16), 0, 64, IPPROTO_UDP, 0,
+			  ip->ip_dst.s_addr, ip->ip_src.s_addr, NULL, 0, l, 0);
 	
-	libnet_do_checksum(buf, IPPROTO_UDP, UDP_H + dnslen);
-	
-	if (libnet_write_ip(lnet_sock, buf, IP_H + UDP_H + dnslen) < 0)
+	if (libnet_write(l) < 0)
 		warn("write");
 
 	fprintf(stderr, "%s.%d > %s.%d:  %d+ %s? %s\n",
-	      libnet_host_lookup(ip->ip_src.s_addr, 0), ntohs(udp->uh_sport),
-	      libnet_host_lookup(ip->ip_dst.s_addr, 0), ntohs(udp->uh_dport),
+	      libnet_addr2name4(ip->ip_src.s_addr, 0), ntohs(udp->uh_sport),
+	      libnet_addr2name4(ip->ip_dst.s_addr, 0), ntohs(udp->uh_dport),
 	      ntohs(dns->id), type == T_A ? "A" : "PTR", name);
 }
 
 static void
 cleanup(int sig)
 {
-	libnet_close_raw_sock(lnet_sock);
+	libnet_destroy(l);
 	pcap_close(pcap_pd);
 	exit(0);
 }
@@ -276,6 +275,7 @@
 	extern char *optarg;
 	extern int optind;
 	char *p, *dev, *hosts, buf[1024];
+	char ebuf[LIBNET_ERRBUF_SIZE];
 	int i;
 
 	dev = hosts = NULL;
@@ -306,7 +306,7 @@
 		strlcpy(buf, p, sizeof(buf));
 	}
 	else snprintf(buf, sizeof(buf), "udp dst port 53 and not src %s",
-		      libnet_host_lookup(lnet_ip, 0));
+		      libnet_addr2name4(lnet_ip, LIBNET_DONT_RESOLVE));
 	
 	if ((pcap_pd = pcap_init(dev, buf, 128)) == NULL)
 		errx(1, "couldn't initialize sniffing");
@@ -314,10 +314,10 @@
 	if ((pcap_off = pcap_dloff(pcap_pd)) < 0)
 		errx(1, "couldn't determine link layer offset");
 	
-	if ((lnet_sock = libnet_open_raw_sock(IPPROTO_RAW)) == -1)
+	if ((l = libnet_init(LIBNET_RAW4, dev, ebuf)) == NULL)
 		errx(1, "couldn't initialize sending");
 	
-	libnet_seed_prand();
+	libnet_seed_prand(l);
 	
 	signal(SIGHUP, cleanup);
 	signal(SIGINT, cleanup);
