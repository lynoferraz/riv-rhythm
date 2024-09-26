// Header including all RIV APIs
#include <riv.h>
#include <math.h>
#define SEQT_IMPL
#include "seqt.h"

enum {
    MAGIC_SIZE = 4,

    SCREEN_SIZE = 256,
    TILE_SIZE = 20,
    TIME_SIG = 4,

    TOP_Y = 20,
    PERFECT_DISTANCE = 2,
    NICE_DISTANCE = 10,
    GOOD_DISTANCE = 20,
    ANIMATION_N_FRAMES = 15,
    PRESS_N_FRAMES = 5,
    UPDATE_DIFFICULTY_N_FRAMES = 560,
    N_COLS = 4,
    MAX_COLS = 6,
    BREATHING_FRAMES = 90,
    UPDATING_FRAMES = 30,
    MORE_FREQ = 5,
    N_ANIMATION_FRAMES = 40,
};

enum {
    STATE_NOTHING,
    STATE_MISS,
    STATE_BAD,
    STATE_GOOD,
    STATE_NICE,
    STATE_PERFECT,
};

// Game state
bool started; // true when game has started
int start_frame;
bool ended; // true when game has ended
bool last_tick = false;
int frame_increase_speed;
int frame_decrease_empty;
uint64_t spirtesheet_controls;

static int col_sprite_ids[MAX_COLS] = {3,1,4,5,0,2};
static int col_sprite_order[MAX_COLS] = {2,3,4,5,1,6};
static int x_cols[MAX_COLS];
static int key_codes[MAX_COLS] = {RIV_GAMEPAD1_LEFT,RIV_GAMEPAD1_UP,RIV_GAMEPAD1_DOWN,RIV_GAMEPAD1_RIGHT,RIV_GAMEPAD1_L1,RIV_GAMEPAD1_L2};

static bool pressed[MAX_COLS];
static int  sliding_arrows[MAX_COLS][SCREEN_SIZE];
static int  pressed_match[MAX_COLS];
static int  animation_match[MAX_COLS];
static int  animation_frames[MAX_COLS];
static bool sliding_speed_indicator[SCREEN_SIZE];

float tile_speed;
float new_tile_speed;
bool perfect_hit;
bool nice_hit;
bool good_hit;

uint64_t combo_moves = 0;
uint64_t consecutive_misses = 0;
int score = 0;

int base_score = 100;

int n_perfects = 0;
int n_nice = 0;
int n_good = 0;
int n_miss = 0;
int n_bad = 0;
int max_combo_score = 0;
int max_combo = 0;

int music_bpm = 0;
float hits_per_second;
float note_period;
float beat_guide_tick_size;
float frames_per_beat;
seqt_sound *sound;

float perfect_multiplier = 4.0;
float nice_multiplier = 1.5;
float good_multiplier = 1.0;
int empty_ticks_decrease = 1;
int empty_ticks = 0;
float tile_speed_modifier = 3.0;
float tick_freq = 30;
int n_cols = 4;
bool show_stats = true;
int max_misses = 5;
int n_loops = 4;


// utils

bool rythm_tick() {
    return ((riv->frame / (int)tick_freq) % 2 == 0);
}

void read_incard_data(uint8_t *data,int from,int size) {
    char magic[5];
    for (int i=0; i<MAGIC_SIZE ; i++) magic[i] = data[from + i];
    magic[MAGIC_SIZE] = '\0';
    // riv_printf("MAGIC: %s\n",magic);
    if (!strcmp(magic,"SEQT")) {
        if (music_bpm) {
            return;
        }
        seqt_init();
        // music_bpm = music->bpm;
        seqt_play((seqt_source*)(data + from),n_loops);
        sound = &seqt.sounds[1];
        music_bpm = sound->source->bpm;

        // seqt_source* music = sound->source;

        // riv_printf("music magic %s\n",music->magic);
        // riv_printf("music version %d\n",music->version);
        // riv_printf("music flags %d\n",music->flags);
        // riv_printf("music bpm %d\n",music->bpm);

    } else if (!strcmp(magic,"MICS")) {
        int wi = from + MAGIC_SIZE; // curr_word_index

        uint32_t n_incards = data[from + wi+3] | (uint32_t)data[from + wi+2] << 8
            | (uint32_t)data[from + wi+1] << 16 | (uint32_t)data[from + wi] << 24;
        wi += MAGIC_SIZE;

        for (int i=0; i<n_incards ; i++) {

            uint32_t from_incard_i = data[from + wi+3] | (uint32_t)data[from + wi+2] << 8
                | (uint32_t)data[from + wi+1] << 16 | (uint32_t)data[from + wi] << 24;
            wi += MAGIC_SIZE;

            uint32_t size_incard_i =data[from + wi+3] | (uint32_t)data[from + wi+2] << 8
                | (uint32_t)data[from + wi+1] << 16 | (uint32_t)data[from + wi] << 24;
            wi += MAGIC_SIZE;

            read_incard_data(data,from_incard_i,size_incard_i);
        }

    }
}



