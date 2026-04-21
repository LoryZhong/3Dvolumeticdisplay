#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

#include "mathc.h"
#include "rammel.h"
#include "input.h"
#include "graphics.h"
#include "voxel.h"
#include "timer.h"

// ── world bounds ──────────────────────────────────────────────
#define WORLD_X  VOXELS_X
#define WORLD_Y  VOXELS_Y
#define WORLD_Z  VOXELS_Z
#define CX       ((WORLD_X - 1) * 0.5f)
#define CY       ((WORLD_Y - 1) * 0.5f)
#define CZ       ((WORLD_Z - 1) * 0.5f)

// ── keyboard state ────────────────────────────────────────────
static bool key_w, key_a, key_s, key_d, key_z, key_x, key_space;

static void keys_clear(void) {
    key_w = key_a = key_s = key_d = key_z = key_x = key_space = false;
}

// ── ship ──────────────────────────────────────────────────────
static float ship_x, ship_y, ship_z;
static float ship_vx, ship_vy, ship_vz;
static const float ship_accel    = 300.0f;
static const float ship_damping  = 0.92f;
static const float ship_max_speed = 80.0f;
static int  ship_lives;
static float ship_invuln;         // invulnerability timer after respawn
static bool ship_alive;

static const pixel_t ship_body_colour  = HEXPIX(00FF55);
static const pixel_t ship_wing_colour  = HEXPIX(0055FF);
static const pixel_t ship_flame_colour = HEXPIX(FFAA00);

static void ship_init(void) {
    ship_x = CX;
    ship_y = CY;
    ship_z = CZ;
    ship_vx = ship_vy = ship_vz = 0;
    ship_alive = true;
    ship_invuln = 2.0f;
}

static void ship_clamp(void) {
    ship_x = clamp(ship_x, 2.0f, WORLD_X - 3.0f);
    ship_y = clamp(ship_y, 2.0f, WORLD_Y - 3.0f);
    ship_z = clamp(ship_z, 2.0f, WORLD_Z - 3.0f);
}

static void ship_update(float dt) {
    if (!ship_alive) return;
    if (ship_invuln > 0) ship_invuln -= dt;

    float ax = 0, ay = 0, az = 0;

    // keyboard
    if (key_d)  ax += ship_accel;
    if (key_a)  ax -= ship_accel;
    if (key_w)  ay -= ship_accel;
    if (key_s)  ay += ship_accel;
    if (key_x)  az += ship_accel;
    if (key_z)  az -= ship_accel;

    // gamepad
    ax += input_get_axis(0, AXIS_LS_X) * ship_accel;
    ay += input_get_axis(0, AXIS_LS_Y) * ship_accel;
    az += (input_get_axis(0, AXIS_RT) - input_get_axis(0, AXIS_LT)) * ship_accel;

    ship_vx += ax * dt;
    ship_vy += ay * dt;
    ship_vz += az * dt;

    ship_vx *= ship_damping;
    ship_vy *= ship_damping;
    ship_vz *= ship_damping;

    // limit speed
    float spd = sqrtf(ship_vx*ship_vx + ship_vy*ship_vy + ship_vz*ship_vz);
    if (spd > ship_max_speed) {
        float s = ship_max_speed / spd;
        ship_vx *= s;
        ship_vy *= s;
        ship_vz *= s;
    }

    ship_x += ship_vx * dt;
    ship_y += ship_vy * dt;
    ship_z += ship_vz * dt;
    ship_clamp();
}

static inline void set_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x < WORLD_X && (uint32_t)y < WORLD_Y && (uint32_t)z < WORLD_Z) {
        vol[VOXEL_INDEX(x, y, z)] = c;
    }
}

static void ship_draw(pixel_t* vol) {
    if (!ship_alive) return;

    // blink during invulnerability
    if (ship_invuln > 0 && ((int)(ship_invuln * 10) & 1)) return;

    int sx = (int)ship_x;
    int sy = (int)ship_y;
    int sz = (int)ship_z;

    //       nose pointing -Y (forward)
    //       body: 3 voxels long along Y
    set_voxel(vol, sx, sy - 2, sz, ship_body_colour);
    set_voxel(vol, sx, sy - 1, sz, ship_body_colour);
    set_voxel(vol, sx, sy,     sz, ship_body_colour);
    set_voxel(vol, sx, sy + 1, sz, ship_body_colour);

    // wings along X at sy
    set_voxel(vol, sx - 1, sy,     sz, ship_wing_colour);
    set_voxel(vol, sx - 2, sy + 1, sz, ship_wing_colour);
    set_voxel(vol, sx + 1, sy,     sz, ship_wing_colour);
    set_voxel(vol, sx + 2, sy + 1, sz, ship_wing_colour);

    // flame when thrusting forward
    if (key_w || input_get_axis(0, AXIS_LS_Y) < -0.1f) {
        set_voxel(vol, sx, sy + 2, sz, ship_flame_colour);
        if (rand() & 1)
            set_voxel(vol, sx, sy + 3, sz, HEXPIX(FF5500));
    }
}

