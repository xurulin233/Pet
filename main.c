// Web pet: serve static files from web_root, plus pet state API.

#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ini.h"
#include "mongoose.h"

static const char *s_listening_addr = "http://0.0.0.0:8000";
static const char *s_root_dir = "web_root";
static const char *s_config_path = NULL;  // -c <path>; NULL = no persistence

static int s_signo;
static void signal_handler(int signo) { s_signo = signo; }

// Pet state. hunger/happiness range 0..100.
struct pet {
  double hunger;
  double happiness;
  int64_t last_tick_ms;
  int64_t feed_count;
};

static struct pet s_pet = {30.0, 80.0, 0, 0};

static void pet_tick(void) {
  int64_t now = (int64_t) mg_millis();
  if (s_pet.last_tick_ms == 0) {
    s_pet.last_tick_ms = now;
    return;
  }
  double dt = (double) (now - s_pet.last_tick_ms) / 1000.0;
  s_pet.last_tick_ms = now;

  s_pet.hunger += dt * 0.2;
  if (s_pet.hunger > 100) s_pet.hunger = 100;

  double target = s_pet.hunger > 70 ? 20 : (s_pet.hunger < 30 ? 90 : 50);
  double k = 0.05;
  s_pet.happiness += (target - s_pet.happiness) * (1 - exp(-k * dt));
  if (s_pet.happiness < 0) s_pet.happiness = 0;
  if (s_pet.happiness > 100) s_pet.happiness = 100;
}

static const char *pet_mood(void) {
  if (s_pet.hunger > 80) return "starving";
  if (s_pet.hunger > 60) return "hungry";
  if (s_pet.happiness > 75) return "happy";
  if (s_pet.happiness < 30) return "sad";
  return "ok";
}

static void send_state(struct mg_connection *c) {
  mg_http_reply(
      c, 200, "Content-Type: application/json\r\n",
      "{%m:%g,%m:%g,%m:%m,%m:%lld}\n",
      MG_ESC("hunger"), s_pet.hunger,
      MG_ESC("happiness"), s_pet.happiness,
      MG_ESC("mood"), MG_ESC(pet_mood()),
      MG_ESC("feed_count"), (int64_t) s_pet.feed_count);
}

static void cb(struct mg_connection *c, int ev, void *ev_data) {
  if (ev != MG_EV_HTTP_MSG) return;
  struct mg_http_message *hm = ev_data;

  if (mg_match(hm->uri, mg_str("/api/pet"), NULL)) {
    pet_tick();
    send_state(c);
  } else if (mg_match(hm->uri, mg_str("/api/feed"), NULL)) {
    pet_tick();
    s_pet.hunger -= 25;
    if (s_pet.hunger < 0) s_pet.hunger = 0;
    s_pet.happiness += 10;
    if (s_pet.happiness > 100) s_pet.happiness = 100;
    s_pet.feed_count++;
    send_state(c);
  } else {
    struct mg_http_serve_opts opts = {0};
    opts.root_dir = s_root_dir;
    mg_http_serve_dir(c, hm, &opts);
  }

  MG_INFO(("%.*s %.*s", (int) hm->method.len, hm->method.buf,
           (int) hm->uri.len, hm->uri.buf));
}

// INI handler: pull pet.* keys into s_pet.
static int load_handler(const char *section, const char *key,
                        const char *value, void *ud) {
  (void) ud;
  if (strcmp(section, "pet") != 0) return 0;
  if (strcmp(key, "hunger") == 0) {
    s_pet.hunger = atof(value);
  } else if (strcmp(key, "happiness") == 0) {
    s_pet.happiness = atof(value);
  } else if (strcmp(key, "feed_count") == 0) {
    s_pet.feed_count = (int64_t) atoll(value);
  }
  return 0;
}

static void load_config(const char *path) {
  pet_tick();  // make sure last_tick_ms gets initialized after load
  int rc = ini_load(path, load_handler, NULL);
  if (rc == -1) {
    MG_INFO(("Config %s not found, using defaults", path));
  } else {
    MG_INFO(("Loaded state from %s: hunger=%.1f happiness=%.1f feeds=%lld",
             path, s_pet.hunger, s_pet.happiness,
             (long long) s_pet.feed_count));
  }
  s_pet.last_tick_ms = 0;  // reset so first tick after load doesn't jump
}

static int save_config(const char *path) {
  pet_tick();  // bring state up to "now" before persisting
  FILE *fp = fopen(path, "w");
  if (fp == NULL) {
    MG_ERROR(("Cannot write config %s", path));
    return -1;
  }
  fprintf(fp,
          "; Web pet state. Auto-saved on shutdown.\n"
          "[pet]\n"
          "hunger     = %.4f\n"
          "happiness  = %.4f\n"
          "feed_count = %lld\n",
          s_pet.hunger, s_pet.happiness, (long long) s_pet.feed_count);
  fclose(fp);
  MG_INFO(("Saved state to %s", path));
  return 0;
}

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [-c CONFIG] [-l ADDR] [-d DIR]\n"
          "  -c CONFIG  INI file to load/save pet state (default: none)\n"
          "  -l ADDR    listen address (default: %s)\n"
          "  -d DIR     web root directory (default: %s)\n",
          prog, s_listening_addr, s_root_dir);
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  struct mg_mgr mgr;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
      s_config_path = argv[++i];
    } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
      s_listening_addr = argv[++i];
    } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      s_root_dir = argv[++i];
    } else {
      usage(argv[0]);
    }
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  setvbuf(stdout, NULL, _IONBF, 0);
  mg_log_set(MG_LL_INFO);
  mg_mgr_init(&mgr);

  if (s_config_path != NULL) load_config(s_config_path);

  if (mg_http_listen(&mgr, s_listening_addr, cb, NULL) == NULL) {
    MG_ERROR(("Cannot listen on %s", s_listening_addr));
    return 1;
  }

  MG_INFO(("Pet server listening on %s", s_listening_addr));
  MG_INFO(("Open http://localhost:8000 in your browser"));
  MG_INFO(("Config: %s", s_config_path ? s_config_path : "(none)"));
  while (s_signo == 0) mg_mgr_poll(&mgr, 1000);

  if (s_config_path != NULL) save_config(s_config_path);
  mg_mgr_free(&mgr);
  MG_INFO(("Exited on signal %d", s_signo));
  return 0;
}
