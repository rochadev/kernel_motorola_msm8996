#ifndef __NFS_NETNS_H__
#define __NFS_NETNS_H__

#include <net/net_namespace.h>
#include <net/netns/generic.h>

struct nfs_net {
	struct cache_detail *nfs_dns_resolve;
};

extern int nfs_net_id;

#endif