// ── bullets ───────────────────────────────────────────────────
#define MAX_BULLETS 64

typedef struct {
    float x, y, z;
    float vy;
    float life;
    bool active;
} bullet_t;

static bullet_t bullets[MAX_BULLETS];

static const float bullet_speed = 120.0f;
static const float bullet_life  = 2.0f;
static float fire_cooldown = 0;
static const float fire_rate = 0.08f;

static void bullets_init(void) {
    memset(bullets, 0, sizeof(bullets));
}

static void bullet_spawn(void) {
    if (!ship_alive) return;
    for (int i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) {
            bullets[i].active = true;
            bullets[i].x = ship_x;
            bullets[i].y = ship_y - 3;
            bullets[i].z = ship_z;
            bullets[i].vy = -bullet_speed;
            bullets[i].life = bullet_life;
            return;
        }
    }
}

static void bullets_update(float dt) {
    if (fire_cooldown > 0) fire_cooldown -= dt;

    bool firing = key_space || input_get_button(0, BUTTON_RB, BUTTON_HELD)
                            || input_get_button(0, BUTTON_A, BUTTON_HELD);

    if (firing && fire_cooldown <= 0 && ship_alive) {
        bullet_spawn();
        fire_cooldown = fire_rate;
    }

    for (int i = 0; i < MAX_BULLETS; ++i) {
        bullet_t* b = &bullets[i];
        if (!b->active) continue;

        b->y += b->vy * dt;
        b->life -= dt;

        if (b->life <= 0 || b->y < 0 || b->y >= WORLD_Y) {
            b->active = false;
        }
    }
}

static void bullets_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_BULLETS; ++i) {
        bullet_t* b = &bullets[i];
        if (!b->active) continue;

        int bx = (int)b->x;
        int by = (int)b->y;
        int bz = (int)b->z;

        set_voxel(vol, bx, by,     bz, HEXPIX(FFFF55));
        set_voxel(vol, bx, by + 1, bz, HEXPIX(FFAA00));
    }
}

// ── asteroids (enemies) ───────────────────────────────────────
#define MAX_ASTEROIDS 32

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int   radius;
    int   hp;
    bool  active;
    pixel_t colour;
} asteroid_t;

static asteroid_t asteroids[MAX_ASTEROIDS];
static float spawn_timer = 0;
static float spawn_interval = 2.0f;
static int score = 0;
static int wave = 1;
static int kills_this_wave = 0;
static const int kills_per_wave = 8;

static void asteroids_init(void) {
    memset(asteroids, 0, sizeof(asteroids));
    spawn_timer = 1.0f;
    spawn_interval = 2.0f;
    score = 0;
    wave = 1;
    kills_this_wave = 0;
}

static float rand_f(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static void asteroid_spawn(void) {
    for (int i = 0; i < MAX_ASTEROIDS; ++i) {
        if (!asteroids[i].active) {
            asteroid_t* a = &asteroids[i];
            a->active = true;
            a->radius = 2 + rand() % 3;
            a->hp = a->radius;
            a->x = rand_f(a->radius + 2, WORLD_X - a->radius - 2);
            a->y = -a->radius;  // spawn off-screen at top
            a->z = rand_f(a->radius + 2, WORLD_Z - a->radius - 2);
            a->vy = rand_f(10.0f, 25.0f + wave * 3.0f);
            a->vx = rand_f(-8.0f, 8.0f);
            a->vz = rand_f(-5.0f, 5.0f);

            int ci = rand() % 5;
            pixel_t palette[] = {
                HEXPIX(AA5555), HEXPIX(AAAAAA), HEXPIX(55AAAA),
                HEXPIX(AA55AA), HEXPIX(AAAA55)
            };
            a->colour = palette[ci];
            return;
        }
    }
}

// ── particles (explosions) ────────────────────────────────────
#define MAX_PARTICLES 512

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    pixel_t colour;
    bool active;
} particle_t;

static particle_t particles[MAX_PARTICLES];

static void particles_init(void) {
    memset(particles, 0, sizeof(particles));
}