void initialize() {

    if (riv->incard_len > 0) {
        read_incard_data(riv->incard,0,riv->incard_len);
    }

    if (!music_bpm) {
        seqt_init();
        seqt_play(seqt_make_source_from_file("04.seqt.rivcard"), n_loops);
        sound = &seqt.sounds[1];
        music_bpm = sound->source->bpm;
    }

    int total_blanks_x_px = SCREEN_SIZE - n_cols * TILE_SIZE;
    int spaces_x_px = total_blanks_x_px / (n_cols + 1);
    int leftover_spaces_x_px = (total_blanks_x_px - spaces_x_px*(n_cols + 1)) / 2 ;

    int col_inds[MAX_COLS];
    for (int c = 0; c < n_cols; c++) col_inds[c] = c;

    // bubble sort
    bool swapped;
    do {
        swapped = false;
        for (int c = 1; c < n_cols; c++) {
            if (col_sprite_order[col_inds[c-1]] > col_sprite_order[col_inds[c]]) { //swap
                int aux_ind = col_inds[c-1];
                col_inds[c-1] = col_inds[c];
                col_inds[c] = aux_ind;
                swapped =true;
            }
        }
    } while (swapped);

    x_cols[col_inds[0]] = spaces_x_px + leftover_spaces_x_px;
    for (int c = 1; c < n_cols; c++) {
        x_cols[col_inds[c]] = spaces_x_px + (TILE_SIZE + spaces_x_px) * c;
    }

    // initialize start animation
    int frames_distance_animation = 2 * TILE_SIZE;
    for (int c = 0; c < n_cols; c++) sliding_arrows[col_inds[c]][SCREEN_SIZE - 1] = - frames_distance_animation*(c + 1);
    for (int c = 0; c < n_cols; c++) sliding_arrows[col_inds[c]][SCREEN_SIZE - 1 - TILE_SIZE] = - frames_distance_animation*(1 + n_cols + c );

    riv->outcard_len = riv_snprintf((char*)riv->outcard, RIV_SIZE_OUTCARD,
        "JSON{\"frame\":%d,\"score\":%d,\"final_speed\":%.5f,\"max_combo\":%d,\"max_combo_score\":%d,\"n_perfect\":%d,\"n_nice\":%d,\"n_good\":%d,\"n_miss\":%d}",
        riv->frame,score, new_tile_speed, max_combo, max_combo_score,n_perfects,n_nice,n_good,n_miss);
}

// Called when game starts
void start_game() {
    riv_printf("GAME START\n");

    for (int c = 0; c < n_cols; c++) {
        for (int i=0; i < SCREEN_SIZE; i++) {
            sliding_arrows[c][i] = 0;
        }
    }
    started = true;
    start_frame = riv->frame;


    hits_per_second = music_bpm*((1.0*TIME_SIG)/60); // beats per second
    note_period = hits_per_second/riv->target_fps;
    frames_per_beat = riv->target_fps/(music_bpm/60.0);

    tick_freq = frames_per_beat;

    new_tile_speed = (tile_speed_modifier * TILE_SIZE) / tick_freq;
    tile_speed = new_tile_speed;
}

// Called when game ends
void end_game() {
    riv_printf("GAME OVER\n");
    ended = true;
    // Quit in 3 seconds
    riv->quit_frame = riv->frame + 1*riv->target_fps;
}

