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

#include "shipxb11.h"

static void initialise_audio(Game *game)
{
	SDL_AudioSpec obtained;
	int status = 1;
	game->audio.index = 0;
	game->audio.playing = SDL_FALSE;

	for (unsigned int i = 0; i < MAX_SOUNDS; i++) {
		game->audio.audio_info[i].wave_buffer = NULL;
		game->audio.audio_info[i].wave_length = 0;
		game->audio.audio_info[i].converted = SDL_FALSE;
	}

#if SDL_PATCHLEVEL > 15
	status = SDL_GetAudioDeviceSpec(0, 0, &game->audio.device_spec);
#endif
	game->audio.device_spec.callback = NULL;

	if (status != 0) {
		game->audio.device_spec.freq = 44100;
		game->audio.device_spec.format = AUDIO_S16;
		game->audio.device_spec.channels = 2;
		game->audio.device_spec.samples = 4096;
	}

	game->audio.id = SDL_OpenAudioDevice(NULL, 0, &game->audio.device_spec, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);

	if (game->audio.id == 0) {
		return;
	}

	game->audio.device_spec.freq = obtained.freq;
	game->audio.device_spec.format = obtained.format;
	game->audio.device_spec.channels = obtained.channels;
	game->audio.device_spec.silence = obtained.silence;
	game->audio.device_spec.samples = obtained.samples;
	game->audio.device_spec.size = obtained.size;
	game->audio.device_spec.callback = NULL;
	return;
}

static void close_audio(Game *game)
{
	SDL_CloseAudioDevice(game->audio.id);

	for (unsigned int i = 0; i < game->audio.index; i++) {
		if (game->audio.audio_info[i].wave_buffer == NULL) {
			continue;
		}

		if (game->audio.audio_info[i].converted) {
			SDL_free(game->audio.audio_info[i].wave_buffer);
		} else {
			SDL_FreeWAV(game->audio.audio_info[i].wave_buffer);
		}
	}
}

static int load_audio(Game *game, const char *path)
{
	SDL_AudioCVT cvt;
	SDL_AudioSpec *audio_spec;
	audio_spec = SDL_LoadWAV(path, &game->audio.audio_info[game->audio.index].audio_spec, &game->audio.audio_info[game->audio.index].wave_buffer, &game->audio.audio_info[game->audio.index].wave_length);

	if (audio_spec == NULL) {
		return 1;
	}

	SDL_BuildAudioCVT(&cvt, audio_spec->format, audio_spec->channels, audio_spec->freq, game->audio.device_spec.format, game->audio.device_spec.channels, game->audio.device_spec.freq);

	if (cvt.needed) {
		cvt.len = game->audio.audio_info[game->audio.index].wave_length;
		cvt.buf = (Uint8 *)SDL_malloc(cvt.len * cvt.len_mult);
		memcpy(cvt.buf, game->audio.audio_info[game->audio.index].wave_buffer, game->audio.audio_info[game->audio.index].wave_length);
		SDL_ConvertAudio(&cvt);
		SDL_FreeWAV(game->audio.audio_info[game->audio.index].wave_buffer);
		game->audio.audio_info[game->audio.index].converted = SDL_TRUE;
		game->audio.audio_info[game->audio.index].wave_buffer = cvt.buf;
		game->audio.audio_info[game->audio.index].wave_length = cvt.len_cvt;
	}

	game->audio.index++;
	SDL_PauseAudioDevice(game->audio.id, 0);
	return 0;
}

static int check_dimensions(Game *game)
{
	SDL_Rect rect;
	int status = SDL_GetDisplayBounds(0, &rect);

	if (status != 0) {
		fprintf(stderr, "%s: SDL_GetDisplayBounds failed in function %s\n", game->title, __func__);
		fprintf(stderr, "%s\n", SDL_GetError());
	} else {
		if (rect.w < WIDTH || rect.h < HEIGHT) {
			fprintf(stderr, "%s: Screen must be at least %d x %d.\n", game->title, WIDTH, HEIGHT);
			return 1;
		}
	}

	game->width = WIDTH;
	game->height = HEIGHT;
	return 0;
}

