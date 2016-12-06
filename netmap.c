/*-
 * (c) 2016
 *
 * Example usage of netmap(4) device using high-level API.
 * Requires FreeBSD.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/ethernet.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


static void
print_hex(const unsigned char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		printf("%02x%c", buf[i],
		    (i == len - 1) || (i % 32 == 31) ? '\n' : ':');
	}
}

static void
my_cb(u_char *arg, const struct nm_pkthdr *h, const u_char *buf)
{
	int *count;
	struct ether_header *ehdr;

	count = (int *)arg;
	ehdr = (struct ether_header *)buf;

	(*count)++;

	fprintf(stderr, "Received %d bytes at %p count %d\n", h->len, buf,
	    *count);
	fprintf(stderr, "Type: %02hx\n", ntohs(ehdr->ether_type));
	print_hex(buf, h->len);
	fprintf(stderr, "\n");
}

int
main(int argc, char **argv)
{
	int kq, count, ret;
	char *devname;
	bool running;

	struct nm_desc *desc;
	struct timespec tv;
	struct kevent ev;

	devname = "netmap:em0";
	if (argc >= 2)
		devname = argv[1];

	if (signal(SIGINT, SIG_IGN) == SIG_ERR)
		err(1, "signal failed");

	kq = kqueue();
	assert(kq != -1);

	desc = nm_open(devname, NULL, 0, NULL);
	if (desc == NULL)
		err(1, "nm_open failed");

	fprintf(stderr, "Opened device %s at fd %d (%u/%u) %d %u\n",
	    desc->req.nr_name, NETMAP_FD(desc), desc->first_rx_ring,
	    desc->req.nr_rx_rings, desc->done_mmap, desc->req.nr_memsize);

	EV_SET(&ev, NETMAP_FD(desc), EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
	ret = kevent(kq, &ev, 1, NULL, 0, NULL);
	assert(ret != -1);

	EV_SET(&ev, SIGINT, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, 0);
	ret = kevent(kq, &ev, 1, NULL, 0, NULL);
	assert(ret != -1);

	tv.tv_sec = 1;
	tv.tv_nsec = 0;
	running = true;
	while (running) {
		ret = kevent(kq, NULL, 0, &ev, 1, &tv);

		if (ret < 0)
			err(1, "kevent(2) failed");

		if (ret == 0)
			fprintf(stderr, "Timeout.\n");

		if (ret == 1) {
			switch (ev.filter) {
			case EVFILT_READ:
				nm_dispatch(desc, -1, my_cb, (void *)&count);
				break;
			case EVFILT_SIGNAL:
				running = false;
				break;
			}
		}

	}

	fprintf(stderr, "Closing.\n");
	nm_close(desc);

	return (0);
}