bool update_score(int state) {
    float multiplier = 0;
    switch (state) {
    case STATE_PERFECT:
        combo_moves++;
        consecutive_misses = 0;
        multiplier = (1.0 + (combo_moves * 0.1)) * perfect_multiplier;
        n_perfects++;
        break;
    case STATE_NICE:
        combo_moves++;
        consecutive_misses = 0;
        multiplier = (1.0 + (combo_moves * 0.1)) * nice_multiplier;
        n_nice++;
        break;
    case STATE_GOOD:
        combo_moves++;
        consecutive_misses = 0;
        multiplier = (1.0 + (combo_moves * 0.1)) * good_multiplier;
        n_good++;
        break;
    case STATE_BAD:
        combo_moves = 0;
        consecutive_misses++;
        n_bad++;
    case STATE_MISS:
        combo_moves = 0;
        consecutive_misses++;
        n_miss++;
    default:
        return false; 
    }
    int press_score = ceil(100 * multiplier);
    if (combo_moves > max_combo) max_combo = combo_moves;
    if (press_score > max_combo_score) max_combo_score = press_score;
    score += press_score;
    return true;
}

// Update game logic
void update_game() {

    // update difficulty
    if ((riv->frame - start_frame) % UPDATE_DIFFICULTY_N_FRAMES == 0) {
        if ((riv->frame - start_frame) % (MORE_FREQ * UPDATE_DIFFICULTY_N_FRAMES) == 0 && empty_ticks > 1) { 
            // update rand_blank_vs_four
            frame_decrease_empty = riv->frame + BREATHING_FRAMES;
        } else {
            // every other update
            if (tick_freq > 1) 
                frame_increase_speed = riv->frame + BREATHING_FRAMES;
        }
    }
    if (riv->frame == frame_decrease_empty) empty_ticks -= empty_ticks_decrease;
    if (riv->frame == frame_increase_speed) {
        tick_freq--;
        tile_speed = new_tile_speed;
        new_tile_speed = (tile_speed_modifier * TILE_SIZE) / tick_freq;
    }

    // end game
    uint64_t note_frame = (uint64_t)floor(((double)(riv->frame - sound->start_frame) * hits_per_second * sound->speed) / riv->target_fps);
    if (riv->keys[RIV_GAMEPAD1_SELECT].press || consecutive_misses >= max_misses || 
            sound->loops >= 0 && (note_frame / seqt_get_source_track_size(sound->source)) >= (uint64_t)sound->loops) {
        end_game();
        return;
    }

    // play music
    seqt_poll();

    // reset pressed
    perfect_hit = false;
    nice_hit = false;
    good_hit = false;
    for (int c = 0; c < n_cols; c++) {
        pressed[c] = false;
        pressed_match[c] = STATE_NOTHING;
    }

    // detect colums presses and misses
    for (int c = 0; c < N_COLS; c++) {
        // update animation
        if (animation_frames[c]) animation_frames[c]--;
        else animation_match[c] = 0;

        // detect pressed
        if (riv->keys[key_codes[c]].down) pressed[c] = true;

        bool left_screen = false;
        for (int i = 0; i < SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) {
                // check match press
                bool match = false;
                if (!pressed_match[c] && riv->keys[key_codes[c]].press) {
                    // distance from sliding to arrow
                    int distance = abs(i - TOP_Y);
                    if (distance < PERFECT_DISTANCE) {
                        pressed_match[c] = STATE_PERFECT;
                        match = true;
                        perfect_hit = true;
                        animation_frames[c] = N_ANIMATION_FRAMES;
                        animation_match[c] = STATE_PERFECT;
                    } else if (distance < NICE_DISTANCE) {
                        pressed_match[c] = STATE_NICE;
                        match = true;
                        nice_hit = true;
                        animation_frames[c] = N_ANIMATION_FRAMES;
                        animation_match[c] = STATE_NICE;
                    } else if (distance < GOOD_DISTANCE) {
                        pressed_match[c] = STATE_GOOD;
                        match = true;
                        good_hit = true;
                        animation_frames[c] = N_ANIMATION_FRAMES;
                        animation_match[c] = STATE_GOOD;
                    } else {
                        pressed_match[c] = STATE_BAD;
                        animation_frames[c] = N_ANIMATION_FRAMES;
                        animation_match[c] = STATE_BAD;
                    }
                    update_score(pressed_match[c]);
                }

                if (!match) {

                    int s = sliding_arrows[c][i] > frame_increase_speed ? new_tile_speed : tile_speed;
                    // riv_printf("if %i - nf %i s: %i speed %i new %i\n",sliding_arrows[c][i],new_speed_frame,s,new_speed,speed);
                    if (i-tile_speed >= 0) {
                        sliding_arrows[c][i-(int)round(tile_speed)] = sliding_arrows[c][i];
                    } else {
                        left_screen = true;
                    }
                }
                sliding_arrows[c][i] = 0;
            }
        }
        if (left_screen || (!pressed_match[c] && riv->keys[key_codes[c]].press)) {
            pressed_match[c] = STATE_MISS;
            animation_frames[c] = N_ANIMATION_FRAMES;
            animation_match[c] = STATE_MISS;
            update_score(STATE_MISS);
        }
    }

    bool curr_tick = rythm_tick();
    if (curr_tick != last_tick && riv->frame > frame_decrease_empty && riv->frame > frame_increase_speed) {
        // add new 
        // riv_printf("update %u\n",riv_rand_uint(3));
        uint64_t col_index = riv_rand_uint(3+empty_ticks);
        if (col_index < n_cols) {
            sliding_arrows[col_index][SCREEN_SIZE-1] = riv->frame;
        }
        last_tick = curr_tick;
    }

    riv->outcard_len = riv_snprintf((char*)riv->outcard, RIV_SIZE_OUTCARD,
        "JSON{\"frame\":%d,\"score\":%d,\"final_speed\":%.5f,\"max_combo\":%d,\"max_combo_score\":%d,\"n_perfect\":%d,\"n_nice\":%d,\"n_good\":%d,\"n_miss\":%d,\"n_bad\":%d}",
        riv->frame,score, new_tile_speed, max_combo, max_combo_score,n_perfects,n_nice,n_good,n_miss,n_bad);
}

