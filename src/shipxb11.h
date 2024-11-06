/*
	shipxb11
	Copyright (C) 2022 Craig McPartland

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL2_rotozoom.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ALIEN_POPULATION 10
#define ALIEN_TYPE 4
#define FPS 60
#define GAME_TITLE "Ship XB11"
#define HEIGHT 800
#define LEFT_KEY 0x4
#define LINE_Y 70
#define MAX_SOUNDS 1
#define NO_KEY 0
#define PAUSE_MSG 5
#define RIGHT_KEY 0x1
#define WIDTH 600

#define set_rect(R, X, Y, W, H) R.x = X; R.y = Y; R.w = W; R.h = H

typedef struct {
	SDL_AudioSpec audio_spec;
	SDL_bool converted;
	Uint8 *wave_buffer;
	Uint32 wave_length;
} AudioInfo;

typedef struct {
	SDL_bool playing;
	AudioInfo audio_info[MAX_SOUNDS];
	SDL_AudioDeviceID id;
	SDL_AudioSpec device_spec;
	unsigned int index;
} Audio;

typedef struct {
	SDL_bool is_animated;
	SDL_bool is_visible;
	double dx;
	double dy;
	double x;
	double y;
	int current_frame;
	int frame_count;
	int frame_delay;
	int next_frame_time;
	int width;
	int height;
	SDL_Texture **texture;
} Sprite;

typedef struct {
	SDL_bool is_exploding;
	SDL_bool missile_is_launched;
	int missile_x;
	int missile_y;
	unsigned int key;
	Sprite sprite;
} Craft;

typedef struct {
	int score_digit[7];
	int high_digit[7];
	int high;
	int score;
	int visible_high;
	int visible_score;
	int char_width[10];
	int char_height[10];
	SDL_Texture *digit_texture[10];
} Score;

typedef struct { /* Asteroid debris. */
	Craft upper_left;
	Craft upper_right;
	Craft lower_left;
	Craft lower_right;
	int quarters_remaining;
} Debris;

typedef struct {
	Audio audio;
	SDL_bool paused;
	const char *title;
	Craft alien[ALIEN_TYPE][ALIEN_POPULATION];
	Craft asteroid;
	Craft bigblue;
	Craft player;
	Debris debris;
	int alien_count;
	int alien_type;
	int height;
	int level;
	int lives;
	int width;
	Score score;
	Sprite alien_sprite[ALIEN_TYPE];
	Sprite background;
	Sprite explosion;
	Sprite line;
	Sprite missile;
	Sprite big_blue_missiles;
	Sprite player_missile;
	SDL_Texture *game_over_message;
	SDL_Texture *paused_message[PAUSE_MSG];
	SDL_Texture *pause_screen;
	SDL_Renderer *renderer;
	SDL_Window *window;
	TTF_Font *font;
} Game;

static void initialise_audio(Game *);
static void close_audio(Game *);
static int load_audio(Game *, const char *);
static int check_dimensions(Game *);
static int initialise_game(Game *);
static SDL_bool has_intersection(Sprite *, Sprite *);
static SDL_Surface *load_image_with_index(Game *, char *, unsigned int);
static void set_sprite_width_height(Sprite *);
static int load_sprite(Game *, Sprite *, char *);
static void set_sprite_defaults(Sprite *);
static void copy_sprite(Game *, Sprite *, Sprite *);
static void draw_sprite(Game *, Sprite *);
static int initialise_sprite(Game *, Sprite *, char *);
static void draw_background(Game *);
static int initialise_sdl(Game *);
static void initialise_craft(Craft *);
static void reset_player(Game *);
static void kill_asteroid(Game *);
static void reset_bigblue(Game *);
static int initialise_bigblue(Game *);
static int initialise_player(Game *);
static int initialise_alien_type(Game *, int, char *);
static void reset_aliens(Game *);
static int initialise_aliens(Game *);
static int initialise_explosion(Game *);
static int initialise_missile(Game *);
static int initialise_player_missile(Game *);
static void reset_asteroid(Game *);
static int initialise_line(Game *);
static void reset_asteroid_quarters(Game *);
static int initialise_asteroid_quarters(Game *);
static int initialise_sprites(Game *);
static void stop_animation(Sprite *);
static void explode(Game *, Craft *);
static void launch_missile(Game *);
static void create_pause_screen(Game *);
static void restart_after_game_over(Game *);
static int handle_key_down(Game *, SDL_Event *);
static int handle_key_up(Game *, SDL_Event *);
static int handle_event(Game *, SDL_Event *);
static void get_texture_dimensions(SDL_Texture *, int *, int *);
static SDL_Texture *create_text_texture(Game *, char *);
static int create_text_textures(Game *);
static void draw_lives(Game *);
static void draw_score_digits(Game *);
static void draw_high_score_digits(Game *);
static void draw_scores(Game *);
static void draw_aliens(Game *);
static void draw_asteroid_quarters(Game *);
static int render_graphics(Game *);
static void move_big_blue_missiles(Game *);
static void move_bigblue(Game *);
static void level_up(Game *);
static void move_alien_missile(Game *, Craft *);
static void check_if_player_missile_hit_alien(Game *, Craft *);
static void check_if_quarter_hit_alien(Game *, Craft *, Craft *);
static void check_if_quarters_hit_alien(Game *, Craft *);
static void check_if_player_missile_hit_asteroid(Game *);
static void check_if_alien_missile_hit_player(Game *, Craft *);
static void move_alien_ship(Game *, Craft *);
static void fire_alien_ship_missile(Game *, Craft *);
static void move_aliens(Game *);
static void move_player(Game *);
static void check_if_player_missile_hit_bigblue(Game *);
static void check_if_quarter_hit_bigblue(Game *, Craft *);
static void check_if_quarters_hit_bigblue(Game *);
static void move_player_missile(Game *);
static void move_asteroid(Game *);
static void move_asteroid_quarters(Game *);
static void move_graphics(Game *);
static void show_game_over_message(Game *);
static void bring_on_big_blue_at_random(Game *);
static void bring_on_asteroid_at_random(Game *);
static void bring_on_others_at_random(Game *);
static void show_paused_message(Game *);
static int play_game(Game *);
static void reset_game(Game *);
static int initialise_textures(Game *);
static void free_sprite(Sprite *);
static void free_graphics(Game *);

