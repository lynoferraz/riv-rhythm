// Header including all RIV APIs
#include <riv.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define SEQT_IMPL
#include "seqt.h"

enum {
    MAGIC_SIZE = 4,

    SCREEN_SIZE = 256,
    TILE_SIZE = 20,
    TIME_SIG = 4,

    TOP_Y = TILE_SIZE - 4,
    N_SLIDING_TILES = (SCREEN_SIZE - TOP_Y)/TILE_SIZE,

    PERFECT_DISTANCE = TILE_SIZE/10,
    NICE_DISTANCE = TILE_SIZE/2,
    GOOD_DISTANCE = TILE_SIZE,

    MAX_COLS = 6,
    MAX_TICKS = 2,
    MAX_NOTE_INTERVAL = 4,
    
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

enum {
    HIT_TICK,
    BEAT_TICK,
};

enum {
    NOT_ENDED,
    MUSIC_END,
    FORCED_END,
    MISSES_END,
};

// Game state
bool wait; // true when game has started
int random_wait_frame;
bool started; // true when game has started
bool ended; // true when game has ended
bool last_tick = false;
int frame_increase_speed;
int frame_increase_speed_buffer;
uint64_t spritesheet_controls;
int last_note_evaluated = -1;
int counter_last_speed_change = 0;
int counter_last_interval_change = 0;
int counter_last_track_change = 0;
uint8_t end_reason = NOT_ENDED;

static int col_sprite_ids[MAX_COLS] = {3,1,4,5,0,2};
static int col_sprite_order[MAX_COLS] = {2,3,4,5,1,6};
static int x_cols[MAX_COLS];
static int key_codes[MAX_COLS] = {RIV_GAMEPAD1_LEFT,RIV_GAMEPAD1_UP,RIV_GAMEPAD1_DOWN,RIV_GAMEPAD1_RIGHT,RIV_GAMEPAD1_L1,RIV_GAMEPAD1_R1};
static int alternative_key_codes[MAX_COLS] = {RIV_GAMEPAD1_A3,RIV_GAMEPAD1_A4,RIV_GAMEPAD1_A1,RIV_GAMEPAD1_A2,RIV_GAMEPAD1_L2,RIV_GAMEPAD1_R2};

static bool pressed[MAX_COLS];
static int  sliding_arrows[MAX_COLS][SCREEN_SIZE];
static int  pressed_match[MAX_COLS];
static int  animation_match[MAX_COLS];
static int  animation_frames[MAX_COLS];
static bool sliding_speed_indicator[SCREEN_SIZE];
static int  sliding_ticks[MAX_TICKS][SCREEN_SIZE];
static int  tick_colors[MAX_TICKS] = {RIV_COLOR_GREY,RIV_COLOR_LIGHTGREY,};

float tile_speed;
float new_tile_speed = 1;
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

float hits_per_second;
float note_period;
float beat_guide_tick_size;
float frames_per_beat;
int n_sounds = 0;
int chosen_sound_ind = -1;
uint64_t chosen_sound;
uint64_t sound_ids[SEQT_MAX_SOUNDS];
int frames_until_mark = 0; // depends on the current speed

uint64_t notes_y_cols_mapping[SEQT_NOTES_TRACKS][SEQT_NOTES_ROWS];
uint64_t n_notes_y_used = SEQT_NOTES_ROWS;


float perfect_multiplier = 4.0;
float nice_multiplier = 1.5;
float good_multiplier = 1.0;
int notes_interval = 3;
int speed_increase_interval = 14;
int notes_increase_interval = 21;
float tile_speed_modifier = 1.5;
int n_cols = 4;
bool show_stats = true;
int max_misses = 10;
int n_loops = 8;
uint8_t focus_track = 1;
static uint8_t next_tracks[SEQT_NOTES_TRACKS] = {1,2,3,0};
static uint8_t track_change_intervals[SEQT_NOTES_TRACKS] = {0,0,0,0};
int fix_frame = 0;

// utils

uint64_t get_note_frame(uint64_t frame) {
    seqt_sound *sound = seqt_get_sound(chosen_sound);
    if (frame < sound->start_frame) return 0;

    uint64_t note_frame = (uint64_t)floor(((double)(frame - sound->start_frame) * hits_per_second * sound->speed) / riv->target_fps);

    if (sound->loops >= 0 && (note_frame / seqt_get_source_track_size(sound->source)) >= (uint64_t)sound->loops)
        return seqt_get_source_track_size(sound->source) * (uint64_t)sound->loops - 1;

    return note_frame;
}

uint64_t get_note_x(uint64_t track,uint64_t note_frame) {
    seqt_sound *sound = seqt_get_sound(chosen_sound);

    return note_frame % maxu(sound->source->track_sizes[track], SEQT_NOTES_COLUMNS);
}

void read_incard_data(uint8_t *data,int from,int size) {
    char magic[5];
    for (int i=0; i<MAGIC_SIZE ; i++) magic[i] = data[from + i];
    magic[MAGIC_SIZE] = '\0';

    if (!strcmp(magic,"SEQT")) {
        sound_ids[n_sounds] = seqt_play((seqt_source*)(data + from),n_loops);
        n_sounds++;
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

void update_notes_mapping() {
    // create mapping for used notes in focus track
    seqt_sound *sound = seqt_get_sound(chosen_sound);
    for (int t = 0; t < SEQT_NOTES_TRACKS; t++) {
        bool row_has_note[SEQT_NOTES_ROWS];
        for (int y = 0; y < SEQT_NOTES_ROWS; y++) {
            row_has_note[y] = false;
        }
        uint64_t n_notes_x = maxu(sound->source->track_sizes[t], SEQT_NOTES_COLUMNS);
        for (int x = 0; x < n_notes_x; x++) {
            if (x % notes_interval == 0) {
                for (int y = 0; y < SEQT_NOTES_ROWS; y++) {
                    if (sound->source->pages[t][y][x].periods > 0) {
                        row_has_note[y] = true;
                    }
                }
            }

        }
        uint8_t arrow_cols = 0;
        for (int y = 0; y < SEQT_NOTES_ROWS; y++) {
            if (row_has_note[y]) {
                notes_y_cols_mapping[t][y] = arrow_cols%n_cols;
                arrow_cols++;
            }
        }
    }
}
void initialize() {

    seqt_init();
    if (riv->incard_len > 0) {
        read_incard_data(riv->incard,0,riv->incard_len);
    }

    if (n_sounds == 0) {
        sound_ids[n_sounds] = seqt_play(seqt_make_source_from_file("seqs/01.seqt.rivcard"), n_loops);
        n_sounds++;
        sound_ids[n_sounds] = seqt_play(seqt_make_source_from_file("seqs/04.seqt.rivcard"), n_loops);
        n_sounds++;
        sound_ids[n_sounds] = seqt_play(seqt_make_source_from_file("seqs/f6.seqt.01.rivcard"), n_loops);
        n_sounds++;
        sound_ids[n_sounds] = seqt_play(seqt_make_source_from_file("seqs/07.seqt.rivcard"), n_loops);
        n_sounds++;
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
        "JSON{\"frame\":%d,\"score\":%d,\"notes_interval\":%d,\"speed\":%.5f,\"max_combo\":%d,\"max_combo_score\":%d,\"n_perfect\":%d,\"n_nice\":%d,\"n_good\":%d,\"n_miss\":%d,\"n_bad\":%d,\"end_reason\":%d}",
        riv->frame, score, notes_interval, new_tile_speed, max_combo, max_combo_score,n_perfects,n_nice,n_good,n_miss,n_bad,NOT_ENDED);
}

void random_wait() {
    random_wait_frame = riv->frame + riv_rand_uint(riv->target_fps/2);
    wait = true;
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

    int16_t music_bpm = seqt_get_sound(chosen_sound)->source->bpm;
    hits_per_second = music_bpm*((1.0*TIME_SIG)/60); // beats per second
    note_period = hits_per_second/riv->target_fps;
    frames_per_beat = riv->target_fps/(music_bpm/60.0);

    tile_speed = new_tile_speed;

    frames_until_mark = (int)round(N_SLIDING_TILES*TILE_SIZE/new_tile_speed);

    seqt_set_start(chosen_sound,((double)frames_until_mark)/riv->target_fps);
    seqt_seek(chosen_sound,0.0);


    update_notes_mapping();
}

// Called when game ends
void end_game() {
    riv_printf("GAME OVER\n");
    ended = true;

    // final oucard
    riv->outcard_len = riv_snprintf((char*)riv->outcard, RIV_SIZE_OUTCARD,
        "JSON{\"frame\":%d,\"score\":%d,\"notes_interval\":%d,\"speed\":%.5f,\"max_combo\":%d,\"max_combo_score\":%d,\"n_perfect\":%d,\"n_nice\":%d,\"n_good\":%d,\"n_miss\":%d,\"n_bad\":%d,\"end_reason\":%d}",
        riv->frame, score, notes_interval, new_tile_speed, max_combo, max_combo_score,n_perfects,n_nice,n_good,n_miss,n_bad,end_reason);

    // Quit in 2 seconds
    riv->quit_frame = riv->frame + 2*riv->target_fps;
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
    // end game
    seqt_sound *sound = seqt_get_sound(chosen_sound);
    // seqt_sound *sound = &seqt.sounds[chosen_sound];
    if (riv->keys[RIV_GAMEPAD1_SELECT].press || (max_misses > 0 && consecutive_misses >= max_misses) || !sound) {
        if (riv->keys[RIV_GAMEPAD1_SELECT].press) {
            end_reason = FORCED_END;
        } else if (max_misses > 0 && consecutive_misses >= max_misses) {
            end_reason = MISSES_END;
        } else if (!sound) {
            end_reason = MUSIC_END;
        }
        end_game();
        return;
    }

    // reset pressed
    perfect_hit = false;
    nice_hit = false;
    good_hit = false;
    for (int c = 0; c < n_cols; c++) {
        pressed[c] = false;
        pressed_match[c] = STATE_NOTHING;
    }

    // detect colums presses and misses
    for (int c = 0; c < n_cols; c++) {
        // update animation
        if (animation_frames[c]) animation_frames[c]--;
        else animation_match[c] = 0;

        // detect pressed
        if (riv->keys[key_codes[c]].down) pressed[c] = true;
        else if (riv->keys[alternative_key_codes[c]].down) pressed[c] = true;

        bool left_screen = false;
        for (int i = 0; i < SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) {
                // check match press
                bool match = false;
                if (!pressed_match[c] && (riv->keys[key_codes[c]].press || riv->keys[alternative_key_codes[c]].press)) {
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

                    float s = sliding_arrows[c][i] > frame_increase_speed ? new_tile_speed : tile_speed;
                    int new_ind = (int)round(SCREEN_SIZE - 1 - (riv->frame - sliding_arrows[c][i]) * s);
                    if (new_ind == i) continue;
                    if (new_ind >= 0) {
                        sliding_arrows[c][new_ind] = sliding_arrows[c][i];
                    } else {
                        left_screen = true;
                    }
                }
                sliding_arrows[c][i] = 0;
            }
        }
        if (left_screen) {
            pressed_match[c] = STATE_MISS;
            animation_frames[c] = N_ANIMATION_FRAMES;
            animation_match[c] = STATE_MISS;
            update_score(STATE_MISS);
        }
        if (!pressed_match[c] && (riv->keys[key_codes[c]].press || riv->keys[alternative_key_codes[c]].press)) {
            pressed_match[c] = STATE_BAD;
            animation_frames[c] = N_ANIMATION_FRAMES;
            animation_match[c] = STATE_BAD;
            update_score(STATE_BAD);
        }
    }

    // update tick position
    for (int t = 0; t < MAX_TICKS; t++) {
        for (int i = 0; i < SCREEN_SIZE; i++) {
            if (sliding_ticks[t][i]) {
                float s = sliding_ticks[t][i] > frame_increase_speed ? new_tile_speed : tile_speed;
                int new_ind = (int)round(SCREEN_SIZE - 1 - (riv->frame - sliding_ticks[t][i]) * s);
                if (new_ind == i) continue;
                if (new_ind >= 0) {
                    sliding_ticks[t][new_ind] = sliding_ticks[t][i];
                }
                sliding_ticks[t][i] = 0;
            }
        }
    }

    // add new arrows
    int note_to_evaluate = get_note_frame(sound->frame+frames_until_mark+fix_frame);

    if (note_to_evaluate > last_note_evaluated) {
        // add beat/hit tick
        if (note_to_evaluate % SEQT_TIME_SIG != 0) {
            sliding_ticks[HIT_TICK][SCREEN_SIZE-1] = riv->frame;
        } else {
            sliding_ticks[BEAT_TICK][SCREEN_SIZE-1] = riv->frame;
        }

        // add arrow
        if (note_to_evaluate % notes_interval == 0) {
            for (uint64_t note_y = 0; note_y < SEQT_NOTES_ROWS; ++note_y) {
                seqt_note note = sound->source->pages[focus_track][note_y][get_note_x(focus_track,note_to_evaluate)];
                if (note.periods > 0) {
                    sliding_arrows[notes_y_cols_mapping[focus_track][note_y]][SCREEN_SIZE-1] = riv->frame;
                }
            }
        }
        
        // update speed difficulty
        counter_last_speed_change++;
        if (speed_increase_interval > 0 && 
                counter_last_speed_change/SEQT_NOTES_COLUMNS >= speed_increase_interval &&
                riv->frame > frame_increase_speed_buffer) {
            tile_speed = new_tile_speed;
            new_tile_speed = tile_speed_modifier * tile_speed;
            frame_increase_speed = riv->frame;
            frame_increase_speed_buffer = riv->frame + frames_until_mark;
            frames_until_mark = (int)round(N_SLIDING_TILES*TILE_SIZE/new_tile_speed);
            counter_last_speed_change = 0;
        }

        // update notes interval difficulty
        counter_last_interval_change++;
        if (notes_increase_interval > 0 && counter_last_interval_change/SEQT_NOTES_COLUMNS >= notes_increase_interval) {
            if (notes_interval > 1) {
                notes_interval = notes_interval -1;
                update_notes_mapping();
            }
            counter_last_interval_change = 0;
        }
        
        // change track
        counter_last_track_change++;
        if (track_change_intervals[focus_track] > 0 && counter_last_track_change/SEQT_NOTES_COLUMNS >= track_change_intervals[focus_track]) {
            focus_track = next_tracks[focus_track];
            counter_last_track_change = 0;
        }
        
        last_note_evaluated = note_to_evaluate;
    }

    // play music
    seqt_poll_sound(sound);

    // update outcard
    riv->outcard_len = riv_snprintf((char*)riv->outcard, RIV_SIZE_OUTCARD,
        "JSON{\"frame\":%d,\"score\":%d,\"notes_interval\":%d,\"speed\":%.5f,\"max_combo\":%d,\"max_combo_score\":%d,\"n_perfect\":%d,\"n_nice\":%d,\"n_good\":%d,\"n_miss\":%d,\"n_bad\":%d,\"end_reason\":%d}",
        riv->frame, score, notes_interval, new_tile_speed, max_combo, max_combo_score,n_perfects,n_nice,n_good,n_miss,n_bad,NOT_ENDED);
}

// Draw the game canvas
void draw_game() {
    riv_clear(perfect_hit || nice_hit ? RIV_COLOR_SLATE : RIV_COLOR_DARKSLATE);

    // draw tick markings
    for (int t = 0; t < MAX_TICKS; t++) {
        for (int i = 0; i < SCREEN_SIZE - TILE_SIZE/2; i++) {
            if (sliding_ticks[t][i]) {
                riv_draw_line(0,i+TILE_SIZE/2,SCREEN_SIZE-1,i+TILE_SIZE/2,tick_colors[t]);
            }
        }
    }

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

        // draw press animation
        if (pressed[c]) {

            riv->draw.pal_enabled = true;
            riv->draw.pal[RIV_COLOR_BLACK] = RIV_COLOR_RED; // red
            riv->draw.pal[RIV_COLOR_WHITE] = RIV_COLOR_LIGHTPEACH;
            riv_draw_sprite(col_sprite_ids[c], spritesheet_controls, x_cols[c] + dx, TOP_Y + dy, 1, 1, 1, 1);
            riv->draw.pal[RIV_COLOR_BLACK] = RIV_COLOR_BLACK;
            riv->draw.pal[RIV_COLOR_WHITE] = RIV_COLOR_WHITE;
            riv->draw.pal_enabled = false;
        } else riv_draw_sprite(col_sprite_ids[c], spritesheet_controls, x_cols[c] + dx, TOP_Y + dy, 1, 1, 1, 1);
        

        for (int i=0; i<SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) riv_draw_sprite(col_sprite_ids[c], spritesheet_controls, x_cols[c] + dx, i, 1, 1, 1, 1);
        }
    }
    