static void particle_spawn(float px, float py, float pz, pixel_t colour, int count) {
    for (int n = 0; n < count; ++n) {
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!particles[i].active) {
                particle_t* p = &particles[i];
                p->active = true;
                p->x = px;
                p->y = py;
                p->z = pz;
                p->vx = rand_f(-40.0f, 40.0f);
                p->vy = rand_f(-40.0f, 40.0f);
                p->vz = rand_f(-40.0f, 40.0f);
                p->life = rand_f(0.3f, 1.0f);
                p->colour = colour;
                break;
            }
        }
    }
}

static void particles_update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particle_t* p = &particles[i];
        if (!p->active) continue;

        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        p->life -= dt;

        // slow down
        p->vx *= 0.96f;
        p->vy *= 0.96f;
        p->vz *= 0.96f;

        if (p->life <= 0) {
            p->active = false;
        }
    }
}

static void particles_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particle_t* p = &particles[i];
        if (!p->active) continue;

        // fade colour
        pixel_t c = p->colour;
        if (p->life < 0.3f) {
            int r = R_PIX(c) >> 1;
            int g = G_PIX(c) >> 1;
            int b = B_PIX(c) >> 1;
            c = RGBPIX(r, g, b);
        }

        set_voxel(vol, (int)p->x, (int)p->y, (int)p->z, c);
    }
}

// ── collision ─────────────────────────────────────────────────

static float dist_sq(float x0, float y0, float z0, float x1, float y1, float z1) {
    float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    return dx*dx + dy*dy + dz*dz;
}

static void asteroids_update(float dt) {
    // spawning
    spawn_timer -= dt;
    if (spawn_timer <= 0) {
        asteroid_spawn();
        spawn_timer = spawn_interval;
    }

    for (int i = 0; i < MAX_ASTEROIDS; ++i) {
        asteroid_t* a = &asteroids[i];
        if (!a->active) continue;

        a->x += a->vx * dt;
        a->y += a->vy * dt;
        a->z += a->vz * dt;

        // bounce off walls (X, Z)
        if (a->x < a->radius || a->x >= WORLD_X - a->radius) {
            a->vx = -a->vx;
            a->x = clamp(a->x, (float)a->radius, (float)(WORLD_X - a->radius - 1));
        }
        if (a->z < a->radius || a->z >= WORLD_Z - a->radius) {
            a->vz = -a->vz;
            a->z = clamp(a->z, (float)a->radius, (float)(WORLD_Z - a->radius - 1));
        }

        // off screen bottom => deactivate
        if (a->y > WORLD_Y + a->radius + 5) {
            a->active = false;
            continue;
        }

        // bullet-asteroid collision
        float hit_r_sq = (float)(a->radius + 1) * (a->radius + 1);
        for (int j = 0; j < MAX_BULLETS; ++j) {
            bullet_t* b = &bullets[j];
            if (!b->active) continue;

            if (dist_sq(b->x, b->y, b->z, a->x, a->y, a->z) < hit_r_sq) {
                b->active = false;
                a->hp--;

                // hit spark
                particle_spawn(b->x, b->y, b->z, HEXPIX(FFFFFF), 4);

                if (a->hp <= 0) {
                    // explode
                    particle_spawn(a->x, a->y, a->z, a->colour, 20 + a->radius * 5);
                    particle_spawn(a->x, a->y, a->z, HEXPIX(FFFF55), 8);
                    a->active = false;

                    score += a->radius * 10;
                    kills_this_wave++;

                    if (kills_this_wave >= kills_per_wave) {
                        wave++;
                        kills_this_wave = 0;
                        spawn_interval = max(0.4f, spawn_interval - 0.2f);
                        printf("wave %d!\n", wave);
                    }
                    break;
                }
            }
        }

        if (!a->active) continue;

        // ship-asteroid collision
        if (ship_alive && ship_invuln <= 0) {
            float col_r = (float)(a->radius + 2);
            if (dist_sq(ship_x, ship_y, ship_z, a->x, a->y, a->z) < col_r * col_r) {
                // ship hit
                particle_spawn(ship_x, ship_y, ship_z, ship_body_colour, 30);
                particle_spawn(ship_x, ship_y, ship_z, HEXPIX(FF5500), 20);

                ship_lives--;
                if (ship_lives <= 0) {
                    ship_alive = false;
                    printf("game over! score: %d  waves: %d\n", score, wave);
                } else {
                    ship_x = CX;
                    ship_y = CY;
                    ship_z = CZ;
                    ship_vx = ship_vy = ship_vz = 0;
                    ship_invuln = 2.0f;
                    printf("lives: %d\n", ship_lives);
                }
            }
        }
    }
}