// Draw the game map
void draw_game() {
    // riv_clear(rythm_tick() || ended ? RIV_COLOR_DARKSLATE : RIV_COLOR_SLATE);
    riv_clear(perfect_hit || nice_hit ? RIV_COLOR_SLATE : RIV_COLOR_DARKSLATE);

    // perfect hit animation
    int dx = 0;
    int dy = 0;
    if (perfect_hit) {
        dx = riv_rand_int(-1,1);
        dy = riv_rand_int(-1,1);
    }

    for (int c = 0; c < n_cols; c++) {
        // draw press resul

        // draw press result
        switch (animation_match[c]) {
        case STATE_PERFECT:
            riv_draw_text("PERFECT!", RIV_SPRITESHEET_FONT_5X7, RIV_BOTTOMLEFT, x_cols[c] + riv_rand_int(-1,1) + dx, TOP_Y - 2 + riv_rand_int(-1,1) + dy, 1, (animation_frames[c] / 6) % 2 ? RIV_COLOR_GOLD : RIV_COLOR_ORANGE);
            break;
        case STATE_NICE:
            riv_draw_text("Nice!", RIV_SPRITESHEET_FONT_5X7, RIV_BOTTOMLEFT, x_cols[c] + dx, TOP_Y - 2 + dy, 1, (animation_frames[c] / 10) % 2 ? RIV_COLOR_GREEN : RIV_COLOR_LIGHTGREEN);
            break;
        case STATE_GOOD:
            riv_draw_text("Good", RIV_SPRITESHEET_FONT_5X7, RIV_BOTTOMLEFT, x_cols[c] + dx, TOP_Y - 2 + dy, 1, (animation_frames[c] / 12) % 2 ? RIV_COLOR_LIGHTBLUE : RIV_COLOR_LIGHTBLUE);
            break;
        case STATE_BAD:
            riv_draw_text("Bad", RIV_SPRITESHEET_FONT_5X7, RIV_BOTTOMLEFT, x_cols[c] + dx, TOP_Y - 2 + dy, 1, (animation_frames[c] / 15) % 2 ? RIV_COLOR_LIGHTRED : RIV_COLOR_RED);
            break;
        case STATE_MISS:
            riv_draw_text("Miss", RIV_SPRITESHEET_FONT_5X7, RIV_BOTTOMLEFT, x_cols[c] + dx, TOP_Y - 2 + dy, 1, (animation_frames[c] / 15) % 2 ? RIV_COLOR_LIGHTGREY : RIV_COLOR_GREY);
            break;
        default:
            break;
        }
        // if (nearest_distances[c] < SCREEN_SIZE && riv->frame - last_presses[c] < ANIMATION_N_FRAMES) {
        //     if (nearest_distances[c] < PERFECT_DISTANCE) {
        //         riv_draw_text("PERFECT", RIV_SPRITESHEET_FONT_3X5, RIV_BOTTOM, x_cols[c] + dx, TOP_Y + dy -4, 1, RIV_COLOR_GOLD);
        //     } else if (nearest_distances[c] < NICE_DISTANCE) {
        //         riv_draw_text("Nice", RIV_SPRITESHEET_FONT_3X5, RIV_BOTTOM, x_cols[c] + dx, TOP_Y + dy -4, 1, RIV_COLOR_GREEN);
        //     } else if (nearest_distances[c] < GOOD_DISTANCE) {
        //         riv_draw_text("Good", RIV_SPRITESHEET_FONT_3X5, RIV_BOTTOM, x_cols[c] + dx, TOP_Y + dy -4, 1, RIV_COLOR_LIGHTBLUE);
        //     } else riv_draw_text("Miss", RIV_SPRITESHEET_FONT_3X5, RIV_BOTTOM, x_cols[c] + dx, TOP_Y + dy -4, 1, RIV_COLOR_RED);
        // }

        // draw press animation
        if (pressed[c]) {

            riv->draw.pal_enabled = true;
            riv->draw.pal[RIV_COLOR_BLACK] = RIV_COLOR_RED; // red
            riv->draw.pal[RIV_COLOR_WHITE] = RIV_COLOR_LIGHTPEACH;
            riv_draw_sprite(col_sprite_ids[c], spirtesheet_controls, x_cols[c] + dx, TOP_Y + dy, 1, 1, 1, 1);
            riv->draw.pal[RIV_COLOR_BLACK] = RIV_COLOR_BLACK;
            riv->draw.pal[RIV_COLOR_WHITE] = RIV_COLOR_WHITE;
            riv->draw.pal_enabled = false;
        } else riv_draw_sprite(col_sprite_ids[c], spirtesheet_controls, x_cols[c] + dx, TOP_Y + dy, 1, 1, 1, 1);
        

        for (int i=0; i<SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) riv_draw_sprite(col_sprite_ids[c], spirtesheet_controls, x_cols[c] + dx, i, 1, 1, 1, 1);
        }
    }
    
    // draw score and combo
    char buf[128];
    if (show_stats) {
        riv_snprintf(buf, sizeof(buf), "COMBO: %d",combo_moves);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPRIGHT, 246, 240, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "bad/miss: %d",n_bad+n_miss);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPRIGHT, 246, 220, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "Score: %d",score);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPLEFT, 10, 240, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "Speed: %.2f\n",new_tile_speed);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOP, 128, 240, 1, RIV_COLOR_WHITE);
    }
    if (riv->frame < frame_decrease_empty && rythm_tick()) {
        riv_snprintf(buf, sizeof(buf), "MORE MOVES!");
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_CENTER, 128, 160, 1, RIV_COLOR_RED);
    }
    if (riv->frame < frame_increase_speed && rythm_tick()) {
        riv_snprintf(buf, sizeof(buf), "MORE SPEED!");
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_CENTER, 128, 160, 1, RIV_COLOR_RED);
    }
}