    // draw score and combo
    char buf[128];
    if (show_stats) {
        riv_snprintf(buf, sizeof(buf), "COMBO: %d",combo_moves);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPRIGHT, 246, 240, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "bad/miss: %d",n_bad+n_miss);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPRIGHT, 246, 220, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "Speed: %.2f\n",new_tile_speed);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPLEFT, 10, 220, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "Diff.: %d",1+MAX_NOTE_INTERVAL-notes_interval);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOPLEFT, 10, 240, 1, RIV_COLOR_WHITE);
        riv_snprintf(buf, sizeof(buf), "Score: %d",score);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOP, 128, 220, 1, RIV_COLOR_WHITE);
    }

}

void update_start_screen() {

    // song selection
    if (riv->frame > 0) {
        if (n_sounds > 0) {
            if (chosen_sound_ind < 0) {
                chosen_sound_ind = 0;
                chosen_sound = sound_ids[chosen_sound_ind];
            }
            // seqt_sound *sound = &seqt.sounds[chosen_sound];
            if (riv->keys[RIV_GAMEPAD1_RIGHT].press || riv->keys[RIV_GAMEPAD1_LEFT].press) {
                if (riv->keys[RIV_GAMEPAD1_RIGHT].press) {
                    chosen_sound_ind = chosen_sound_ind == n_sounds - 1 ? 0 : chosen_sound_ind + 1;
                } else if (riv->keys[RIV_GAMEPAD1_LEFT].press) {
                    chosen_sound_ind = chosen_sound_ind == 0 ? n_sounds - 1 : chosen_sound_ind - 1;
                }
                chosen_sound = sound_ids[chosen_sound_ind];
                seqt_set_start(chosen_sound,0.2);
                seqt_seek(chosen_sound,0.0);
            }
            seqt_poll_sound(seqt_get_sound(chosen_sound));
        }
    }

    // floating arrow animation
    float speed = (1.0 * TILE_SIZE) / 40;
    // add some
    for (int c = 0; c < n_cols; c++) {
        for (int i=0; i < SCREEN_SIZE; i++) {
            if (sliding_arrows[c][i]) {
                int next_i = (int)round(SCREEN_SIZE - 1 - speed * (riv->frame - sliding_arrows[c][i]));
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
                riv_draw_sprite(col_sprite_ids[c], spritesheet_controls, x_cols[c], i, 1, 1, 1, 1);
            }
        }
    }

    // Draw title
    riv_draw_text("Rythm'n Rives",RIV_SPRITESHEET_FONT_3X5,RIV_CENTER,130,130,4,RIV_COLOR_DARKPINK);
    riv_draw_text("Rythm'n Rives",RIV_SPRITESHEET_FONT_3X5,RIV_CENTER,128,128,4,RIV_COLOR_PINK);
    
    // Make "press to start blink" by changing the color depending on the frame number
    uint32_t col = (riv->frame / 30) % 2 == 0 ? RIV_COLOR_LIGHTTEAL : RIV_COLOR_GREEN;
    // Draw press to start
    if (riv->frame)
        riv_draw_text("PRESS A1/Z TO START", RIV_SPRITESHEET_FONT_3X5, RIV_CENTER, 128, 128+32, 2, col);

    if (chosen_sound) {
        char buf[128];
        riv_snprintf(buf, sizeof(buf), "Sound to play: %d/%d",chosen_sound,n_sounds);
        riv_draw_text(buf, RIV_SPRITESHEET_FONT_5X7, RIV_TOP, 128, 128+64, 1, RIV_COLOR_WHITE);
    }
}