static void asteroids_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_ASTEROIDS; ++i) {
        asteroid_t* a = &asteroids[i];
        if (!a->active) continue;

        int cx = (int)a->x;
        int cy = (int)a->y;
        int cz = (int)a->z;
        int r  = a->radius;
        int rsq = r * r;

        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dz = -r; dz <= r; ++dz) {
                    if (dx*dx + dy*dy + dz*dz <= rsq) {
                        set_voxel(vol, cx+dx, cy+dy, cz+dz, a->colour);
                    }
                }
            }
        }
    }
}

// ── starfield background ──────────────────────────────────────
#define NUM_STARS 120

typedef struct {
    float x, y, z;
} star_t;

static star_t stars[NUM_STARS];

static void stars_init(void) {
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].x = rand_f(0, WORLD_X);
        stars[i].y = rand_f(0, WORLD_Y);
        stars[i].z = rand_f(0, WORLD_Z);
    }
}

static void stars_update(float dt) {
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].y += 15.0f * dt;
        if (stars[i].y >= WORLD_Y) {
            stars[i].y -= WORLD_Y;
            stars[i].x = rand_f(0, WORLD_X);
            stars[i].z = rand_f(0, WORLD_Z);
        }
    }
}

static void stars_draw(pixel_t* vol) {
    for (int i = 0; i < NUM_STARS; ++i) {
        int sx = (int)stars[i].x;
        int sy = (int)stars[i].y;
        int sz = (int)stars[i].z;
        if ((uint32_t)sx < WORLD_X && (uint32_t)sy < WORLD_Y && (uint32_t)sz < WORLD_Z) {
            // dim stars
            vol[VOXEL_INDEX(sx, sy, sz)] = HEXPIX(555555);
        }
    }
}

// ── HUD: show lives as small dots in a corner ─────────────────
static void hud_draw(pixel_t* vol) {
    for (int i = 0; i < ship_lives && i < 5; ++i) {
        set_voxel(vol, 2 + i * 3, WORLD_Y - 3, WORLD_Z - 2, HEXPIX(FF0000));
        set_voxel(vol, 3 + i * 3, WORLD_Y - 3, WORLD_Z - 2, HEXPIX(FF0000));
        set_voxel(vol, 2 + i * 3, WORLD_Y - 3, WORLD_Z - 3, HEXPIX(FF0000));
        set_voxel(vol, 3 + i * 3, WORLD_Y - 3, WORLD_Z - 3, HEXPIX(FF0000));
    }
}

// ── game over restart ─────────────────────────────────────────
static float gameover_timer = 0;

static void game_reset(void) {
    ship_init();
    ship_lives = 3;
    bullets_init();
    asteroids_init();
    particles_init();
    stars_init();
    keys_clear();
    gameover_timer = 0;
}

// ── main ──────────────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!voxel_buffer_map()) {
        printf("waiting for voxel buffer\n");
        do {
            sleep(1);
        } while (!voxel_buffer_map());
    }

    timer_init();
    game_reset();

    printf("=== SPACE SHOOTER ===\n");
    printf("  WASD  move\n");
    printf("  Z/X   descend/ascend\n");
    printf("  SPACE fire\n");
    printf("  R     restart\n");
    printf("  ESC   quit\n\n");

    input_set_nonblocking();

    for (int ch = 0; ch != 27; ch = getchar()) {
        timer_tick();
        float dt = (float)timer_delta_time * 0.001f;
        dt = clamp(dt, 0.001f, 0.1f);

        // ── keyboard input ──
        switch (ch) {
            case 'w': key_w = true;  break;
            case 'a': key_a = true;  break;
            case 's': key_s = true;  break;
            case 'd': key_d = true;  break;
            case 'z': key_z = true;  break;
            case 'x': key_x = true;  break;
            case ' ': key_space = true; break;
            case 'r':
            case 'R':
                game_reset();
                printf("restarted! score: 0\n");
                break;
        }

        input_update();

        // ── update ──
        ship_update(dt);
        bullets_update(dt);
        asteroids_update(dt);
        particles_update(dt);
        stars_update(dt);

        if (!ship_alive) {
            gameover_timer += dt;
            // auto restart after 5s or on gamepad press
            if (gameover_timer > 5.0f || input_get_button(0, BUTTON_A, BUTTON_PRESSED)) {
                game_reset();
            }
        }

        // ── draw ──
        pixel_t* vol = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(vol);

        stars_draw(vol);
        asteroids_draw(vol);
        bullets_draw(vol);
        ship_draw(vol);
        particles_draw(vol);
        hud_draw(vol);

        voxel_buffer_swap();

        // reset one-shot keys each frame
        keys_clear();

        timer_sleep_until(TIMER_SINCE_TICK, 30);
    }

    voxel_buffer_unmap();

    return 0;
}