static int initialise_game(Game *game)
{
	for (int i = 0; i < 7; i++) {
		game->score.high_digit[i] = 0;
	}

	game->paused = SDL_TRUE;
	game->title = GAME_TITLE;
	game->score.visible_high = 0;
	game->score.high = 0;
	game->debris.quarters_remaining = 0;
	reset_game(game);

	int status = initialise_sdl(game);

	if (status != 0) {
		return status;
	}

	status = check_dimensions(game);

	if (status != 0) {
		TTF_CloseFont(game->font);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	game->window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, game->width, game->height, 0);

	if (game->window == NULL) {
		fprintf(stderr, "%s: In function %s ", game->title, __func__);
		fprintf(stderr, "SDL_CreateWindow failed. %s\n", SDL_GetError());
		TTF_CloseFont(game->font);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	game->renderer = SDL_CreateRenderer(game->window, -1, 0);

	if (game->renderer == NULL) {
		fprintf(stderr, "%s: In function %s ", game->title, __func__);
		fprintf(stderr, "SDL_CreateRenderer failed. %s\n", SDL_GetError());
		SDL_DestroyWindow(game->window);
		TTF_CloseFont(game->font);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	SDL_SetRenderDrawColor(game->renderer, 255, 255, 0, SDL_ALPHA_OPAQUE);
	game->pause_screen = NULL;
	game->game_over_message = NULL;

	status = initialise_textures(game);

	if (status != 0) {
		SDL_DestroyRenderer(game->renderer);
		SDL_DestroyWindow(game->window);
		TTF_CloseFont(game->font);
		TTF_Quit();
		SDL_Quit();
		return status;
	}

	return initialise_sprites(game);
}

static SDL_bool has_intersection(Sprite *s1, Sprite *s2)
{
	return !(s2->x > (s1->x + s1->width) || (s2->x + s2->width) < s1->x || s2->y > (s1->y + s1->height) || (s2->y + s2->height) < s1->y);
}

static SDL_Surface *load_image_with_index(Game *game, char *path, unsigned int indx)
{
	SDL_Surface *surface = NULL;
	size_t path_len = strlen(path) + 3;
	char *ext = strrchr(path, '.');
	char *filename = (char *)malloc(path_len * sizeof(char));

	if (filename == NULL) {
		fprintf(stderr, "%s: malloc returned NULL in function %s\n", game->title, __func__);
		exit(1);
	}

	snprintf(filename, path_len, "%.*s%02d%s", (int)(ext - path), path, indx, ext);
	surface = IMG_Load(filename);

	if (surface == NULL && indx == 0) {
		fprintf(stderr, "%s: In function %s\n", game->title, __func__);
		fprintf(stderr, "%s: Failed to load %s.\n", game->title, filename);
	}

	free(filename);
	return surface;
}

static void set_sprite_width_height(Sprite *sprite)
{
	int acc, height, width;
	Uint32 format;
	SDL_QueryTexture(sprite->texture[0], &format, &acc, &width, &height);
	sprite->width = width;
	sprite->height = height;
}

static int load_sprite(Game *game, Sprite *sprite, char *path)
{
	int indx = 0;
	SDL_Surface *surface;

	while ((surface = load_image_with_index(game, path, indx)) != NULL) {
		sprite->texture = realloc(sprite->texture, sizeof(SDL_Texture **) * (indx + 1));

		if (sprite->texture == NULL) {
			fprintf(stderr, "%s: realloc returned NULL in function %s\n", game->title, __func__);
			exit(1);
		}

		sprite->texture[indx] = SDL_CreateTextureFromSurface(game->renderer, surface);
		SDL_FreeSurface(surface);

		if (sprite->texture[indx] == NULL) {
			for (int i = 0; i < indx; i++) {
				SDL_DestroyTexture(sprite->texture[i]);
			}

			return 1;
		}

		indx++;
	}

	if (indx == 0) {
		return 1;
	}

	set_sprite_width_height(sprite);
	sprite->frame_count = indx - 1;
	return 0;
}

static void set_sprite_defaults(Sprite *sprite)
{
	sprite->texture = NULL;
	sprite->x = sprite->y = 0.0;
	sprite->width = sprite->height = 0;
	sprite->current_frame = sprite->frame_delay = sprite->next_frame_time = 0;
	sprite->dx = sprite->dy = 0.0;
	sprite->is_visible = SDL_FALSE;
	sprite->is_animated = SDL_FALSE;
}

static void copy_sprite(Game *game, Sprite *copy, Sprite *sprite)
{
	int i = sprite->frame_count + 1;
	set_sprite_defaults(copy);
	copy->width = sprite->width;
	copy->height = sprite->height;
	copy->frame_count = sprite->frame_count;
	copy->texture = (SDL_Texture **)malloc(sizeof(SDL_Texture *) * i);

	if (copy->texture == NULL) {
		fprintf(stderr, "%s: malloc returned NULL in function %s\n", game->title, __func__);
		exit(1);
	}

	do {
		i--;
		copy->texture[i] = sprite->texture[i];
	} while (i > 0);
}

static void draw_sprite(Game *game, Sprite *sprite)
{
	if (!sprite->is_visible) {
		return ;
	}

	SDL_Rect drect = { (int)sprite->x, (int)sprite->y, sprite->width, sprite->height };
	SDL_RenderCopy(game->renderer, sprite->texture[sprite->current_frame], NULL, &drect);

	if (!sprite->is_animated) {
		return;
	}

	if (sprite->next_frame_time != 0) {
		sprite->next_frame_time--;
		return;
	}

	sprite->next_frame_time = sprite->frame_delay;

	if (sprite->current_frame < sprite->frame_count) {
		sprite->current_frame++;
	} else {
		sprite->current_frame = 0;
	}
}

static int initialise_sprite(Game *game, Sprite *sprite, char *image_path)
{
	set_sprite_defaults(sprite);
	return load_sprite(game, sprite, image_path);
}

static void draw_background(Game *game)
{
	static int y;
	SDL_Rect srect = { 0, 0, game->width, game->height - y };
	SDL_Rect drect = { 0, y, game->width, game->height - y };
	SDL_RenderCopy(game->renderer, game->background.texture[0], &srect, &drect);
	set_rect(srect, 0, game->height - y, game->width, y);
	set_rect(drect, 0, 0, game->width, y);
	SDL_RenderCopy(game->renderer, game->background.texture[0], &srect, &drect);
	y++;

	if (y == game->height) {
		y = 0;
	}
}

static int initialise_sdl(Game *game)
{
	int status = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS);

	if (status != 0) {
		fprintf(stderr, "%s: In function %s ", game->title, __func__);
		fprintf(stderr, "SDL_Init failed. %s\n", SDL_GetError());
		return 1;
	}

	status = TTF_Init();
	
	if (status != 0) {
		fprintf(stderr, "%s: In function %s ", game->title, __func__);
		fprintf(stderr, "TTF_Init failed. %s\n", TTF_GetError());
		SDL_Quit();
		return 1;
	}

	game->font = TTF_OpenFont(DATADIR"/BigBottomCartoon.ttf", 18);
	
	if (game->font == NULL) {
		fprintf(stderr, "%s: In function %s ", game->title, __func__);
		fprintf(stderr, "Failed to open font. %s\n", TTF_GetError());
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	return 0;
}

static void initialise_craft(Craft *craft)
{
	craft->is_exploding = SDL_FALSE;
	craft->missile_is_launched = SDL_FALSE;
}

static void reset_player(Game *game)
{
	game->player_missile.x = game->player_missile.y = 0;
	game->player_missile.is_visible = SDL_FALSE;
	game->player.sprite.is_visible = SDL_TRUE;
}

static void kill_asteroid(Game *game)
{
	game->asteroid.sprite.is_visible = SDL_FALSE;
	game->asteroid.is_exploding = SDL_FALSE;
	game->debris.upper_left.sprite.is_visible = SDL_FALSE;
	game->debris.upper_right.sprite.is_visible = SDL_FALSE;
	game->debris.lower_left.sprite.is_visible = SDL_FALSE;
	game->debris.lower_right.sprite.is_visible = SDL_FALSE;
}

static void reset_bigblue(Game *game)
{
	initialise_craft(&game->bigblue);
	game->big_blue_missiles.is_visible = SDL_FALSE;
	game->bigblue.sprite.is_visible = SDL_FALSE;
	game->bigblue.sprite.x = game->width;
	game->bigblue.sprite.y = game->height / 2;
	game->bigblue.sprite.frame_delay = 3;
}

static int initialise_bigblue(Game *game)
{
	int status = initialise_sprite(game, &game->bigblue.sprite, DATADIR"/bigblue.png");

	if (status != 0) {
		return status;
	}

	reset_bigblue(game);
	return 0;
}

static int initialise_player(Game *game)
{
	initialise_craft(&game->player);
	game->player.key = NO_KEY;
	int status = initialise_sprite(game, &game->player.sprite, DATADIR"/player.png");
	game->player.sprite.x = game->width / 2 - game->player.sprite.width / 2;
	game->player.sprite.y = game->height - game->player.sprite.height - 20;
	game->player.sprite.is_animated = SDL_TRUE;
	game->player.sprite.frame_delay = 1;
	game->player.sprite.is_visible = SDL_TRUE;
	return status;
}

static int initialise_alien_type(Game *game, int indx, char *path)
{
	int status = initialise_sprite(game, &game->alien_sprite[indx], path);

	if (status != 0) {
		return status;
	}

	for (int i = 0; i < game->alien_count; i++) {
		initialise_craft(&game->alien[indx][i]);
		copy_sprite(game, &game->alien[indx][i].sprite, &game->alien_sprite[indx]);
		game->alien[indx][i].sprite.is_animated = SDL_TRUE;
	}

	return 0;
}

static void reset_aliens(Game *game)
{
	int leader_x = 5;
	int leader_y = 20;

	for (int i = 0; i < game->alien_type; i++) {
		for (int j = 0; j < game->alien_count; j++) {
			game->alien[i][j].missile_is_launched = SDL_FALSE;
			game->alien[i][j].sprite.is_visible = SDL_TRUE;
			game->alien[i][j].is_exploding = SDL_FALSE;
			game->alien[i][j].sprite.dx = ((i & 1) << 2) - 2;
			game->alien[i][j].sprite.dy = 0.1;
			game->alien[i][j].sprite.x = leader_x + j * (game->alien[i][j].sprite.width + 20.0);
			game->alien[i][j].sprite.y = leader_y + (i + 1) * (game->alien[i][j].sprite.height + 20.0);
		}
	}
}

static int initialise_aliens(Game *game)
{
	int count = 0;
	int status = initialise_alien_type(game, count++, DATADIR"/purple.png");

	if (status == 0) {
		status = initialise_alien_type(game, count++, DATADIR"/green.png");
	}

	if (status == 0) {
		status = initialise_alien_type(game, count++, DATADIR"/yellow.png");
	}

	if (status == 0) {
		status = initialise_alien_type(game, count, DATADIR"/cyan.png");
	}

	reset_aliens(game);
	return status;
}

static int initialise_explosion(Game *game)
{
	int status = initialise_sprite(game, &game->explosion, DATADIR"/explosion.png");
	game->explosion.is_animated = SDL_TRUE;
	return status;
}

static int initialise_missile(Game *game)
{
	int status = initialise_sprite(game, &game->missile, DATADIR"/missile.png");
	game->missile.x = game->missile.y = 0;
	game->missile.frame_delay = 3;
	game->missile.is_animated = SDL_TRUE;
	game->missile.is_visible = SDL_TRUE;
	return status;
}

static int initialise_player_missile(Game *game)
{
	int status = initialise_sprite(game, &game->player_missile, DATADIR"/playmis.png");
	game->player_missile.x = game->player_missile.y = 0;
	game->player_missile.is_visible = SDL_FALSE;
	game->player_missile.frame_delay = 3;
	game->player_missile.is_animated = SDL_TRUE;
	return status;
}

static void reset_asteroid(Game *game)
{
	int x[2] = { game->width, -game->asteroid.sprite.width };
	int rand_zero_one = rand() & 1;
	initialise_craft(&game->asteroid);
	initialise_craft(&game->debris.upper_left);
	initialise_craft(&game->debris.upper_right);
	initialise_craft(&game->debris.lower_left);
	initialise_craft(&game->debris.lower_right);
	game->asteroid.sprite.x = x[rand_zero_one];
	game->asteroid.sprite.y = LINE_Y + (rand() & 128);
	game->asteroid.sprite.dx = (rand_zero_one << 1) - 1;
	game->asteroid.sprite.dy = 1;
	game->asteroid.sprite.is_visible = SDL_TRUE;
}

static int initialise_line(Game *game)
{
	int status = initialise_sprite(game, &game->line, DATADIR"/line.png");
	game->line.x = 50;
	game->line.y = LINE_Y;
	game->line.is_visible = SDL_TRUE;
	return status;
}

static void reset_asteroid_quarters(Game *game)
{
	int x = game->asteroid.sprite.x;
	int y = game->asteroid.sprite.y;
	initialise_craft(&game->debris.upper_left);
	initialise_craft(&game->debris.upper_right);
	initialise_craft(&game->debris.lower_left);
	initialise_craft(&game->debris.lower_right);
	game->debris.upper_left.sprite.x = x;
	game->debris.upper_left.sprite.y = y;
	game->debris.upper_right.sprite.x = x + game->asteroid.sprite.width / 2;
	game->debris.upper_right.sprite.y = y;
	game->debris.lower_left.sprite.x = x;
	game->debris.lower_left.sprite.y = y + game->asteroid.sprite.height / 2;
	game->debris.lower_right.sprite.x = x + game->asteroid.sprite.width / 2;
	game->debris.lower_right.sprite.y = y + game->asteroid.sprite.height / 2;
	game->debris.upper_left.sprite.dx = -0.25;
	game->debris.upper_left.sprite.dy = -1;
	game->debris.upper_right.sprite.dx = 0.25;
	game->debris.upper_right.sprite.dy = -1;
	game->debris.lower_left.sprite.dx = -0.25;
	game->debris.lower_left.sprite.dy = 1;
	game->debris.lower_right.sprite.dx = 0.25;
	game->debris.lower_right.sprite.dy = 1;
	game->debris.upper_left.sprite.is_visible = SDL_TRUE;
	game->debris.upper_right.sprite.is_visible = SDL_TRUE;
	game->debris.lower_left.sprite.is_visible = SDL_TRUE;
	game->debris.lower_right.sprite.is_visible = SDL_TRUE;
	game->debris.quarters_remaining = 4;
}

static int initialise_asteroid_quarters(Game *game)
{
	int status = initialise_sprite(game, &game->debris.upper_left.sprite, DATADIR"/ul.png");

	if (status == 0) {
		status = initialise_sprite(game, &game->debris.upper_right.sprite, DATADIR"/ur.png");
		game->debris.upper_left.sprite.is_animated = SDL_TRUE;
	}

	if (status == 0) {
		status = initialise_sprite(game, &game->debris.lower_left.sprite, DATADIR"/ll.png");
		game->debris.upper_right.sprite.is_animated = SDL_TRUE;
	}

	if (status == 0) {
		status = initialise_sprite(game, &game->debris.lower_right.sprite, DATADIR"/lr.png");
		game->debris.lower_left.sprite.is_animated = SDL_TRUE;
	}

	game->debris.lower_right.sprite.is_animated = SDL_TRUE;
	return status;
}

static int initialise_sprites(Game *game)
{
	int status = initialise_bigblue(game);

	if (status == 0) {
		status = initialise_player(game);
	}

	if (status == 0) {
		status = initialise_aliens(game);
	}

	if (status == 0) {
		status = initialise_sprite(game, &game->background, DATADIR"/background.jpg");
	}

	if (status == 0) {
		status = initialise_explosion(game);
	}

	if (status == 0) {
		status = initialise_missile(game);
	}

	if (status == 0) {
		status = initialise_player_missile(game);
	}

	if (status == 0) {
		status = initialise_line(game);
	}

	if (status == 0) {
		status = initialise_sprite(game, &game->big_blue_missiles, DATADIR"/missiles.png");
	}

	if (status == 0) {
		status = initialise_sprite(game, &game->asteroid.sprite, DATADIR"/asteroid.png");
	}

	if (status == 0) {
		status = initialise_asteroid_quarters(game);
	}

	return status;
}

static void stop_animation(Sprite *sprite)
{
	sprite->is_animated = SDL_FALSE;
	sprite->current_frame = 0;
	sprite->next_frame_time = 0;
}

static void explode(Game *game, Craft *craft)
{
	game->explosion.is_visible = SDL_TRUE;
	game->explosion.x = craft->sprite.x + craft->sprite.width / 2 - game->explosion.width / 2;
	game->explosion.y = craft->sprite.y + craft->sprite.height / 2 - game->explosion.height / 2;

	if (craft->sprite.is_visible) {
		draw_sprite(game, &game->explosion);

		if (game->explosion.current_frame == game->explosion.frame_count) {
			game->explosion.current_frame = 0;
			craft->is_exploding = SDL_FALSE;

			if (craft == &game->player && game->lives > 0) {
				game->lives--;
			} else {
				craft->sprite.is_visible = SDL_FALSE;				 
			}

			game->audio.playing = SDL_FALSE;
			return;
		}
	}

	if (game->audio.playing == SDL_FALSE) {
		game->audio.playing = SDL_TRUE;
		SDL_ClearQueuedAudio(game->audio.id);
		SDL_QueueAudio(game->audio.id, game->audio.audio_info[0].wave_buffer, game->audio.audio_info[0].wave_length);
	}
}

static void launch_missile(Game *game)
{
	int launcher_x[4] = { 3, 9, 22, 28 };
	static unsigned int i;

	if (!game->player_missile.is_visible) {
		game->player_missile.is_visible = SDL_TRUE;
		game->player_missile.x = game->player.sprite.x + launcher_x[i & 3];
		game->player_missile.y = game->player.sprite.y;
		i++;
	}
}

static void create_pause_screen(Game *game)
{
	if (!game->paused) {
		return;
	}

	SDL_Surface *capture;
	Uint32 format = SDL_GetWindowPixelFormat(game->window);

	if (format != SDL_PIXELFORMAT_UNKNOWN) {
		capture = SDL_CreateRGBSurfaceWithFormat(0, game->width, game->height, SDL_BITSPERPIXEL(format), format);
	} else {
		fprintf(stderr, "%s: %s\n", game->title, SDL_GetError());
		format = SDL_PIXELFORMAT_ARGB8888;
		capture = SDL_CreateRGBSurface(0, game->width, game->height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
	}

	if (capture == NULL) {
		fprintf(stderr, "%s: %s\n", game->title, SDL_GetError());
		return;
	}

	if (game->pause_screen != NULL) {
		SDL_DestroyTexture(game->pause_screen);
	}

	SDL_RenderReadPixels(game->renderer, NULL, format, capture->pixels, capture->pitch);
	game->pause_screen = SDL_CreateTextureFromSurface(game->renderer, capture);
	SDL_FreeSurface(capture);
}

static void restart_after_game_over(Game *game)
{
	reset_game(game);
	game->paused = SDL_FALSE;
}

static int handle_key_down(Game *game, SDL_Event *event)
{
	if (game->pause_screen == NULL && game->paused) {
		game->paused = SDL_FALSE;
		return 1;
	}

	if (game->paused && event->key.keysym.scancode != SDL_SCANCODE_P && event->key.keysym.scancode != SDL_SCANCODE_Q && event->key.keysym.scancode != SDL_SCANCODE_N) {
		return 1;
	}

	switch (event->key.keysym.scancode) {
		case SDL_SCANCODE_LEFT:
			game->player.key = LEFT_KEY;
			break;
		case SDL_SCANCODE_RIGHT:
			game->player.key = RIGHT_KEY;
			break;
		case SDL_SCANCODE_SPACE:
		case SDL_SCANCODE_UP:
			launch_missile(game);
			break;
		case SDL_SCANCODE_N:
			restart_after_game_over(game);
			break;
		case SDL_SCANCODE_P:
			if (game->lives != 0) {
				game->paused ^= SDL_TRUE;
				create_pause_screen(game);
			}
			break;
		case SDL_SCANCODE_Q:
			return 0;
		default:
			break;
	}

	return 1;
}

static int handle_key_up(Game *game, SDL_Event *event)
{
	switch (event->key.keysym.scancode) {
		case SDL_SCANCODE_LEFT:
			game->player.key &= ~LEFT_KEY;
			break;
		case SDL_SCANCODE_RIGHT:
			game->player.key &= ~RIGHT_KEY;
			break;
		default:
			break;
	}

	return 0;
}

static int handle_event(Game *game, SDL_Event *event)
{
	switch(event->type) {
		case SDL_QUIT:
			return 0;
		case SDL_KEYDOWN:
			return handle_key_down(game, event);
		case SDL_KEYUP:
			handle_key_up(game, event);
			break;
		default:
			return 1;
	}

	return 1;
}

static void get_texture_dimensions(SDL_Texture *texture, int *width, int *height)
{
	int acc;
	Uint32 format;
	SDL_QueryTexture(texture, &format, &acc, width, height);
}

static SDL_Texture *create_text_texture(Game *game, char *text)
{
	SDL_Color colour = { 255, 255, 0, 128 };
	SDL_Surface *surface = TTF_RenderText_Solid(game->font, text, colour);

	if (surface == NULL) {
		return NULL;
	}

	SDL_Texture *texture = SDL_CreateTextureFromSurface(game->renderer, surface);
	SDL_FreeSurface(surface);
	return texture;
}

static int create_text_textures(Game *game)
{
	char string[] = { '\0', '\0' };

	for (int i = 0; i < 10; i++) {
		string[0] = i + '0';
		game->score.digit_texture[i] = create_text_texture(game, string);

		if (game->score.digit_texture[i] != NULL) {
			continue;
		}

		for (int j = 0; j < i; j++) {
			SDL_DestroyTexture(game->score.digit_texture[j]);
		}

		return 1;
	}

	return 0;
}

static void draw_lives(Game *game)
{
	int sx = game->player.sprite.x;
	int sy = game->player.sprite.y;
	game->player.sprite.x = game->width / 2 - ((game->player.sprite.width + 2) * game->lives) / 2;
	game->player.sprite.y = 10;
	int inc = game->player.sprite.width + 2;

	for (int i = 0; i < game->lives; i++) {
		draw_sprite(game, &game->player.sprite);
		game->player.sprite.x += inc;
	}

	game->player.sprite.x = sx;
	game->player.sprite.y = sy;
}

static void draw_score_digits(Game *game)
{
	SDL_Rect drect;
	int span = 0;

	for (int i = 0; i < 7; i++) {
		set_rect(drect, 5 + span, 1, game->score.char_width[game->score.score_digit[i]], game->score.char_height[game->score.score_digit[i]]);
		SDL_RenderCopy(game->renderer, game->score.digit_texture[game->score.score_digit[i]], NULL, &drect);
		span += game->score.char_width[game->score.score_digit[i]];
	}
}

static void draw_high_score_digits(Game *game)
{
	SDL_Rect drect;
	int span = 0;

	for (int i = 0; i < 7; i++) {
		set_rect(drect, WIDTH - 120 + span, 1, game->score.char_width[game->score.high_digit[i]], game->score.char_height[game->score.high_digit[i]]);
		SDL_RenderCopy(game->renderer, game->score.digit_texture[game->score.high_digit[i]], NULL, &drect);
		span += game->score.char_width[game->score.high_digit[i]];
	}
}

static void draw_scores(Game *game)
{
	int i = 6;

	if (game->score.visible_score < game->score.score) {
		while (i >= 0) {
			game->score.score_digit[i]++;

			if (game->score.score_digit[i] > 9) {
				game->score.score_digit[i] = 0;
				i--;
			} else {
				break;
			}
		}

		game->score.visible_score++;
	}

	if (game->score.visible_high < game->score.high) {
		i = 6;

		while (i >= 0) {
			game->score.high_digit[i]++;

			if (game->score.high_digit[i] > 9) {
				game->score.high_digit[i] = 0;
				i--;
			} else
				break;
		}

		game->score.visible_high++;
	}

	draw_score_digits(game);
	draw_high_score_digits(game);

	if (game->score.score > game->score.high) {
		game->score.high = game->score.score;
	}
}

static void draw_aliens(Game *game)
{
	for (int i = 0; i < game->alien_type; i++) {
		for (int j = 0; j < game->alien_count; j++) {
			draw_sprite(game, &game->alien[i][j].sprite);

			if (game->alien[i][j].is_exploding) {
				explode(game, &game->alien[i][j]);
			}

			if (game->alien[i][j].missile_is_launched) {
				game->missile.x = game->alien[i][j].missile_x;
				game->missile.y = game->alien[i][j].missile_y;
				draw_sprite(game, &game->missile);
			}
		}
	}
}

static void draw_asteroid_quarters(Game *game)
{
	draw_sprite(game, &game->debris.upper_left.sprite);
	draw_sprite(game, &game->debris.upper_right.sprite);
	draw_sprite(game, &game->debris.lower_left.sprite);
	draw_sprite(game, &game->debris.lower_right.sprite);
}

static int render_graphics(Game *game)
{
	draw_aliens(game);
	draw_sprite(game, &game->bigblue.sprite);
	draw_sprite(game, &game->asteroid.sprite);
	draw_asteroid_quarters(game);

	if (game->bigblue.is_exploding) {
		explode(game, &game->bigblue);
	}

	if (game->asteroid.is_exploding) {
		explode(game, &game->asteroid);
	}

	draw_sprite(game, &game->player.sprite);

	if (game->player.is_exploding) {
		explode(game, &game->player);
	}

	draw_sprite(game, &game->player_missile);
	draw_sprite(game, &game->big_blue_missiles);
	draw_lives(game);
	draw_scores(game);
	draw_sprite(game, &game->line);
	return 0;
}

static void move_big_blue_missiles(Game *game)
{
	if (game->big_blue_missiles.is_visible) {
		game->big_blue_missiles.y += 2;

		if (game->big_blue_missiles.y > game->height) {
			game->big_blue_missiles.y = 0;
			game->big_blue_missiles.is_visible = SDL_FALSE;
			return;
		}

		if (has_intersection(&game->big_blue_missiles, &game->player.sprite)) {
			game->big_blue_missiles.y = 0;
			game->big_blue_missiles.is_visible = SDL_FALSE;
			game->player.is_exploding = SDL_TRUE;
		}

		return;
	}

	if ((rand() & 1023) < game->level && game->bigblue.sprite.is_visible) {
		game->big_blue_missiles.x = game->bigblue.sprite.x;
		game->big_blue_missiles.y = game->bigblue.sprite.y + 101;
		game->big_blue_missiles.is_visible = SDL_TRUE;
	}
}

static void move_bigblue(Game *game)
{
	static int hit_time, dx = -2;

	if (game->bigblue.sprite.is_animated) {
		hit_time++;

		if (hit_time == 500) {
			stop_animation(&game->bigblue.sprite);
			hit_time = 0;
		}
	} else {
		hit_time = 0;
	}

	game->bigblue.sprite.x += dx;

	if (game->bigblue.sprite.x < -game->bigblue.sprite.width) {
		game->bigblue.sprite.x = game->width;
	}
}

static void level_up(Game *game)
{
	game->level++;

	if (game->alien_type < ALIEN_TYPE) {
		game->alien_type++;
	}

	if (game->lives < 6) {
		game->lives++;
	}

	reset_aliens(game);
}

static void move_alien_missile(Game *game, Craft *alien)
{
	if (!alien->missile_is_launched) {
		return;
	}

	alien->missile_y += 2;
	game->missile.x = alien->missile_x;
	game->missile.y = alien->missile_y;

	if (alien->missile_y > game->height) {
		alien->missile_is_launched = SDL_FALSE;
	}
}

static void check_if_player_missile_hit_alien(Game *game, Craft *alien)
{
	if (!game->player_missile.is_visible || !alien->sprite.is_visible) {
		return;
	}

	if (has_intersection(&alien->sprite, &game->player_missile)) {
		alien->is_exploding = SDL_TRUE;
		game->player_missile.is_visible = SDL_FALSE;
		game->score.score += 20;
	}
}

static void check_if_quarter_hit_alien(Game *game, Craft *quarter, Craft *alien)
{
	if (!quarter->sprite.is_visible || alien->is_exploding) {
		return;
	}

	if (has_intersection(&alien->sprite, &quarter->sprite)) {
		alien->is_exploding = SDL_TRUE;
		game->score.score += 20;
	}
}

static void check_if_quarters_hit_alien(Game *game, Craft *alien)
{
	if (!alien->sprite.is_visible) {
		return;
	}

	check_if_quarter_hit_alien(game, &game->debris.upper_left, alien);
	check_if_quarter_hit_alien(game, &game->debris.upper_right, alien);
	check_if_quarter_hit_alien(game, &game->debris.lower_left, alien);
	check_if_quarter_hit_alien(game, &game->debris.lower_right, alien);
}

static void check_if_player_missile_hit_asteroid(Game *game)
{
	if (!game->player_missile.is_visible || !game->asteroid.sprite.is_visible) {
		return;
	}

	if (has_intersection(&game->asteroid.sprite, &game->player_missile)) {
		game->player_missile.is_visible = SDL_FALSE;
		game->score.score += 20;
		reset_asteroid_quarters(game);
		game->asteroid.is_exploding = SDL_TRUE;
	}
}

static void check_if_alien_missile_hit_player(Game *game, Craft *alien)
{
	if (!alien->missile_is_launched) {
		return;
	}

	if (has_intersection(&game->missile, &game->player.sprite)) {
		alien->missile_is_launched = SDL_FALSE;
		game->player.is_exploding = SDL_TRUE;
	}
}

static void move_alien_ship(Game *game, Craft *alien)
{
	alien->sprite.x += alien->sprite.dx;
	alien->sprite.y += alien->sprite.dy;

	if (alien->sprite.x > game->width - alien->sprite.width || alien->sprite.x < 0) {
		alien->sprite.dx = -alien->sprite.dx;
	}

	if (game->level <= ALIEN_TYPE) {
		return;
	}

	if ((rand() & 8191) > 8189) {
		alien->sprite.dy = 1.0;
	}

	if (alien->sprite.y > 600 || alien->sprite.y < 72) {
		alien->sprite.dy = -alien->sprite.dy;
	}
}

static void fire_alien_ship_missile(Game *game, Craft *alien)
{
	if ((rand() & 1023) >= game->level || alien->missile_is_launched) {
		return;
	}

	alien->missile_is_launched = SDL_TRUE;
	alien->missile_x = alien->sprite.x + alien->sprite.width / 2;
	alien->missile_y = alien->sprite.y + alien->sprite.height;
}

static void move_aliens(Game *game)
{
	int aliens_alive = 0;

	for (int i = 0; i < game->alien_type; i++) {
		for (int j = 0; j < game->alien_count; j++) {
			move_alien_missile(game, &game->alien[i][j]);
			check_if_alien_missile_hit_player(game, &game->alien[i][j]);

			if (game->alien[i][j].sprite.is_visible) {
				aliens_alive++;
				check_if_player_missile_hit_alien(game, &game->alien[i][j]);
				check_if_quarters_hit_alien(game, &game->alien[i][j]);
				move_alien_ship(game, &game->alien[i][j]);
				fire_alien_ship_missile(game, &game->alien[i][j]);
			}
		}
	}

	if (aliens_alive == 0) {
		level_up(game);
	}
}

static void move_player(Game *game)
{
	static int x = WIDTH / 2;

	if (game->player.key == LEFT_KEY && x >= game->player.sprite.x) {
		x -= 2;
	} else if (game->player.key == RIGHT_KEY && x <= game->player.sprite.x) {
		x += 2;
	}

	if (game->player.sprite.x > x) {
		if (game->player.sprite.x > 0) {
			game->player.sprite.x--;
		}
	} else if (game->player.sprite.x < x) {
		if (game->player.sprite.x < game->width - game->player.sprite.width) {
			game->player.sprite.x++;
		}
	}
}

static void check_if_player_missile_hit_bigblue(Game *game)
{
	if (!game->bigblue.sprite.is_visible || !has_intersection(&game->bigblue.sprite, &game->player_missile)) {
		return;
	}

	game->player_missile.is_visible = SDL_FALSE;

	if (game->bigblue.sprite.is_animated) {
		stop_animation(&game->bigblue.sprite);
		game->bigblue.is_exploding = SDL_TRUE;
		game->score.score += 100;
	} else {
		game->bigblue.sprite.is_animated = SDL_TRUE;
	}
}

static void check_if_quarter_hit_bigblue(Game *game, Craft *quarter)
{
	if (!has_intersection(&game->bigblue.sprite, &quarter->sprite)) {
		return;
	}

	if (game->bigblue.sprite.is_animated) {
		stop_animation(&game->bigblue.sprite);
		game->bigblue.is_exploding = SDL_TRUE;
		game->score.score += 100;
	} else {
		game->bigblue.sprite.is_animated = SDL_TRUE;
	}
}

static void check_if_quarters_hit_bigblue(Game *game)
{
	if (!game->bigblue.sprite.is_visible) {
		return;
	}

	check_if_quarter_hit_bigblue(game, &game->debris.upper_left);
	check_if_quarter_hit_bigblue(game, &game->debris.upper_right);
	check_if_quarter_hit_bigblue(game, &game->debris.lower_left);
	check_if_quarter_hit_bigblue(game, &game->debris.lower_right);
}

static void move_player_missile(Game *game)
{
	if (!game->player_missile.is_visible) {
		return;
	}

	game->player_missile.y -= 5;

	if (game->player_missile.y < LINE_Y) {
		game->player_missile.is_visible = SDL_FALSE;
	}

	check_if_player_missile_hit_bigblue(game);
}

static void move_asteroid(Game *game)
{
	if (!game->asteroid.sprite.is_visible) {
		return;
	}

	game->asteroid.sprite.x += game->asteroid.sprite.dx;
	game->asteroid.sprite.y += game->asteroid.sprite.dy;
	check_if_player_missile_hit_asteroid(game);

	if (game->asteroid.sprite.x > game->width || game->asteroid.sprite.y > game->height || game->asteroid.sprite.x < -game->asteroid.sprite.width) {
		game->asteroid.sprite.is_visible = SDL_FALSE;
	}
}

static void move_asteroid_quarters(Game *game)
{
	if (game->debris.quarters_remaining == 0) {
		return;
	}

	if (game->debris.upper_left.sprite.is_visible) {
		game->debris.upper_left.sprite.x += game->debris.upper_left.sprite.dx;
		game->debris.upper_left.sprite.y += game->debris.upper_left.sprite.dy;

		if (game->debris.upper_left.sprite.x < -game->debris.upper_left.sprite.width || game->debris.upper_left.sprite.y < -game->debris.upper_left.sprite.height) {
			game->debris.quarters_remaining--;
			game->debris.upper_left.sprite.is_visible = SDL_FALSE;
		}
	}

	if (game->debris.upper_right.sprite.is_visible) {
		game->debris.upper_right.sprite.x += game->debris.upper_right.sprite.dx;
		game->debris.upper_right.sprite.y += game->debris.upper_right.sprite.dy;

		if (game->debris.upper_right.sprite.x > game->width || game->debris.upper_right.sprite.y < -game->debris.upper_right.sprite.height) {
			game->debris.quarters_remaining--;
			game->debris.upper_right.sprite.is_visible = SDL_FALSE;
		}
	}

	if (game->debris.lower_left.sprite.is_visible) {
		game->debris.lower_left.sprite.x += game->debris.lower_left.sprite.dx;
		game->debris.lower_left.sprite.y += game->debris.lower_left.sprite.dy;

		if (game->debris.lower_left.sprite.x < -game->debris.lower_left.sprite.width || game->debris.lower_left.sprite.y > game->height) {
			game->debris.quarters_remaining--;
			game->debris.lower_left.sprite.is_visible = SDL_FALSE;
		}
	}

	if (game->debris.lower_right.sprite.is_visible) {
		game->debris.lower_right.sprite.x += game->debris.lower_right.sprite.dx;
		game->debris.lower_right.sprite.y += game->debris.lower_right.sprite.dy;

		if (game->debris.lower_right.sprite.x > game->width || game->debris.lower_right.sprite.y > game->height) {
			game->debris.quarters_remaining--;
			game->debris.lower_right.sprite.is_visible = SDL_FALSE;
		}
	}

	check_if_quarters_hit_bigblue(game);
}

static void move_graphics(Game *game)
{
	move_bigblue(game);
	move_big_blue_missiles(game);
	move_aliens(game);
	move_player(game);
	move_player_missile(game);
	move_asteroid(game);
	move_asteroid_quarters(game);
}

static void show_game_over_message(Game *game)
{
	static int width, height;

	if (width == 0) {
		get_texture_dimensions(game->game_over_message, &width, &height);
	}

	SDL_Rect rect;
	set_rect(rect, game->width / 2 - width / 2, game->height / 2 - height / 2 - 40, width, height);
	SDL_RenderCopy(game->renderer, game->game_over_message, NULL, &rect);
}

static void bring_on_big_blue_at_random(Game *game)
{
	if (game->bigblue.sprite.is_visible) {
		return;
	}

	if ((rand() & 8191) > 8189) {
		reset_bigblue(game);
		game->bigblue.sprite.is_visible = SDL_TRUE;
	}
}

static void bring_on_asteroid_at_random(Game *game)
{
	if (game->asteroid.sprite.is_visible) {
		return;
	}

	if (game->debris.quarters_remaining != 0) {
		return;
	}

	if ((rand() & 8191) > 8182) {
		reset_asteroid(game);
	}
}

static void bring_on_others_at_random(Game *game)
{
	bring_on_big_blue_at_random(game);
	bring_on_asteroid_at_random(game);
}

static void show_paused_message(Game *game)
{
	int hp = 0;
	static int width[PAUSE_MSG], height[PAUSE_MSG];
	SDL_Rect rect;

	if (width[0] == 0) {
		for (int i = 0; i < PAUSE_MSG; i++) {
			get_texture_dimensions(game->paused_message[i], &width[i], &height[i]);
		}
	}

	for (int i = 0; i < PAUSE_MSG; i++) {
		set_rect(rect, game->width / 2 - width[i] / 2, game->height / 2 - height[i] / 2 + hp, width[i], height[i]);
		SDL_RenderCopy(game->renderer, game->paused_message[i], NULL, &rect);
		hp += height[i] + 10;
	}

	double x = game->player_missile.x;
	double y = game->player_missile.y;
	game->player_missile.x = game->width / 2 - width[0] / 2 - 24;
	game->player_missile.y = game->height / 2 - height[0] / 2 + 10;
	SDL_bool is_visible = game->player_missile.is_visible;
	game->player_missile.is_visible = SDL_TRUE;
	draw_sprite(game, &game->player_missile);
	game->player_missile.is_visible = is_visible;
	game->player_missile.x = x;
	game->player_missile.y = y;
	x = game->player.sprite.x;
	y = game->player.sprite.y;
	game->player.sprite.x = game->width / 2 - width[1] / 2 - 40;
	game->player.sprite.y = game->height / 2 - height[1] / 2 + height[0] + 10;
	draw_sprite(game, &game->player.sprite);
	game->player.sprite.x = x;
	game->player.sprite.y = y;
}

static int play_game(Game *game)
{
	SDL_Event event;
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 100000;
	Uint32 frame_delay_ticks = (Uint32)((double)SDL_GetPerformanceFrequency() / (double)FPS);
	Uint64 start_time = SDL_GetPerformanceCounter();

	while (1) {
		if (SDL_PollEvent(&event) != 0) {
			if (handle_event(game, &event) == 0) {
				break;
			}
		}

		if (game->paused) {
			SDL_Rect srect = { 0, 0, game->width, game->height };
			SDL_Rect drect = { 0, 0, game->width, game->height };

			if (game->pause_screen != NULL) {
				SDL_RenderCopy(game->renderer, game->pause_screen, &srect, &drect);
			} else {
				SDL_RenderCopy(game->renderer, game->background.texture[0], &srect, &drect);
			}

			if (game->lives == 0) {
				show_game_over_message(game); 
			}	 

			show_paused_message(game);
			SDL_RenderPresent(game->renderer);
			nanosleep(&ts, NULL);
			continue;
		}

		if (game->lives == 0) {
			game->paused = SDL_TRUE;
			create_pause_screen(game);
		}

		draw_background(game);
		render_graphics(game);
		bring_on_others_at_random(game);
		move_graphics(game);
		SDL_RenderPresent(game->renderer);
		Uint64 diff = SDL_GetPerformanceCounter() - start_time;

		while (diff < frame_delay_ticks) {
			nanosleep(&ts, NULL);
			diff = SDL_GetPerformanceCounter() - start_time;
		}

		start_time = SDL_GetPerformanceCounter();
	}

	return 0;
}

static void reset_game(Game *game)
{
	for (int i = 0; i < 7; i++) {
		game->score.score_digit[i] = 0;
	}

	game->alien_count = ALIEN_POPULATION;
	game->level = 1;
	game->lives = 3;
	game->score.score = 0;
	game->score.visible_score = 0;
	game->alien_type = 1;
	reset_aliens(game);
	reset_bigblue(game);
	reset_player(game);
	kill_asteroid(game);
}

static int initialise_textures(Game *game)
{
	int acc;
	Uint32 format;

	int status = create_text_textures(game);

	if (status != 0) {
		return 1;
	}

	for (int i = 0; i < 10; i++) {
		SDL_QueryTexture(game->score.digit_texture[i], &format, &acc, &game->score.char_width[i], &game->score.char_height[i]);
	}

	game->game_over_message = create_text_texture(game, "Game Over! (Press n for new game)");

	if (game->game_over_message == NULL) {
		return 1;
	}

	int i = 0;
	game->paused_message[i] = create_text_texture(game, " - Space or cursor up.");

	if (game->paused_message[i] == NULL) {
		return 1;
	}

	game->paused_message[++i] = create_text_texture(game, " - Cursor left / right.");

	if (game->paused_message[i] == NULL) {
		return 1;
	}

	game->paused_message[++i] = create_text_texture(game, "P - Pause / Play");

	if (game->paused_message[i] == NULL) {
		return 1;
	}

	game->paused_message[++i] = create_text_texture(game, "N - New Game");

	if (game->paused_message[i] == NULL) {
		return 1;
	}

	game->paused_message[++i] = create_text_texture(game, "Q - Quit");

	if (game->paused_message[i] == NULL) {
		return 1;
	}

	return 0;
}

static void free_sprite(Sprite *sprite)
{
	for (int i = 0; i < sprite->frame_count; i++) {
		SDL_DestroyTexture(sprite->texture[i]);
	}

	free(sprite->texture);
}

static void free_graphics(Game *game)
{
	free_sprite(&game->background);
	free_sprite(&game->explosion);
	free_sprite(&game->line);
	free_sprite(&game->missile);
	free_sprite(&game->big_blue_missiles);
	free_sprite(&game->player_missile);
	free_sprite(&game->asteroid.sprite);
	free_sprite(&game->bigblue.sprite);
	free_sprite(&game->player.sprite);
	free_sprite(&game->debris.upper_left.sprite);
	free_sprite(&game->debris.upper_right.sprite);
	free_sprite(&game->debris.lower_left.sprite);
	free_sprite(&game->debris.lower_right.sprite);
	SDL_DestroyTexture(game->pause_screen);
	SDL_DestroyTexture(game->game_over_message);

	for (int i = 0; i < ALIEN_TYPE; i++) {
		for (int j = 0; j < ALIEN_POPULATION; j++) {
			free(game->alien[i][j].sprite.texture);
		}

		free_sprite(&game->alien_sprite[i]);
	}

	for (int i = 0; i < 10; i++) {
		SDL_DestroyTexture(game->score.digit_texture[i]);
	}

	for (int i = 0; i < PAUSE_MSG; i++) {
		SDL_DestroyTexture(game->paused_message[i]);
	}

	SDL_DestroyRenderer(game->renderer);
	SDL_DestroyWindow(game->window);
	TTF_Quit();
	SDL_Quit();
}

int main(int argc, char *argv[])
{
	Game game;
	int status = initialise_game(&game);

	if (status != 0) {
		return 1;
	}

	initialise_audio(&game);

	if (game.audio.id != 0) {
		load_audio(&game, DATADIR"/explode.wav");
	}

	SDL_ShowCursor(SDL_DISABLE);
	play_game(&game);
	SDL_ShowCursor(SDL_ENABLE);
	TTF_CloseFont(game.font);

	if (game.audio.id != 0) {
		close_audio(&game);
	}

	free_graphics(&game);
	return 0;
}