// Draw game over screen
void draw_end_screen() {
    // Draw last game frame
    draw_game();
    // Draw GAME OVER
    char buf[128];
    if (end_reason == FORCED_END) {
        riv_snprintf(buf, sizeof(buf), "Gave up!");
    } else if (end_reason == MUSIC_END) {
        riv_snprintf(buf, sizeof(buf), "Done!");
    } else if (end_reason == MISSES_END) {
        riv_snprintf(buf, sizeof(buf), "Too Bad!");
    } else {
        riv_snprintf(buf, sizeof(buf), "GAME OVER");
    }
    riv_draw_text(buf, RIV_SPRITESHEET_FONT_3X5, RIV_CENTER, 128, 128, 4, RIV_COLOR_LIGHTRED);
}

// Called every frame to update game state
void update() {
    if (!wait) { // Game not started yet
        // Let game start whenever a key has been pressed
        if ((riv->keys[RIV_GAMEPAD1_A1].press || riv->keys[RIV_GAMEPAD1_A2].press) && chosen_sound) {
            random_wait();
            return;
        }

        update_start_screen();
    } else if (!started) { // waiting
        if (riv->frame > random_wait_frame) {
            start_game();
        }
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
            if (strcmp(argv[i], "-n-cols") == 0) {
                n_cols = clampu(atoi(argv[i+1]),1,MAX_COLS);
            } else if (strcmp(argv[i], "-speed") == 0) {
                new_tile_speed = strtof(argv[i+1], NULL);
            } else if (strcmp(argv[i], "-speed-modifier") == 0) {
                tile_speed_modifier = strtof(argv[i+1], NULL);
            } else if (strcmp(argv[i], "-speed-increase-interval") == 0) {
                speed_increase_interval = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-notes-interval") == 0) {
                notes_interval = clampu(atoi(argv[i+1]),1,MAX_NOTE_INTERVAL);
            } else if (strcmp(argv[i], "-notes-increase-interval") == 0) {
                notes_increase_interval = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-track-change-intervals") == 0) {
                char *delim = ","; 
                char *token = strtok(argv[i+1], delim);
                for (int t = 0; token != NULL && t < SEQT_NOTES_TRACKS; t++) {
                    track_change_intervals[t] = atoi(token);
                    token = strtok(NULL, delim); 
                }
            } else if (strcmp(argv[i], "-next-tracks") == 0) {
                char *delim = ","; 
                char *token = strtok(argv[i+1], delim);
                for (int t = 0; token != NULL && t < SEQT_NOTES_TRACKS; t++) {
                    next_tracks[t] = atoi(token);
                    token = strtok(NULL, delim); 
                }
            } else if (strcmp(argv[i], "-track") == 0) {
                focus_track = clampu(atoi(argv[i+1]),0,SEQT_NOTES_TRACKS-1);
            } else if (strcmp(argv[i], "-good-multiplier") == 0) {
                good_multiplier = strtof(argv[i+1], NULL);
            } else if (strcmp(argv[i], "-perfect-multiplier") == 0) {
                perfect_multiplier = strtof(argv[i+1], NULL);
            } else if (strcmp(argv[i], "-show-stats") == 0) {
                show_stats = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-max-misses") == 0) {
                max_misses = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-n-loops") == 0) {
                n_loops = atoi(argv[i+1]);
            } else if (strcmp(argv[i], "-fix-frame") == 0) {
                fix_frame = atoi(argv[i+1]);
            }
        }
    }

    spritesheet_controls = riv_make_spritesheet(riv_make_image("controls.png", 0xff), TILE_SIZE, TILE_SIZE);

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
