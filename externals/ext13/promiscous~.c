#include "m_pd.h"
#include "ext13.h"
#include <sys/types.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netdb.h>



/* ------------------------ promiscous_tilde~ ----------------------------- */


#define DEFAULT_NIC     "eth0"  //NIC

static t_class *promiscous_tilde_class;


typedef struct _promiscous_tilde
{
     t_object x_obj;
     int opened;
     int sock;
     char* cbuf;
     t_symbol* interface;
} t_promiscous_tilde;


static int setnic_promisc(char *nic_name){
    int sock;           // socket desc
    struct ifreq f;

    if( (sock = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ALL))) < 0){
        post("promiscous~ failed open socket, must be root (euid)");
        return(-1);
    }
    strcpy(f.ifr_name, nic_name);
    if( ioctl(sock, SIOCGIFFLAGS, &f) < 0) {
        post("promisous~ failed to get interface flags, continue anyway");
        return(sock);
    }
    f.ifr_flags |= IFF_PROMISC;
    if( ioctl(sock, SIOCSIFFLAGS, &f) < 0) {
        post("promiscous~ failed to set promisous mode , continue anyway");
    }
    return(sock);
}


t_int *promiscous_tilde_perform(t_int *w)
{
    	t_promiscous_tilde*  x = (t_promiscous_tilde*)(w[1]);
    	int n = (int)(w[3]);/*number of samples*/
    	int l = 0;
    	int ll = 0;
    	int r;
	static unsigned char packet;
	fd_set fdset;
	struct timeval timeout;
	t_float* out = (t_float *)w[2];
	int cptr[n];

	if (x->opened){
        	timeout.tv_sec = 0;
        	timeout.tv_usec = 0;
        	FD_ZERO(&fdset);
        	FD_SET(x->sock,&fdset);
        	if (r = select(x->sock+1,&fdset,NULL,NULL,&timeout) && x->sock){
        	/*	l = recv(x->sock, &cptr,n, 0);*/
        		l = read(x->sock, (char*) &cptr,n);
        	};
        	if (l < 0) l = 0;
        	while (l--){
        		*out++ = cptr[n-l] / 32767.;
        		ll++;
        	}
        }
	while (ll < n){
		*out++ = 0.;
		ll++;
   	}
   	return (w + 4);
}

static void promiscous_tilde_dsp(t_promiscous_tilde *x, t_signal **sp)
{
	dsp_add(promiscous_tilde_perform, 3, x, sp[0]->s_vec,
	sp[0]->s_n);
}


static void promiscous_tilde_free(t_promiscous_tilde *x){
 /*LATER*/
}


static void *promiscous_tilde_new()
{
	t_promiscous_tilde *x = (t_promiscous_tilde *)pd_new(promiscous_tilde_class);
	outlet_new(&x->x_obj, gensym("signal"));

	x->interface = gensym("eth0");

	if((x->sock = setnic_promisc(x->interface->s_name))<0){
		post ("could not open interface");
		x->opened = 0;
	}else{
		x->opened = 1;
	}
	return (x);
}


void promiscous_tilde_setup(void)
{
	promiscous_tilde_class = class_new(gensym("promiscous~"), (t_newmethod) promiscous_tilde_new, 0,
		sizeof(t_promiscous_tilde), CLASS_NOINLET, 0);
	class_addmethod(promiscous_tilde_class, (t_method) promiscous_tilde_dsp, gensym("dsp"), 0);
}
