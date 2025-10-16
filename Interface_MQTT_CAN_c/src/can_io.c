#include "can_io.h"
#include "table.h"       // table_find_by_id
#include "mqtt_io.h"     // mqtt_on_can_message
#include "log.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if.h> 
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>

static bool set_nonblock(int fd){
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0) return false;
  return (fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0);
}

bool can_init(can_ctx_t *c, const char *ifname){
  if (!c || !ifname) return false;
  memset(c, 0, sizeof(*c));
  c->fd = -1;

  int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0){ LOGE("socket(PF_CAN): %s", strerror(errno)); return false; }

  struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
  snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
  if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0){
    LOGE("SIOCGIFINDEX(%s): %s", ifname, strerror(errno));
    close(fd); return false;
  }

  // IMPORTANT : ne pas recevoir nos propres trames (coupe les doublons)
  int recv_own = 0; // 0 = off
  if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own, sizeof(recv_own)) < 0) {
    LOGW("setsockopt(CAN_RAW_RECV_OWN_MSGS): %s", strerror(errno));
  }

  struct sockaddr_can addr; memset(&addr, 0, sizeof(addr));
  addr.can_family  = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
    LOGE("bind CAN(%s): %s", ifname, strerror(errno));
    close(fd); return false;
  }

  if (!set_nonblock(fd)){
    LOGW("fcntl(O_NONBLOCK) échoué (socket bloquant)");
  }

  int rcvbuf = 256*1024;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
  int sndbuf = 256*1024;
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

  c->fd = fd;
  return true;
}


bool can_send(can_ctx_t *c, uint32_t can_id, const uint8_t data[8]){
  if (!c || c->fd < 0) return false;

  struct can_frame f; memset(&f, 0, sizeof(f));
  f.can_id  = can_id & CAN_SFF_MASK;  // ID standard (11 bits)
  f.can_dlc = 8;                      // trames fixes 8 octets
  memcpy(f.data, data, 8);

  ssize_t n = write(c->fd, &f, sizeof(f));
  if (n < 0){
    LOGE("CAN write: %s", strerror(errno));
    return false;
  }
  return (n == (ssize_t)sizeof(f));
}

/* Draine jusqu'à max_frames trames si disponibles (non-bloquant).
   Utilise select() avec timeout nul puis lit jusqu'à EAGAIN.
   NOTE: on passe table et mqtt en param pour éviter dépendances circulaires. */
void can_poll(can_ctx_t *c, const table_t *t, mqtt_ctx_t *m, int max_frames){
  if (!c || c->fd < 0) return;
  if (max_frames <= 0) max_frames = 8;

  fd_set rfds; struct timeval tv = (struct timeval){0,0};
  FD_ZERO(&rfds); FD_SET(c->fd, &rfds);
  int rv = select(c->fd + 1, &rfds, NULL, NULL, &tv);
  if (rv <= 0 || !FD_ISSET(c->fd, &rfds)) return;

  for (int i = 0; i < max_frames; i++){
    struct can_frame f; ssize_t n = read(c->fd, &f, sizeof(f));
    if (n < 0){
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      LOGW("CAN read: %s", strerror(errno));
      break;
    }
    if ((size_t)n != sizeof(f)) break;

    /* 1) tentative par ID de trame normal */
    const entry_t *e = t ? table_find_by_canid(t, f.can_id) : NULL;
    const uint8_t *payload = f.data;
    uint8_t shifted[8];

    /* 2) si inconnu, tenter le mode tunnel: ID 16 bits dans data[0..1] */
    if (!e && f.can_dlc >= 2){
      uint16_t inner_id = ((uint16_t)f.data[0] << 8) | f.data[1];
      e = table_find_by_canid(t, inner_id);
      if (e){
        /* on “retire” les 2 octets d’en-tête et on recale le payload */
        memset(shifted, 0, sizeof(shifted));
        size_t copy = (f.can_dlc > 2) ? (size_t)(f.can_dlc - 2) : 0;
        if (copy > 6) copy = 6;                 /* place restante (8-2) */
        memcpy(shifted, f.data + 2, copy);
        payload = shifted;
      }
    }

    if (!e) continue;
    (void)mqtt_handle_can_message(m, e, payload);
  }
}






void can_cleanup(can_ctx_t *c){
  if (!c) return;
  if (c->fd >= 0){
    close(c->fd);
    c->fd = -1;
  }
}
