#ifndef LOG_H
#define LOG_H



static inline const char* now_ts(void) {
  static char buf[32];
  time_t t = time(NULL);
  struct tm tm;
  
  localtime_r(&t, &tm);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

#define LOGI(fmt, ...) fprintf(stdout, "[%s] [INFO] " fmt "\n",  now_ts(), ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stdout, "[%s] [WARN] " fmt "\n",  now_ts(), ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[%s] [ERR ] " fmt "\n",  now_ts(), ##__VA_ARGS__)

#endif

// End of file