void update_start_screen() {

    float speed = (1.0 * TILE_SIZE) / 40;
    // add some
    for (int c = 0; c < n_cols; c++) {
        for (int i=0; i < SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) {
                int next_i = (int)round(SCREEN_SIZE - 1 - speed * (riv->frame - sliding_arrows[c][i]));
                // riv_printf("col has something col %u y %u next %u val %i \n",c,i, next_i, cols[c][i]);
                if (next_i >= 0) sliding_arrows[c][next_i] = sliding_arrows[c][i];
                else sliding_arrows[c][SCREEN_SIZE - 1] = riv->frame;
                if (i != next_i) sliding_arrows[c][i] = 0;
            }
        }
    }
}

// Draw game start screen
void draw_start_screen() {

    // Draw title bg
    riv_clear(RIV_COLOR_DARKSLATE);


    // draw animation
    for (int c = 0; c < n_cols; c++) {
        for (int i=0; i<SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) {
                riv_draw_sprite(col_sprite_ids[c], spirtesheet_controls, x_cols[c], i, 1, 1, 1, 1);
            }
        }
    }

    // Draw title
    riv_draw_text("Rythm'n Rives",RIV_SPRITESHEET_FONT_3X5,RIV_CENTER,130,130,4,RIV_COLOR_DARKPINK);
    riv_draw_text("Rythm'n Rives",RIV_SPRITESHEET_FONT_3X5,RIV_CENTER,128,128,4,RIV_COLOR_PINK);
    
    // Make "press to start blink" by changing the color depending on the frame number
    uint32_t col = rythm_tick() ? RIV_COLOR_LIGHTTEAL : RIV_COLOR_GREEN;
    // Draw press to start
    riv_draw_text("PRESS TO START", RIV_SPRITESHEET_FONT_3X5, RIV_CENTER, 128, 128+32, 2, col);
}

