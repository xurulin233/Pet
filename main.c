// Web pet: serve static files from web_root, plus pet state API.

#include <signal.h>
#include <math.h>
#include <time.h>
#include "mongoose.h"

static const char *s_listening_addr = "http://0.0.0.0:8000";
static const char *s_root_dir = "web_root";

static int s_signo;
static void signal_handler(int signo) { s_signo = signo; }

// Pet state. hunger/happiness range 0..100.
// hunger increases over time; feeding decreases it and bumps happiness.
// happiness drifts toward 50 when hungry, up when full.
struct pet {
  double hunger;
  double happiness;
  int64_t last_tick_ms;
  int64_t feed_count;
};

static struct pet s_pet = {30.0, 80.0, 0, 0};

static int64_t now_ms(void) {
  return (int64_t) mg_millis();
}

// Advance simulation up to "now". Called on every API hit.
static void pet_tick(void) {
  int64_t now = now_ms();
  if (s_pet.last_tick_ms == 0) {
    s_pet.last_tick_ms = now;
    return;
  }
  double dt = (double) (now - s_pet.last_tick_ms) / 1000.0;  // seconds
  s_pet.last_tick_ms = now;

  // Hunger rises ~1 per 5s. Tune to taste.
  s_pet.hunger += dt * 0.2;
  if (s_pet.hunger > 100) s_pet.hunger = 100;

  // Happiness drifts: high hunger drags it down, low hunger lets it rise.
  double target = s_pet.hunger > 70 ? 20 : (s_pet.hunger < 30 ? 90 : 50);
  double k = 0.05;  // approach speed
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

int main(void) {
  struct mg_mgr mgr;

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  setvbuf(stdout, NULL, _IONBF, 0);
  mg_log_set(MG_LL_INFO);
  mg_mgr_init(&mgr);

  if (mg_http_listen(&mgr, s_listening_addr, cb, NULL) == NULL) {
    MG_ERROR(("Cannot listen on %s", s_listening_addr));
    return 1;
  }

  MG_INFO(("Pet server listening on %s", s_listening_addr));
  MG_INFO(("Open http://localhost:8000 in your browser"));
  while (s_signo == 0) mg_mgr_poll(&mgr, 1000);
  mg_mgr_free(&mgr);
  return 0;
}
