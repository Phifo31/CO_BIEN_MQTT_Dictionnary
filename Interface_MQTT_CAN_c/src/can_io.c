#include "can_io.h"
#include "log.h"
#include "pack.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdio.h>

#ifndef FAKE_CAN
  /* ===== SocketCAN (Linux réel) ===== */
  #include <sys/socket.h>
  #include <sys/ioctl.h>
  #include <net/if.h>
  #include <linux/can.h>
  #include <linux/can/raw.h>
#endif

typedef struct can_thread_arg_s {
  can_ctx_t *ctx;
} can_thread_arg_t;

#ifdef FAKE_CAN
/* ---------- Backend FAKE (FIFOs) ---------- */
static const char *default_in_path  = "../can_in.pipe";   // depuis build/
static const char *default_out_path = "../can_out.pipe";

bool can_init(can_ctx_t *ctx, const table_t *t, mqtt_ctx_t *mqtt, const char *ifname) {
  (void)ifname;
  memset(ctx, 0, sizeof(*ctx));
  ctx->table = t;
  ctx->mqtt  = mqtt;
  ctx->fd    = -1;

  /* Ouvre les FIFOs en lecture+écriture pour ne pas bloquer si l’autre extrémité manque */
  int fd_in  = open(default_in_path,  O_RDWR | O_NONBLOCK);
  int fd_out = open(default_out_path, O_RDWR | O_NONBLOCK);
  if (fd_in < 0 || fd_out < 0) {
    LOGE("FAKE_CAN: ouvrir %s / %s: %s", default_in_path, default_out_path, strerror(errno));
    if (fd_in >=0) close(fd_in);
    if (fd_out>=0) close(fd_out);
    return false;
  }
  /* On “recycle” ctx->fd pour lire; on garde fd_out dans ifname[] via un cast sale -> plutôt stocker globalement */
  ctx->fd = fd_in;
  snprintf(ctx->ifname, sizeof(ctx->ifname), "%d", fd_out); /* on stocke le fd_out sous forme de texte */
  LOGI("FAKE_CAN: IN=%s (fd=%d), OUT=%s (fd=%s)", default_in_path, fd_in, default_out_path, ctx->ifname);
  return true;
}

static int get_out_fd(const can_ctx_t *ctx) {
  /* on a mis le fd_out dans ifname[] en texte */
  return atoi(ctx->ifname);
}

bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]) {
  int fd_out = get_out_fd(ctx);
  if (fd_out < 0) return false;
  char line[64];
  /* format: HEXID#16HEXBYTES, ex: 1310#018000FDFF0A0000 */
  int n = snprintf(line, sizeof(line),
                   "%X#%02X%02X%02X%02X%02X%02X%02X%02X\n",
                   can_id,
                   data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7]);
  ssize_t w = write(fd_out, line, (size_t)n);
  return (w == n);
}

static bool parse_line(const char *s, uint32_t *can_id, uint8_t out[8]) {
  /* attend "ID#DATA" avec DATA = 16 hex chars (8 octets) */
  const char *hash = strchr(s, '#');
  if (!hash) return false;
  char idbuf[16]={0};
  size_t idlen = (size_t)(hash - s);
  if (idlen==0 || idlen >= sizeof(idbuf)) return false;
  memcpy(idbuf, s, idlen);
  *can_id = (uint32_t) strtoul(idbuf, NULL, 16);

  const char *hex = hash + 1;
  size_t hlen = 0;
  while (hex[hlen] && hex[hlen] != '\n' && hex[hlen] != '\r') hlen++;
  if (hlen < 2 || hlen % 2 != 0) return false;

  memset(out, 0, 8);
  size_t bytes = hlen/2;
  if (bytes > 8) bytes = 8;
  for (size_t i=0;i<bytes;i++){
    char b[3] = { hex[2*i], hex[2*i+1], 0 };
    out[i] = (uint8_t) strtoul(b, NULL, 16);
  }
  /* pad implicite déjà 0 */
  return true;
}

void* can_rx_loop(void *arg) {
  can_ctx_t *ctx = (can_ctx_t*) arg;
  FILE *fp = fdopen(ctx->fd, "r");
  if (!fp) {
    LOGE("FAKE_CAN: fdopen: %s", strerror(errno));
    return NULL;
  }
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    uint32_t can_id=0; uint8_t data[8];
    if (!parse_line(buf, &can_id, data)) {
      LOGW("FAKE_CAN: ligne ignorée: %s", buf);
      continue;
    }
    const entry_t *e = table_find_by_id(ctx->table, can_id);
    if (!e) { LOGW("ID CAN inconnu: 0x%X", can_id); continue; }
    mqtt_on_can_message(ctx->mqtt, e, data);
  }
  return NULL;
}

void can_cleanup(can_ctx_t *ctx) {
  if (!ctx) return;
  int fd_out = get_out_fd(ctx);
  if (ctx->fd >= 0) close(ctx->fd);
  if (fd_out >= 0) close(fd_out);
  ctx->fd = -1;
}

#else
/* ---------- Backend SocketCAN (Linux réel) ---------- */
bool can_init(can_ctx_t *ctx, const table_t *t, mqtt_ctx_t *mqtt, const char *ifname) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->table = t;
  ctx->mqtt  = mqtt;
  snprintf(ctx->ifname, sizeof(ctx->ifname), "%s", ifname);

  int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (s < 0) { LOGE("socket CAN_RAW: %s", strerror(errno)); return false; }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
  if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
    LOGE("SIOCGIFINDEX(%s): %s", ifname, strerror(errno));
    close(s);
    return false;
  }

  struct sockaddr_can addr = {0};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOGE("bind CAN %s: %s", ifname, strerror(errno));
    close(s);
    return false;
  }

  ctx->fd = s;
  LOGI("SocketCAN ouvert sur %s", ifname);
  return true;
}

bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]) {
  struct can_frame frame = {0};
  frame.can_id = can_id;
  frame.can_dlc = 8;
  memcpy(frame.data, data, 8);
  ssize_t n = write(ctx->fd, &frame, sizeof(frame));
  if (n != sizeof(frame)) {
    LOGE("write CAN: %s", strerror(errno));
    return false;
  }
  return true;
}

void* can_rx_loop(void *arg) {
  can_ctx_t *ctx = (can_ctx_t*) arg;
  struct can_frame frame;
  for (;;) {
    ssize_t n = read(ctx->fd, &frame, sizeof(frame));
    if (n < 0) {
      if (errno == EINTR) continue;
      LOGE("read CAN: %s", strerror(errno));
      break;
    }
    if ((size_t)n < sizeof(struct can_frame)) continue;
    const entry_t *e = table_find_by_id(ctx->table, frame.can_id);
    if (!e) { LOGW("ID CAN inconnu: 0x%X", frame.can_id); continue; }
    mqtt_on_can_message(ctx->mqtt, e, frame.data);
  }
  return NULL;
}

void can_cleanup(can_ctx_t *ctx) {
  if (ctx && ctx->fd > 0) close(ctx->fd);
  ctx->fd = -1;
}
#endif