// Draw game over screen
void draw_end_screen() {
    // Draw last game frame
    draw_game();
    // Draw GAME OVER
    riv_draw_text("GAME OVER", RIV_SPRITESHEET_FONT_3X5, RIV_CENTER, 128, 128, 4, RIV_COLOR_LIGHTRED);
}

// Called every frame to update game state
void update() {
    if (!started) { // Game not started yet
        // Let game start whenever a key has been pressed
        if (riv->key_toggle_count > 0) {
            start_game();
            return;
        }

        update_start_screen();
    } else if (!ended) { // Game is progressing
        update_game();
    }
}

// Called every frame to draw the game
void draw() {
    // Clear screen
    riv_clear(RIV_COLOR_DARKSLATE);
    // Draw different screens depending on the game state
    if (!started) { // Game not started yet
        draw_start_screen();
    } else if (!ended) { // Game is progressing
        draw_game();
    } else { // Game ended
        draw_end_screen();
    }
}

// Entry point
int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (argc % 2 == 0) {
            riv_printf("Wrong number of arguments\n");
            return 1;
        }

        for (int i = 1; i < argc; i+=2) {
            // riv_printf("args %s=%s\n", argv[i],argv[i+1]);
            if (strcmp(argv[i], "-n-cols") == 0) {
                n_cols = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-tile-speed-modifier") == 0) {
                char *endstr;
                tile_speed_modifier = strtof(argv[i+1], &endstr);
            } else if (strcmp(argv[i], "-empty-ticks") == 0) {
                empty_ticks = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-empty-ticks-decrease") == 0) {
                empty_ticks_decrease = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-good-multiplier") == 0) {
                char *endstr;
                good_multiplier = strtof(argv[i+1], &endstr);
            } else if (strcmp(argv[i], "-perfect-multiplier") == 0) {
                char *endstr;
                perfect_multiplier = strtof(argv[i+1], &endstr);
            } else if (strcmp(argv[i], "-show-stats") == 0) {
                show_stats = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-max-misses") == 0) {
                max_misses = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-n-loops") == 0) {
                n_loops = atoi(argv[i+1]);
            }
        }
    }

    spirtesheet_controls = riv_make_spritesheet(riv_make_image("controls.png", 0xff), TILE_SIZE, TILE_SIZE);

    initialize();

    // Main loop, keep presenting frames until user quit or game ends
    do {
        // Update game state
        update();
        // Draw game graphics
        draw();
    } while(riv_present());
    return 0;
}
